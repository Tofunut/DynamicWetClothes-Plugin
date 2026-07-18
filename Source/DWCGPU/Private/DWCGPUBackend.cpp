#include "DWCGPUBackend.h"

#include "Utility/DWCDataUVBufferView.h"
#include "TextureResource.h"
#include "WetRendering/WetMaterialParameters.h"

#include "DWCGPUShaders.h"
#include "CachedGeometry.h"
#include "Components/DynamicWetClothesComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/WetClothingSettings.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"
#include "SkeletalRenderPublic.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCGPU, Log, All);

namespace DWCGPUBackendPrivate
{
// Set true only for the separate point-sampling diagnostic.
// Restart PIE after changing this because render targets are created during Initialize().
constexpr bool GDWCForceNearestWetnessSampling = false;

constexpr int32 DWCFullSimulationMapVersion = FDWCGPULODBakeData::CurrentMapBakeVersion;
constexpr float DWCSeamTransferScale = 1.0f;

uint32 FloatToBits(const float Value)
{
    uint32 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(uint32));
    return Bits;
}

struct alignas(16) FUint4GPU
{
    uint32 X = 0;
    uint32 Y = 0;
    uint32 Z = 0;
    uint32 W = 0;

    FUint4GPU() = default;
    FUint4GPU(uint32 InX, uint32 InY, uint32 InZ, uint32 InW)
        : X(InX), Y(InY), Z(InZ), W(InW)
    {
    }
};

static_assert(sizeof(FUint4GPU) == 16, "FUint4GPU must match HLSL uint4.");
static_assert(sizeof(FVector4f) == 16, "FVector4f must match HLSL float4.");

struct FTriangleAbsorptionDispatch
{
    FIntPoint DispatchMin = FIntPoint::ZeroValue;
    FIntPoint DispatchSize = FIntPoint::ZeroValue;
    FVector4f UV01 = FVector4f(0, 0, 0, 0);
    FVector4f UV2AndSettings = FVector4f(0, 0, 0, 0);
    FVector4f ContactAndRadius = FVector4f(0, 0, 0, 1);
    FVector4f P0AndAmount = FVector4f(0, 0, 0, 0);
    FVector4f P1AndMaxWetness = FVector4f(0, 0, 0, 1);
    FVector4f P2AndMode = FVector4f(0, 0, 0, 0);
};

struct FSlotRenderDispatch
{
    int32 SlotRuntimeIndex = INDEX_NONE;
    int32 StaticSlotIndex = INDEX_NONE;
    int32 Resolution = 0;
    FTextureRenderTargetResource* CurrentResource = nullptr;
    FTextureRenderTargetResource* NextResource = nullptr;
    TArray<FTriangleAbsorptionDispatch> AbsorptionDispatches;
};

bool BuildDispatchBounds(
    const FDWCGPUBakedTriangle& Triangle,
    const int32 Resolution,
    FIntPoint& OutMin,
    FIntPoint& OutSize)
{
    if (Resolution <= 0)
    {
        return false;
    }

    const double MinU = FMath::Min3(Triangle.UV0.X, Triangle.UV1.X, Triangle.UV2.X);
    const double MaxU = FMath::Max3(Triangle.UV0.X, Triangle.UV1.X, Triangle.UV2.X);
    const double MinV = FMath::Min3(Triangle.UV0.Y, Triangle.UV1.Y, Triangle.UV2.Y);
    const double MaxV = FMath::Max3(Triangle.UV0.Y, Triangle.UV1.Y, Triangle.UV2.Y);

    const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * Resolution), 0, Resolution - 1);
    const int32 MinY = FMath::Clamp(FMath::FloorToInt(MinV * Resolution), 0, Resolution - 1);
    const int32 MaxX = FMath::Clamp(FMath::CeilToInt(MaxU * Resolution) - 1, 0, Resolution - 1);
    const int32 MaxY = FMath::Clamp(FMath::CeilToInt(MaxV * Resolution) - 1, 0, Resolution - 1);

    if (MaxX < MinX || MaxY < MinY)
    {
        return false;
    }

    OutMin = FIntPoint(MinX, MinY);
    OutSize = FIntPoint(MaxX - MinX + 1, MaxY - MinY + 1);
    return OutSize.X > 0 && OutSize.Y > 0;
}

FVector4f MakePositionAndValue(const FVector& Position, const float Value)
{
    return FVector4f(
        static_cast<float>(Position.X),
        static_cast<float>(Position.Y),
        static_cast<float>(Position.Z),
        Value);
}

void FillTriangleUVs(const FDWCGPUBakedTriangle& Triangle, FTriangleAbsorptionDispatch& OutDispatch)
{
    OutDispatch.UV01 = FVector4f(
        static_cast<float>(Triangle.UV0.X),
        static_cast<float>(Triangle.UV0.Y),
        static_cast<float>(Triangle.UV1.X),
        static_cast<float>(Triangle.UV1.Y));
    OutDispatch.UV2AndSettings = FVector4f(
        static_cast<float>(Triangle.UV2.X),
        static_cast<float>(Triangle.UV2.Y),
        0.0f,
        0.0f);
}

void CollectExpectedWettableSlots(const UWetClothingAsset& Asset, TSet<int32>& OutMaterialSlots)
{
    OutMaterialSlots.Reset();
    for (const FWetClothingWettableMaterialSlotState& SlotState : Asset.PartData.EditableWetPartData.WettableMaterialSlots)
    {
        if (SlotState.bIsWettableSlot && SlotState.MaterialSlotIndex != INDEX_NONE)
        {
            OutMaterialSlots.Add(SlotState.MaterialSlotIndex);
        }
    }
}

FString JoinSortedSlotSet(const TSet<int32>& MaterialSlots)
{
    TArray<int32> SortedSlots = MaterialSlots.Array();
    SortedSlots.Sort();

    TArray<FString> SlotStrings;
    SlotStrings.Reserve(SortedSlots.Num());
    for (const int32 MaterialSlot : SortedSlots)
    {
        SlotStrings.Add(FString::FromInt(MaterialSlot));
    }
    return FString::Join(SlotStrings, TEXT(","));
}

bool MaterialHasTextureParameter(UMaterialInterface* Material, const FName ParameterName)
{
    if (Material == nullptr || ParameterName.IsNone())
    {
        return false;
    }

    TArray<FMaterialParameterInfo> ParameterInfos;
    TArray<FGuid> ParameterIds;
    Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
    return ParameterInfos.ContainsByPredicate(
        [ParameterName](const FMaterialParameterInfo& ParameterInfo)
        {
            return ParameterInfo.Name == ParameterName;
        });
}

bool MaterialHasScalarParameter(UMaterialInterface* Material, const FName ParameterName)
{
    if (Material == nullptr || ParameterName.IsNone())
    {
        return false;
    }

    TArray<FMaterialParameterInfo> ParameterInfos;
    TArray<FGuid> ParameterIds;
    Material->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
    return ParameterInfos.ContainsByPredicate(
        [ParameterName](const FMaterialParameterInfo& ParameterInfo)
        {
            return ParameterInfo.Name == ParameterName;
        });
}

template <typename ElementType>
FRDGBufferRef RegisterOrUploadStructuredBuffer(
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
} // namespace DWCGPUBackendPrivate

using namespace DWCGPUBackendPrivate;

struct FDWCGPUBackend::FStaticSimulationData
{
    struct FSectionData
    {
        TArray<FUint4GPU> TriangleIndices;
        TArray<FVector4f> TriangleUV01;
        TArray<FVector4f> TriangleUV2RestArea;
    };

    struct FSlotData
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 Resolution = 0;
        TArray<FUint4GPU> TexelLookup;
        TArray<FUint4GPU> SeamDestinations;
        TArray<FVector4f> SeamIncoming;
    };

    int32 TriangleCount = 0;
    TArray<FSectionData> Sections;
    TArray<FVector4f> Profiles;
    TArray<uint32> TriangleProfileIndices;
    TArray<FSlotData> Slots;
};

