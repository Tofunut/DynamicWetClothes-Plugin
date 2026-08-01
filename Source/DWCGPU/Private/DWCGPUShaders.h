#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

/** Applies a resolved world-space contact, wet-all triangle, or Data-UV stamp. */
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
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TexelLookup)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, WetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, PendingWetnessTexture)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** Applies many positive wetness contacts in one destination-gather pass using tile-local contact bins. */
class FDWCApplyBinnedAbsorptionCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCApplyBinnedAbsorptionCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCApplyBinnedAbsorptionCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, TextureSize)
        SHADER_PARAMETER(FIntPoint, TileGridSize)
        SHADER_PARAMETER(uint32, TileSize)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Contacts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, TileBins)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileContactIndices)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TexelLookup)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, PendingWetnessTexture)
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
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, TrianglePositions)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** Fallback pose update used when Compute Skin Cache geometry is unavailable for the receiver. */
class FDWCUpdateRestTriangleFlowCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCUpdateRestTriangleFlowCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCUpdateRestTriangleFlowCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, TriangleCount)
        SHADER_PARAMETER(uint32, PositionCount)
        SHADER_PARAMETER(FMatrix44f, LocalToWorld)
        SHADER_PARAMETER(FVector4f, WorldGravityDirection)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, RestPositions)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TriangleIndices)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleUV01)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleUV2RestArea)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, TriangleFlow)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, TriangleMetric)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, TrianglePositions)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** Applies Niagara GPU-written wet contact candidates directly to wet-map texels. */
class FDWCApplyNiagaraWetCollisionCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCApplyNiagaraWetCollisionCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCApplyNiagaraWetCollisionCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, TextureSize)
        SHADER_PARAMETER(int32, MaxContacts)
        SHADER_PARAMETER(FVector3f, ReceiverBoundsMin)
        SHADER_PARAMETER(FVector3f, ReceiverBoundsMax)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, Contacts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<int>, ContactCount)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TexelLookup)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TrianglePositions)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, PendingWetnessTexture)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** Resolves each Niagara GPU contact to one nearest current-pose wettable triangle. */
class FDWCResolveNiagaraDropletContactsCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCResolveNiagaraDropletContactsCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCResolveNiagaraDropletContactsCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, TriangleCount)
        SHADER_PARAMETER(uint32, ContactIndexOffset)
        SHADER_PARAMETER(int32, MaxContacts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, Contacts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<int>, ContactCount)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TrianglePositions)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleUV01)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleUV2AndDroplet)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleFlowDropletSettings)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, TriangleFlowSpawnPositionSpread)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TriangleSurfaceMetadata)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, ResolvedStaticContacts)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, ResolvedFlowContacts)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** Writes resolved Niagara GPU contacts into the selected slot's Droplet1 or Droplet2 RT. */
class FDWCStampNiagaraDropletsCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCStampNiagaraDropletsCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCStampNiagaraDropletsCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, TextureSize)
        SHADER_PARAMETER(uint32, TriangleCount)
        SHADER_PARAMETER(uint32, MaterialSlotIndex)
        SHADER_PARAMETER(uint32, bDroplet2)
        SHADER_PARAMETER(int32, MaxContacts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, Contacts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<int>, ContactCount)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, ResolvedContacts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleUV2AndDroplet)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleFlowDropletSettings)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TriangleSurfaceMetadata)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TexelLookup)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, TargetSurface)
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
        SHADER_PARAMETER(float, DryRateScale)
        SHADER_PARAMETER(float, CapillaryImmediateAbsorptionFraction)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceWetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PendingWetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, DestinationWetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, DestinationPendingWetnessTexture)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TexelLookup)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleFlow)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleMetric)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Profiles)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriangleProfileIndices)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** 8-neighbor CPU-style Pending spread, gravity bias, and drying. */
class FDWCDiffuseDry8CS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCDiffuseDry8CS);
    SHADER_USE_PARAMETER_STRUCT(FDWCDiffuseDry8CS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, TextureSize)
        SHADER_PARAMETER(float, DeltaSeconds)
        SHADER_PARAMETER(float, MaxWetness)
        SHADER_PARAMETER(float, DryRateScale)
        SHADER_PARAMETER(float, CapillaryImmediateAbsorptionFraction)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceWetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PendingWetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, DestinationWetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, DestinationPendingWetnessTexture)
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
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourcePendingWetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, DestinationWetnessTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, DestinationPendingWetnessTexture)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, SeamDestinations)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SeamIncoming)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TexelLookup)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TriangleFlow)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Profiles)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriangleProfileIndices)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** Writes one droplet stamp into a slot-local surface-state RT. */
class FDWCSurfaceDropletStampCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCSurfaceDropletStampCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCSurfaceDropletStampCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, TextureSize)
        SHADER_PARAMETER(uint32, TriangleCount)
        SHADER_PARAMETER(FIntPoint, StampMinPixel)
        SHADER_PARAMETER(FIntPoint, StampDispatchSize)
        SHADER_PARAMETER(FVector2f, StampUV)
        SHADER_PARAMETER(FVector2f, StampCenterPixels)
        SHADER_PARAMETER(FVector2f, StampHalfSizePixels)
        SHADER_PARAMETER(float, StampAmount)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TexelLookup)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, TargetSurface)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

/** Applies the shared Wetness Profile Droplet Dry Rate to single-channel Droplet1/2 wetness. */
class FDWCSurfaceWetnessDryInPlaceCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDWCSurfaceWetnessDryInPlaceCS);
    SHADER_USE_PARAMETER_STRUCT(FDWCSurfaceWetnessDryInPlaceCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, TextureSize)
        SHADER_PARAMETER(float, DeltaSeconds)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, Surface)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, TexelLookup)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Profiles)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriangleProfileIndices)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

