#include "SurfaceWater/DWCSurfaceWaterSimulationState.h"

#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "Profiling/DWCStats.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "ShaderParameterStruct.h"

class FDWCCPUSurfaceWaterStampCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCCPUSurfaceWaterStampCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCCPUSurfaceWaterStampCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, Resolution)
        SHADER_PARAMETER(FIntPoint, StampMinPixel)
        SHADER_PARAMETER(FIntPoint, StampDispatchSize)
        SHADER_PARAMETER(FVector2f, StampCenterPixels)
        SHADER_PARAMETER(FVector2f, StampHalfSizePixels)
        SHADER_PARAMETER(float, StampAmount)
        SHADER_PARAMETER(float, StampTimeSeconds)
        SHADER_PARAMETER(float, StampLifetimeSeconds)
        SHADER_PARAMETER(float, EncodedDataUVFlowAngle)
        SHADER_PARAMETER(float, EncodedNormalUVFlowAngle)
        SHADER_PARAMETER(uint32, StampType)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TargetSurface)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(
    FDWCCPUSurfaceWaterStampCS,
    "/DWCGPU/DWCCPUSurfaceWaterStamp.usf",
    "StampCS",
    SF_Compute);

namespace
{
    struct FDWCCPUSurfaceWaterRenderStamp
    {
        EDWCCPUSurfaceWaterStampType Type = EDWCCPUSurfaceWaterStampType::Droplet;
        FVector2f UV = FVector2f::ZeroVector;
        float Amount = 0.0f;
        FVector2f HalfSizePixels = FVector2f::ZeroVector;
        float LifetimeSeconds = 0.0f;
        float EncodedDataUVFlowAngle = 0.75f;
        float EncodedNormalUVFlowAngle = 0.75f;
    };

