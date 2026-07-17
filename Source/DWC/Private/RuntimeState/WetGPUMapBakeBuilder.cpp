#if WITH_EDITOR

#include "RuntimeState/WetGPUMapBakeBuilder.h"

#include "Async/ParallelFor.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "DataAssets/WetnessProfile.h"
#include "Misc/ScopedSlowTask.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace DWCWetGPUMapBakePrivate
{
constexpr double PositionQuantizeScale = 1000.0;
constexpr double UVEdgeEpsilon = 1.0e-6;

void SetGPUMapBakeError(FString* OutErrorMessage, const FString& Message)
{
    if (OutErrorMessage)
    {
        *OutErrorMessage = Message;
    }
}

void EnterGPUMapBakeProgressFrame(FScopedSlowTask* SlowTask, const float Work, const FText& Message)
{
    if (SlowTask != nullptr)
    {
        SlowTask->EnterProgressFrame(Work, Message);
    }
}

void EnterGPUMapBakeProgressTo(
    FScopedSlowTask* SlowTask,
    float& ConsumedWork,
    const float TargetWork,
    const FText& Message)
{
    const float Delta = TargetWork - ConsumedWork;
    if (Delta > KINDA_SMALL_NUMBER)
    {
        EnterGPUMapBakeProgressFrame(SlowTask, Delta, Message);
        ConsumedWork += Delta;
    }
}

struct FQuantizedPosition
{
    int64 X = 0;
    int64 Y = 0;
    int64 Z = 0;

    explicit FQuantizedPosition(const FVector3f& Position)
        : X(FMath::RoundToInt64(Position.X * PositionQuantizeScale))
        , Y(FMath::RoundToInt64(Position.Y * PositionQuantizeScale))
        , Z(FMath::RoundToInt64(Position.Z * PositionQuantizeScale))
    {
    }

    bool operator==(const FQuantizedPosition& Other) const
    {
        return X == Other.X && Y == Other.Y && Z == Other.Z;
    }
};

uint32 HashGPUMapBakeInt64(const int64 Value)
{
    const uint64 U = static_cast<uint64>(Value);
    return HashCombine(::GetTypeHash(static_cast<uint32>(U)), ::GetTypeHash(static_cast<uint32>(U >> 32)));
}

uint32 GetTypeHash(const FQuantizedPosition& Position)
{
    return HashCombine(HashCombine(HashGPUMapBakeInt64(Position.X), HashGPUMapBakeInt64(Position.Y)), HashGPUMapBakeInt64(Position.Z));
}

bool LessPosition(const FQuantizedPosition& A, const FQuantizedPosition& B)
{
    if (A.X != B.X) return A.X < B.X;
    if (A.Y != B.Y) return A.Y < B.Y;
    return A.Z < B.Z;
}

struct FPositionEdgeKey
{
    FQuantizedPosition A;
    FQuantizedPosition B;

    FPositionEdgeKey(const FVector3f& InA, const FVector3f& InB)
        : A(InA), B(InB)
    {
        if (LessPosition(B, A))
        {
            Swap(A, B);
        }
    }

    bool operator==(const FPositionEdgeKey& Other) const
    {
        return A == Other.A && B == Other.B;
    }
};

uint32 GetTypeHash(const FPositionEdgeKey& Key)
{
    return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
}

struct FEdgeRef
{
    int32 MaterialSlotIndex = INDEX_NONE;
    FVector3f PositionA = FVector3f::ZeroVector;
    FVector3f PositionB = FVector3f::ZeroVector;
    FVector3f FaceNormal = FVector3f::UpVector;
    FVector2D UVA = FVector2D::ZeroVector;
    FVector2D UVB = FVector2D::ZeroVector;
};

bool ComputeUVBarycentric(
    const FVector2D& Point,
    const FVector2D& A,
    const FVector2D& B,
    const FVector2D& C,
    FVector3f& OutBarycentric)
{
    const FVector2D V0 = B - A;
    const FVector2D V1 = C - A;
    const FVector2D V2 = Point - A;
    const double Denominator = V0.X * V1.Y - V1.X * V0.Y;
    if (FMath::Abs(Denominator) <= SMALL_NUMBER)
    {
        return false;
    }

    const float WeightB = static_cast<float>((V2.X * V1.Y - V1.X * V2.Y) / Denominator);
    const float WeightC = static_cast<float>((V0.X * V2.Y - V2.X * V0.Y) / Denominator);
    const float WeightA = 1.0f - WeightB - WeightC;
    OutBarycentric = FVector3f(WeightA, WeightB, WeightC);
    return WeightA >= -0.0001f && WeightB >= -0.0001f && WeightC >= -0.0001f;
}


uint32 PackBarycentricXY(const FVector3f& Barycentric)
{
    const uint32 PackedX = static_cast<uint32>(FMath::RoundToInt(
        FMath::Clamp(Barycentric.X, 0.0f, 1.0f) * 65535.0f));
    const uint32 PackedY = static_cast<uint32>(FMath::RoundToInt(
        FMath::Clamp(Barycentric.Y, 0.0f, 1.0f) * 65535.0f));
    return (PackedX & 0xffffu) | ((PackedY & 0xffffu) << 16u);
}

FVector3f UnpackBarycentricXY(const uint32 Packed)
{
    const float X = static_cast<float>(Packed & 0xffffu) / 65535.0f;
    const float Y = static_cast<float>((Packed >> 16u) & 0xffffu) / 65535.0f;
    return FVector3f(X, Y, 1.0f - X - Y);
}

int32 UVToTexelIndex(const FVector2D& UV, const int32 Resolution)
{
    if (UV.X < 0.0 || UV.X > 1.0 || UV.Y < 0.0 || UV.Y > 1.0 || Resolution <= 0)
    {
        return INDEX_NONE;
    }

    const int32 X = FMath::Clamp(FMath::FloorToInt(UV.X * Resolution), 0, Resolution - 1);
    const int32 Y = FMath::Clamp(FMath::FloorToInt(UV.Y * Resolution), 0, Resolution - 1);
    return Y * Resolution + X;
}

int32 FindNearestValidTexel(
    const FVector2D& UV,
    const int32 Resolution,
    const TArray<uint8>& ValidMask)
{
    const int32 BaseIndex = UVToTexelIndex(UV, Resolution);
    if (BaseIndex == INDEX_NONE || ValidMask.Num() != Resolution * Resolution)
    {
        return INDEX_NONE;
    }

    const int32 BaseX = BaseIndex % Resolution;
    const int32 BaseY = BaseIndex / Resolution;
    int32 BestIndex = INDEX_NONE;
    double BestDistanceSquared = TNumericLimits<double>::Max();

    // The exact UV edge can fall between texels or just outside the rasterized half-open
    // edge. Search a small local neighborhood for the actual surface texel.
    for (int32 Radius = 0; Radius <= 2; ++Radius)
    {
        for (int32 OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
        {
            for (int32 OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
            {
                const int32 X = BaseX + OffsetX;
                const int32 Y = BaseY + OffsetY;
                if (X < 0 || Y < 0 || X >= Resolution || Y >= Resolution)
                {
                    continue;
                }

                const int32 CandidateIndex = Y * Resolution + X;
                if (!ValidMask.IsValidIndex(CandidateIndex) || ValidMask[CandidateIndex] == 0)
                {
                    continue;
                }

                const FVector2D CandidateUV(
                    (static_cast<double>(X) + 0.5) / Resolution,
                    (static_cast<double>(Y) + 0.5) / Resolution);
                const double DistanceSquared = FVector2D::DistSquared(UV, CandidateUV);
                if (DistanceSquared < BestDistanceSquared)
                {
                    BestDistanceSquared = DistanceSquared;
                    BestIndex = CandidateIndex;
                }
            }
        }

        if (BestIndex != INDEX_NONE)
        {
            break;
        }
    }

    return BestIndex;
}

bool AreSameUVEdge(const FEdgeRef& A, const FEdgeRef& B)
{
    const bool bForward = A.UVA.Equals(B.UVA, UVEdgeEpsilon) && A.UVB.Equals(B.UVB, UVEdgeEpsilon);
    const bool bReverse = A.UVA.Equals(B.UVB, UVEdgeEpsilon) && A.UVB.Equals(B.UVA, UVEdgeEpsilon);
    return bForward || bReverse;
}

void AddSeamDirection(
    const FVector2D& SourceA,
    const FVector2D& SourceB,
    const FVector2D& DestinationA,
    const FVector2D& DestinationB,
    const int32 Resolution,
    const TArray<uint8>& ValidMask,
    TMap<int32, TMap<int32, float>>& InOutDestinationToSources)
{
    const double SourcePixels = (SourceB - SourceA).Size() * Resolution;
    const double DestinationPixels = (DestinationB - DestinationA).Size() * Resolution;
    const int32 SampleCount = FMath::Max(2, FMath::CeilToInt(FMath::Max(SourcePixels, DestinationPixels)) + 1);

    for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
    {
        const double Alpha = SampleCount > 1
            ? static_cast<double>(SampleIndex) / static_cast<double>(SampleCount - 1)
            : 0.0;

        const int32 SourceTexel = FindNearestValidTexel(
            FMath::Lerp(SourceA, SourceB, Alpha),
            Resolution,
            ValidMask);
        const int32 DestinationTexel = FindNearestValidTexel(
            FMath::Lerp(DestinationA, DestinationB, Alpha),
            Resolution,
            ValidMask);
        if (SourceTexel == INDEX_NONE || DestinationTexel == INDEX_NONE || SourceTexel == DestinationTexel)
        {
            continue;
        }

        float& SampleWeight = InOutDestinationToSources.FindOrAdd(DestinationTexel).FindOrAdd(SourceTexel);
        SampleWeight += 1.0f;
    }
}


struct FTrianglePartMetadata
{
    int32 IslandID = INDEX_NONE;
    int32 WetPartEntryIndex = INDEX_NONE;
};

FWetnessProfileParameters ResolveProfileParametersForEntry(
    const UWetClothingAsset& Asset,
    const int32 EntryIndex)
{
    if (!Asset.PartData.EditableWetPartData.WetPartEntries.IsValidIndex(EntryIndex))
    {
        return FWetnessProfileParameters();
    }

    const FWetPartProfileAssignment& Assignment =
        Asset.PartData.EditableWetPartData.WetPartEntries[EntryIndex].ProfileAssignment;
    if (Assignment.SourceProfile.IsValid())
    {
        if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(Assignment.SourceProfile.TryLoad()))
        {
            return SourceProfile->GetParameters();
        }
    }
    return Assignment.Parameters;
}

FDWCGPUProfileParameters MakeGPUProfile(const FWetnessProfileParameters& Parameters)
{
    FDWCGPUProfileParameters Result;
    Result.AbsorptionMultiplier = Parameters.GetAbsorptionMultiplier();
    Result.SpreadRatePerSecond = Parameters.GetSpreadRatePerSecond();
    Result.DryRatePerSecond = Parameters.GetDryRatePerSecond();
    Result.GravityFlowStrength = Parameters.GetGravityFlowStrength();
    return Result;
}

int32 FindOrAddProfile(
    TArray<FDWCGPUProfileParameters>& Profiles,
    const FDWCGPUProfileParameters& Candidate)
{
    for (int32 Index = 0; Index < Profiles.Num(); ++Index)
    {
        if (Profiles[Index].Equals(Candidate))
        {
            return Index;
        }
    }
    return Profiles.Add(Candidate);
}

bool IsWettableMaterialSlot(const UWetClothingAsset& Asset, const int32 MaterialSlotIndex)
{
    return Asset.IsMaterialSlotWettable(MaterialSlotIndex);
}

bool BuildTrianglePartLookup(
    const UWetClothingAsset& Asset,
    const int32 LODIndex,
    TMap<int32, FTrianglePartMetadata>& OutLookup,
    TMap<int32, int32>& OutDefaultEntryByMaterial,
    FString* OutErrorMessage)
{
    OutLookup.Reset();
    OutDefaultEntryByMaterial.Reset();
#if WITH_EDITORONLY_DATA
    const FDWCEditorUVTopologyData* Topology = Asset.FindOriginalUVTopologyForLOD(LODIndex);
    const FString CurrentTopologySignature = UWetClothingAsset::BuildMeshContentSignature(
        Asset.GetRuntimeSkeletalMesh(),
        LODIndex,
        Asset.GetOriginalUVChannelIndex());
    if (Topology == nullptr ||
        !Topology->bIsValid ||
        Topology->LODIndex != LODIndex ||
        Topology->UVChannelIndex != Asset.GetOriginalUVChannelIndex() ||
        CurrentTopologySignature.IsEmpty() ||
        Topology->BuildSignature != CurrentTopologySignature)
    {
        SetGPUMapBakeError(OutErrorMessage, TEXT("Original UV island topology is missing or stale. Regenerate DWC Data UV."));
        return false;
    }

    TMap<int32, TMap<int32, int32>> EntryByIslandByMaterial;
    const TArray<FWetClothingWetPartEntry>& Entries = Asset.PartData.EditableWetPartData.WetPartEntries;
    for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
    {
        const FWetClothingWetPartEntry& Entry = Entries[EntryIndex];
        if (!Asset.IsMaterialSlotWettable(Entry.MaterialSlotIndex))
        {
            continue;
        }
        if (Entry.WetPartID == 0)
        {
            if (OutDefaultEntryByMaterial.Contains(Entry.MaterialSlotIndex))
            {
                SetGPUMapBakeError(
                    OutErrorMessage,
                    FString::Printf(
                        TEXT("Wettable material slot %d has more than one default Wet Part entry."),
                        Entry.MaterialSlotIndex));
                return false;
            }
            OutDefaultEntryByMaterial.Add(Entry.MaterialSlotIndex, EntryIndex);
        }
        else
        {
            TMap<int32, int32>& EntryByIsland = EntryByIslandByMaterial.FindOrAdd(Entry.MaterialSlotIndex);
            for (const int32 IslandID : Entry.AssignedUVIslandIDs)
            {
                if (EntryByIsland.Contains(IslandID))
                {
                    SetGPUMapBakeError(
                        OutErrorMessage,
                        FString::Printf(
                            TEXT("Original-UV island %d in material slot %d is assigned to more than one Wet Part."),
                            IslandID,
                            Entry.MaterialSlotIndex));
                    return false;
                }
                EntryByIsland.Add(IslandID, EntryIndex);
            }
        }
    }

    for (const FDWCOriginalUVIslandTopology& Island : Topology->Islands)
    {
        if (!Asset.IsMaterialSlotWettable(Island.MaterialSlotIndex))
        {
            continue;
        }
        int32 EntryIndex = INDEX_NONE;
        if (const int32* DefaultEntry = OutDefaultEntryByMaterial.Find(Island.MaterialSlotIndex))
        {
            EntryIndex = *DefaultEntry;
        }
        if (const TMap<int32, int32>* ByIsland = EntryByIslandByMaterial.Find(Island.MaterialSlotIndex))
        {
            if (const int32* OverrideEntry = ByIsland->Find(Island.IslandID))
            {
                EntryIndex = *OverrideEntry;
            }
        }
        if (!Entries.IsValidIndex(EntryIndex))
        {
            SetGPUMapBakeError(
                OutErrorMessage,
                FString::Printf(
                    TEXT("Wettable material slot %d has Original-UV island %d with no default or explicit Wet Part assignment."),
                    Island.MaterialSlotIndex,
                    Island.IslandID));
            return false;
        }
        for (const int32 TriangleID : Island.TriangleIndices)
        {
            if (TriangleID < 0 || OutLookup.Contains(TriangleID))
            {
                SetGPUMapBakeError(
                    OutErrorMessage,
                    FString::Printf(
                        TEXT("Original-UV topology contains an invalid or duplicate triangle ID %d."),
                        TriangleID));
                return false;
            }
            OutLookup.Add(TriangleID, {Island.IslandID, EntryIndex});
        }
    }

    if (OutLookup.IsEmpty())
    {
        SetGPUMapBakeError(OutErrorMessage, TEXT("No wettable Original-UV islands are assigned to wet parts."));
        return false;
    }
    return true;
#else
    SetGPUMapBakeError(OutErrorMessage, TEXT("GPU bake is editor-only."));
    return false;
#endif
}

class FGPUWetMapSignatureBuilder
{
public:
    void AddBytes(const void* Data, const SIZE_T NumBytes)
    {
        const uint8* Bytes = static_cast<const uint8*>(Data);
        for (SIZE_T Index = 0; Index < NumBytes; ++Index)
        {
            Hash ^= static_cast<uint64>(Bytes[Index]);
            Hash *= 1099511628211ull;
        }
    }

    template <typename TValue>
    void AddValue(const TValue& Value)
    {
        AddBytes(&Value, sizeof(TValue));
    }

    void AddString(const FString& Value)
    {
        const int32 Length = Value.Len();
        AddValue(Length);
        if (Length > 0)
        {
            AddBytes(*Value, static_cast<SIZE_T>(Length) * sizeof(TCHAR));
        }
    }

    FString Finalize() const
    {
        return FString::Printf(
            TEXT("DWC_GPU_%016llX"),
            static_cast<unsigned long long>(Hash));
    }
private:
    uint64 Hash = 1469598103934665603ull;
};

TMap<FString, FString> GGPUWetSignatureCache;

FString MakeSignatureCachePrefix(
    const UWetClothingAsset& Asset,
    const int32 LODIndex,
    const TCHAR* Kind)
{
    return FString::Printf(TEXT("%s|%p|LOD=%d"), Kind, &Asset, LODIndex);
}

bool TryGetCachedSignature(const FString& CacheKey, FString& OutSignature)
{
    if (const FString* CachedSignature = GGPUWetSignatureCache.Find(CacheKey))
    {
        OutSignature = *CachedSignature;
        return true;
    }
    return false;
}

void StoreCachedSignature(const FString& CacheKey, const FString& Signature)
{
    GGPUWetSignatureCache.Add(CacheKey, Signature);
}

void AddProfileParametersToSignature(FGPUWetMapSignatureBuilder& Builder, const FWetnessProfileParameters& Parameters)
{
    Builder.AddValue(Parameters.Absorption);
    Builder.AddValue(Parameters.SpreadRate);
    Builder.AddValue(Parameters.DryRate);
    Builder.AddValue(Parameters.GravityFlowStrength);
}

bool ResolveWettableMaterialSlots(
    const UWetClothingAsset& Asset,
    TArray<int32>& OutMaterialSlots,
    FString* OutErrorMessage)
{
    OutMaterialSlots.Reset();
    const USkeletalMesh* RuntimeMesh = Asset.GetRuntimeSkeletalMesh();
    const int32 MaterialSlotCount = RuntimeMesh ? RuntimeMesh->GetMaterials().Num() : 0;
    if (RuntimeMesh == nullptr)
    {
        SetGPUMapBakeError(OutErrorMessage, TEXT("No Source Skeletal Mesh is available."));
        return false;
    }
    for (const FWetClothingWettableMaterialSlotState& SlotState : Asset.PartData.EditableWetPartData.WettableMaterialSlots)
    {
        if (!SlotState.bIsWettableSlot)
        {
            continue;
        }
        if (SlotState.MaterialSlotIndex < 0 || SlotState.MaterialSlotIndex >= MaterialSlotCount)
        {
            SetGPUMapBakeError(
                OutErrorMessage,
                FString::Printf(TEXT("Wettable material slot index %d is invalid for the Source Mesh."), SlotState.MaterialSlotIndex));
            return false;
        }
        OutMaterialSlots.AddUnique(SlotState.MaterialSlotIndex);
    }
    if (OutMaterialSlots.IsEmpty())
    {
        SetGPUMapBakeError(OutErrorMessage, TEXT("No material slot is marked Is Wettable."));
        return false;
    }
    OutMaterialSlots.Sort();
    return true;
}

FString BuildRuntimeSignatureCacheKey(
    const UWetClothingAsset& Asset,
    const USkeletalMesh& RuntimeMesh,
    const FSkeletalMeshLODRenderData& LODData,
    const FDWCDataUVPerLOD& DataUV,
    const TArray<uint32>& IndexBuffer,
    const TArray<int32>& WettableMaterialSlots,
    const int32 LODIndex)
{
    FGPUWetMapSignatureBuilder Builder;
    Builder.AddString(MakeSignatureCachePrefix(Asset, LODIndex, TEXT("Runtime")));
    Builder.AddString(RuntimeMesh.GetPathName());
    Builder.AddValue(FDWCGPULODBakeData::CurrentRuntimeDataVersion);
    Builder.AddValue(Asset.GetDWCDataUVChannelIndex());
    Builder.AddValue(static_cast<int32>(LODData.GetNumVertices()));
    Builder.AddValue(static_cast<int32>(LODData.GetNumTexCoords()));
    Builder.AddValue(IndexBuffer.Num());
    Builder.AddString(DataUV.MeshSignature);
    Builder.AddValue(DataUV.RenderVertexCount);
    Builder.AddValue(DataUV.DataUVs.Num());

    Builder.AddValue(WettableMaterialSlots.Num());
    for (const int32 MaterialSlotIndex : WettableMaterialSlots)
    {
        Builder.AddValue(MaterialSlotIndex);
    }

    Builder.AddValue(LODData.RenderSections.Num());
    for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
    {
        Builder.AddValue(Section.MaterialIndex);
        Builder.AddValue(Section.BaseIndex);
        Builder.AddValue(Section.NumTriangles);
        Builder.AddValue(Section.BaseVertexIndex);
        Builder.AddValue(Section.NumVertices);
    }

    const TArray<FWetClothingWettableMaterialSlotState>& WettableSlots =
        Asset.PartData.EditableWetPartData.WettableMaterialSlots;
    TArray<int32> WettableSlotIndices;
    for (int32 SlotStateIndex = 0; SlotStateIndex < WettableSlots.Num(); ++SlotStateIndex)
    {
        if (WettableSlots[SlotStateIndex].bIsWettableSlot)
        {
            WettableSlotIndices.Add(SlotStateIndex);
        }
    }
    WettableSlotIndices.Sort([&WettableSlots](const int32 A, const int32 B)
    {
        const FWetClothingWettableMaterialSlotState& Left = WettableSlots[A];
        const FWetClothingWettableMaterialSlotState& Right = WettableSlots[B];
        if (Left.MaterialSlotIndex != Right.MaterialSlotIndex)
        {
            return Left.MaterialSlotIndex < Right.MaterialSlotIndex;
        }
        return Left.ComponentPath < Right.ComponentPath;
    });
    Builder.AddValue(WettableSlotIndices.Num());
    for (const int32 SlotStateIndex : WettableSlotIndices)
    {
        const FWetClothingWettableMaterialSlotState& SlotState = WettableSlots[SlotStateIndex];
        Builder.AddString(SlotState.ComponentPath);
        Builder.AddValue(SlotState.MaterialSlotIndex);
    }

    const TArray<FWetClothingWetPartEntry>& Entries = Asset.PartData.EditableWetPartData.WetPartEntries;
    TArray<int32> WettableEntryIndices;
    for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
    {
        if (Asset.IsMaterialSlotWettable(Entries[EntryIndex].MaterialSlotIndex))
        {
            WettableEntryIndices.Add(EntryIndex);
        }
    }
    WettableEntryIndices.Sort([&Entries](const int32 A, const int32 B)
    {
        const FWetClothingWetPartEntry& Left = Entries[A];
        const FWetClothingWetPartEntry& Right = Entries[B];
        if (Left.MaterialSlotIndex != Right.MaterialSlotIndex)
        {
            return Left.MaterialSlotIndex < Right.MaterialSlotIndex;
        }
        if (Left.WetPartID != Right.WetPartID)
        {
            return Left.WetPartID < Right.WetPartID;
        }
        return Left.ComponentPath < Right.ComponentPath;
    });
    Builder.AddValue(Asset.GetOriginalUVChannelIndex());
    Builder.AddValue(WettableEntryIndices.Num());
    for (const int32 EntryIndex : WettableEntryIndices)
    {
        const FWetClothingWetPartEntry& Entry = Entries[EntryIndex];
        Builder.AddString(Entry.ComponentPath);
        Builder.AddValue(Entry.MaterialSlotIndex);
        Builder.AddValue(Asset.GetOriginalUVChannelIndex());
        Builder.AddValue(Entry.WetPartID);

        TArray<int32> SortedIslandIDs = Entry.AssignedUVIslandIDs;
        SortedIslandIDs.Sort();
        Builder.AddValue(SortedIslandIDs.Num());
        for (const int32 IslandID : SortedIslandIDs)
        {
            Builder.AddValue(IslandID);
        }

        Builder.AddString(Entry.ProfileAssignment.SourceProfile.ToString());
        Builder.AddValue(static_cast<uint8>(Entry.ProfileAssignment.BlendMode));
        AddProfileParametersToSignature(Builder, ResolveProfileParametersForEntry(Asset, EntryIndex));
    }

    return Builder.Finalize();
}

FString BuildMapSignatureFromRuntimeSignature(
    const FString& RuntimeSignature,
    const int32 Resolution)
{
    FGPUWetMapSignatureBuilder Builder;
    Builder.AddString(RuntimeSignature);
    Builder.AddValue(FDWCGPULODBakeData::CurrentMapBakeVersion);
    Builder.AddValue(Resolution);
    return Builder.Finalize();
}
} // namespace DWCWetGPUMapBakePrivate

using namespace DWCWetGPUMapBakePrivate;

bool FWetGPUMapBakeBuilder::BuildLODRuntimeSignature(
    const UWetClothingAsset& Asset,
    const int32 LODIndex,
    FString& OutSignature,
    FString* OutErrorMessage)
{
    OutSignature.Reset();

    const USkeletalMesh* RuntimeMesh = Asset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        SetGPUMapBakeError(OutErrorMessage, TEXT("No Source Skeletal Mesh is available."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = RuntimeMesh->GetResourceForRendering();
    if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d render data is unavailable."), LODIndex));
        return false;
    }

    const FDWCDataUVPerLOD* DataUV = Asset.FindGeneratedDataUVForLOD(LODIndex);
    if (DataUV == nullptr || !DataUV->bIsValid)
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d has no generated DWC Data UV payload."), LODIndex));
        return false;
    }

    TArray<int32> WettableMaterialSlots;
    if (!ResolveWettableMaterialSlots(Asset, WettableMaterialSlots, OutErrorMessage))
    {
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    TArray<uint32> IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
    if (IndexBuffer.IsEmpty())
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d index buffer is empty."), LODIndex));
        return false;
    }

    FGPUWetMapSignatureBuilder Builder;
    Builder.AddString(RuntimeMesh->GetPathName());
    Builder.AddValue(LODIndex);
    Builder.AddValue(FDWCGPULODBakeData::CurrentRuntimeDataVersion);

    const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
    const int32 TexCoordCount = static_cast<int32>(LODData.GetNumTexCoords());
    if (DataUV->RenderVertexCount != VertexCount || DataUV->DataUVs.Num() != VertexCount)
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d generated DWC Data UV payload does not match render vertex count."), LODIndex));
        return false;
    }

    const FString CurrentDataUVSignature = UWetClothingAsset::BuildMeshContentSignature(
        RuntimeMesh,
        LODIndex,
        Asset.GetDWCDataUVChannelIndex());
    if (CurrentDataUVSignature.IsEmpty() || DataUV->MeshSignature != CurrentDataUVSignature)
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d generated DWC Data UV payload is stale."), LODIndex));
        return false;
    }

    const FString CacheKey = BuildRuntimeSignatureCacheKey(
        Asset,
        *RuntimeMesh,
        LODData,
        *DataUV,
        IndexBuffer,
        WettableMaterialSlots,
        LODIndex);
    if (TryGetCachedSignature(CacheKey, OutSignature))
    {
        SetGPUMapBakeError(OutErrorMessage, TEXT(""));
        return true;
    }

    Builder.AddValue(VertexCount);
    Builder.AddValue(TexCoordCount);
    Builder.AddValue(IndexBuffer.Num());
    for (const uint32 Index : IndexBuffer)
    {
        Builder.AddValue(Index);
    }

    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        const FVector3f Position = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
        Builder.AddValue(Position.X);
        Builder.AddValue(Position.Y);
        Builder.AddValue(Position.Z);
    }

    Builder.AddString(DataUV->MeshSignature);
    Builder.AddValue(DataUV->DataUVs.Num());
    for (const FVector2f& UV : DataUV->DataUVs)
    {
        Builder.AddValue(UV.X);
        Builder.AddValue(UV.Y);
    }

    Builder.AddValue(WettableMaterialSlots.Num());
    for (const int32 MaterialSlotIndex : WettableMaterialSlots)
    {
        Builder.AddValue(MaterialSlotIndex);
    }

    const int32 SectionCount = LODData.RenderSections.Num();
    Builder.AddValue(SectionCount);
    for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
    {
        Builder.AddValue(Section.MaterialIndex);
        Builder.AddValue(Section.BaseIndex);
        Builder.AddValue(Section.NumTriangles);
        Builder.AddValue(Section.BaseVertexIndex);
        Builder.AddValue(Section.NumVertices);
    }

    const TArray<FWetClothingWettableMaterialSlotState>& WettableSlots =
        Asset.PartData.EditableWetPartData.WettableMaterialSlots;
    TArray<int32> WettableSlotIndices;
    for (int32 SlotStateIndex = 0; SlotStateIndex < WettableSlots.Num(); ++SlotStateIndex)
    {
        if (WettableSlots[SlotStateIndex].bIsWettableSlot)
        {
            WettableSlotIndices.Add(SlotStateIndex);
        }
    }
    WettableSlotIndices.Sort([&WettableSlots](const int32 A, const int32 B)
    {
        const FWetClothingWettableMaterialSlotState& Left = WettableSlots[A];
        const FWetClothingWettableMaterialSlotState& Right = WettableSlots[B];
        if (Left.MaterialSlotIndex != Right.MaterialSlotIndex)
        {
            return Left.MaterialSlotIndex < Right.MaterialSlotIndex;
        }
        return Left.ComponentPath < Right.ComponentPath;
    });
    Builder.AddValue(WettableSlotIndices.Num());
    for (const int32 SlotStateIndex : WettableSlotIndices)
    {
        const FWetClothingWettableMaterialSlotState& SlotState = WettableSlots[SlotStateIndex];
        Builder.AddString(SlotState.ComponentPath);
        Builder.AddValue(SlotState.MaterialSlotIndex);
    }

    const TArray<FWetClothingWetPartEntry>& Entries = Asset.PartData.EditableWetPartData.WetPartEntries;
    TArray<int32> WettableEntryIndices;
    for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
    {
        if (Asset.IsMaterialSlotWettable(Entries[EntryIndex].MaterialSlotIndex))
        {
            WettableEntryIndices.Add(EntryIndex);
        }
    }
    WettableEntryIndices.Sort([&Entries](const int32 A, const int32 B)
    {
        const FWetClothingWetPartEntry& Left = Entries[A];
        const FWetClothingWetPartEntry& Right = Entries[B];
        if (Left.MaterialSlotIndex != Right.MaterialSlotIndex)
        {
            return Left.MaterialSlotIndex < Right.MaterialSlotIndex;
        }
        if (Left.WetPartID != Right.WetPartID)
        {
            return Left.WetPartID < Right.WetPartID;
        }
        return Left.ComponentPath < Right.ComponentPath;
    });
    // Part Mode is fixed to the asset's Original UV. Do not let a stale per-entry
    // UV field invalidate an otherwise identical GPU bake.
    Builder.AddValue(Asset.GetOriginalUVChannelIndex());
    Builder.AddValue(WettableEntryIndices.Num());
    for (const int32 EntryIndex : WettableEntryIndices)
    {
        const FWetClothingWetPartEntry& Entry = Entries[EntryIndex];
        Builder.AddString(Entry.ComponentPath);
        Builder.AddValue(Entry.MaterialSlotIndex);
        Builder.AddValue(Asset.GetOriginalUVChannelIndex());
        Builder.AddValue(Entry.WetPartID);
        TArray<int32> SortedIslandIDs = Entry.AssignedUVIslandIDs;
        SortedIslandIDs.Sort();
        Builder.AddValue(SortedIslandIDs.Num());
        for (const int32 IslandID : SortedIslandIDs)
        {
            Builder.AddValue(IslandID);
        }

        Builder.AddString(Entry.ProfileAssignment.SourceProfile.ToString());
        Builder.AddValue(static_cast<uint8>(Entry.ProfileAssignment.BlendMode));
        AddProfileParametersToSignature(Builder, ResolveProfileParametersForEntry(Asset, EntryIndex));
    }

    OutSignature = Builder.Finalize();
    StoreCachedSignature(CacheKey, OutSignature);
    SetGPUMapBakeError(OutErrorMessage, TEXT(""));
    return true;
}

