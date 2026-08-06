#include "DWCGPUPreviewSimulator.h"

#include "DWCGPUShaders.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace DWCGPUPreviewPrivate
{
    // Single Splash intentionally starts dry, waits briefly, then applies exactly one contact.
    // Keep a 100% absorption profile below MaxWetness so spread/gravity/drying remain visible.
    constexpr float PreviewScenarioContactAmount = 0.035f;

    struct alignas(16) FUint4
    {
        uint32 X = 0;
        uint32 Y = 0;
        uint32 Z = 0;
        uint32 W = 0;
    };

    uint32 PackFloatToBits(const float Value)
    {
        uint32 Bits = 0u;
        static_assert(sizeof(Bits) == sizeof(Value), "Preview lookup float packing size mismatch.");
        FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    struct FSurfaceStamp
    {
        FVector2f UV = FVector2f::ZeroVector;
        FVector2f HalfSizePixels = FVector2f(8.0f, 8.0f);
        float Amount = 0.0f;
        bool bDroplet2 = false;
    };

    template <typename ElementType>
    FRDGBufferRef RegisterOrUpload(
        FRDGBuilder& GraphBuilder,
        TRefCountPtr<FRDGPooledBuffer>& PooledBuffer,
        const TCHAR* Name,
        const TArray<ElementType>& Data)
    {
        if (PooledBuffer.IsValid())
        {
            return GraphBuilder.RegisterExternalBuffer(PooledBuffer, Name);
        }
        if (Data.IsEmpty())
        {
            return nullptr;
        }
        FRDGBufferRef Buffer = CreateStructuredBuffer(GraphBuilder, Name, Data);
        GraphBuilder.QueueBufferExtraction(Buffer, &PooledBuffer);
        return Buffer;
    }

}

using namespace DWCGPUPreviewPrivate;

struct FDWCGPUPreviewSimulator::FRenderState
{
    TArray<FUint4> TexelLookup;
    TArray<FVector4f> TriangleFlow;
    TArray<FVector4f> TriangleMetric;
    TArray<uint32> TriangleProfileIndices;
    TRefCountPtr<FRDGPooledBuffer> TexelLookupBuffer;
    TRefCountPtr<FRDGPooledBuffer> TriangleFlowBuffer;
    TRefCountPtr<FRDGPooledBuffer> TriangleMetricBuffer;
    TRefCountPtr<FRDGPooledBuffer> TriangleProfileIndicesBuffer;
};

FDWCGPUPreviewSimulator::~FDWCGPUPreviewSimulator()
{
    Shutdown();
}

TStrongObjectPtr<UTextureRenderTarget2D> FDWCGPUPreviewSimulator::CreateRenderTarget(
    const FName& Name,
    const bool bBilinear) const
{
    UObject* Outer = WorldContextObject.IsValid()
        ? WorldContextObject.Get()
        : GetTransientPackage();
    UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(Outer, Name, RF_Transient);
    RT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_R16f;
    RT->ClearColor = FLinearColor::Black;
    RT->bAutoGenerateMips = false;
    RT->bCanCreateUAV = true;
    RT->Filter = bBilinear ? TF_Bilinear : TF_Nearest;
    RT->AddressX = TA_Clamp;
    RT->AddressY = TA_Clamp;
    RT->InitCustomFormat(Resolution, Resolution, PF_R16F, false);
    RT->UpdateResourceImmediate(true);
    return TStrongObjectPtr<UTextureRenderTarget2D>(RT);
}

bool FDWCGPUPreviewSimulator::Initialize(const FDWCGPUPreviewInitArgs& Args)
{
    Shutdown();
    WorldContextObject = Args.WorldContextObject;
    Resolution = FMath::Clamp(Args.Resolution, 64, 1024);
    MaxWetness = FMath::Max(0.01f, Args.MaxWetness);
    CapillaryImmediateAbsorptionFraction = FMath::Clamp(
        Args.CapillaryImmediateAbsorptionFraction,
        0.0f,
        1.0f);
    bUseEightDirectionDiffusion = Args.bUseEightDirectionDiffusion;

    WetnessMaps.Add(CreateRenderTarget(TEXT("DWC_WPPreview_Wetness0"), false));
    WetnessMaps.Add(CreateRenderTarget(TEXT("DWC_WPPreview_Wetness1"), false));
    PendingWetnessMaps.Add(CreateRenderTarget(TEXT("DWC_WPPreview_Pending0"), false));
    PendingWetnessMaps.Add(CreateRenderTarget(TEXT("DWC_WPPreview_Pending1"), false));
    Droplet1Map = CreateRenderTarget(TEXT("DWC_WPPreview_Droplet1"), true);
    Droplet2Map = CreateRenderTarget(TEXT("DWC_WPPreview_Droplet2"), true);

    RenderState = MakeShared<FRenderState, ESPMode::ThreadSafe>();
    constexpr float PreviewRestSurfaceArea = 10000.0f;
    const float PreviewRestTexelArea = PreviewRestSurfaceArea /
        static_cast<float>(Resolution * Resolution);
    const uint32 PackedRestTexelArea = PackFloatToBits(PreviewRestTexelArea);

    RenderState->TexelLookup.SetNum(Resolution * Resolution);
    for (FUint4& Lookup : RenderState->TexelLookup)
    {
        Lookup.X = 0u;                       // Triangle ID.
        Lookup.Y = 0u;                       // Packed barycentric coordinates.
        Lookup.Z = PackedRestTexelArea;      // Positive rest area required by diffusion.
        Lookup.W = 1u;                       // Island 0 + valid texel.
    }
    // The normalized preview domain uses +V as its canonical downward direction.
    // TriangleMetric is a local UV metric tensor, not the triangle's total surface
    // area. Feeding the full preview area here suppresses visible diffusion.
    RenderState->TriangleFlow.Add(FVector4f(0.0f, 1.0f, 1.0f, 1.0f));
    RenderState->TriangleMetric.Add(FVector4f(1.0f, 0.0f, 1.0f, 1.0f));
    RenderState->TriangleProfileIndices.Add(0u);

    CachedParameters = MakeUnique<FWetnessProfileParameters>();
    bInitialized = true;
    Restart();
    return true;
}

void FDWCGPUPreviewSimulator::SetProfileParameters(const FWetnessProfileParameters& Parameters)
{
    if (!CachedParameters)
    {
        CachedParameters = MakeUnique<FWetnessProfileParameters>();
    }
    *CachedParameters = Parameters;
}

void FDWCGPUPreviewSimulator::SetScenarioSplashUV(const FVector2f InSplashUV)
{
    ScenarioSplashUV = FVector2f(
        FMath::Clamp(InSplashUV.X, 0.001f, 0.999f),
        FMath::Clamp(InSplashUV.Y, 0.001f, 0.999f));
}

void FDWCGPUPreviewSimulator::SetPreviewChannels(
    const bool bAbsorbedEnabled,
    const bool bSurfaceEnabled,
    const bool bDroplet1Enabled,
    const bool bDroplet2Enabled)
{
    bPreviewAbsorbedEnabled = bAbsorbedEnabled;
    bPreviewSurfaceEnabled = bSurfaceEnabled;
    bPreviewDroplet1Enabled = bDroplet1Enabled;
    bPreviewDroplet2Enabled = bDroplet2Enabled;
}

void FDWCGPUPreviewSimulator::ClearAllRenderTargets()
{
    UObject* Context = WorldContextObject.IsValid() ? WorldContextObject.Get() : GetTransientPackage();
    for (const TStrongObjectPtr<UTextureRenderTarget2D>& RT : WetnessMaps)
    {
        if (RT.IsValid()) UKismetRenderingLibrary::ClearRenderTarget2D(Context, RT.Get(), FLinearColor::Black);
    }
    for (const TStrongObjectPtr<UTextureRenderTarget2D>& RT : PendingWetnessMaps)
    {
        if (RT.IsValid()) UKismetRenderingLibrary::ClearRenderTarget2D(Context, RT.Get(), FLinearColor::Black);
    }
    if (Droplet1Map.IsValid()) UKismetRenderingLibrary::ClearRenderTarget2D(Context, Droplet1Map.Get(), FLinearColor::Black);
    if (Droplet2Map.IsValid()) UKismetRenderingLibrary::ClearRenderTarget2D(Context, Droplet2Map.Get(), FLinearColor::Black);
}

void FDWCGPUPreviewSimulator::Restart()
{
    if (!bInitialized)
    {
        return;
    }
    CurrentWetnessIndex = 0;
    CurrentPendingIndex = 0;
    bManualSplashRequested = false;
    ClearAllRenderTargets();
    // ClearRenderTarget2D queues work on the render thread. Restart is an editor-only,
    // infrequent operation, so wait here to guarantee that no previous loop contents
    // are visible before the first Single Splash step.
    FlushRenderingCommands();
}

void FDWCGPUPreviewSimulator::RequestSplash()
{
    bManualSplashRequested = true;
}

void FDWCGPUPreviewSimulator::Step(const float DeltaSeconds, const float /*ScenarioTimeSeconds*/)
{
    if (!bInitialized || !RenderState.IsValid() || !CachedParameters ||
        WetnessMaps.Num() != 2 || PendingWetnessMaps.Num() != 2)
    {
        return;
    }

    const float SafeDelta = FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
    const FVector2f SplashUV = ScenarioSplashUV;
    const FWetnessProfileParameters Parameters = *CachedParameters;
    const FResolvedAbsorbedWaterSimulationParameters Absorbed =
        Parameters.ResolveAbsorbedWaterSimulation();
    TArray<FVector4f> ProfileValues;
    ProfileValues.Add(FVector4f(
        FMath::Max(0.0f, Absorbed.SpreadRatePerSecond),
        FMath::Max(0.0f, Absorbed.DryRatePerSecond),
        FMath::Max(0.0f, Absorbed.GravityFlowStrength),
        FMath::Max(0.0f, Parameters.GetDropletDryRatePerSecond())));

    const bool bWriteSingleSplash = bManualSplashRequested;
    bool bWriteAbsorptionSplash = false;
    TArray<FSurfaceStamp> SurfaceStamps;
    if (bWriteSingleSplash)
    {
        bManualSplashRequested = false;
        bWriteAbsorptionSplash =
            bPreviewAbsorbedEnabled && Parameters.SupportsAbsorbedWetness();

        if (bPreviewSurfaceEnabled && Parameters.SupportsSurfaceWater())
        {
            const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
            // When Surface Water is isolated in the preview, inject the full contact
            // amount so its own parameters can still be inspected even when the profile
            // absorbs 100% of runtime contact water. Combined mode keeps the runtime split.
            const float SurfaceInputFraction = bPreviewAbsorbedEnabled
                ? FMath::Clamp(Parameters.GetRejectedWaterFraction(), 0.0f, 1.0f)
                : 1.0f;
            const float SurfaceAmount = PreviewScenarioContactAmount * SurfaceInputFraction;
            const float Droplet1Probability = FMath::Clamp(
                Surface.DropletSpawnProbability,
                0.0f,
                1.0f);
            const float Droplet2Probability = FMath::Clamp(
                Surface.DropletFlowSpawnProbability,
                0.0f,
                1.0f);

            // A one-contact scenario cannot usefully communicate Bernoulli probability by
            // sometimes displaying nothing. Keep the runtime-authored chance as a visible
            // stamp-strength response while guaranteeing one deterministic demonstration.
            if (bPreviewDroplet1Enabled &&
                Droplet1Probability > KINDA_SMALL_NUMBER &&
                SurfaceAmount > KINDA_SMALL_NUMBER)
            {
                FSurfaceStamp& Stamp = SurfaceStamps.AddDefaulted_GetRef();
                Stamp.UV = SplashUV;
                Stamp.HalfSizePixels = FVector2f(
                    FMath::Max(0.5f, Surface.DropletHeightPixels),
                    FMath::Max(0.5f, Surface.DropletRadiusPixels));
                Stamp.Amount = SurfaceAmount *
                    FMath::Lerp(0.35f, 1.0f, FMath::Sqrt(Droplet1Probability));
            }

            if (Surface.bUseSecondaryDroplets &&
                bPreviewDroplet2Enabled &&
                Droplet2Probability > KINDA_SMALL_NUMBER &&
                SurfaceAmount > KINDA_SMALL_NUMBER)
            {
                FSurfaceStamp& Stamp = SurfaceStamps.AddDefaulted_GetRef();
                Stamp.bDroplet2 = true;
                const float Spread = FMath::Clamp(
                    Surface.DropletFlowSpawnPositionSpread,
                    0.0f,
                    1.0f);
                // Secondary Droplet position variation offsets only the initial stamp.
                // It is not a time-dependent surface diffusion pass.
                Stamp.UV = FVector2f(
                    FMath::Clamp(SplashUV.X + 0.08f * Spread, 0.02f, 0.98f),
                    FMath::Clamp(SplashUV.Y + 0.05f * Spread, 0.02f, 0.98f));
                Stamp.HalfSizePixels = FVector2f(
                    FMath::Max(0.5f, Surface.DropletFlowHeightPixels),
                    FMath::Max(0.5f, Surface.DropletFlowRadiusPixels));
                Stamp.Amount = SurfaceAmount *
                    FMath::Lerp(0.35f, 1.0f, FMath::Sqrt(Droplet2Probability));
            }
        }
    }

    UTextureRenderTarget2D* CurrentWetness = WetnessMaps[CurrentWetnessIndex].Get();
    UTextureRenderTarget2D* NextWetness = WetnessMaps[1 - CurrentWetnessIndex].Get();
    UTextureRenderTarget2D* CurrentPending = PendingWetnessMaps[CurrentPendingIndex].Get();
    UTextureRenderTarget2D* NextPending = PendingWetnessMaps[1 - CurrentPendingIndex].Get();
    if (!CurrentWetness || !NextWetness || !CurrentPending || !NextPending ||
        !Droplet1Map.IsValid() || !Droplet2Map.IsValid())
    {
        return;
    }

    FTextureRenderTargetResource* CurrentWetnessResource = CurrentWetness->GameThread_GetRenderTargetResource();
    FTextureRenderTargetResource* NextWetnessResource = NextWetness->GameThread_GetRenderTargetResource();
    FTextureRenderTargetResource* CurrentPendingResource = CurrentPending->GameThread_GetRenderTargetResource();
    FTextureRenderTargetResource* NextPendingResource = NextPending->GameThread_GetRenderTargetResource();
    FTextureRenderTargetResource* Droplet1Resource = Droplet1Map->GameThread_GetRenderTargetResource();
    FTextureRenderTargetResource* Droplet2Resource = Droplet2Map->GameThread_GetRenderTargetResource();
    const TSharedPtr<FRenderState, ESPMode::ThreadSafe> RTState = RenderState;
    const int32 RTResolution = Resolution;
    const float RTMaxWetness = MaxWetness;
    const float RTImmediateAbsorption = CapillaryImmediateAbsorptionFraction;
    const bool bRTUseEightDirections = bUseEightDirectionDiffusion;
    const float AbsorptionAmount = PreviewScenarioContactAmount *
        FMath::Max(0.0f, Absorbed.AbsorptionMultiplier);

    ENQUEUE_RENDER_COMMAND(DWCGPUPreviewStep)(
        [RTState,
         CurrentWetnessResource,
         NextWetnessResource,
         CurrentPendingResource,
         NextPendingResource,
         Droplet1Resource,
         Droplet2Resource,
         RTResolution,
         RTMaxWetness,
         RTImmediateAbsorption,
         SafeDelta,
         bRTUseEightDirections,
         bWriteAbsorptionSplash,
         SplashUV,
         AbsorptionAmount,
         ProfileValues = MoveTemp(ProfileValues),
         SurfaceStamps = MoveTemp(SurfaceStamps)](FRHICommandListImmediate& RHICmdList)
        {
            if (!RTState.IsValid() || !CurrentWetnessResource || !NextWetnessResource ||
                !CurrentPendingResource || !NextPendingResource || !Droplet1Resource || !Droplet2Resource)
            {
                return;
            }
            FRHITexture* CurrentWetnessRHI = CurrentWetnessResource->GetRenderTargetTexture();
            FRHITexture* NextWetnessRHI = NextWetnessResource->GetRenderTargetTexture();
            FRHITexture* CurrentPendingRHI = CurrentPendingResource->GetRenderTargetTexture();
            FRHITexture* NextPendingRHI = NextPendingResource->GetRenderTargetTexture();
            FRHITexture* Droplet1RHI = Droplet1Resource->GetRenderTargetTexture();
            FRHITexture* Droplet2RHI = Droplet2Resource->GetRenderTargetTexture();
            if (!CurrentWetnessRHI || !NextWetnessRHI || !CurrentPendingRHI || !NextPendingRHI ||
                !Droplet1RHI || !Droplet2RHI)
            {
                return;
            }

            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGBufferRef LookupBuffer = RegisterOrUpload(
                GraphBuilder, RTState->TexelLookupBuffer, TEXT("DWC.WPPreview.Lookup"), RTState->TexelLookup);
            FRDGBufferRef FlowBuffer = RegisterOrUpload(
                GraphBuilder, RTState->TriangleFlowBuffer, TEXT("DWC.WPPreview.Flow"), RTState->TriangleFlow);
            FRDGBufferRef MetricBuffer = RegisterOrUpload(
                GraphBuilder, RTState->TriangleMetricBuffer, TEXT("DWC.WPPreview.Metric"), RTState->TriangleMetric);
            FRDGBufferRef ProfileBuffer = ProfileValues.IsEmpty()
                ? nullptr
                : CreateStructuredBuffer(GraphBuilder, TEXT("DWC.WPPreview.Profiles"), ProfileValues);
            FRDGBufferRef ProfileIndicesBuffer = RegisterOrUpload(
                GraphBuilder,
                RTState->TriangleProfileIndicesBuffer,
                TEXT("DWC.WPPreview.ProfileIndices"),
                RTState->TriangleProfileIndices);
            if (!LookupBuffer || !FlowBuffer || !MetricBuffer || !ProfileBuffer || !ProfileIndicesBuffer)
            {
                GraphBuilder.Execute();
                return;
            }

            FRDGBufferSRVRef LookupSRV = GraphBuilder.CreateSRV(LookupBuffer);
            FRDGBufferSRVRef FlowSRV = GraphBuilder.CreateSRV(FlowBuffer);
            FRDGBufferSRVRef MetricSRV = GraphBuilder.CreateSRV(MetricBuffer);
            FRDGBufferSRVRef ProfileSRV = GraphBuilder.CreateSRV(ProfileBuffer);
            FRDGBufferSRVRef ProfileIndicesSRV = GraphBuilder.CreateSRV(ProfileIndicesBuffer);

            auto RegisterTexture = [&GraphBuilder](FRHITexture* Texture, const TCHAR* Name)
            {
                TRefCountPtr<IPooledRenderTarget> External = ::CreateRenderTarget(Texture, Name);
                return GraphBuilder.RegisterExternalTexture(External);
            };
            FRDGTextureRef CurrentWetnessTexture = RegisterTexture(CurrentWetnessRHI, TEXT("DWC.WPPreview.CurrentWetness"));
            FRDGTextureRef NextWetnessTexture = RegisterTexture(NextWetnessRHI, TEXT("DWC.WPPreview.NextWetness"));
            FRDGTextureRef CurrentPendingTexture = RegisterTexture(CurrentPendingRHI, TEXT("DWC.WPPreview.CurrentPending"));
            FRDGTextureRef NextPendingTexture = RegisterTexture(NextPendingRHI, TEXT("DWC.WPPreview.NextPending"));
            FRDGTextureRef Droplet1Texture = RegisterTexture(Droplet1RHI, TEXT("DWC.WPPreview.Droplet1"));
            FRDGTextureRef Droplet2Texture = RegisterTexture(Droplet2RHI, TEXT("DWC.WPPreview.Droplet2"));

            const FRDGTextureDesc WorkingDesc = FRDGTextureDesc::Create2D(
                FIntPoint(RTResolution, RTResolution),
                PF_R16F,
                FClearValueBinding::Black,
                TexCreate_ShaderResource | TexCreate_UAV);
            FRDGTextureRef AppliedWetness = GraphBuilder.CreateTexture(WorkingDesc, TEXT("DWC.WPPreview.AppliedWetness"));
            FRDGTextureRef AppliedPending = GraphBuilder.CreateTexture(WorkingDesc, TEXT("DWC.WPPreview.AppliedPending"));
            AddCopyTexturePass(GraphBuilder, CurrentWetnessTexture, AppliedWetness);
            AddCopyTexturePass(GraphBuilder, CurrentPendingTexture, AppliedPending);

            TShaderMapRef<FDWCSurfaceWetnessDryInPlaceCS> SurfaceDryShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            auto AddSurfaceDry = [&](FRDGTextureRef SurfaceTexture, const TCHAR* Name)
            {
                FDWCSurfaceWetnessDryInPlaceCS::FParameters* P =
                    GraphBuilder.AllocParameters<FDWCSurfaceWetnessDryInPlaceCS::FParameters>();
                P->TextureSize = FIntPoint(RTResolution, RTResolution);
                P->DeltaSeconds = SafeDelta;
                P->Surface = GraphBuilder.CreateUAV(SurfaceTexture);
                P->TexelLookup = LookupSRV;
                P->Profiles = ProfileSRV;
                P->TriangleProfileIndices = ProfileIndicesSRV;
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("DWC WP Preview Surface Dry %s", Name),
                    SurfaceDryShader,
                    P,
                    FIntVector(FMath::DivideAndRoundUp(RTResolution, 8), FMath::DivideAndRoundUp(RTResolution, 8), 1));
            };
            AddSurfaceDry(Droplet1Texture, TEXT("Droplet1"));
            AddSurfaceDry(Droplet2Texture, TEXT("Droplet2"));

            TShaderMapRef<FDWCSurfaceDropletStampCS> SurfaceStampShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            for (const FSurfaceStamp& Stamp : SurfaceStamps)
            {
                const FVector2f CenterPixels = Stamp.UV * static_cast<float>(RTResolution);
                const FIntPoint MinPixel(
                    FMath::Clamp(FMath::FloorToInt(CenterPixels.X - Stamp.HalfSizePixels.X - 1.0f), 0, RTResolution - 1),
                    FMath::Clamp(FMath::FloorToInt(CenterPixels.Y - Stamp.HalfSizePixels.Y - 1.0f), 0, RTResolution - 1));
                const FIntPoint MaxPixel(
                    FMath::Clamp(FMath::CeilToInt(CenterPixels.X + Stamp.HalfSizePixels.X + 1.0f), 0, RTResolution - 1),
                    FMath::Clamp(FMath::CeilToInt(CenterPixels.Y + Stamp.HalfSizePixels.Y + 1.0f), 0, RTResolution - 1));
                const FIntPoint DispatchSize = MaxPixel - MinPixel + FIntPoint(1, 1);
                FDWCSurfaceDropletStampCS::FParameters* P =
                    GraphBuilder.AllocParameters<FDWCSurfaceDropletStampCS::FParameters>();
                P->TextureSize = FIntPoint(RTResolution, RTResolution);
                P->TriangleCount = 1u;
                P->StampMinPixel = MinPixel;
                P->StampDispatchSize = DispatchSize;
                P->StampUV = Stamp.UV;
                P->StampCenterPixels = CenterPixels;
                P->StampHalfSizePixels = Stamp.HalfSizePixels;
                P->StampAmount = Stamp.Amount;
                P->TexelLookup = LookupSRV;
                P->TargetSurface = GraphBuilder.CreateUAV(Stamp.bDroplet2 ? Droplet2Texture : Droplet1Texture);
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("DWC WP Preview Surface Stamp"),
                    SurfaceStampShader,
                    P,
                    FIntVector(FMath::DivideAndRoundUp(DispatchSize.X, 8), FMath::DivideAndRoundUp(DispatchSize.Y, 8), 1));
            }

            if (bWriteAbsorptionSplash && AbsorptionAmount > 0.0f)
            {
                const FVector2f CenterUV = SplashUV;
                // Keep the initial mark compact enough that spreading is visually
                // distinguishable from the contact footprint itself.
                const FVector2f HalfSizePixels(12.0f, 16.0f);
                const FVector2f CenterPixels = CenterUV * static_cast<float>(RTResolution);
                const FIntPoint MinPixel(
                    FMath::Clamp(FMath::FloorToInt(CenterPixels.X - HalfSizePixels.X - 1.0f), 0, RTResolution - 1),
                    FMath::Clamp(FMath::FloorToInt(CenterPixels.Y - HalfSizePixels.Y - 1.0f), 0, RTResolution - 1));
                const FIntPoint MaxPixel(
                    FMath::Clamp(FMath::CeilToInt(CenterPixels.X + HalfSizePixels.X + 1.0f), 0, RTResolution - 1),
                    FMath::Clamp(FMath::CeilToInt(CenterPixels.Y + HalfSizePixels.Y + 1.0f), 0, RTResolution - 1));
                const FIntPoint DispatchSize = MaxPixel - MinPixel + FIntPoint(1, 1);
                FDWCApplyTriangleAbsorptionCS::FParameters* P =
                    GraphBuilder.AllocParameters<FDWCApplyTriangleAbsorptionCS::FParameters>();
                P->TextureSize = FIntPoint(RTResolution, RTResolution);
                P->DispatchMin = MinPixel;
                P->DispatchSize = DispatchSize;
                P->UV01 = FVector4f(
                    CenterUV.X,
                    CenterUV.Y,
                    HalfSizePixels.X / static_cast<float>(RTResolution),
                    HalfSizePixels.Y / static_cast<float>(RTResolution));
                P->UV2AndSettings = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
                P->ContactAndRadius = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
                P->P0AndAmount = FVector4f(0.0f, 0.0f, 0.0f, AbsorptionAmount);
                P->P1AndMaxWetness = FVector4f(0.0f, 0.0f, 0.0f, RTMaxWetness);
                P->P2AndMode = FVector4f(0.0f, 0.0f, 0.0f, 2.0f);
                P->TexelLookup = LookupSRV;
                P->WetnessTexture = GraphBuilder.CreateUAV(AppliedWetness);
                P->PendingWetnessTexture = GraphBuilder.CreateUAV(AppliedPending);
                TShaderMapRef<FDWCApplyTriangleAbsorptionCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("DWC WP Preview Absorption Stamp"),
                    Shader,
                    P,
                    FIntVector(FMath::DivideAndRoundUp(DispatchSize.X, 8), FMath::DivideAndRoundUp(DispatchSize.Y, 8), 1));
            }

            if (bRTUseEightDirections)
            {
                FDWCDiffuseDry8CS::FParameters* P = GraphBuilder.AllocParameters<FDWCDiffuseDry8CS::FParameters>();
                P->TextureSize = FIntPoint(RTResolution, RTResolution);
                P->DeltaSeconds = SafeDelta;
                P->MaxWetness = RTMaxWetness;
                P->DryRateScale = 1.0f;
                P->CapillaryImmediateAbsorptionFraction = RTImmediateAbsorption;
                P->SourceWetnessTexture = AppliedWetness;
                P->PendingWetnessTexture = AppliedPending;
                P->DestinationWetnessTexture = GraphBuilder.CreateUAV(NextWetnessTexture);
                P->DestinationPendingWetnessTexture = GraphBuilder.CreateUAV(NextPendingTexture);
                P->TexelLookup = LookupSRV;
                P->TriangleFlow = FlowSRV;
                P->TriangleMetric = MetricSRV;
                P->Profiles = ProfileSRV;
                P->TriangleProfileIndices = ProfileIndicesSRV;
                TShaderMapRef<FDWCDiffuseDry8CS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("DWC WP Preview Diffuse Dry 8"),
                    Shader,
                    P,
                    FIntVector(FMath::DivideAndRoundUp(RTResolution, 8), FMath::DivideAndRoundUp(RTResolution, 8), 1));
            }
            else
            {
                FDWCDiffuseDryCS::FParameters* P = GraphBuilder.AllocParameters<FDWCDiffuseDryCS::FParameters>();
                P->TextureSize = FIntPoint(RTResolution, RTResolution);
                P->DeltaSeconds = SafeDelta;
                P->MaxWetness = RTMaxWetness;
                P->DryRateScale = 1.0f;
                P->CapillaryImmediateAbsorptionFraction = RTImmediateAbsorption;
                P->SourceWetnessTexture = AppliedWetness;
                P->PendingWetnessTexture = AppliedPending;
                P->DestinationWetnessTexture = GraphBuilder.CreateUAV(NextWetnessTexture);
                P->DestinationPendingWetnessTexture = GraphBuilder.CreateUAV(NextPendingTexture);
                P->TexelLookup = LookupSRV;
                P->TriangleFlow = FlowSRV;
                P->TriangleMetric = MetricSRV;
                P->Profiles = ProfileSRV;
                P->TriangleProfileIndices = ProfileIndicesSRV;
                TShaderMapRef<FDWCDiffuseDryCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("DWC WP Preview Diffuse Dry 4"),
                    Shader,
                    P,
                    FIntVector(FMath::DivideAndRoundUp(RTResolution, 8), FMath::DivideAndRoundUp(RTResolution, 8), 1));
            }

            GraphBuilder.Execute();
        });

    CurrentWetnessIndex = 1 - CurrentWetnessIndex;
    CurrentPendingIndex = 1 - CurrentPendingIndex;
}

UTextureRenderTarget2D* FDWCGPUPreviewSimulator::GetWetnessMap() const
{
    return WetnessMaps.IsValidIndex(CurrentWetnessIndex) ? WetnessMaps[CurrentWetnessIndex].Get() : nullptr;
}

UTextureRenderTarget2D* FDWCGPUPreviewSimulator::GetDroplet1Map() const
{
    return Droplet1Map.Get();
}

UTextureRenderTarget2D* FDWCGPUPreviewSimulator::GetDroplet2Map() const
{
    return Droplet2Map.Get();
}

void FDWCGPUPreviewSimulator::Shutdown()
{
    if (!bInitialized && WetnessMaps.IsEmpty() && PendingWetnessMaps.IsEmpty() &&
        !Droplet1Map.IsValid() && !Droplet2Map.IsValid() && !RenderState.IsValid())
    {
        return;
    }

    bInitialized = false;
    FlushRenderingCommands();
    WetnessMaps.Reset();
    PendingWetnessMaps.Reset();
    Droplet1Map.Reset();
    Droplet2Map.Reset();
    RenderState.Reset();
    CachedParameters.Reset();
    CurrentWetnessIndex = 0;
    CurrentPendingIndex = 0;
    bManualSplashRequested = false;
}