struct FDWCGPUBackend::FRenderState
{
    struct FSectionBuffers
    {
        TRefCountPtr<FRDGPooledBuffer> TriangleIndices;
        TRefCountPtr<FRDGPooledBuffer> TriangleUV01;
        TRefCountPtr<FRDGPooledBuffer> TriangleUV2RestArea;
    };

    struct FSlotBuffers
    {
        TRefCountPtr<FRDGPooledBuffer> TexelLookup;
        TRefCountPtr<FRDGPooledBuffer> SeamDestinations;
        TRefCountPtr<FRDGPooledBuffer> SeamIncoming;
    };

    TArray<FSectionBuffers> Sections;
    TArray<FSlotBuffers> Slots;
    TRefCountPtr<FRDGPooledBuffer> Profiles;
    TRefCountPtr<FRDGPooledBuffer> TriangleProfileIndices;
    TRefCountPtr<FRDGPooledBuffer> TriangleFlow;
    TRefCountPtr<FRDGPooledBuffer> TriangleMetric;
    bool bWarnedMissingCachedGeometry = false;
};

UTextureRenderTarget2D* FDWCGPUBackend::FMaterialSlotRuntime::GetCurrentMap() const
{
    return WetnessMaps.IsValidIndex(CurrentTextureIndex) ? WetnessMaps[CurrentTextureIndex].Get() : nullptr;
}

UTextureRenderTarget2D* FDWCGPUBackend::FMaterialSlotRuntime::GetNextMap() const
{
    const int32 NextIndex = 1 - CurrentTextureIndex;
    return WetnessMaps.IsValidIndex(NextIndex) ? WetnessMaps[NextIndex].Get() : nullptr;
}

void FDWCGPUBackend::FMaterialSlotRuntime::SwapMaps()
{
    CurrentTextureIndex = 1 - CurrentTextureIndex;
}

bool FDWCGPUBackend::Initialize(const FDWCGPUBackendInitArgs& Args)
{
    Shutdown();

    if (!Args.OwnerComponent || !Args.TargetSkeletalMesh || !Args.WetClothingAsset ||
        !Args.WetMaterialInstances || Args.LODIndex < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("DWCGPU: Full wetness-map simulation requires an owner, mesh, asset, and valid LOD."));
        return false;
    }

    const USkeletalMesh* SkeletalMesh = Args.TargetSkeletalMesh->GetSkeletalMeshAsset();
    const FDWCGPULODBakeData& GPUData = Args.WetClothingAsset->GetGPUWetMapRuntimeData(Args.LODIndex);
    if (!SkeletalMesh || !Args.WetClothingAsset->IsGPUWetMapDataValidForMesh(SkeletalMesh, Args.LODIndex) ||
        !GPUData.bMapDataValid || GPUData.MapBakeVersion != DWCFullSimulationMapVersion || GPUData.LODIndex != Args.LODIndex)
    {
        UE_LOG(LogTemp, Warning, TEXT("DWCGPU: GPU simulation maps are missing or out of date for %s. Use Bake Maps in the Wet Clothing Asset editor."), *GetNameSafe(SkeletalMesh));
        return false;
    }

    OwnerComponent = Args.OwnerComponent;
    TargetSkeletalMesh = Args.TargetSkeletalMesh;
    WetClothingAsset = Args.WetClothingAsset;
    WetMaterialInstances = Args.WetMaterialInstances;
    WetnessMapParameterName = DWCWetMaterialParameters::WetnessMap();
    LODIndex = Args.LODIndex;
    MaxWetness = Args.WetnessSettings ? FMath::Max(0.0f, Args.WetnessSettings->MaxWetness) : 1.0f;
    SpreadRateScale = FMath::Max(0.0f, Args.SpreadRateScale);
    DryRateScale = FMath::Max(0.0f, Args.DryRateScale);
    GravityFlowStrengthScale = FMath::Max(0.0f, Args.GravityFlowStrengthScale);
    bUseEightDirectionDiffusion = Args.bUseEightDirectionDiffusion;

    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Initializing backend. Owner='%s', mesh='%s', asset='%s', LOD=%d, DWCDataUV=%d, wetnessParam='%s', bakedSlots=%d, bakedTriangles=%d, maxWetness=%.3f, spreadScale=%.3f, dryScale=%.3f, gravityScale=%.3f."),
            *GetNameSafe(Args.OwnerComponent),
            *GetNameSafe(Args.TargetSkeletalMesh),
            *GetNameSafe(Args.WetClothingAsset),
            LODIndex,
            Args.WetClothingAsset->GetDWCDataUVChannelIndex(),
            *WetnessMapParameterName.ToString(),
            GPUData.MaterialSlots.Num(),
            GPUData.Triangles.Num(),
            MaxWetness,
            SpreadRateScale,
            DryRateScale,
            GravityFlowStrengthScale);
    }

    if (!BuildStaticSimulationData() || !CreateSlotResources())
    {
        Shutdown();
        return false;
    }

    // Debug lookup is optional. Normal GPU simulation must not fail when debug metadata is unavailable.
    BuildDebugVertexLookup();

    bInitialized = true;
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Backend initialized. Runtime material slots=%d."),
            MaterialSlots.Num());
    }
    return true;
}