bool FWetGPUMapBakeBuilder::BuildLODMapSignature(
    const UWetClothingAsset& Asset,
    const int32 LODIndex,
    FString& OutSignature,
    FString* OutErrorMessage)
{
    FString RuntimeSignature;
    if (!BuildLODRuntimeSignature(Asset, LODIndex, RuntimeSignature, OutErrorMessage))
    {
        return false;
    }

    const int32 Resolution = Asset.GetSetupSettings().GetGPUSimulationMapResolution();
    const FString CacheKey = FString::Printf(
        TEXT("%s|Runtime=%s|Resolution=%d|Version=%d"),
        *MakeSignatureCachePrefix(Asset, LODIndex, TEXT("Map")),
        *RuntimeSignature,
        Resolution,
        FDWCGPULODBakeData::CurrentMapBakeVersion);
    if (TryGetCachedSignature(CacheKey, OutSignature))
    {
        SetGPUMapBakeError(OutErrorMessage, TEXT(""));
        return true;
    }

    OutSignature = BuildMapSignatureFromRuntimeSignature(RuntimeSignature, Resolution);
    StoreCachedSignature(CacheKey, OutSignature);
    SetGPUMapBakeError(OutErrorMessage, TEXT(""));
    return true;
}

static bool BuildLODInternal(
    UWetClothingAsset& Asset,
    const int32 LODIndex,
    const bool bBuildMaps,
    FString* OutErrorMessage,
    FScopedSlowTask* ExternalSlowTask)
{
    // Build transactionally so a failed map bake does not destroy previously valid Save-generated runtime data.
    FDWCGPULODBakeData Output;

    TUniquePtr<FScopedSlowTask> OwnedSlowTask;
    FScopedSlowTask* SlowTask = ExternalSlowTask;
    if (SlowTask == nullptr)
    {
        OwnedSlowTask = MakeUnique<FScopedSlowTask>(
            bBuildMaps ? 7.0f : 5.0f,
            FText::FromString(FString::Printf(
                TEXT("%s DWC GPU data for LOD%d..."),
                bBuildMaps ? TEXT("Baking") : TEXT("Building"),
                LODIndex)));
        SlowTask = OwnedSlowTask.Get();
        SlowTask->MakeDialog(false);
    }
    EnterGPUMapBakeProgressFrame(
        SlowTask,
        1.0f,
        FText::FromString(FString::Printf(TEXT("Validating LOD%d mesh, DWC Data UV, and wettable slot inputs..."), LODIndex)));

    USkeletalMesh* RuntimeMesh = Asset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        SetGPUMapBakeError(OutErrorMessage, TEXT("No Source Skeletal Mesh is available."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = RuntimeMesh->GetResourceForRendering();
    if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d render data is unavailable."), LODIndex));
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    TArray<uint32> IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
    if (IndexBuffer.IsEmpty())
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d index buffer is empty."), LODIndex));
        return false;
    }

    const FDWCDataUVPerLOD* DataUV = Asset.FindGeneratedDataUVForLOD(LODIndex);
    if (DataUV == nullptr || !DataUV->bIsValid)
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d has no generated DWC Data UV payload."), LODIndex));
        return false;
    }

    const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
    if (DataUV->RenderVertexCount != VertexCount || DataUV->DataUVs.Num() != VertexCount)
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d generated DWC Data UV payload does not match render vertex count."), LODIndex));
        return false;
    }

    const FString CurrentDataUVSignature = UWetClothingAsset::BuildMeshContentSignature(
        RuntimeMesh,
        LODIndex,
        Asset.GetDWCDataUVChannelIndex());
    if (CurrentDataUVSignature.IsEmpty() || DataUV->MeshSignature != CurrentDataUVSignature)
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d generated DWC Data UV payload is stale."), LODIndex));
        return false;
    }

    TArray<int32> WettableMaterialSlots;
    if (!ResolveWettableMaterialSlots(Asset, WettableMaterialSlots, OutErrorMessage))
    {
        return false;
    }

    EnterGPUMapBakeProgressFrame(
        SlowTask,
        1.0f,
        FText::FromString(FString::Printf(TEXT("Preparing LOD%d material-slot map buffers and runtime signatures..."), LODIndex)));

    FString RuntimeSignature;
    if (!FWetGPUMapBakeBuilder::BuildLODRuntimeSignature(Asset, LODIndex, RuntimeSignature, OutErrorMessage))
    {
        return false;
    }

    FString MapSignature;
    if (bBuildMaps && !FWetGPUMapBakeBuilder::BuildLODMapSignature(Asset, LODIndex, MapSignature, OutErrorMessage))
    {
        return false;
    }

    const int32 Resolution = Asset.GetSetupSettings().GetGPUSimulationMapResolution();
    const int32 TexelCount = Resolution * Resolution;
    const int32 TriangleCapacity = IndexBuffer.Num() / 3;

    TMap<int32, FTrianglePartMetadata> TrianglePartLookup;
    TMap<int32, int32> DefaultEntryByMaterial;
    if (!BuildTrianglePartLookup(Asset, LODIndex, TrianglePartLookup, DefaultEntryByMaterial, OutErrorMessage))
    {
        return false;
    }

    Output.RuntimeDataVersion = FDWCGPULODBakeData::CurrentRuntimeDataVersion;
    Output.BulkDataVersion = FDWCGPULODBakeData::CurrentBulkDataVersion;
    Output.MapBakeVersion = bBuildMaps ? FDWCGPULODBakeData::CurrentMapBakeVersion : 0;
    Output.LODIndex = LODIndex;
    Output.MeshSignature = DataUV->MeshSignature;
    Output.SourceDataSignature = UWetClothingAsset::BuildMeshContentSignature(Asset.GetSourceSkeletalMesh(), LODIndex, Asset.GetOriginalUVChannelIndex());
    Output.RuntimeSignature = MoveTemp(RuntimeSignature);
    Output.MapSignature = MoveTemp(MapSignature);
    Output.Triangles.Reserve(TriangleCapacity);

    FDWCTriangleValidationSummary ValidationSummary;
    auto RecordExampleTriangle = [&ValidationSummary](const int32 TriangleIndex)
    {
        if (ValidationSummary.ExampleTriangleIndices.Num() < 16)
        {
            ValidationSummary.ExampleTriangleIndices.AddUnique(TriangleIndex);
        }
    };

    TMap<int32, int32> MaterialToOutputIndex;
    for (const int32 MaterialSlotIndex : WettableMaterialSlots)
    {
        FDWCGPUMaterialSlotBakeData& Slot = Output.MaterialSlots.AddDefaulted_GetRef();
        Slot.MaterialSlotIndex = MaterialSlotIndex;
        Slot.UVChannelIndex = Asset.GetDWCDataUVChannelIndex();
        Slot.Resolution = bBuildMaps ? Resolution : 0;
        if (bBuildMaps)
        {
            Slot.TexelTriangleIDs.Init(INDEX_NONE, TexelCount);
            Slot.PackedTexelBarycentricXY.Init(0u, TexelCount);
            Slot.RestTexelAreas.Init(0.0f, TexelCount);
            Slot.ValidMask.Init(0, TexelCount);
        }
        MaterialToOutputIndex.Add(MaterialSlotIndex, Output.MaterialSlots.Num() - 1);
    }

    TMap<FPositionEdgeKey, TArray<FEdgeRef>> PositionEdges;
    TMap<int32, int32> CoveredTexelCountByTriangle;
    TMap<int32, TArray<int32>> IncidentTrianglesByVertex;

    const float SectionProgressFrame = 1.0f / FMath::Max(1, LODData.RenderSections.Num());
    for (int32 RenderSectionIndex = 0; RenderSectionIndex < LODData.RenderSections.Num(); ++RenderSectionIndex)
    {
        const FSkelMeshRenderSection& Section = LODData.RenderSections[RenderSectionIndex];
        EnterGPUMapBakeProgressFrame(
            SlowTask,
            SectionProgressFrame,
            FText::FromString(FString::Printf(
                TEXT("%s LOD%d section %d/%d..."),
                bBuildMaps ? TEXT("Rasterizing triangle IDs and barycentric texels for") : TEXT("Collecting wettable triangles from"),
                LODIndex,
                RenderSectionIndex + 1,
                LODData.RenderSections.Num())));

        const int32* SlotOutputIndex = MaterialToOutputIndex.Find(Section.MaterialIndex);
        if (!Section.IsValid() || !SlotOutputIndex)
        {
            continue;
        }

        FDWCGPUMaterialSlotBakeData& Slot = Output.MaterialSlots[*SlotOutputIndex];

        const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
        const int32 LastIndex = FMath::Min(FirstIndex + static_cast<int32>(Section.NumTriangles * 3), IndexBuffer.Num());

        for (int32 IndexOffset = FirstIndex; IndexOffset + 2 < LastIndex; IndexOffset += 3)
        {
            const int32 RenderTriangleID = IndexOffset / 3;
            ++ValidationSummary.TotalWettableTriangles;
            const FTrianglePartMetadata* PartMetadata = TrianglePartLookup.Find(RenderTriangleID);
            FTrianglePartMetadata FallbackPartMetadata;
            if (PartMetadata == nullptr)
            {
                ++ValidationSummary.DegenerateOriginalUVTriangles;
                RecordExampleTriangle(RenderTriangleID);
                if (const int32* DefaultEntry = DefaultEntryByMaterial.Find(Section.MaterialIndex))
                {
                    FallbackPartMetadata.IslandID = INDEX_NONE;
                    FallbackPartMetadata.WetPartEntryIndex = *DefaultEntry;
                    PartMetadata = &FallbackPartMetadata;
                }
                else
                {
                    continue;
                }
            }

            const int32 V0 = static_cast<int32>(IndexBuffer[IndexOffset]);
            const int32 V1 = static_cast<int32>(IndexBuffer[IndexOffset + 1]);
            const int32 V2 = static_cast<int32>(IndexBuffer[IndexOffset + 2]);
            if (V0 < 0 || V0 >= VertexCount || V1 < 0 || V1 >= VertexCount || V2 < 0 || V2 >= VertexCount)
            {
                SetGPUMapBakeError(
                    OutErrorMessage,
                    FString::Printf(
                        TEXT("Wettable triangle %d contains an invalid LOD%d render-vertex index."),
                        RenderTriangleID,
                        LODIndex));
                Output = FDWCGPULODBakeData();
                return false;
            }

            const FVector3f P0 = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V0);
            const FVector3f P1 = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V1);
            const FVector3f P2 = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V2);
            const FVector2D UV0(DataUV->DataUVs[V0]);
            const FVector2D UV1(DataUV->DataUVs[V1]);
            const FVector2D UV2(DataUV->DataUVs[V2]);

            auto IsDataUVInRange = [](const FVector2D& UV)
            {
                constexpr double Epsilon = 1.0e-6;
                return UV.X >= -Epsilon && UV.X <= 1.0 + Epsilon &&
                       UV.Y >= -Epsilon && UV.Y <= 1.0 + Epsilon;
            };

            if (!IsDataUVInRange(UV0) || !IsDataUVInRange(UV1) || !IsDataUVInRange(UV2))
            {
                ++ValidationSummary.InvalidUVTriangles;
                RecordExampleTriangle(RenderTriangleID);
                continue;
            }

            const double UVDoubleArea = FMath::Abs(
                (UV1.X - UV0.X) * (UV2.Y - UV0.Y) -
                (UV1.Y - UV0.Y) * (UV2.X - UV0.X));
            if (UVDoubleArea <= SMALL_NUMBER)
            {
                ++ValidationSummary.DegenerateDWCDataUVTriangles;
                RecordExampleTriangle(RenderTriangleID);
                continue;
            }

            const FVector3f RestCross = FVector3f::CrossProduct(P1 - P0, P2 - P0);
            const FVector3f RestFaceNormal = RestCross.GetSafeNormal();
            const float RestSurfaceArea = 0.5f * RestCross.Size();
            if (RestSurfaceArea <= SMALL_NUMBER || RestFaceNormal.IsNearlyZero())
            {
                ++ValidationSummary.Degenerate3DTriangles;
                RecordExampleTriangle(RenderTriangleID);
                continue;
            }

            const int32 TriangleID = Output.Triangles.Num();
            FDWCGPUBakedTriangle& Triangle = Output.Triangles.AddDefaulted_GetRef();
            Triangle.TriangleID = TriangleID;
            Triangle.RenderTriangleID = RenderTriangleID;
            Triangle.MaterialSlotIndex = Section.MaterialIndex;
            Triangle.RenderSectionIndex = RenderSectionIndex;
            Triangle.UVChannelIndex = Slot.UVChannelIndex;
            Triangle.UVIslandID = PartMetadata->IslandID;
            Triangle.VertexIndices = FIntVector(V0, V1, V2);
            Triangle.UV0 = UV0;
            Triangle.UV1 = UV1;
            Triangle.UV2 = UV2;
            Triangle.RestSurfaceArea = RestSurfaceArea;
            Triangle.ProfileIndex = FindOrAddProfile(
                Output.Profiles,
                MakeGPUProfile(ResolveProfileParametersForEntry(Asset, PartMetadata->WetPartEntryIndex)));

            IncidentTrianglesByVertex.FindOrAdd(V0).Add(TriangleID);
            IncidentTrianglesByVertex.FindOrAdd(V1).Add(TriangleID);
            IncidentTrianglesByVertex.FindOrAdd(V2).Add(TriangleID);

            if (bBuildMaps)
            {
                PositionEdges.FindOrAdd(FPositionEdgeKey(P0, P1)).Add({ Section.MaterialIndex, P0, P1, RestFaceNormal, UV0, UV1 });
                PositionEdges.FindOrAdd(FPositionEdgeKey(P1, P2)).Add({ Section.MaterialIndex, P1, P2, RestFaceNormal, UV1, UV2 });
                PositionEdges.FindOrAdd(FPositionEdgeKey(P2, P0)).Add({ Section.MaterialIndex, P2, P0, RestFaceNormal, UV2, UV0 });
            }

            if (!bBuildMaps)
            {
                continue;
            }

            const double MinU = FMath::Min3(UV0.X, UV1.X, UV2.X);
            const double MaxU = FMath::Max3(UV0.X, UV1.X, UV2.X);
            const double MinV = FMath::Min3(UV0.Y, UV1.Y, UV2.Y);
            const double MaxV = FMath::Max3(UV0.Y, UV1.Y, UV2.Y);

            const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * Resolution), 0, Resolution - 1);
            const int32 MaxX = FMath::Clamp(FMath::FloorToInt(MaxU * Resolution), 0, Resolution - 1);
            const int32 MinY = FMath::Clamp(FMath::FloorToInt(MinV * Resolution), 0, Resolution - 1);
            const int32 MaxY = FMath::Clamp(FMath::FloorToInt(MaxV * Resolution), 0, Resolution - 1);

            for (int32 Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32 X = MinX; X <= MaxX; ++X)
                {
                    const FVector2D TexelUV(
                        (static_cast<double>(X) + 0.5) / Resolution,
                        (static_cast<double>(Y) + 0.5) / Resolution);
                    FVector3f Barycentric;
                    if (!ComputeUVBarycentric(TexelUV, UV0, UV1, UV2, Barycentric))
                    {
                        continue;
                    }

                    const int32 TexelIndex = Y * Resolution + X;
                    if (Slot.ValidMask[TexelIndex] != 0 && Slot.TexelTriangleIDs[TexelIndex] != TriangleID)
                    {
                        // Adjacent UV triangles are allowed to meet on an edge. A texel center can
                        // land exactly on that shared edge, so only treat interior/interior coverage
                        // as a real overlap. Keep the first triangle for boundary-only coverage.
                        const FVector3f ExistingBarycentric =
                            UnpackBarycentricXY(Slot.PackedTexelBarycentricXY[TexelIndex]);
                        const float ExistingMinWeight = FMath::Min3(
                            ExistingBarycentric.X,
                            ExistingBarycentric.Y,
                            ExistingBarycentric.Z);
                        const float NewMinWeight = FMath::Min3(
                            Barycentric.X,
                            Barycentric.Y,
                            Barycentric.Z);

                        if (ExistingMinWeight > 0.0002f && NewMinWeight > 0.0002f)
                        {
                            SetGPUMapBakeError(
                                OutErrorMessage,
                                FString::Printf(
                                    TEXT("GPU data UV overlap detected in material slot %d at texel (%d,%d)."),
                                    Section.MaterialIndex,
                                    X,
                                    Y));
                            Output = FDWCGPULODBakeData();
                            return false;
                        }

                        continue;
                    }

                    Slot.ValidMask[TexelIndex] = 1;
                    Slot.TexelTriangleIDs[TexelIndex] = TriangleID;
                    Slot.PackedTexelBarycentricXY[TexelIndex] = PackBarycentricXY(Barycentric);
                    CoveredTexelCountByTriangle.FindOrAdd(TriangleID)++;
                }
            }
        }
    }

    if (bBuildMaps)
    {
        EnterGPUMapBakeProgressFrame(
            SlowTask,
            1.0f,
            FText::FromString(FString::Printf(TEXT("Computing LOD%d GPU texel areas..."), LODIndex)));
        for (FDWCGPUMaterialSlotBakeData& Slot : Output.MaterialSlots)
        {
            if (!Slot.ValidMask.Contains(static_cast<uint8>(1)))
            {
                SetGPUMapBakeError(
                    OutErrorMessage,
                    FString::Printf(
                        TEXT("No LOD%d texels were rasterized for GPU wet-map material slot %d."),
                        LODIndex,
                        Slot.MaterialSlotIndex));
                Output = FDWCGPULODBakeData();
                return false;
            }

            for (int32 TexelIndex = 0; TexelIndex < Slot.TexelTriangleIDs.Num(); ++TexelIndex)
            {
                const int32 TriangleID = Slot.TexelTriangleIDs[TexelIndex];
                const int32* CoveredCount = CoveredTexelCountByTriangle.Find(TriangleID);
                if (TriangleID != INDEX_NONE && CoveredCount && *CoveredCount > 0 && Output.Triangles.IsValidIndex(TriangleID))
                {
                    Slot.RestTexelAreas[TexelIndex] = Output.Triangles[TriangleID].RestSurfaceArea / static_cast<float>(*CoveredCount);
                }
            }
        }
    }

    TArray<int32> IncidentVertexIndices;
    EnterGPUMapBakeProgressFrame(
        SlowTask,
        1.0f,
        FText::FromString(FString::Printf(TEXT("Building LOD%d vertex incident triangle lookup..."), LODIndex)));
    IncidentTrianglesByVertex.GetKeys(IncidentVertexIndices);
    IncidentVertexIndices.Sort();
    for (const int32 VertexIndex : IncidentVertexIndices)
    {
        FDWCGPUVertexIncidentTriangles& Incident = Output.VertexIncidentTriangles.AddDefaulted_GetRef();
        Incident.SourceVertexIndex = VertexIndex;
        Incident.TriangleIDs = MoveTemp(IncidentTrianglesByVertex.FindChecked(VertexIndex));
    }

    if (bBuildMaps)
    {
        EnterGPUMapBakeProgressFrame(
            SlowTask,
            1.0f,
            FText::FromString(FString::Printf(TEXT("Building LOD%d same-material seam transfers..."), LODIndex)));
        TMap<int32, TMap<int32, TMap<int32, float>>> SeamMappingsByMaterial;

        for (const TPair<FPositionEdgeKey, TArray<FEdgeRef>>& Pair : PositionEdges)
        {
            const TArray<FEdgeRef>& Edges = Pair.Value;
            for (int32 AIndex = 0; AIndex < Edges.Num(); ++AIndex)
            {
                for (int32 BIndex = AIndex + 1; BIndex < Edges.Num(); ++BIndex)
                {
                    const FEdgeRef& A = Edges[AIndex];
                    const FEdgeRef& B = Edges[BIndex];
                    if (A.MaterialSlotIndex != B.MaterialSlotIndex || AreSameUVEdge(A, B))
                    {
                        continue; // Cross-material transfer is intentionally unsupported.
                    }

                    // Coincident but disconnected front/back layers can share the same quantized
                    // position edge. Do not create a seam bridge across strongly opposing faces.
                    if (FVector3f::DotProduct(A.FaceNormal, B.FaceNormal) < -0.25f)
                    {
                        continue;
                    }

                    TMap<int32, TMap<int32, float>>& DestinationToSources =
                        SeamMappingsByMaterial.FindOrAdd(A.MaterialSlotIndex);

                    const bool bSameDirection =
                        FVector3f::DistSquared(A.PositionA, B.PositionA) <= FVector3f::DistSquared(A.PositionA, B.PositionB);
                    const FVector2D BStart = bSameDirection ? B.UVA : B.UVB;
                    const FVector2D BEnd = bSameDirection ? B.UVB : B.UVA;

                    const int32* SlotIndex = MaterialToOutputIndex.Find(A.MaterialSlotIndex);
                    if (!SlotIndex || !Output.MaterialSlots.IsValidIndex(*SlotIndex))
                    {
                        continue;
                    }

                    const TArray<uint8>& ValidMask = Output.MaterialSlots[*SlotIndex].ValidMask;
                    AddSeamDirection(A.UVA, A.UVB, BStart, BEnd, Resolution, ValidMask, DestinationToSources);
                    AddSeamDirection(BStart, BEnd, A.UVA, A.UVB, Resolution, ValidMask, DestinationToSources);
                }
            }
        }

        for (FDWCGPUMaterialSlotBakeData& Slot : Output.MaterialSlots)
        {
            TMap<int32, TMap<int32, float>>* DestinationToSources =
                SeamMappingsByMaterial.Find(Slot.MaterialSlotIndex);
            if (!DestinationToSources)
            {
                continue;
            }

            TArray<int32> Destinations;
            DestinationToSources->GetKeys(Destinations);
            Destinations.Sort();

            for (const int32 DestinationTexel : Destinations)
            {
                TMap<int32, float>& Sources = (*DestinationToSources)[DestinationTexel];
                TArray<int32> SourceIndices;
                Sources.GetKeys(SourceIndices);
                SourceIndices.Sort();

                float TotalWeight = 0.0f;
                for (const int32 SourceTexel : SourceIndices)
                {
                    TotalWeight += FMath::Max(0.0f, Sources[SourceTexel]);
                }

                if (TotalWeight <= SMALL_NUMBER)
                {
                    continue;
                }

                FDWCGPUSeamDestination& Destination = Slot.SeamDestinations.AddDefaulted_GetRef();
                Destination.DestinationTexelIndex = DestinationTexel;
                Destination.IncomingStartIndex = Slot.SeamIncoming.Num();
                Destination.IncomingCount = SourceIndices.Num();

                for (const int32 SourceTexel : SourceIndices)
                {
                    FDWCGPUSeamIncoming& Incoming = Slot.SeamIncoming.AddDefaulted_GetRef();
                    Incoming.SourceTexelIndex = SourceTexel;
                    Incoming.Weight = FMath::Max(0.0f, Sources[SourceTexel]) / TotalWeight;
                }
            }
        }
    }

    ValidationSummary.GPUUsableTriangles = Output.Triangles.Num();
    ValidationSummary.CPUUsableTriangles = FMath::Max(0, ValidationSummary.TotalWettableTriangles - ValidationSummary.Degenerate3DTriangles);

    // These counts are persistent metadata used by HasGPURuntimeDataPayload() and
    // IsGPURuntimeDataValidForMesh(). They must be populated for the runtime-only
    // Save path as well as for the explicit map-bake path.
    Output.ProfileCount = Output.Profiles.Num();
    Output.TriangleCount = Output.Triangles.Num();
    Output.VertexIncidentRecordCount = Output.VertexIncidentTriangles.Num();

    Output.bRuntimeDataValid = !Output.Triangles.IsEmpty() && !Output.Profiles.IsEmpty() && !Output.VertexIncidentTriangles.IsEmpty();
    Output.RuntimeBuildGuid = FGuid::NewGuid();
    Output.bMapDataValid = bBuildMaps && Output.bRuntimeDataValid && !Output.MaterialSlots.IsEmpty();
    if (Output.bMapDataValid)
    {
        Output.MaterialSlotMapCount = Output.MaterialSlots.Num();
        Output.MapBakeGuid = FGuid::NewGuid();
    }
    else
    {
        Output.MaterialSlots.Reset();
        Output.MaterialSlotMapCount = 0;
        Output.MapBakeVersion = 0;
        Output.MapSignature.Reset();
        Output.MapBakeGuid.Invalidate();
    }

    const bool bSucceeded = bBuildMaps ? Output.bMapDataValid : Output.bRuntimeDataValid;
    if (!bSucceeded)
    {
        SetGPUMapBakeError(OutErrorMessage, TEXT("GPU runtime/map data did not contain any valid wettable triangles."));
        return false;
    }

    if (!bBuildMaps)
    {
        Asset.SetValidationSummary(ValidationSummary);
    }

    if (bBuildMaps)
    {
        EnterGPUMapBakeProgressFrame(
            SlowTask,
            1.0f,
            FText::FromString(FString::Printf(TEXT("Committing LOD%d GPU simulation maps into the WCA runtime payload..."), LODIndex)));
        // Explicit map baking must not replace Save-generated runtime structures. Only commit the
        // resolution-dependent texel/seam payload after confirming it was built against the same runtime signature.
        FDWCGPULODBakeData* Existing = Asset.BakedGPUWetMapLODs.FindByPredicate(
            [LODIndex](const FDWCGPULODBakeData& Candidate)
            {
                return Candidate.LODIndex == LODIndex;
            });
        if (Existing == nullptr || !Existing->bRuntimeDataValid || Existing->RuntimeSignature != Output.RuntimeSignature)
        {
            SetGPUMapBakeError(
                OutErrorMessage,
                TEXT("GPU runtime data changed while baking maps. Save the Wet Clothing Asset, then bake the GPU maps again."));
            return false;
        }

        Existing->RuntimeDataVersion = Output.RuntimeDataVersion;
        Existing->BulkDataVersion = Output.BulkDataVersion;
        Existing->bRuntimeDataValid = Output.bRuntimeDataValid;
        Existing->MeshSignature = MoveTemp(Output.MeshSignature);
        Existing->SourceDataSignature = MoveTemp(Output.SourceDataSignature);
        Existing->RuntimeSignature = MoveTemp(Output.RuntimeSignature);
        Existing->Profiles = MoveTemp(Output.Profiles);
        Existing->Triangles = MoveTemp(Output.Triangles);
        Existing->VertexIncidentTriangles = MoveTemp(Output.VertexIncidentTriangles);
        Existing->RuntimeBuildGuid = Output.RuntimeBuildGuid;
        Existing->ProfileCount = Existing->Profiles.Num();
        Existing->TriangleCount = Existing->Triangles.Num();
        Existing->VertexIncidentRecordCount = Existing->VertexIncidentTriangles.Num();
        Existing->MapBakeVersion = Output.MapBakeVersion;
        Existing->bMapDataValid = Output.bMapDataValid;
        Existing->MapSignature = MoveTemp(Output.MapSignature);
        Existing->MaterialSlots = MoveTemp(Output.MaterialSlots);
        Existing->MaterialSlotMapCount = Existing->MaterialSlots.Num();
        Existing->MapBakeGuid = Output.MapBakeGuid;
    }
    else
    {
        if (FDWCGPULODBakeData* Existing = Asset.BakedGPUWetMapLODs.FindByPredicate(
                [LODIndex](const FDWCGPULODBakeData& Candidate)
                {
                    return Candidate.LODIndex == LODIndex;
                }))
        {
            *Existing = MoveTemp(Output);
        }
        else
        {
            Asset.BakedGPUWetMapLODs.Add(MoveTemp(Output));
        }
    }

    SetGPUMapBakeError(OutErrorMessage, TEXT(""));
    return true;
}

