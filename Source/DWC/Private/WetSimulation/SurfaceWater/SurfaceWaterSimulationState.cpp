#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationState.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderGraphBuilder.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"

class FSurfaceWaterStampCS : public FGlobalShader
{
  public:
    DECLARE_GLOBAL_SHADER(FSurfaceWaterStampCS);
    SHADER_USE_PARAMETER_STRUCT(FSurfaceWaterStampCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, Resolution)
        SHADER_PARAMETER(FIntPoint, StampMinPixel)
        SHADER_PARAMETER(FIntPoint, StampDispatchSize)
        SHADER_PARAMETER(FVector2f, StampCenterPixels)
        SHADER_PARAMETER(FVector2f, StampHalfSizePixels)
        SHADER_PARAMETER(float, StampAmount)
        SHADER_PARAMETER(float, StampTimeSeconds)
        SHADER_PARAMETER(float, StampLifetimeSeconds)
        SHADER_PARAMETER(uint32, bFlowStamp)
        SHADER_PARAMETER(uint32, bHasFlowMap)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FlowMap)
        SHADER_PARAMETER_SAMPLER(SamplerState, FlowSampler)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TargetSurface)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSurfaceWaterStampCS, "/DynamicWetClothes/SurfaceWaterStamp.usf", "StampCS", SF_Compute);

namespace
{
    struct FSurfaceWaterRenderStamp
    {
        ESurfaceWaterStampType Type = ESurfaceWaterStampType::Droplet;
        FVector2f UV = FVector2f::ZeroVector;
        float Amount = 0.0f;
        FVector2f HalfSizePixels = FVector2f::ZeroVector;
        float LifetimeSeconds = 0.0f;
    };

    UTextureRenderTarget2D* CreateSurfaceRenderTarget(UObject* Outer, const int32 Resolution)
    {
        UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(Outer, NAME_None, RF_Transient);
        RenderTarget->RenderTargetFormat = RTF_RGBA32f;
        RenderTarget->ClearColor = FLinearColor::Transparent;
        RenderTarget->bAutoGenerateMips = false;
        RenderTarget->bCanCreateUAV = true;
        RenderTarget->AddressX = TA_Clamp;
        RenderTarget->AddressY = TA_Clamp;
        RenderTarget->InitAutoFormat(Resolution, Resolution);
        RenderTarget->UpdateResourceImmediate(true);
        return RenderTarget;
    }
}

bool FSurfaceWaterSimulationState::Initialize(UObject* Outer, const int32 InResolution)
{
    Release();
    Resolution = FMath::Clamp(InResolution, 16, 4096);
    DropletRenderTarget.Reset(CreateSurfaceRenderTarget(Outer, Resolution));
    FlowRenderTarget.Reset(CreateSurfaceRenderTarget(Outer, Resolution));
    return IsValid();
}

bool FSurfaceWaterSimulationState::IsValid() const
{
    return DropletRenderTarget.IsValid() && FlowRenderTarget.IsValid() && Resolution > 0;
}

void FSurfaceWaterSimulationState::Reset()
{
    PendingStamps.Reset();
    for (UTextureRenderTarget2D* RenderTarget : {DropletRenderTarget.Get(), FlowRenderTarget.Get()})
    {
        if (RenderTarget)
        {
            RenderTarget->ClearColor = FLinearColor::Transparent;
            RenderTarget->UpdateResourceImmediate(true);
        }
    }
}

void FSurfaceWaterSimulationState::Release()
{
    PendingStamps.Reset();
    DropletRenderTarget.Reset();
    FlowRenderTarget.Reset();
    Resolution = 0;
}

void FSurfaceWaterSimulationState::QueueStamp(const FSurfaceWaterStamp& Stamp)
{
    if (!IsValid() || Stamp.UV.ContainsNaN() || !FMath::IsFinite(Stamp.UV.X) || !FMath::IsFinite(Stamp.UV.Y) ||
        Stamp.Amount <= 0.0f || Stamp.LifetimeSeconds <= 0.0f)
    {
        return;
    }
    PendingStamps.Add(Stamp);
}

FSurfaceWaterStamp FSurfaceWaterSimulationState::BuildStamp(
    const ESurfaceWaterStampType Type,
    const FVector2f& UV,
    const float Amount,
    const FVector2f& HalfSizePixels,
    const float LifetimeSeconds) const
{
    FSurfaceWaterStamp Stamp;
    Stamp.Type = Type;
    Stamp.UV = UV;
    Stamp.Amount = Amount;
    Stamp.LifetimeSeconds = FMath::Max(0.01f, LifetimeSeconds);
    Stamp.HalfSizePixels = HalfSizePixels;
    return Stamp;
}

void FSurfaceWaterSimulationState::QueueDropletStamp(
    const FVector2f& UV,
    const float Amount,
    const float RadiusPixels,
    const float LifetimeSeconds)
{
    QueueStamp(BuildStamp(
        ESurfaceWaterStampType::Droplet,
        UV,
        Amount,
        FVector2f(FMath::Max(0.5f, RadiusPixels)),
        LifetimeSeconds));
}

void FSurfaceWaterSimulationState::QueueFlowStamp(
    const FVector2f& UV,
    const float Amount,
    const float WidthPixels,
    const float LengthPixels,
    const float LifetimeSeconds)
{
    QueueStamp(BuildStamp(
        ESurfaceWaterStampType::Flow,
        UV,
        Amount,
        FVector2f(FMath::Max(0.5f, WidthPixels * 0.5f), FMath::Max(0.5f, LengthPixels * 0.5f)),
        LifetimeSeconds));
}