bool FDWCGPUBackend::BuildStaticSimulationData()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (!Asset)
    {
        return false;
    }

    const FDWCGPULODBakeData& Baked = Asset->GetGPUWetMapRuntimeData(LODIndex);
    const int32 ExpectedDWCDataUVChannel = Asset->GetDWCDataUVChannelIndex();
    if (ExpectedDWCDataUVChannel == INDEX_NONE)
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Wet Clothing Asset '%s' has no generated DWC Data UV channel. Rebuild DWC Data UV before using GPU simulation."),
            *GetNameSafe(Asset));
        return false;
    }

    TSet<int32> ExpectedWettableSlots;
    CollectExpectedWettableSlots(*Asset, ExpectedWettableSlots);
    if (ExpectedWettableSlots.IsEmpty())
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Wet Clothing Asset '%s' has no wettable material slots for GPU simulation."),
            *GetNameSafe(Asset));
        return false;
    }

    TSharedPtr<FStaticSimulationData, ESPMode::ThreadSafe> Data = MakeShared<FStaticSimulationData, ESPMode::ThreadSafe>();
    Data->TriangleCount = Baked.Triangles.Num();
    Data->Profiles.Reserve(Baked.Profiles.Num());
    for (const FDWCGPUProfileParameters& Profile : Baked.Profiles)
    {
        Data->Profiles.Add(FVector4f(
            FMath::Max(0.0f, Profile.SpreadRatePerSecond * SpreadRateScale),
            FMath::Max(0.0f, Profile.DryRatePerSecond * DryRateScale),
            FMath::Max(0.0f, Profile.GravityFlowStrength * GravityFlowStrengthScale),
            1.0f));
    }
    if (Data->TriangleCount <= 0 || Data->Profiles.IsEmpty())
    {
        return false;
    }

    Data->TriangleProfileIndices.Init(MAX_uint32, Data->TriangleCount);

    int32 MaxSectionIndex = INDEX_NONE;
    for (const FDWCGPUBakedTriangle& Triangle : Baked.Triangles)
    {
        if (Triangle.TriangleID != INDEX_NONE)
        {
            MaxSectionIndex = FMath::Max(MaxSectionIndex, Triangle.RenderSectionIndex);
        }
    }
    Data->Sections.SetNum(FMath::Max(0, MaxSectionIndex + 1));

    for (const FDWCGPUBakedTriangle& Triangle : Baked.Triangles)
    {
        if (Triangle.TriangleID == INDEX_NONE || !Data->TriangleProfileIndices.IsValidIndex(Triangle.TriangleID) ||
            !Data->Profiles.IsValidIndex(Triangle.ProfileIndex) || !Data->Sections.IsValidIndex(Triangle.RenderSectionIndex))
        {
            return false;
        }
        if (Triangle.UVChannelIndex != ExpectedDWCDataUVChannel)
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Baked triangle %d in '%s' uses UV%d, but the asset now uses DWC Data UV%d. Rebuild DWC Data UV, save the Wet Clothing Asset, then Bake GPU Simulation Maps."),
                Triangle.TriangleID,
                *GetNameSafe(Asset),
                Triangle.UVChannelIndex,
                ExpectedDWCDataUVChannel);
            return false;
        }
        if (!ExpectedWettableSlots.Contains(Triangle.MaterialSlotIndex))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Baked triangle %d in '%s' targets material slot %d, which is no longer marked wettable. Save the Wet Clothing Asset and Bake GPU Simulation Maps again."),
                Triangle.TriangleID,
                *GetNameSafe(Asset),
                Triangle.MaterialSlotIndex);
            return false;
        }

        Data->TriangleProfileIndices[Triangle.TriangleID] = static_cast<uint32>(Triangle.ProfileIndex);

        FStaticSimulationData::FSectionData& Section = Data->Sections[Triangle.RenderSectionIndex];
        Section.TriangleIndices.Add(FUint4GPU(
            static_cast<uint32>(Triangle.VertexIndices.X),
            static_cast<uint32>(Triangle.VertexIndices.Y),
            static_cast<uint32>(Triangle.VertexIndices.Z),
            static_cast<uint32>(Triangle.TriangleID)));
        Section.TriangleUV01.Add(FVector4f(
            static_cast<float>(Triangle.UV0.X), static_cast<float>(Triangle.UV0.Y),
            static_cast<float>(Triangle.UV1.X), static_cast<float>(Triangle.UV1.Y)));
        Section.TriangleUV2RestArea.Add(FVector4f(
            static_cast<float>(Triangle.UV2.X), static_cast<float>(Triangle.UV2.Y),
            Triangle.RestSurfaceArea, 0.0f));
    }

    TSet<int32> SeenMaterialSlots;
    for (const FDWCGPUMaterialSlotBakeData& BakedSlot : Baked.MaterialSlots)
    {
        const int64 ExpectedCount64 = static_cast<int64>(BakedSlot.Resolution) * static_cast<int64>(BakedSlot.Resolution);
        if (BakedSlot.Resolution <= 0 || ExpectedCount64 > MAX_int32 || SeenMaterialSlots.Contains(BakedSlot.MaterialSlotIndex))
        {
            return false;
        }
        if (!ExpectedWettableSlots.Contains(BakedSlot.MaterialSlotIndex))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Baked GPU map for '%s' targets material slot %d, which is no longer marked wettable. Save the Wet Clothing Asset and Bake GPU Simulation Maps again."),
                *GetNameSafe(Asset),
                BakedSlot.MaterialSlotIndex);
            return false;
        }
        if (BakedSlot.UVChannelIndex != ExpectedDWCDataUVChannel)
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Baked GPU map for '%s' slot %d uses UV%d, but the asset now uses DWC Data UV%d. Rebuild DWC Data UV, save the Wet Clothing Asset, then Bake GPU Simulation Maps."),
                *GetNameSafe(Asset),
                BakedSlot.MaterialSlotIndex,
                BakedSlot.UVChannelIndex,
                ExpectedDWCDataUVChannel);
            return false;
        }
        SeenMaterialSlots.Add(BakedSlot.MaterialSlotIndex);
        const int32 ExpectedCount = static_cast<int32>(ExpectedCount64);
        if (BakedSlot.Resolution <= 0 || BakedSlot.TexelTriangleIDs.Num() != ExpectedCount ||
            BakedSlot.PackedTexelBarycentricXY.Num() != ExpectedCount ||
            BakedSlot.RestTexelAreas.Num() != ExpectedCount || BakedSlot.ValidMask.Num() != ExpectedCount)
        {
            return false;
        }

        FStaticSimulationData::FSlotData& Slot = Data->Slots.AddDefaulted_GetRef();
        Slot.MaterialSlotIndex = BakedSlot.MaterialSlotIndex;
        Slot.Resolution = BakedSlot.Resolution;
        Slot.TexelLookup.Reserve(ExpectedCount);
        for (int32 TexelIndex = 0; TexelIndex < ExpectedCount; ++TexelIndex)
        {
            const int32 SignedTriangleID = BakedSlot.TexelTriangleIDs[TexelIndex];
            const bool bValidTexel = BakedSlot.ValidMask[TexelIndex] != 0;
            if ((bValidTexel && (!Baked.Triangles.IsValidIndex(SignedTriangleID) ||
                                Baked.Triangles[SignedTriangleID].MaterialSlotIndex != BakedSlot.MaterialSlotIndex ||
                                BakedSlot.RestTexelAreas[TexelIndex] <= 0.0f)) ||
                (!bValidTexel && SignedTriangleID != INDEX_NONE))
            {
                return false;
            }
            const uint32 TriangleID = SignedTriangleID == INDEX_NONE
                ? MAX_uint32
                : static_cast<uint32>(SignedTriangleID);
            uint32 IslandAndValid = 0u;
            if (bValidTexel)
            {
                IslandAndValid = static_cast<uint32>(FMath::Max(0, Baked.Triangles[SignedTriangleID].UVIslandID) + 1);
            }
            Slot.TexelLookup.Add(FUint4GPU(
                TriangleID,
                BakedSlot.PackedTexelBarycentricXY[TexelIndex],
                FloatToBits(BakedSlot.RestTexelAreas[TexelIndex]),
                IslandAndValid));
        }

        for (const FDWCGPUSeamDestination& Destination : BakedSlot.SeamDestinations)
        {
            if (Destination.DestinationTexelIndex < 0 || Destination.DestinationTexelIndex >= ExpectedCount ||
                Destination.IncomingStartIndex < 0 || Destination.IncomingCount <= 0 ||
                Destination.IncomingStartIndex + Destination.IncomingCount > BakedSlot.SeamIncoming.Num())
            {
                return false;
            }

            const uint32 CompactStart = static_cast<uint32>(Slot.SeamIncoming.Num());
            for (int32 IncomingOffset = 0; IncomingOffset < Destination.IncomingCount; ++IncomingOffset)
            {
                const FDWCGPUSeamIncoming& Incoming = BakedSlot.SeamIncoming[Destination.IncomingStartIndex + IncomingOffset];
                if (Incoming.SourceTexelIndex < 0 || Incoming.SourceTexelIndex >= ExpectedCount || Incoming.Weight <= 0.0f)
                {
                    return false;
                }
                Slot.SeamIncoming.Add(FVector4f(
                    static_cast<float>(Incoming.SourceTexelIndex), Incoming.Weight, 0.0f, 0.0f));
            }

            const uint32 CompactCount = static_cast<uint32>(Slot.SeamIncoming.Num()) - CompactStart;
            if (CompactCount > 0)
            {
                Slot.SeamDestinations.Add(FUint4GPU(
                    static_cast<uint32>(Destination.DestinationTexelIndex), CompactStart, CompactCount, 0u));
            }
        }
    }

    if (SeenMaterialSlots.Num() != ExpectedWettableSlots.Num())
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Baked GPU material-slot maps for '%s' do not match the current wettable slots. Baked=[%s], Expected=[%s]. Bake GPU Simulation Maps again."),
            *GetNameSafe(Asset),
            *JoinSortedSlotSet(SeenMaterialSlots),
            *JoinSortedSlotSet(ExpectedWettableSlots));
        return false;
    }

    if (Data->Slots.IsEmpty() || Data->TriangleProfileIndices.Contains(MAX_uint32))
    {
        return false;
    }

    StaticSimulationData = Data;
    RenderState = MakeShared<FRenderState, ESPMode::ThreadSafe>();
    RenderState->Sections.SetNum(Data->Sections.Num());
    RenderState->Slots.SetNum(Data->Slots.Num());
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Static data ready for asset '%s'. Sections=%d, materialSlots=%d, triangles=%d, profiles=%d, DWCDataUV=%d."),
            *GetNameSafe(Asset),
            Data->Sections.Num(),
            Data->Slots.Num(),
            Data->TriangleCount,
            Data->Profiles.Num(),
            ExpectedDWCDataUVChannel);
    }
    return true;
}


