#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

/** Applies a resolved world-space contact to one current-pose triangle UV region. */
class FDWCApplyTriangleAbsorptionCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCApplyTriangleAbsorptionCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCApplyTriangleAbsorptionCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, TextureSize)
        SHADER_PARAMETER(FIntPoint, DispatchMin)
        SHADER_PARAMETER(FIntPoint, DispatchSize)
        SHADER_PARAMETER(FVector4f, UV01)
        SHADER_PARAMETER(FVector4f, UV2AndSettings)
        SHADER_PARAMETER(FVector4f, ContactAndRadius)
        SHADER_PARAMETER(FVector4f, P0AndAmount)
        SHADER_PARAMETER(FVector4f, P1AndMaxWetness)
        SHADER_PARAMETER(FVector4f, P2AndMode)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, WetnessTexture)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** Builds one dynamic gravity/area entry for every simulation-LOD triangle in a render section. */
class FDWCUpdateTriangleFlowCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCUpdateTriangleFlowCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCUpdateTriangleFlowCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, TriangleCount)
        SHADER_PARAMETER(uint32, PositionIndexBase)
        SHADER_PARAMETER(FMatrix44f, LocalToWorld)
        SHADER_PARAMETER(FVector4f, WorldGravityDirection)
        SHADER_PARAMETER_SRV(Buffer<float>, PositionBuffer)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TriangleIndices)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleUV01)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleUV2RestArea)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, TriangleFlow)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, TriangleMetric)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** Conservative destination gather for absorbed-wetness spread, gravity bias, and drying. */
class FDWCDiffuseDryCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCDiffuseDryCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCDiffuseDryCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, TextureSize)
        SHADER_PARAMETER(float, DeltaSeconds)
        SHADER_PARAMETER(float, MaxWetness)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceWetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, DestinationWetnessTexture)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TexelLookup)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleFlow)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleMetric)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Profiles)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriangleProfileIndices)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** Destination-oriented seam gather; each thread owns exactly one destination texel. */
class FDWCSeamGatherCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCSeamGatherCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCSeamGatherCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, TextureSize)
        SHADER_PARAMETER(uint32, SeamDestinationCount)
        SHADER_PARAMETER(float, DeltaSeconds)
        SHADER_PARAMETER(float, SeamTransferScale)
        SHADER_PARAMETER(float, MaxWetness)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceWetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, DestinationWetnessTexture)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, SeamDestinations)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SeamIncoming)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TexelLookup)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleFlow)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Profiles)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriangleProfileIndices)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