uint64 FSurfaceWaterSimulationState::GetEstimatedGpuMemoryBytes() const
{
    // Two RGBA32f render targets, one each for droplet and flow accumulation.
    return Resolution > 0 ? static_cast<uint64>(Resolution) * Resolution * 16ull * 2ull : 0ull;
}

bool FSurfaceWaterSimulationState::FlushStamps(UTexture2D* FlowMap, const float CurrentSurfaceTimeSeconds)
{
    if (!IsValid() || bSimulationPaused || PendingStamps.IsEmpty())
    {
        return false;
    }

    FTextureRenderTargetResource* DropletResource = DropletRenderTarget->GameThread_GetRenderTargetResource();
    FTextureRenderTargetResource* FlowTargetResource = FlowRenderTarget->GameThread_GetRenderTargetResource();
    FTextureResource* FlowMapResource = FlowMap ? FlowMap->GetResource() : nullptr;
    const int32 TargetResolution = Resolution;
    TArray<FSurfaceWaterStamp> StampsToApply = MoveTemp(PendingStamps);
    PendingStamps.Reset();

    TArray<FSurfaceWaterRenderStamp> RenderStamps;
    RenderStamps.Reserve(StampsToApply.Num());
    for (const FSurfaceWaterStamp& Stamp : StampsToApply)
    {
        FSurfaceWaterRenderStamp& RenderStamp = RenderStamps.AddDefaulted_GetRef();
        RenderStamp.Type = Stamp.Type;
        RenderStamp.UV = Stamp.UV;
        RenderStamp.Amount = Stamp.Amount;
        RenderStamp.HalfSizePixels = Stamp.HalfSizePixels;
        RenderStamp.LifetimeSeconds = Stamp.LifetimeSeconds;
    }

    ENQUEUE_RENDER_COMMAND(DWCSurfaceWaterStamp)(
        [DropletResource, FlowTargetResource, FlowMapResource, TargetResolution, CurrentSurfaceTimeSeconds,
         RenderStamps = MoveTemp(RenderStamps)](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGTextureRef DropletTexture = GraphBuilder.RegisterExternalTexture(
                CreateRenderTarget(DropletResource->GetRenderTargetTexture(), TEXT("DWC_SurfaceDroplet")));
            FRDGTextureRef FlowTexture = GraphBuilder.RegisterExternalTexture(
                CreateRenderTarget(FlowTargetResource->GetRenderTargetTexture(), TEXT("DWC_SurfaceFlow")));

            FRDGTextureRef BakedFlowTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
            const bool bHasFlowMap = FlowMapResource && FlowMapResource->TextureRHI.IsValid();
            if (bHasFlowMap)
            {
                BakedFlowTexture = GraphBuilder.RegisterExternalTexture(
                    CreateRenderTarget(FlowMapResource->TextureRHI, TEXT("DWC_SurfaceBakedFlowMap")));
            }

            TShaderMapRef<FSurfaceWaterStampCS> StampShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            for (const FSurfaceWaterRenderStamp& Stamp : RenderStamps)
            {
                const FVector2f CenterPixels = Stamp.UV * static_cast<float>(TargetResolution);
                const float BoundingRadius = FMath::Max(Stamp.HalfSizePixels.X, Stamp.HalfSizePixels.Y);
                const FIntPoint MinPixel(
                    FMath::Clamp(FMath::FloorToInt(CenterPixels.X - BoundingRadius), 0, TargetResolution - 1),
                    FMath::Clamp(FMath::FloorToInt(CenterPixels.Y - BoundingRadius), 0, TargetResolution - 1));
                const FIntPoint MaxPixel(
                    FMath::Clamp(FMath::CeilToInt(CenterPixels.X + BoundingRadius), 0, TargetResolution - 1),
                    FMath::Clamp(FMath::CeilToInt(CenterPixels.Y + BoundingRadius), 0, TargetResolution - 1));
                const FIntPoint DispatchSize = MaxPixel - MinPixel + FIntPoint(1, 1);
                if (DispatchSize.X <= 0 || DispatchSize.Y <= 0)
                {
                    continue;
                }

                FSurfaceWaterStampCS::FParameters* Parameters = GraphBuilder.AllocParameters<FSurfaceWaterStampCS::FParameters>();
                Parameters->Resolution = FIntPoint(TargetResolution, TargetResolution);
                Parameters->StampMinPixel = MinPixel;
                Parameters->StampDispatchSize = DispatchSize;
                Parameters->StampCenterPixels = CenterPixels;
                Parameters->StampHalfSizePixels = Stamp.HalfSizePixels;
                Parameters->StampAmount = Stamp.Amount;
                Parameters->StampTimeSeconds = CurrentSurfaceTimeSeconds;
                Parameters->StampLifetimeSeconds = Stamp.LifetimeSeconds;
                Parameters->bFlowStamp = Stamp.Type == ESurfaceWaterStampType::Flow ? 1u : 0u;
                Parameters->bHasFlowMap = bHasFlowMap ? 1u : 0u;
                Parameters->FlowMap = BakedFlowTexture;
                Parameters->FlowSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
                Parameters->TargetSurface = GraphBuilder.CreateUAV(
                    Stamp.Type == ESurfaceWaterStampType::Flow ? FlowTexture : DropletTexture);

                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("DWC Surface %s Stamp", Stamp.Type == ESurfaceWaterStampType::Flow ? TEXT("Flow") : TEXT("Droplet")),
                    StampShader,
                    Parameters,
                    FComputeShaderUtils::GetGroupCount(DispatchSize, FIntPoint(8, 8)));
            }
            GraphBuilder.Execute();
        });

    return true;
}