bool FDWCGPUBackend::BuildDebugVertexLookup()
{
    DebugVertexDataUVs.Reset();
    DebugVertexMaterialSlots.Reset();

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return false;
    }

    FDWCDataUVBufferView DataUVView;
    if (!DataUVView.Initialize(
            Asset->GetRuntimeSkeletalMesh(),
            LODIndex,
            Asset->GetDWCDataUVChannelIndex()))
    {
        UE_LOG(LogDWCGPU, Warning, TEXT("DWCGPU: Could not build debug vertex lookup because the DWC Data UV buffer is unavailable."));
        return false;
    }

    const FWetClothingPrecomputedSimulationData& Precomputed = Asset->GetPrecomputedSimulationData(LODIndex);
    const int32 VertexCount = DataUVView.NumVertices();
    if (VertexCount <= 0 || Precomputed.Vertices.Num() != VertexCount)
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Debug vertex lookup count mismatch. UV vertices=%d, precomputed vertices=%d."),
            VertexCount,
            Precomputed.Vertices.Num());
        return false;
    }

    DebugVertexDataUVs.SetNumZeroed(VertexCount);
    DebugVertexMaterialSlots.Init(INDEX_NONE, VertexCount);
    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        const FWetClothingPrecomputedVertexData& VertexData = Precomputed.Vertices[VertexIndex];
        if (!VertexData.bIsWettable)
        {
            continue;
        }

        DebugVertexDataUVs[VertexIndex] = DataUVView.GetUV(VertexIndex);
        DebugVertexMaterialSlots[VertexIndex] = VertexData.MaterialSlotIndex;
    }

    return true;
}

bool FDWCGPUBackend::CreateSlotResources()
{
    USkeletalMeshComponent* MeshComponent = TargetSkeletalMesh.Get();
    UDynamicWetClothesComponent* Component = OwnerComponent.Get();
    if (!MeshComponent || !Component || !WetMaterialInstances || !StaticSimulationData.IsValid())
    {
        return false;
    }

    if (WetMaterialInstances->Num() < MeshComponent->GetNumMaterials())
    {
        WetMaterialInstances->SetNum(MeshComponent->GetNumMaterials());
    }

    MaterialSlots.Reset();
    bool bAllMaterialBindingsValid = true;
    for (int32 StaticSlotIndex = 0; StaticSlotIndex < StaticSimulationData->Slots.Num(); ++StaticSlotIndex)
    {
        const FStaticSimulationData::FSlotData& StaticSlot = StaticSimulationData->Slots[StaticSlotIndex];
        FMaterialSlotRuntime& Slot = MaterialSlots.AddDefaulted_GetRef();
        Slot.MaterialSlotIndex = StaticSlot.MaterialSlotIndex;
        Slot.StaticSlotIndex = StaticSlotIndex;
        Slot.Resolution = StaticSlot.Resolution;
        Slot.WetnessMaps.Reserve(2);

        for (int32 TextureIndex = 0; TextureIndex < 2; ++TextureIndex)
        {
            UTextureRenderTarget2D* WetnessMap = NewObject<UTextureRenderTarget2D>(Component);
            WetnessMap->RenderTargetFormat = ETextureRenderTargetFormat::RTF_R16f;
            WetnessMap->ClearColor = FLinearColor::Black;
            WetnessMap->bAutoGenerateMips = false;
            WetnessMap->bCanCreateUAV = true;

            if constexpr (GDWCForceNearestWetnessSampling)
            {
                WetnessMap->Filter = TF_Nearest;
                WetnessMap->AddressX = TA_Clamp;
                WetnessMap->AddressY = TA_Clamp;
            }

            WetnessMap->InitCustomFormat(Slot.Resolution, Slot.Resolution, PF_R16F, false);
            WetnessMap->UpdateResourceImmediate(true);
            Slot.WetnessMaps.Add(TStrongObjectPtr<UTextureRenderTarget2D>(WetnessMap));
        }

        if (Slot.MaterialSlotIndex >= 0 && Slot.MaterialSlotIndex < MeshComponent->GetNumMaterials())
        {
            UMaterialInstanceDynamic* MID = WetMaterialInstances->IsValidIndex(Slot.MaterialSlotIndex)
                ? (*WetMaterialInstances)[Slot.MaterialSlotIndex]
                : nullptr;
            if (!MID)
            {
                MID = MeshComponent->CreateAndSetMaterialInstanceDynamic(Slot.MaterialSlotIndex);
                if (WetMaterialInstances->IsValidIndex(Slot.MaterialSlotIndex))
                {
                    (*WetMaterialInstances)[Slot.MaterialSlotIndex] = MID;
                }
            }

            if (MID)
            {
                const bool bHasWetnessMapParameter = MaterialHasTextureParameter(MID, WetnessMapParameterName);
                if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
                {
                    UE_LOG(
                        LogDWCGPU,
                        Log,
                        TEXT("DWCGPU: Slot %d material binding check. MID='%s', sourceMaterial='%s', resolution=%d, hasTextureParam=%s, currentMap='%s'."),
                        Slot.MaterialSlotIndex,
                        *GetNameSafe(MID),
                        *GetNameSafe(MeshComponent->GetMaterial(Slot.MaterialSlotIndex)),
                        Slot.Resolution,
                        bHasWetnessMapParameter ? TEXT("true") : TEXT("false"),
                        *GetNameSafe(Slot.GetCurrentMap()));
                }
                if (!bHasWetnessMapParameter)
                {
                    TArray<FString> MissingParameters;
                    if (!bHasWetnessMapParameter)
                    {
                        MissingParameters.Add(WetnessMapParameterName.ToString());
                    }

                    UE_LOG(
                        LogDWCGPU,
                        Warning,
                        TEXT("DWCGPU: Material '%s' on mesh '%s' slot %d cannot display GPU wetness. Missing parameters: %s. Reapply the generated DWC wet material or refresh the material setup so it samples the GPU wetness map with the WCA DWC Data UV channel."),
                        *GetNameSafe(MID),
                        *GetNameSafe(MeshComponent),
                        Slot.MaterialSlotIndex,
                        *FString::Join(MissingParameters, TEXT(", ")));
                    bAllMaterialBindingsValid = false;
                    continue;
                }

                MID->SetTextureParameterValue(WetnessMapParameterName, Slot.GetCurrentMap());
                Slot.MaterialInstance = MID;
                UE_LOG(
                    LogDWCGPU,
                    Log,
                    TEXT("DWCGPU: Render binding active. Mesh='%s', slot=%d, MID='%s', wetnessRT='%s', resolution=%d, textureParam='%s'."),
                    *GetNameSafe(MeshComponent),
                    Slot.MaterialSlotIndex,
                    *GetNameSafe(MID),
                    *GetNameSafe(Slot.GetCurrentMap()),
                    Slot.Resolution,
                    *WetnessMapParameterName.ToString());
                if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
                {
                    UE_LOG(
                        LogDWCGPU,
                        Log,
                        TEXT("DWCGPU: Slot %d bound GPU wetness render target '%s' to '%s'."),
                        Slot.MaterialSlotIndex,
                        *GetNameSafe(Slot.GetCurrentMap()),
                        *WetnessMapParameterName.ToString());
                }
            }
            else
            {
                UE_LOG(
                    LogDWCGPU,
                    Warning,
                    TEXT("DWCGPU: Could not create a dynamic material instance for mesh '%s' slot %d."),
                    *GetNameSafe(MeshComponent),
                    Slot.MaterialSlotIndex);
                bAllMaterialBindingsValid = false;
            }
        }
        else
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Baked material slot %d is out of range for mesh '%s' (%d materials). Bake GPU Simulation Maps again for the current runtime mesh."),
                Slot.MaterialSlotIndex,
                *GetNameSafe(MeshComponent),
                MeshComponent->GetNumMaterials());
            bAllMaterialBindingsValid = false;
        }
    }

    return !MaterialSlots.IsEmpty() && bAllMaterialBindingsValid;
}