    UTextureRenderTarget2D* CreateSurfaceRenderTarget(UObject* Outer, const int32 Resolution)
    {
        if (Outer == nullptr || Resolution <= 0)
        {
            return nullptr;
        }

        UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(Outer, NAME_None, RF_Transient);
        if (RenderTarget == nullptr)
        {
            return nullptr;
        }

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

bool FDWCSurfaceWaterSimulationState::Initialize(UObject* Outer, const int32 InResolution)
{
    Release();
    Resolution = FMath::Clamp(InResolution, 16, 4096);
    DropletRenderTarget.Reset(CreateSurfaceRenderTarget(Outer, Resolution));
    RivuletRenderTarget.Reset(CreateSurfaceRenderTarget(Outer, Resolution));
    return IsValid();
}

bool FDWCSurfaceWaterSimulationState::IsValid() const
{
    return DropletRenderTarget.IsValid() && RivuletRenderTarget.IsValid() && Resolution > 0;
}

void FDWCSurfaceWaterSimulationState::Reset()
{
    PendingStamps.Reset();
    for (UTextureRenderTarget2D* RenderTarget : {DropletRenderTarget.Get(), RivuletRenderTarget.Get()})
    {
        if (RenderTarget != nullptr)
        {
            RenderTarget->ClearColor = FLinearColor::Transparent;
            RenderTarget->UpdateResourceImmediate(true);
        }
    }
}

void FDWCSurfaceWaterSimulationState::Release()
{
    PendingStamps.Reset();
    DropletRenderTarget.Reset();
    RivuletRenderTarget.Reset();
    Resolution = 0;
    bSimulationPaused = false;
}

void FDWCSurfaceWaterSimulationState::QueueStamp(const FDWCCPUSurfaceWaterStamp& Stamp)
{
    if (!IsValid() || Stamp.UV.ContainsNaN() || !FMath::IsFinite(Stamp.UV.X) || !FMath::IsFinite(Stamp.UV.Y) ||
        Stamp.Amount <= 0.0f || Stamp.LifetimeSeconds <= 0.0f)
    {
        return;
    }

    PendingStamps.Add(Stamp);
    FDWCWorkloadStats::RecordSurfaceWaterStampQueued(PendingStamps.Num());
}

FDWCCPUSurfaceWaterStamp FDWCSurfaceWaterSimulationState::BuildStamp(
    const EDWCCPUSurfaceWaterStampType Type,
    const FVector2f& UV,
    const float Amount,
    const FVector2f& HalfSizePixels,
    const float LifetimeSeconds,
    const float EncodedDataUVFlowAngle,
    const float EncodedNormalUVFlowAngle) const
{
    FDWCCPUSurfaceWaterStamp Stamp;
    Stamp.Type = Type;
    Stamp.UV = UV;
    Stamp.Amount = Amount;
    Stamp.LifetimeSeconds = FMath::Max(0.01f, LifetimeSeconds);
    Stamp.HalfSizePixels = HalfSizePixels;
    Stamp.EncodedDataUVFlowAngle = FMath::Clamp(EncodedDataUVFlowAngle, 0.0f, 1.0f);
    Stamp.EncodedNormalUVFlowAngle = FMath::Clamp(EncodedNormalUVFlowAngle, 0.0f, 1.0f);
    return Stamp;
}

void FDWCSurfaceWaterSimulationState::QueueDropletStamp(
    const FVector2f& UV,
    const float Amount,
    const float RadiusPixels,
    const float LifetimeSeconds)
{
    QueueStamp(BuildStamp(
        EDWCCPUSurfaceWaterStampType::Droplet,
        UV,
        Amount,
        FVector2f(FMath::Max(0.5f, RadiusPixels)),
        LifetimeSeconds));
}

void FDWCSurfaceWaterSimulationState::QueueRivuletStamp(
    const FVector2f& UV,
    const float EncodedDataUVFlowAngle,
    const float EncodedNormalUVFlowAngle,
    const float Amount,
    const float WidthPixels,
    const float LengthPixels,
    const float LifetimeSeconds)
{
    QueueStamp(BuildStamp(
        EDWCCPUSurfaceWaterStampType::Rivulet,
        UV,
        Amount,
        FVector2f(FMath::Max(0.5f, WidthPixels * 0.5f), FMath::Max(0.5f, LengthPixels * 0.5f)),
        LifetimeSeconds,
        EncodedDataUVFlowAngle,
        EncodedNormalUVFlowAngle));
}

uint64 FDWCSurfaceWaterSimulationState::GetEstimatedGpuMemoryBytes() const
{
    // Two RGBA32f render targets: droplet state and rivulet state.
    return Resolution > 0 ? static_cast<uint64>(Resolution) * Resolution * 16ull * 2ull : 0ull;
}

uint64 FDWCSurfaceWaterSimulationState::GetAllocatedMemoryBytes() const
{
    return sizeof(*this) + PendingStamps.GetAllocatedSize();
}

bool FDWCSurfaceWaterSimulationState::FlushStamps(const float CurrentSurfaceTimeSeconds)
{
    if (!IsValid() || bSimulationPaused || PendingStamps.IsEmpty())
    {
        return false;
    }

    FTextureRenderTargetResource* DropletResource = DropletRenderTarget->GameThread_GetRenderTargetResource();
    FTextureRenderTargetResource* RivuletResource = RivuletRenderTarget->GameThread_GetRenderTargetResource();
    if (DropletResource == nullptr || RivuletResource == nullptr)
    {
        return false;
    }

    const int32 TargetResolution = Resolution;
    TArray<FDWCCPUSurfaceWaterStamp> StampsToApply = MoveTemp(PendingStamps);
    PendingStamps.Reset();
    FDWCWorkloadStats::RecordSurfaceWaterStampsSubmitted(StampsToApply.Num());

    TArray<FDWCCPUSurfaceWaterRenderStamp> RenderStamps;
    RenderStamps.Reserve(StampsToApply.Num());
    for (const FDWCCPUSurfaceWaterStamp& Stamp : StampsToApply)
    {
        FDWCCPUSurfaceWaterRenderStamp& RenderStamp = RenderStamps.AddDefaulted_GetRef();
        RenderStamp.Type = Stamp.Type;
        RenderStamp.UV = Stamp.UV;
        RenderStamp.Amount = Stamp.Amount;
        RenderStamp.HalfSizePixels = Stamp.HalfSizePixels;
        RenderStamp.LifetimeSeconds = Stamp.LifetimeSeconds;
        RenderStamp.EncodedDataUVFlowAngle = Stamp.EncodedDataUVFlowAngle;
        RenderStamp.EncodedNormalUVFlowAngle = Stamp.EncodedNormalUVFlowAngle;
    }

    ENQUEUE_RENDER_COMMAND(DWCCPUSurfaceWaterStamp)(
        [DropletResource,
         RivuletResource,
         TargetResolution,
         CurrentSurfaceTimeSeconds,
         RenderStamps = MoveTemp(RenderStamps)](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGTextureRef DropletTexture = GraphBuilder.RegisterExternalTexture(
                CreateRenderTarget(DropletResource->GetRenderTargetTexture(), TEXT("DWC.CPU.SurfaceDroplet")));
            FRDGTextureRef RivuletTexture = GraphBuilder.RegisterExternalTexture(
                CreateRenderTarget(RivuletResource->GetRenderTargetTexture(), TEXT("DWC.CPU.SurfaceRivulet")));

            TShaderMapRef<FDWCCPUSurfaceWaterStampCS> StampShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            for (const FDWCCPUSurfaceWaterRenderStamp& Stamp : RenderStamps)
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

                FDWCCPUSurfaceWaterStampCS::FParameters* Parameters =
                    GraphBuilder.AllocParameters<FDWCCPUSurfaceWaterStampCS::FParameters>();
                Parameters->Resolution = FIntPoint(TargetResolution, TargetResolution);
                Parameters->StampMinPixel = MinPixel;
                Parameters->StampDispatchSize = DispatchSize;
                Parameters->StampCenterPixels = CenterPixels;
                Parameters->StampHalfSizePixels = Stamp.HalfSizePixels;
                Parameters->StampAmount = Stamp.Amount;
                Parameters->StampTimeSeconds = CurrentSurfaceTimeSeconds;
                Parameters->StampLifetimeSeconds = Stamp.LifetimeSeconds;
                Parameters->EncodedDataUVFlowAngle = Stamp.EncodedDataUVFlowAngle;
                Parameters->EncodedNormalUVFlowAngle = Stamp.EncodedNormalUVFlowAngle;
                Parameters->StampType =
                    Stamp.Type == EDWCCPUSurfaceWaterStampType::Rivulet ? 1u : 0u;
                Parameters->TargetSurface = GraphBuilder.CreateUAV(
                    Stamp.Type == EDWCCPUSurfaceWaterStampType::Rivulet ? RivuletTexture : DropletTexture);

                FDWCWorkloadStats::RecordSurfaceWaterGPUDispatch();
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME(
                        "DWC CPU Surface %s Stamp",
                        Stamp.Type == EDWCCPUSurfaceWaterStampType::Rivulet ? TEXT("Rivulet") : TEXT("Droplet")),
                    StampShader,
                    Parameters,
                    FComputeShaderUtils::GetGroupCount(DispatchSize, FIntPoint(8, 8)));
            }

            GraphBuilder.Execute();
        });

    return true;
}