static bool BuildLODMapsOnly(
    UWetClothingAsset& Asset,
    const int32 LODIndex,
    FString* OutErrorMessage,
    FScopedSlowTask* ExternalSlowTask)
{
    if (!Asset.IsGPURuntimeDataValidForMesh(Asset.GetRuntimeSkeletalMesh(), LODIndex))
    {
        SetGPUMapBakeError(
            OutErrorMessage,
            TEXT("GPU runtime data is missing or out of date. Rebuild GPU Runtime Data before baking GPU maps."));
        return false;
    }

    const FDWCGPULODBakeData& RuntimeData = Asset.GetGPUWetMapRuntimeData(LODIndex);
    if (!RuntimeData.bRuntimeDataValid ||
        RuntimeData.Triangles.IsEmpty() ||
        RuntimeData.Profiles.IsEmpty() ||
        RuntimeData.VertexIncidentTriangles.IsEmpty() ||
        RuntimeData.RuntimeSignature.IsEmpty())
    {
        SetGPUMapBakeError(
            OutErrorMessage,
            TEXT("GPU runtime payload is not loaded or does not contain runtime triangles/profiles. Rebuild GPU Runtime Data."));
        return false;
    }

    USkeletalMesh* RuntimeMesh = Asset.GetRuntimeSkeletalMesh();
    const FSkeletalMeshRenderData* RenderData = RuntimeMesh != nullptr ? RuntimeMesh->GetResourceForRendering() : nullptr;
    if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        SetGPUMapBakeError(OutErrorMessage, FString::Printf(TEXT("LOD%d render data is unavailable."), LODIndex));
        return false;
    }

    TArray<int32> WettableMaterialSlots;
    if (!ResolveWettableMaterialSlots(Asset, WettableMaterialSlots, OutErrorMessage))
    {
        return false;
    }

    const int32 Resolution = Asset.GetSetupSettings().GetGPUSimulationMapResolution();
    const int32 TexelCount = Resolution * Resolution;
    if (Resolution <= 0 || TexelCount <= 0)
    {
        SetGPUMapBakeError(OutErrorMessage, TEXT("GPU simulation map resolution is invalid."));
        return false;
    }

    TUniquePtr<FScopedSlowTask> OwnedSlowTask;
    FScopedSlowTask* SlowTask = ExternalSlowTask;
    if (SlowTask == nullptr)
    {
        OwnedSlowTask = MakeUnique<FScopedSlowTask>(
            6.0f,
            FText::FromString(FString::Printf(TEXT("Baking DWC GPU maps for LOD%d..."), LODIndex)));
        SlowTask = OwnedSlowTask.Get();
        SlowTask->MakeDialog(false);
    }
    EnterGPUMapBakeProgressFrame(
        SlowTask,
        0.5f,
        FText::FromString(FString::Printf(TEXT("Preparing LOD%d material-slot map buffers..."), LODIndex)));

    FDWCGPULODBakeData MapOutput;
    MapOutput.LODIndex = LODIndex;
    MapOutput.MapBakeVersion = FDWCGPULODBakeData::CurrentMapBakeVersion;
    MapOutput.MapSignature = BuildMapSignatureFromRuntimeSignature(RuntimeData.RuntimeSignature, Resolution);

    TMap<int32, int32> MaterialToOutputIndex;
    for (const int32 MaterialSlotIndex : WettableMaterialSlots)
    {
        FDWCGPUMaterialSlotBakeData& Slot = MapOutput.MaterialSlots.AddDefaulted_GetRef();
        Slot.MaterialSlotIndex = MaterialSlotIndex;
        Slot.UVChannelIndex = Asset.GetDWCDataUVChannelIndex();
        Slot.Resolution = Resolution;
        Slot.TexelTriangleIDs.Init(INDEX_NONE, TexelCount);
        Slot.PackedTexelBarycentricXY.Init(0u, TexelCount);
        Slot.RestTexelAreas.Init(0.0f, TexelCount);
        Slot.ValidMask.Init(0, TexelCount);
        MaterialToOutputIndex.Add(MaterialSlotIndex, MapOutput.MaterialSlots.Num() - 1);
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
    TArray<int32> CoveredTexelCountByTriangle;
    CoveredTexelCountByTriangle.Init(0, RuntimeData.Triangles.Num());
    TArray<TArray<int32>> TouchedTexelsBySlot;
    TouchedTexelsBySlot.SetNum(MapOutput.MaterialSlots.Num());
    TMap<FPositionEdgeKey, TArray<FEdgeRef>> PositionEdges;

    EnterGPUMapBakeProgressFrame(
        SlowTask,
        0.25f,
        FText::FromString(FString::Printf(
            TEXT("Rasterizing LOD%d GPU wetness-map texels (0/%d triangles)..."),
            LODIndex,
            RuntimeData.Triangles.Num())));

    auto IsDataUVInRange = [](const FVector2D& UV)
    {
        constexpr double Epsilon = 1.0e-6;
        return UV.X >= -Epsilon && UV.X <= 1.0 + Epsilon &&
               UV.Y >= -Epsilon && UV.Y <= 1.0 + Epsilon;
    };

    constexpr int32 ProgressUpdateTriangleInterval = 128;
    constexpr int32 ProgressUpdateEdgeInterval = 128;
    constexpr float RasterizeWork = 2.25f;
    constexpr float SeamCollectWork = 1.0f;
    constexpr float SeamCommitWork = 0.5f;
    float RasterizeConsumedWork = 0.0f;
    for (int32 RuntimeTriangleIndex = 0; RuntimeTriangleIndex < RuntimeData.Triangles.Num(); ++RuntimeTriangleIndex)
    {
        const FDWCGPUBakedTriangle& Triangle = RuntimeData.Triangles[RuntimeTriangleIndex];
        const int32 TriangleID = Triangle.TriangleID;
        if (TriangleID != RuntimeTriangleIndex || !CoveredTexelCountByTriangle.IsValidIndex(TriangleID))
        {
            SetGPUMapBakeError(
                OutErrorMessage,
                FString::Printf(TEXT("GPU runtime triangle %d has an invalid compact TriangleID."), RuntimeTriangleIndex));
            return false;
        }

        const int32* SlotOutputIndex = MaterialToOutputIndex.Find(Triangle.MaterialSlotIndex);
        if (SlotOutputIndex == nullptr || !MapOutput.MaterialSlots.IsValidIndex(*SlotOutputIndex))
        {
            continue;
        }

        const int32 V0 = Triangle.VertexIndices.X;
        const int32 V1 = Triangle.VertexIndices.Y;
        const int32 V2 = Triangle.VertexIndices.Z;
        if (V0 < 0 || V0 >= VertexCount || V1 < 0 || V1 >= VertexCount || V2 < 0 || V2 >= VertexCount)
        {
            SetGPUMapBakeError(
                OutErrorMessage,
                FString::Printf(TEXT("GPU runtime triangle %d contains an invalid LOD%d render-vertex index."), TriangleID, LODIndex));
            return false;
        }

        const FVector3f P0 = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V0);
        const FVector3f P1 = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V1);
        const FVector3f P2 = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V2);
        const FVector2D UV0 = Triangle.UV0;
        const FVector2D UV1 = Triangle.UV1;
        const FVector2D UV2 = Triangle.UV2;
        if (!IsDataUVInRange(UV0) || !IsDataUVInRange(UV1) || !IsDataUVInRange(UV2))
        {
            continue;
        }

        const double UVDoubleArea = FMath::Abs(
            (UV1.X - UV0.X) * (UV2.Y - UV0.Y) -
            (UV1.Y - UV0.Y) * (UV2.X - UV0.X));
        if (UVDoubleArea <= SMALL_NUMBER)
        {
            continue;
        }

        const FVector3f RestCross = FVector3f::CrossProduct(P1 - P0, P2 - P0);
        const FVector3f RestFaceNormal = RestCross.GetSafeNormal();
        if (RestFaceNormal.IsNearlyZero())
        {
            continue;
        }

        PositionEdges.FindOrAdd(FPositionEdgeKey(P0, P1)).Add({ Triangle.MaterialSlotIndex, P0, P1, RestFaceNormal, UV0, UV1 });
        PositionEdges.FindOrAdd(FPositionEdgeKey(P1, P2)).Add({ Triangle.MaterialSlotIndex, P1, P2, RestFaceNormal, UV1, UV2 });
        PositionEdges.FindOrAdd(FPositionEdgeKey(P2, P0)).Add({ Triangle.MaterialSlotIndex, P2, P0, RestFaceNormal, UV2, UV0 });

        FDWCGPUMaterialSlotBakeData& Slot = MapOutput.MaterialSlots[*SlotOutputIndex];
        TArray<int32>& TouchedTexels = TouchedTexelsBySlot[*SlotOutputIndex];

        const double MinU = FMath::Min3(UV0.X, UV1.X, UV2.X);
        const double MaxU = FMath::Max3(UV0.X, UV1.X, UV2.X);
        const double MinV = FMath::Min3(UV0.Y, UV1.Y, UV2.Y);
        const double MaxV = FMath::Max3(UV0.Y, UV1.Y, UV2.Y);

        const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * Resolution), 0, Resolution - 1);
        const int32 MaxX = FMath::Clamp(FMath::FloorToInt(MaxU * Resolution), 0, Resolution - 1);
        const int32 MinY = FMath::Clamp(FMath::FloorToInt(MinV * Resolution), 0, Resolution - 1);
        const int32 MaxY = FMath::Clamp(FMath::FloorToInt(MaxV * Resolution), 0, Resolution - 1);

        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 X = MinX; X <= MaxX; ++X)
            {
                const FVector2D TexelUV(
                    (static_cast<double>(X) + 0.5) / Resolution,
                    (static_cast<double>(Y) + 0.5) / Resolution);
                FVector3f Barycentric;
                if (!ComputeUVBarycentric(TexelUV, UV0, UV1, UV2, Barycentric))
                {
                    continue;
                }

                const int32 TexelIndex = Y * Resolution + X;
                if (Slot.ValidMask[TexelIndex] != 0 && Slot.TexelTriangleIDs[TexelIndex] != TriangleID)
                {
                    const FVector3f ExistingBarycentric =
                        UnpackBarycentricXY(Slot.PackedTexelBarycentricXY[TexelIndex]);
                    const float ExistingMinWeight = FMath::Min3(
                        ExistingBarycentric.X,
                        ExistingBarycentric.Y,
                        ExistingBarycentric.Z);
                    const float NewMinWeight = FMath::Min3(
                        Barycentric.X,
                        Barycentric.Y,
                        Barycentric.Z);

                    if (ExistingMinWeight > 0.0002f && NewMinWeight > 0.0002f)
                    {
                        SetGPUMapBakeError(
                            OutErrorMessage,
                            FString::Printf(
                                TEXT("GPU data UV overlap detected in material slot %d at texel (%d,%d)."),
                                Triangle.MaterialSlotIndex,
                                X,
                                Y));
                        return false;
                    }

                    continue;
                }

                if (Slot.ValidMask[TexelIndex] == 0)
                {
                    TouchedTexels.Add(TexelIndex);
                }
                Slot.ValidMask[TexelIndex] = 1;
                Slot.TexelTriangleIDs[TexelIndex] = TriangleID;
                Slot.PackedTexelBarycentricXY[TexelIndex] = PackBarycentricXY(Barycentric);
                ++CoveredTexelCountByTriangle[TriangleID];
            }
        }

        if ((RuntimeTriangleIndex + 1) % ProgressUpdateTriangleInterval == 0 ||
            RuntimeTriangleIndex + 1 == RuntimeData.Triangles.Num())
        {
            const float TargetWork =
                RuntimeData.Triangles.Num() > 0
                    ? RasterizeWork * static_cast<float>(RuntimeTriangleIndex + 1) / static_cast<float>(RuntimeData.Triangles.Num())
                    : RasterizeWork;
            EnterGPUMapBakeProgressTo(
                SlowTask,
                RasterizeConsumedWork,
                TargetWork,
                FText::FromString(FString::Printf(
                    TEXT("Rasterizing LOD%d GPU wetness-map texels (%d/%d triangles)..."),
                    LODIndex,
                    RuntimeTriangleIndex + 1,
                    RuntimeData.Triangles.Num())));
        }
    }

    EnterGPUMapBakeProgressTo(
        SlowTask,
        RasterizeConsumedWork,
        RasterizeWork,
        FText::FromString(FString::Printf(
            TEXT("Rasterized LOD%d GPU wetness-map texels for %d triangles."),
            LODIndex,
            RuntimeData.Triangles.Num())));

    EnterGPUMapBakeProgressFrame(
        SlowTask,
        1.0f,
        FText::FromString(FString::Printf(TEXT("Computing LOD%d GPU texel areas..."), LODIndex)));
    TArray<uint8> SlotHasTexels;
    SlotHasTexels.Init(0, MapOutput.MaterialSlots.Num());
    ParallelFor(MapOutput.MaterialSlots.Num(), [&MapOutput, &RuntimeData, &CoveredTexelCountByTriangle, &TouchedTexelsBySlot, &SlotHasTexels](const int32 SlotIndex)
    {
        FDWCGPUMaterialSlotBakeData& Slot = MapOutput.MaterialSlots[SlotIndex];
        const TArray<int32>& TouchedTexels = TouchedTexelsBySlot[SlotIndex];
        SlotHasTexels[SlotIndex] = TouchedTexels.IsEmpty() ? 0 : 1;
        for (const int32 TexelIndex : TouchedTexels)
        {
            const int32 TriangleID = Slot.TexelTriangleIDs[TexelIndex];
            if (RuntimeData.Triangles.IsValidIndex(TriangleID) &&
                CoveredTexelCountByTriangle.IsValidIndex(TriangleID) &&
                CoveredTexelCountByTriangle[TriangleID] > 0)
            {
                Slot.RestTexelAreas[TexelIndex] =
                    RuntimeData.Triangles[TriangleID].RestSurfaceArea /
                    static_cast<float>(CoveredTexelCountByTriangle[TriangleID]);
            }
        }
    });

    for (int32 SlotIndex = 0; SlotIndex < MapOutput.MaterialSlots.Num(); ++SlotIndex)
    {
        if (SlotHasTexels[SlotIndex] == 0)
        {
            SetGPUMapBakeError(
                OutErrorMessage,
                FString::Printf(
                    TEXT("No LOD%d texels were rasterized for GPU wet-map material slot %d."),
                    LODIndex,
                    MapOutput.MaterialSlots[SlotIndex].MaterialSlotIndex));
            return false;
        }
    }

    EnterGPUMapBakeProgressFrame(
        SlowTask,
        0.25f,
        FText::FromString(FString::Printf(
            TEXT("Scanning LOD%d edges for same-material seam transfers (0/%d edge groups)..."),
            LODIndex,
            PositionEdges.Num())));
    TMap<int32, TMap<int32, TMap<int32, float>>> SeamMappingsByMaterial;

    int32 ProcessedEdgeGroups = 0;
    float SeamCollectConsumedWork = 0.0f;
    for (const TPair<FPositionEdgeKey, TArray<FEdgeRef>>& Pair : PositionEdges)
    {
        const TArray<FEdgeRef>& Edges = Pair.Value;
        for (int32 AIndex = 0; AIndex < Edges.Num(); ++AIndex)
        {
            for (int32 BIndex = AIndex + 1; BIndex < Edges.Num(); ++BIndex)
            {
                const FEdgeRef& A = Edges[AIndex];
                const FEdgeRef& B = Edges[BIndex];
                if (A.MaterialSlotIndex != B.MaterialSlotIndex || AreSameUVEdge(A, B))
                {
                    continue;
                }

                if (FVector3f::DotProduct(A.FaceNormal, B.FaceNormal) < -0.25f)
                {
                    continue;
                }

                const int32* SlotIndex = MaterialToOutputIndex.Find(A.MaterialSlotIndex);
                if (!SlotIndex || !MapOutput.MaterialSlots.IsValidIndex(*SlotIndex))
                {
                    continue;
                }

                TMap<int32, TMap<int32, float>>& DestinationToSources =
                    SeamMappingsByMaterial.FindOrAdd(A.MaterialSlotIndex);
                const bool bSameDirection =
                    FVector3f::DistSquared(A.PositionA, B.PositionA) <= FVector3f::DistSquared(A.PositionA, B.PositionB);
                const FVector2D BStart = bSameDirection ? B.UVA : B.UVB;
                const FVector2D BEnd = bSameDirection ? B.UVB : B.UVA;
                const TArray<uint8>& ValidMask = MapOutput.MaterialSlots[*SlotIndex].ValidMask;
                AddSeamDirection(A.UVA, A.UVB, BStart, BEnd, Resolution, ValidMask, DestinationToSources);
                AddSeamDirection(BStart, BEnd, A.UVA, A.UVB, Resolution, ValidMask, DestinationToSources);
            }
        }

        ++ProcessedEdgeGroups;
        if (ProcessedEdgeGroups % ProgressUpdateEdgeInterval == 0 ||
            ProcessedEdgeGroups == PositionEdges.Num())
        {
            const float TargetWork =
                PositionEdges.Num() > 0
                    ? SeamCollectWork * static_cast<float>(ProcessedEdgeGroups) / static_cast<float>(PositionEdges.Num())
                    : SeamCollectWork;
            EnterGPUMapBakeProgressTo(
                SlowTask,
                SeamCollectConsumedWork,
                TargetWork,
                FText::FromString(FString::Printf(
                    TEXT("Scanning LOD%d edges for same-material seam transfers (%d/%d edge groups)..."),
                    LODIndex,
                    ProcessedEdgeGroups,
                    PositionEdges.Num())));
        }
    }

    EnterGPUMapBakeProgressTo(
        SlowTask,
        SeamCollectConsumedWork,
        SeamCollectWork,
        FText::FromString(FString::Printf(
            TEXT("Packing LOD%d same-material seam transfers..."),
            LODIndex)));

    int32 ProcessedSeamSlots = 0;
    float SeamCommitConsumedWork = 0.0f;
    for (FDWCGPUMaterialSlotBakeData& Slot : MapOutput.MaterialSlots)
    {
        TMap<int32, TMap<int32, float>>* DestinationToSources =
            SeamMappingsByMaterial.Find(Slot.MaterialSlotIndex);
        if (!DestinationToSources)
        {
            continue;
        }

        TArray<int32> Destinations;
        DestinationToSources->GetKeys(Destinations);
        Destinations.Sort();
        for (const int32 DestinationTexel : Destinations)
        {
            TMap<int32, float>& Sources = (*DestinationToSources)[DestinationTexel];
            TArray<int32> SourceIndices;
            Sources.GetKeys(SourceIndices);
            SourceIndices.Sort();

            float TotalWeight = 0.0f;
            for (const int32 SourceTexel : SourceIndices)
            {
                TotalWeight += FMath::Max(0.0f, Sources[SourceTexel]);
            }
            if (TotalWeight <= SMALL_NUMBER)
            {
                continue;
            }

            FDWCGPUSeamDestination& Destination = Slot.SeamDestinations.AddDefaulted_GetRef();
            Destination.DestinationTexelIndex = DestinationTexel;
            Destination.IncomingStartIndex = Slot.SeamIncoming.Num();
            Destination.IncomingCount = SourceIndices.Num();
            for (const int32 SourceTexel : SourceIndices)
            {
                FDWCGPUSeamIncoming& Incoming = Slot.SeamIncoming.AddDefaulted_GetRef();
                Incoming.SourceTexelIndex = SourceTexel;
                Incoming.Weight = FMath::Max(0.0f, Sources[SourceTexel]) / TotalWeight;
            }
        }

        ++ProcessedSeamSlots;
        const float TargetWork =
            MapOutput.MaterialSlots.Num() > 0
                ? SeamCommitWork * static_cast<float>(ProcessedSeamSlots) / static_cast<float>(MapOutput.MaterialSlots.Num())
                : SeamCommitWork;
        EnterGPUMapBakeProgressTo(
            SlowTask,
            SeamCommitConsumedWork,
            TargetWork,
            FText::FromString(FString::Printf(
                TEXT("Packing LOD%d same-material seam transfers (%d/%d material slots)..."),
                LODIndex,
                ProcessedSeamSlots,
                MapOutput.MaterialSlots.Num())));
    }

    EnterGPUMapBakeProgressTo(
        SlowTask,
        SeamCommitConsumedWork,
        SeamCommitWork,
        FText::FromString(FString::Printf(
            TEXT("Packed LOD%d same-material seam transfers."),
            LODIndex)));

    EnterGPUMapBakeProgressFrame(
        SlowTask,
        0.25f,
        FText::FromString(FString::Printf(TEXT("Committing LOD%d GPU simulation maps into the WCA runtime payload..."), LODIndex)));

    FDWCGPULODBakeData* Existing = Asset.BakedGPUWetMapLODs.FindByPredicate(
        [LODIndex](const FDWCGPULODBakeData& Candidate)
        {
            return Candidate.LODIndex == LODIndex;
        });
    if (Existing == nullptr || !Existing->bRuntimeDataValid || Existing->RuntimeSignature != RuntimeData.RuntimeSignature)
    {
        SetGPUMapBakeError(
            OutErrorMessage,
            TEXT("GPU runtime data changed while baking maps. Rebuild GPU Runtime Data, then bake the GPU maps again."));
        return false;
    }

    Existing->MapBakeVersion = FDWCGPULODBakeData::CurrentMapBakeVersion;
    Existing->bMapDataValid = true;
    Existing->MapSignature = MoveTemp(MapOutput.MapSignature);
    Existing->MaterialSlots = MoveTemp(MapOutput.MaterialSlots);
    Existing->MaterialSlotMapCount = Existing->MaterialSlots.Num();
    Existing->MapBakeGuid = FGuid::NewGuid();

    SetGPUMapBakeError(OutErrorMessage, TEXT(""));
    return true;
}

bool FWetGPUMapBakeBuilder::BuildRuntimeLOD(
    UWetClothingAsset& Asset,
    const int32 LODIndex,
    FString* OutErrorMessage,
    FScopedSlowTask* SlowTask)
{
    return BuildLODInternal(Asset, LODIndex, false, OutErrorMessage, SlowTask);
}

bool FWetGPUMapBakeBuilder::BuildLODMaps(
    UWetClothingAsset& Asset,
    const int32 LODIndex,
    FString* OutErrorMessage,
    FScopedSlowTask* SlowTask)
{
    return BuildLODMapsOnly(Asset, LODIndex, OutErrorMessage, SlowTask);
}

void FWetGPUMapBakeBuilder::ClearSignatureCache()
{
    GGPUWetSignatureCache.Reset();
}

#endif // WITH_EDITOR