bool FDWCGPUBackend::EnqueueResolvedContacts(const TArray<FDWCResolvedSurfaceContact>& Contacts)
{
    if (!bInitialized || Contacts.IsEmpty())
    {
        if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: EnqueueResolvedContacts ignored. initialized=%s, contacts=%d."),
                bInitialized ? TEXT("true") : TEXT("false"),
                Contacts.Num());
        }
        return false;
    }
    PendingContacts.Append(Contacts);
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Queued resolved contacts. Added=%d, pending=%d."),
            Contacts.Num(),
            PendingContacts.Num());
    }
    return true;
}

bool FDWCGPUBackend::ApplyWetAll(const float Amount)
{
    if (!bInitialized || FMath::IsNearlyZero(Amount))
    {
        if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: ApplyWetAll ignored. initialized=%s, amount=%.4f."),
                bInitialized ? TEXT("true") : TEXT("false"),
                Amount);
        }
        return false;
    }
    PendingWetAllAmount += Amount;
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Queued WetAll. Added=%.4f, pending=%.4f."),
            Amount,
            PendingWetAllAmount);
    }
    return true;
}

void FDWCGPUBackend::Update(const float DeltaSeconds)
{
    if (!bInitialized)
    {
        return;
    }

    TArray<FDWCResolvedSurfaceContact> Contacts;
    Swap(Contacts, PendingContacts);
    const float WetAllAmount = PendingWetAllAmount;
    PendingWetAllAmount = 0.0f;
    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose) && (!Contacts.IsEmpty() || !FMath::IsNearlyZero(WetAllAmount) || DebugDispatchLogCount < 3))
    {
        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Update dispatch. contacts=%d, wetAll=%.4f, delta=%.4f."),
            Contacts.Num(),
            WetAllAmount,
            DeltaSeconds);
        ++DebugDispatchLogCount;
    }
    DispatchSimulation(MoveTemp(Contacts), WetAllAmount, FMath::Clamp(DeltaSeconds, 0.0f, 0.25f));
}

void FDWCGPUBackend::DispatchSimulation(
    TArray<FDWCResolvedSurfaceContact>&& Contacts,
    const float WetAllAmount,
    const float DeltaSeconds)
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    USkeletalMeshComponent* MeshComponent = TargetSkeletalMesh.Get();
    if (!Asset || !MeshComponent || !StaticSimulationData.IsValid() || !RenderState.IsValid())
    {
        return;
    }

    const FDWCGPULODBakeData& BakedData = Asset->GetGPUWetMapRuntimeData(LODIndex);
    TArray<FSlotRenderDispatch> SlotDispatches;
    SlotDispatches.Reserve(MaterialSlots.Num());
    const bool bHadWetInput = !Contacts.IsEmpty() || !FMath::IsNearlyZero(WetAllAmount);
    int32 TotalAbsorptionDispatches = 0;

    for (int32 SlotRuntimeIndex = 0; SlotRuntimeIndex < MaterialSlots.Num(); ++SlotRuntimeIndex)
    {
        FMaterialSlotRuntime& Slot = MaterialSlots[SlotRuntimeIndex];
        UTextureRenderTarget2D* CurrentMap = Slot.GetCurrentMap();
        UTextureRenderTarget2D* NextMap = Slot.GetNextMap();
        if (!CurrentMap || !NextMap)
        {
            continue;
        }

        FSlotRenderDispatch& SlotDispatch = SlotDispatches.AddDefaulted_GetRef();
        SlotDispatch.SlotRuntimeIndex = SlotRuntimeIndex;
        SlotDispatch.StaticSlotIndex = Slot.StaticSlotIndex;
        SlotDispatch.Resolution = Slot.Resolution;
        SlotDispatch.CurrentResource = CurrentMap->GameThread_GetRenderTargetResource();
        SlotDispatch.NextResource = NextMap->GameThread_GetRenderTargetResource();

        for (const FDWCResolvedSurfaceContact& Contact : Contacts)
        {
            if (Contact.MaterialSlotIndex != Slot.MaterialSlotIndex || !BakedData.Triangles.IsValidIndex(Contact.TriangleID))
            {
                continue;
            }

            const FDWCGPUBakedTriangle& Triangle = BakedData.Triangles[Contact.TriangleID];
            FTriangleAbsorptionDispatch Dispatch;
            if (!BuildDispatchBounds(Triangle, Slot.Resolution, Dispatch.DispatchMin, Dispatch.DispatchSize))
            {
                continue;
            }

            FillTriangleUVs(Triangle, Dispatch);
            Dispatch.UV2AndSettings.Z = FMath::Max(0.0f, Contact.DistanceToSurface);
            Dispatch.ContactAndRadius = MakePositionAndValue(Contact.ContactWorldPosition, FMath::Max(Contact.Radius, KINDA_SMALL_NUMBER));
            const float AppliedAmount = Contact.Amount > 0.0f ? Contact.Amount * Contact.AbsorptionMultiplier : Contact.Amount;
            Dispatch.P0AndAmount = MakePositionAndValue(Contact.WorldTrianglePosition0, AppliedAmount);
            Dispatch.P1AndMaxWetness = MakePositionAndValue(Contact.WorldTrianglePosition1, MaxWetness);
            Dispatch.P2AndMode = MakePositionAndValue(Contact.WorldTrianglePosition2, 0.0f);
            SlotDispatch.AbsorptionDispatches.Add(Dispatch);
        }

        if (!FMath::IsNearlyZero(WetAllAmount))
        {
            for (const FDWCGPUBakedTriangle& Triangle : BakedData.Triangles)
            {
                if (Triangle.TriangleID == INDEX_NONE || Triangle.MaterialSlotIndex != Slot.MaterialSlotIndex)
                {
                    continue;
                }
                FTriangleAbsorptionDispatch Dispatch;
                if (BuildDispatchBounds(Triangle, Slot.Resolution, Dispatch.DispatchMin, Dispatch.DispatchSize))
                {
                    FillTriangleUVs(Triangle, Dispatch);
                    Dispatch.P0AndAmount.W = WetAllAmount;
                    Dispatch.P1AndMaxWetness.W = MaxWetness;
                    Dispatch.P2AndMode.W = 1.0f;
                    SlotDispatch.AbsorptionDispatches.Add(Dispatch);
                }
            }
        }
        TotalAbsorptionDispatches += SlotDispatch.AbsorptionDispatches.Num();
    }

    if (SlotDispatches.IsEmpty())
    {
        if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose) && bHadWetInput)
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: No slot dispatches were built for wet input. contacts=%d, wetAll=%.4f, materialSlots=%d, asset='%s', mesh='%s'."),
                Contacts.Num(),
                WetAllAmount,
                MaterialSlots.Num(),
                *GetNameSafe(Asset),
                *GetNameSafe(MeshComponent));
        }
        return;
    }

    if (bHadWetInput && TotalAbsorptionDispatches <= 0)
    {
        UE_LOG(
            LogDWCGPU,
            Warning,
            TEXT("DWCGPU: Wet input reached GPU backend, but no absorption dispatches were built. contacts=%d, wetAll=%.4f, slots=%d, asset='%s', mesh='%s'. Check contact material slots, triangle IDs, DWC Data UV bake, and wettable slot setup."),
            Contacts.Num(),
            WetAllAmount,
            SlotDispatches.Num(),
            *GetNameSafe(Asset),
            *GetNameSafe(MeshComponent));
    }

    if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose) && (bHadWetInput || DebugDispatchLogCount <= 3))
    {
        TArray<FString> SlotSummaries;
        SlotSummaries.Reserve(SlotDispatches.Num());
        for (const FSlotRenderDispatch& SlotDispatch : SlotDispatches)
        {
            const int32 MaterialSlotIndex = MaterialSlots.IsValidIndex(SlotDispatch.SlotRuntimeIndex)
                ? MaterialSlots[SlotDispatch.SlotRuntimeIndex].MaterialSlotIndex
                : INDEX_NONE;
            SlotSummaries.Add(FString::Printf(
                TEXT("slot%d:absorb%d:res%d"),
                MaterialSlotIndex,
                SlotDispatch.AbsorptionDispatches.Num(),
                SlotDispatch.Resolution));
        }

        UE_LOG(
            LogDWCGPU,
            Log,
            TEXT("DWCGPU: Built slot dispatches. contacts=%d, wetAll=%.4f, slots=%d, absorptionDispatches=%d, [%s]."),
            Contacts.Num(),
            WetAllAmount,
            SlotDispatches.Num(),
            TotalAbsorptionDispatches,
            *FString::Join(SlotSummaries, TEXT(", ")));
    }

    const FSkeletalMeshObject* MeshObject = MeshComponent->GetMeshObject();
    FVector GravityDirection(0.0, 0.0, -1.0);
    if (const UWorld* World = OwnerComponent.IsValid() ? OwnerComponent->GetWorld() : nullptr)
    {
        GravityDirection = FVector(0.0, 0.0, World->GetGravityZ()).GetSafeNormal();
        if (GravityDirection.IsNearlyZero())
        {
            GravityDirection = FVector::DownVector;
        }
    }
    const FVector4f WorldGravityDirection(
        static_cast<float>(GravityDirection.X),
        static_cast<float>(GravityDirection.Y),
        static_cast<float>(GravityDirection.Z),
        0.0f);

    const TSharedPtr<const FStaticSimulationData, ESPMode::ThreadSafe> StaticData = StaticSimulationData;
    const TSharedPtr<FRenderState, ESPMode::ThreadSafe> RTState = RenderState;
    const float MaxWetnessValue = MaxWetness;
    const int32 SimulationLODIndex = LODIndex;

    // The game-thread-facing material switches to the destination map immediately.
    // The render command is queued before the next scene render and writes that resource in order.
    for (const FSlotRenderDispatch& SlotDispatch : SlotDispatches)
    {
        if (!MaterialSlots.IsValidIndex(SlotDispatch.SlotRuntimeIndex))
        {
            continue;
        }
        FMaterialSlotRuntime& Slot = MaterialSlots[SlotDispatch.SlotRuntimeIndex];
        Slot.SwapMaps();
        if (UMaterialInstanceDynamic* MID = Slot.MaterialInstance.Get())
        {
            MID->SetTextureParameterValue(WetnessMapParameterName, Slot.GetCurrentMap());
            if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose) && (bHadWetInput || DebugDispatchLogCount <= 3))
            {
                UE_LOG(
                    LogDWCGPU,
                    Log,
                    TEXT("DWCGPU: Swapped slot %d to render target '%s' on MID '%s'."),
                    Slot.MaterialSlotIndex,
                    *GetNameSafe(Slot.GetCurrentMap()),
                    *GetNameSafe(MID));
            }
        }
        else if (UE_LOG_ACTIVE(LogDWCGPU, VeryVerbose))
        {
            UE_LOG(
                LogDWCGPU,
                Warning,
                TEXT("DWCGPU: Slot %d has no live MID when swapping wetness map."),
                Slot.MaterialSlotIndex);
        }
    }

    ENQUEUE_RENDER_COMMAND(DWCFullWetMapSimulation)(
        [MeshObject, StaticData, RTState, SlotDispatches = MoveTemp(SlotDispatches), DeltaSeconds, MaxWetnessValue, WorldGravityDirection, SimulationLODIndex, bUseEightDirectionDiffusion = bUseEightDirectionDiffusion](FRHICommandListImmediate& RHICmdList) mutable
        {
            if (!StaticData.IsValid() || !RTState.IsValid())
            {
                return;
            }

            FRDGBuilder GraphBuilder(RHICmdList);

            FRDGBufferRef ProfileBuffer = RegisterOrUploadStructuredBuffer(
                GraphBuilder, RTState->Profiles, TEXT("DWC.Profiles"), StaticData->Profiles);
            FRDGBufferRef TriangleProfileIndexBuffer = RegisterOrUploadStructuredBuffer(
                GraphBuilder, RTState->TriangleProfileIndices, TEXT("DWC.TriangleProfileIndices"), StaticData->TriangleProfileIndices);
            if (!ProfileBuffer || !TriangleProfileIndexBuffer)
            {
                return;
            }
            FRDGBufferSRVRef ProfileSRV = GraphBuilder.CreateSRV(ProfileBuffer);
            FRDGBufferSRVRef TriangleProfileIndexSRV = GraphBuilder.CreateSRV(TriangleProfileIndexBuffer);

            FRDGBufferRef TriangleFlowBuffer = nullptr;
            if (RTState->TriangleFlow.IsValid())
            {
                TriangleFlowBuffer = GraphBuilder.RegisterExternalBuffer(RTState->TriangleFlow, TEXT("DWC.TriangleFlow"));
            }
            else
            {
                TArray<FVector4f> DefaultFlow;
                DefaultFlow.Init(FVector4f(0, 0, 1, 0), StaticData->TriangleCount);
                TriangleFlowBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DWC.TriangleFlow"), DefaultFlow);
                GraphBuilder.QueueBufferExtraction(TriangleFlowBuffer, &RTState->TriangleFlow);
            }

            FRDGBufferRef TriangleMetricBuffer = nullptr;
            if (RTState->TriangleMetric.IsValid())
            {
                TriangleMetricBuffer = GraphBuilder.RegisterExternalBuffer(RTState->TriangleMetric, TEXT("DWC.TriangleMetric"));
            }
            else
            {
                TArray<FVector4f> DefaultMetric;
                DefaultMetric.Init(FVector4f(1, 0, 1, 0), StaticData->TriangleCount);
                TriangleMetricBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DWC.TriangleMetric"), DefaultMetric);
                GraphBuilder.QueueBufferExtraction(TriangleMetricBuffer, &RTState->TriangleMetric);
            }

            FCachedGeometry CachedGeometry;
            const bool bHasCachedGeometry =
                MeshObject &&
                MeshObject->GetCachedGeometry(GraphBuilder, CachedGeometry) &&
                CachedGeometry.LODIndex == SimulationLODIndex;
            if (bHasCachedGeometry)
            {
                FRDGBufferUAVRef FlowUAV = GraphBuilder.CreateUAV(TriangleFlowBuffer);
                FRDGBufferUAVRef MetricUAV = GraphBuilder.CreateUAV(TriangleMetricBuffer);
                for (const FCachedGeometry::Section& CachedSection : CachedGeometry.Sections)
                {
                    const int32 SectionIndex = static_cast<int32>(CachedSection.SectionIndex);
                    if (CachedSection.LODIndex != SimulationLODIndex || !StaticData->Sections.IsValidIndex(SectionIndex) ||
                        !RTState->Sections.IsValidIndex(SectionIndex) || !CachedSection.PositionBuffer)
                    {
                        continue;
                    }

                    const FStaticSimulationData::FSectionData& SectionData = StaticData->Sections[SectionIndex];
                    if (SectionData.TriangleIndices.IsEmpty())
                    {
                        continue;
                    }

                    FRenderState::FSectionBuffers& SectionBuffers = RTState->Sections[SectionIndex];
                    FRDGBufferRef IndicesBuffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SectionBuffers.TriangleIndices, TEXT("DWC.FlowTriangleIndices"), SectionData.TriangleIndices);
                    FRDGBufferRef UV01Buffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SectionBuffers.TriangleUV01, TEXT("DWC.FlowTriangleUV01"), SectionData.TriangleUV01);
                    FRDGBufferRef UV2Buffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SectionBuffers.TriangleUV2RestArea, TEXT("DWC.FlowTriangleUV2Rest"), SectionData.TriangleUV2RestArea);
                    if (!IndicesBuffer || !UV01Buffer || !UV2Buffer)
                    {
                        continue;
                    }

                    FDWCUpdateTriangleFlowCS::FParameters* Parameters =
                        GraphBuilder.AllocParameters<FDWCUpdateTriangleFlowCS::FParameters>();
                    Parameters->TriangleCount = static_cast<uint32>(SectionData.TriangleIndices.Num());
                    Parameters->PositionIndexBase =
                        (CachedSection.TotalVertexCount == CachedSection.NumVertices && CachedSection.VertexBaseIndex > 0)
                            ? CachedSection.VertexBaseIndex
                            : 0u;
                    Parameters->LocalToWorld = FMatrix44f(CachedGeometry.LocalToWorld.ToMatrixWithScale());
                    Parameters->WorldGravityDirection = WorldGravityDirection;
                    Parameters->PositionBuffer = CachedSection.PositionBuffer;
                    Parameters->TriangleIndices = GraphBuilder.CreateSRV(IndicesBuffer);
                    Parameters->TriangleUV01 = GraphBuilder.CreateSRV(UV01Buffer);
                    Parameters->TriangleUV2RestArea = GraphBuilder.CreateSRV(UV2Buffer);
                    Parameters->TriangleFlow = FlowUAV;
                    Parameters->TriangleMetric = MetricUAV;

                    TShaderMapRef<FDWCUpdateTriangleFlowCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("DWC Update Triangle Flow Section %d", SectionIndex),
                        Shader,
                        Parameters,
                        FIntVector(FMath::DivideAndRoundUp(SectionData.TriangleIndices.Num(), 64), 1, 1));
                }
                RTState->bWarnedMissingCachedGeometry = false;
            }
            else if (!RTState->bWarnedMissingCachedGeometry)
            {
                UE_LOG(LogTemp, Warning, TEXT("DWCGPU: Compute Skin Cache geometry for simulation LOD%d is unavailable. Wetness input/spread/dry still runs, but gravity flow reuses the last valid triangle-flow buffer until the target skeletal mesh provides skin-cache geometry for this LOD."), SimulationLODIndex);
                RTState->bWarnedMissingCachedGeometry = true;
            }

            FRDGBufferSRVRef FlowSRV = GraphBuilder.CreateSRV(TriangleFlowBuffer);
            FRDGBufferSRVRef MetricSRV = GraphBuilder.CreateSRV(TriangleMetricBuffer);
            TShaderMapRef<FDWCApplyTriangleAbsorptionCS> AbsorptionShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCDiffuseDryCS> DiffuseDry4Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCTransferScale8CS> TransferScale8Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCDiffuseDry8CS> DiffuseDry8Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FDWCSeamGatherCS> SeamShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

            for (FSlotRenderDispatch& SlotDispatch : SlotDispatches)
            {
                if (!StaticData->Slots.IsValidIndex(SlotDispatch.StaticSlotIndex) ||
                    !RTState->Slots.IsValidIndex(SlotDispatch.StaticSlotIndex) ||
                    !SlotDispatch.CurrentResource || !SlotDispatch.NextResource ||
                    !SlotDispatch.CurrentResource->GetRenderTargetTexture() || !SlotDispatch.NextResource->GetRenderTargetTexture())
                {
                    continue;
                }

                const FStaticSimulationData::FSlotData& StaticSlot = StaticData->Slots[SlotDispatch.StaticSlotIndex];
                FRenderState::FSlotBuffers& SlotBuffers = RTState->Slots[SlotDispatch.StaticSlotIndex];
                FRDGBufferRef LookupBuffer = RegisterOrUploadStructuredBuffer(
                    GraphBuilder, SlotBuffers.TexelLookup, TEXT("DWC.TexelLookup"), StaticSlot.TexelLookup);
                if (!LookupBuffer)
                {
                    continue;
                }
                FRDGBufferSRVRef LookupSRV = GraphBuilder.CreateSRV(LookupBuffer);

                TRefCountPtr<IPooledRenderTarget> CurrentExternal = CreateRenderTarget(
                    SlotDispatch.CurrentResource->GetRenderTargetTexture(), TEXT("DWC.AbsorbedWetness.Current"));
                TRefCountPtr<IPooledRenderTarget> NextExternal = CreateRenderTarget(
                    SlotDispatch.NextResource->GetRenderTargetTexture(), TEXT("DWC.AbsorbedWetness.Next"));
                FRDGTextureRef CurrentTexture = GraphBuilder.RegisterExternalTexture(CurrentExternal);
                FRDGTextureRef NextTexture = GraphBuilder.RegisterExternalTexture(NextExternal);

                FRDGTextureDesc WorkingDesc = CurrentTexture->Desc;
                WorkingDesc.ClearValue = FClearValueBinding::Black;
                FRDGTextureRef InputAppliedTexture = GraphBuilder.CreateTexture(WorkingDesc, TEXT("DWC.AbsorbedWetness.InputApplied"));
                AddCopyTexturePass(GraphBuilder, CurrentTexture, InputAppliedTexture);
                FRDGTextureUAVRef InputUAV = GraphBuilder.CreateUAV(InputAppliedTexture);

                for (int32 DispatchIndex = 0; DispatchIndex < SlotDispatch.AbsorptionDispatches.Num(); ++DispatchIndex)
                {
                    const FTriangleAbsorptionDispatch& Dispatch = SlotDispatch.AbsorptionDispatches[DispatchIndex];
                    FDWCApplyTriangleAbsorptionCS::FParameters* Parameters =
                        GraphBuilder.AllocParameters<FDWCApplyTriangleAbsorptionCS::FParameters>();
                    Parameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                    Parameters->DispatchMin = Dispatch.DispatchMin;
                    Parameters->DispatchSize = Dispatch.DispatchSize;
                    Parameters->UV01 = Dispatch.UV01;
                    Parameters->UV2AndSettings = Dispatch.UV2AndSettings;
                    Parameters->ContactAndRadius = Dispatch.ContactAndRadius;
                    Parameters->P0AndAmount = Dispatch.P0AndAmount;
                    Parameters->P1AndMaxWetness = Dispatch.P1AndMaxWetness;
                    Parameters->P2AndMode = Dispatch.P2AndMode;
                    Parameters->WetnessTexture = InputUAV;
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("DWC Apply Absorption %d", DispatchIndex),
                        AbsorptionShader,
                        Parameters,
                        FIntVector(
                            FMath::DivideAndRoundUp(Dispatch.DispatchSize.X, 8),
                            FMath::DivideAndRoundUp(Dispatch.DispatchSize.Y, 8), 1));
                }

                if (bUseEightDirectionDiffusion)
                {
                    FRDGTextureDesc TransferScaleDesc = FRDGTextureDesc::Create2D(
                        FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution),
                        PF_R16F,
                        FClearValueBinding::White,
                        TexCreate_ShaderResource | TexCreate_UAV);
                    FRDGTextureRef TransferScaleTexture =
                        GraphBuilder.CreateTexture(TransferScaleDesc, TEXT("DWC.AbsorbedWetness.TransferScale"));

                    FDWCTransferScale8CS::FParameters* TransferScaleParameters =
                        GraphBuilder.AllocParameters<FDWCTransferScale8CS::FParameters>();
                    TransferScaleParameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                    TransferScaleParameters->DeltaSeconds = DeltaSeconds;
                    TransferScaleParameters->TransferScaleTexture = GraphBuilder.CreateUAV(TransferScaleTexture);
                    TransferScaleParameters->TexelLookup = LookupSRV;
                    TransferScaleParameters->TriangleFlow = FlowSRV;
                    TransferScaleParameters->TriangleMetric = MetricSRV;
                    TransferScaleParameters->Profiles = ProfileSRV;
                    TransferScaleParameters->TriangleProfileIndices = TriangleProfileIndexSRV;
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("DWC Transfer Scale 8 Slot %d", StaticSlot.MaterialSlotIndex),
                        TransferScale8Shader,
                        TransferScaleParameters,
                        FIntVector(
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8),
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8), 1));

                    FDWCDiffuseDry8CS::FParameters* DiffuseParameters =
                        GraphBuilder.AllocParameters<FDWCDiffuseDry8CS::FParameters>();
                    DiffuseParameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                    DiffuseParameters->DeltaSeconds = DeltaSeconds;
                    DiffuseParameters->MaxWetness = MaxWetnessValue;
                    DiffuseParameters->SourceWetnessTexture = InputAppliedTexture;
                    DiffuseParameters->TransferScaleTexture = TransferScaleTexture;
                    DiffuseParameters->DestinationWetnessTexture = GraphBuilder.CreateUAV(NextTexture);
                    DiffuseParameters->TexelLookup = LookupSRV;
                    DiffuseParameters->TriangleFlow = FlowSRV;
                    DiffuseParameters->TriangleMetric = MetricSRV;
                    DiffuseParameters->Profiles = ProfileSRV;
                    DiffuseParameters->TriangleProfileIndices = TriangleProfileIndexSRV;
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("DWC Diffuse Gravity Dry 8 Slot %d", StaticSlot.MaterialSlotIndex),
                        DiffuseDry8Shader,
                        DiffuseParameters,
                        FIntVector(
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8),
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8), 1));
                }
                else
                {
                    FDWCDiffuseDryCS::FParameters* DiffuseParameters =
                        GraphBuilder.AllocParameters<FDWCDiffuseDryCS::FParameters>();
                    DiffuseParameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                    DiffuseParameters->DeltaSeconds = DeltaSeconds;
                    DiffuseParameters->MaxWetness = MaxWetnessValue;
                    DiffuseParameters->SourceWetnessTexture = InputAppliedTexture;
                    DiffuseParameters->DestinationWetnessTexture = GraphBuilder.CreateUAV(NextTexture);
                    DiffuseParameters->TexelLookup = LookupSRV;
                    DiffuseParameters->TriangleFlow = FlowSRV;
                    DiffuseParameters->TriangleMetric = MetricSRV;
                    DiffuseParameters->Profiles = ProfileSRV;
                    DiffuseParameters->TriangleProfileIndices = TriangleProfileIndexSRV;
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("DWC Diffuse Gravity Dry 4 Slot %d", StaticSlot.MaterialSlotIndex),
                        DiffuseDry4Shader,
                        DiffuseParameters,
                        FIntVector(
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8),
                            FMath::DivideAndRoundUp(SlotDispatch.Resolution, 8), 1));
                }

                if (!StaticSlot.SeamDestinations.IsEmpty() && !StaticSlot.SeamIncoming.IsEmpty())
                {
                    FRDGBufferRef SeamDestBuffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SlotBuffers.SeamDestinations, TEXT("DWC.SeamDestinations"), StaticSlot.SeamDestinations);
                    FRDGBufferRef SeamIncomingBuffer = RegisterOrUploadStructuredBuffer(
                        GraphBuilder, SlotBuffers.SeamIncoming, TEXT("DWC.SeamIncoming"), StaticSlot.SeamIncoming);
                    if (SeamDestBuffer && SeamIncomingBuffer)
                    {
                        FRDGTextureRef SeamResolvedTexture = GraphBuilder.CreateTexture(WorkingDesc, TEXT("DWC.AbsorbedWetness.SeamResolved"));
                        AddCopyTexturePass(GraphBuilder, NextTexture, SeamResolvedTexture);

                        FDWCSeamGatherCS::FParameters* SeamParameters =
                            GraphBuilder.AllocParameters<FDWCSeamGatherCS::FParameters>();
                        SeamParameters->TextureSize = FIntPoint(SlotDispatch.Resolution, SlotDispatch.Resolution);
                        SeamParameters->SeamDestinationCount = static_cast<uint32>(StaticSlot.SeamDestinations.Num());
                        SeamParameters->DeltaSeconds = DeltaSeconds;
                        SeamParameters->SeamTransferScale = DWCSeamTransferScale;
                        SeamParameters->MaxWetness = MaxWetnessValue;
                        SeamParameters->SourceWetnessTexture = NextTexture;
                        SeamParameters->DestinationWetnessTexture = GraphBuilder.CreateUAV(SeamResolvedTexture);
                        SeamParameters->SeamDestinations = GraphBuilder.CreateSRV(SeamDestBuffer);
                        SeamParameters->SeamIncoming = GraphBuilder.CreateSRV(SeamIncomingBuffer);
                        SeamParameters->TexelLookup = LookupSRV;
                        SeamParameters->TriangleFlow = FlowSRV;
                        SeamParameters->Profiles = ProfileSRV;
                        SeamParameters->TriangleProfileIndices = TriangleProfileIndexSRV;
                        FComputeShaderUtils::AddPass(
                            GraphBuilder,
                            RDG_EVENT_NAME("DWC Seam Destination Gather Slot %d", StaticSlot.MaterialSlotIndex),
                            SeamShader,
                            SeamParameters,
                            FIntVector(FMath::DivideAndRoundUp(StaticSlot.SeamDestinations.Num(), 64), 1, 1));
                        AddCopyTexturePass(GraphBuilder, SeamResolvedTexture, NextTexture);
                    }
                }
            }

            GraphBuilder.Execute();
        });

}


void FDWCGPUBackend::Shutdown()
{
    PendingContacts.Reset();
    DebugVertexDataUVs.Reset();
    DebugVertexMaterialSlots.Reset();
    PendingWetAllAmount = 0.0f;

    for (FMaterialSlotRuntime& Slot : MaterialSlots)
    {
        if (UMaterialInstanceDynamic* MID = Slot.MaterialInstance.Get())
        {
            MID->SetTextureParameterValue(WetnessMapParameterName, nullptr);
        }
    }

    if (RenderState.IsValid())
    {
        FlushRenderingCommands();
    }

    MaterialSlots.Reset();
    StaticSimulationData.Reset();
    RenderState.Reset();
    OwnerComponent.Reset();
    TargetSkeletalMesh.Reset();
    WetClothingAsset.Reset();
    WetMaterialInstances = nullptr;
    WetnessMapParameterName = DWCWetMaterialParameters::WetnessMap();
    MaxWetness = 1.0f;
    SpreadRateScale = 1.0f;
    DryRateScale = 1.0f;
    GravityFlowStrengthScale = 1.0f;
    LODIndex = 0;
    DebugDispatchLogCount = 0;
    bInitialized = false;
}
