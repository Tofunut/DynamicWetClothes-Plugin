#include "DataAssets/WetClothingAsset.h"

#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "RuntimeState/WetBoneOptimizationCacheBuilder.h"
#include "Utility/DWCError.h"

namespace
{
    static constexpr double UVQuantizeScale = 100000.0;
    static constexpr float CoincidentVertexNeighborTolerance = 0.001f;

    struct FRuntimeQuantizedUV
    {
        int64 U = 0;
        int64 V = 0;

        FRuntimeQuantizedUV() = default;

        explicit FRuntimeQuantizedUV(const FVector2D& InUV)
        {
            U = FMath::RoundToInt64(InUV.X * UVQuantizeScale);
            V = FMath::RoundToInt64(InUV.Y * UVQuantizeScale);
        }

        bool operator==(const FRuntimeQuantizedUV& Other) const
        {
            return U == Other.U && V == Other.V;
        }
    };

    uint32 HashInt64(int64 Value)
    {
        const uint64 UnsignedValue = static_cast<uint64>(Value);
        const uint32 Low = static_cast<uint32>(UnsignedValue & 0xFFFFFFFFull);
        const uint32 High = static_cast<uint32>((UnsignedValue >> 32) & 0xFFFFFFFFull);
        return HashCombine(::GetTypeHash(Low), ::GetTypeHash(High));
    }

    uint32 GetTypeHash(const FRuntimeQuantizedUV& Value)
    {
        return HashCombine(HashInt64(Value.U), HashInt64(Value.V));
    }

    bool LessUV(const FRuntimeQuantizedUV& A, const FRuntimeQuantizedUV& B)
    {
        return A.U != B.U ? A.U < B.U : A.V < B.V;
    }

    struct FRuntimeUVEdgeKey
    {
        FRuntimeQuantizedUV A;
        FRuntimeQuantizedUV B;

        FRuntimeUVEdgeKey() = default;

        FRuntimeUVEdgeKey(const FVector2D& InA, const FVector2D& InB)
        {
            FRuntimeQuantizedUV QuantizedA(InA);
            FRuntimeQuantizedUV QuantizedB(InB);

            if (LessUV(QuantizedB, QuantizedA))
            {
                A = QuantizedB;
                B = QuantizedA;
            }
            else
            {
                A = QuantizedA;
                B = QuantizedB;
            }
        }

        bool operator==(const FRuntimeUVEdgeKey& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    uint32 GetTypeHash(const FRuntimeUVEdgeKey& Key)
    {
        return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
    }

    struct FWetPartScopeKey
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 UVChannelIndex = INDEX_NONE;

        bool operator==(const FWetPartScopeKey& Other) const
        {
            return MaterialSlotIndex == Other.MaterialSlotIndex && UVChannelIndex == Other.UVChannelIndex;
        }
    };

    uint32 GetTypeHash(const FWetPartScopeKey& Key)
    {
        return HashCombine(::GetTypeHash(Key.MaterialSlotIndex), ::GetTypeHash(Key.UVChannelIndex));
    }

    struct FRuntimeTriangle
    {
        int32     VertexIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
        FVector2D UVs[3];
    };

    struct FRuntimeIsland
    {
        int32       UVIslandID = INDEX_NONE;
        TSet<int32> VertexIndices;
    };


    int32 FindParent(TArray<int32>& Parents, int32 Index)
    {
        if (Parents[Index] == Index)
        {
            return Index;
        }

        Parents[Index] = FindParent(Parents, Parents[Index]);
        return Parents[Index];
    }

    void UnionParents(TArray<int32>& Parents, int32 A, int32 B)
    {
        const int32 RootA = FindParent(Parents, A);
        const int32 RootB = FindParent(Parents, B);

        if (RootA != RootB)
        {
            Parents[RootB] = RootA;
        }
    }

    FString MakeMeshSignature(const USkeletalMesh* SkeletalMesh, const FSkeletalMeshLODRenderData& LODData, int32 LODIndex)
    {
        TArray<uint32> IndexBuffer;
        LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

        return FString::Printf(
            TEXT("%s|LOD=%d|Vertices=%d|Indices=%d|Materials=%d"),
            *GetPathNameSafe(SkeletalMesh),
            LODIndex,
            LODData.GetNumVertices(),
            IndexBuffer.Num(),
            SkeletalMesh != nullptr ? SkeletalMesh->GetMaterials().Num() : 0);
    }

    bool IsMaterialSlotWettable(const FWetClothingEditableWetPartData& EditableWetPartData, const int32 MaterialSlotIndex)
    {
        if (MaterialSlotIndex == INDEX_NONE)
        {
            return false;
        }

        const FWetClothingWettableMaterialSlotState* State = EditableWetPartData.WettableMaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingWettableMaterialSlotState& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });

        return State != nullptr && State->bIsWettableSlot;
    }

    FString MakeSourceDataSignature(const FWetClothingEditableWetPartData& EditableWetPartData)
    {
        TArray<FString> WettableSlotSignatures;
        WettableSlotSignatures.Reserve(EditableWetPartData.WettableMaterialSlots.Num());
        for (const FWetClothingWettableMaterialSlotState& SlotState : EditableWetPartData.WettableMaterialSlots)
        {
            WettableSlotSignatures.Add(FString::Printf(
                TEXT("Slot=%d|Wettable=%d"),
                SlotState.MaterialSlotIndex,
                SlotState.bIsWettableSlot ? 1 : 0));
        }
        WettableSlotSignatures.Sort();

        TArray<FString> EntrySignatures;
        EntrySignatures.Reserve(EditableWetPartData.WetPartEntries.Num());

        for (const FWetClothingWetPartEntry& Entry : EditableWetPartData.WetPartEntries)
        {
            TArray<int32> SortedIslandIDs = Entry.AssignedUVIslandIDs;
            SortedIslandIDs.Sort();

            TArray<FString> IslandStrings;
            IslandStrings.Reserve(SortedIslandIDs.Num());
            for (const int32 IslandID : SortedIslandIDs)
            {
                IslandStrings.Add(FString::FromInt(IslandID));
            }

            EntrySignatures.Add(FString::Printf(
                TEXT("Slot=%d|UV=%d|Part=%d|Islands=%s"),
                Entry.MaterialSlotIndex,
                Entry.UVChannelIndex,
                Entry.WetPartID,
                *FString::Join(IslandStrings, TEXT(","))));
        }

        EntrySignatures.Sort();
        return FString::Printf(
            TEXT("PrecomputedDataVersion=1|WettableSlots=%s|Entries=%s"),
            *FString::Join(WettableSlotSignatures, TEXT(";")),
            *FString::Join(EntrySignatures, TEXT(";")));
    }

    bool BuildRawTriangles(
        const USkeletalMesh*              SkeletalMesh,
        const FSkeletalMeshLODRenderData& LODData,
        const TArray<uint32>&             IndexBuffer,
        int32                             UVChannelIndex,
        int32                             MaterialSlotIndex,
        TArray<FRuntimeTriangle>&         OutTriangles,
        FString*                          OutErrorMessage)
    {
        OutTriangles.Reset();

        if (SkeletalMesh == nullptr || !SkeletalMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("A wet part references an invalid material slot."));
            return false;
        }

        const int32 NumUVChannels = static_cast<int32>(LODData.GetNumTexCoords());
        if (UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("A wet part references a UV channel that is not available on the mesh."));
            return false;
        }

        const int32 VertexCount = LODData.GetNumVertices();

        for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
        {
            if (!Section.IsValid() || Section.MaterialIndex != MaterialSlotIndex)
            {
                continue;
            }

            const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
            const int32 LastIndex = FMath::Min(FirstIndex + static_cast<int32>(Section.NumTriangles * 3), IndexBuffer.Num());

            for (int32 TriangleIndex = FirstIndex; TriangleIndex + 2 < LastIndex; TriangleIndex += 3)
            {
                const uint32 Index0 = IndexBuffer[TriangleIndex];
                const uint32 Index1 = IndexBuffer[TriangleIndex + 1];
                const uint32 Index2 = IndexBuffer[TriangleIndex + 2];

                if (Index0 >= static_cast<uint32>(VertexCount) ||
                    Index1 >= static_cast<uint32>(VertexCount) ||
                    Index2 >= static_cast<uint32>(VertexCount))
                {
                    continue;
                }

                FRuntimeTriangle Triangle;
                Triangle.VertexIndices[0] = static_cast<int32>(Index0);
                Triangle.VertexIndices[1] = static_cast<int32>(Index1);
                Triangle.VertexIndices[2] = static_cast<int32>(Index2);
                Triangle.UVs[0] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index0, UVChannelIndex));
                Triangle.UVs[1] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index1, UVChannelIndex));
                Triangle.UVs[2] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index2, UVChannelIndex));
                OutTriangles.Add(Triangle);
            }
        }

        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
    }

    void BuildIslands(const TArray<FRuntimeTriangle>& RawTriangles, TArray<FRuntimeIsland>& OutIslands)
    {
        OutIslands.Reset();

        if (RawTriangles.Num() == 0)
        {
            return;
        }

        TArray<int32> Parents;
        Parents.SetNum(RawTriangles.Num());

        for (int32 TriangleIndex = 0; TriangleIndex < RawTriangles.Num(); ++TriangleIndex)
        {
            Parents[TriangleIndex] = TriangleIndex;
        }

        TMap<FRuntimeUVEdgeKey, TArray<int32>> EdgeToTriangles;
        for (int32 TriangleIndex = 0; TriangleIndex < RawTriangles.Num(); ++TriangleIndex)
        {
            const FRuntimeTriangle& Triangle = RawTriangles[TriangleIndex];
            EdgeToTriangles.FindOrAdd(FRuntimeUVEdgeKey(Triangle.UVs[0], Triangle.UVs[1])).Add(TriangleIndex);
            EdgeToTriangles.FindOrAdd(FRuntimeUVEdgeKey(Triangle.UVs[1], Triangle.UVs[2])).Add(TriangleIndex);
            EdgeToTriangles.FindOrAdd(FRuntimeUVEdgeKey(Triangle.UVs[2], Triangle.UVs[0])).Add(TriangleIndex);
        }

        for (const TPair<FRuntimeUVEdgeKey, TArray<int32>>& Pair : EdgeToTriangles)
        {
            const TArray<int32>& ConnectedTriangles = Pair.Value;
            if (ConnectedTriangles.Num() <= 1)
            {
                continue;
            }

            const int32 FirstTriangle = ConnectedTriangles[0];
            for (int32 ConnectedIndex = 1; ConnectedIndex < ConnectedTriangles.Num(); ++ConnectedIndex)
            {
                UnionParents(Parents, FirstTriangle, ConnectedTriangles[ConnectedIndex]);
            }
        }

        TMap<int32, int32> RootToIslandArrayIndex;
        for (int32 TriangleIndex = 0; TriangleIndex < RawTriangles.Num(); ++TriangleIndex)
        {
            const int32 Root = FindParent(Parents, TriangleIndex);
            int32*      ExistingIslandIndex = RootToIslandArrayIndex.Find(Root);

            if (ExistingIslandIndex == nullptr)
            {
                FRuntimeIsland Island;
                Island.UVIslandID = OutIslands.Num();

                const int32 NewIslandIndex = OutIslands.Add(MoveTemp(Island));
                RootToIslandArrayIndex.Add(Root, NewIslandIndex);
                ExistingIslandIndex = RootToIslandArrayIndex.Find(Root);
            }

            FRuntimeIsland&         Island = OutIslands[*ExistingIslandIndex];
            const FRuntimeTriangle& Triangle = RawTriangles[TriangleIndex];
            Island.VertexIndices.Add(Triangle.VertexIndices[0]);
            Island.VertexIndices.Add(Triangle.VertexIndices[1]);
            Island.VertexIndices.Add(Triangle.VertexIndices[2]);
        }
    }

    void AddNeighbor(TArray<FWetClothingPrecomputedVertexNeighbors>& NeighborGraph, int32 A, int32 B)
    {
        if (NeighborGraph.IsValidIndex(A) && NeighborGraph.IsValidIndex(B) && A != B)
        {
            NeighborGraph[A].Neighbors.AddUnique(B);
        }
    }

    FIntVector MakeCoincidentVertexPositionKey(const FVector3f& Position)
    {
        static constexpr float QuantizeScale = 1.0f / CoincidentVertexNeighborTolerance;
        return FIntVector(
            FMath::RoundToInt(Position.X * QuantizeScale),
            FMath::RoundToInt(Position.Y * QuantizeScale),
            FMath::RoundToInt(Position.Z * QuantizeScale));
    }

    void ConnectCoincidentPositionNeighbors(
        const FSkeletalMeshLODRenderData&              LODData,
        const TArray<FWetClothingPrecomputedVertexData>& VertexData,
        TArray<FWetClothingPrecomputedVertexNeighbors>& NeighborGraph)
    {
        TMap<FIntVector, TArray<int32>> VerticesByPosition;
        const int32                     VertexCount = LODData.GetNumVertices();
        VerticesByPosition.Reserve(VertexCount);

        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            if (!VertexData.IsValidIndex(VertexIndex) || !VertexData[VertexIndex].bIsWettable)
            {
                continue;
            }

            const FVector3f Position = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
            VerticesByPosition.FindOrAdd(MakeCoincidentVertexPositionKey(Position)).Add(VertexIndex);
        }

        const float ToleranceSquared = FMath::Square(CoincidentVertexNeighborTolerance);
        for (const TPair<FIntVector, TArray<int32>>& Pair : VerticesByPosition)
        {
            const TArray<int32>& Vertices = Pair.Value;
            for (int32 IndexA = 0; IndexA < Vertices.Num(); ++IndexA)
            {
                const int32     VertexA = Vertices[IndexA];
                const FVector3f PositionA = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexA);

                for (int32 IndexB = IndexA + 1; IndexB < Vertices.Num(); ++IndexB)
                {
                    const int32     VertexB = Vertices[IndexB];
                    const FVector3f PositionB = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexB);
                    if ((PositionA - PositionB).SizeSquared() > ToleranceSquared)
                    {
                        continue;
                    }

                    AddNeighbor(NeighborGraph, VertexA, VertexB);
                    AddNeighbor(NeighborGraph, VertexB, VertexA);
                }
            }
        }
    }

    void BuildNeighborGraph(
        const FSkeletalMeshLODRenderData&              LODData,
        const TArray<uint32>&                          IndexBuffer,
        const TArray<FWetClothingPrecomputedVertexData>& VertexData,
        TArray<FWetClothingPrecomputedVertexNeighbors>& OutNeighborGraph)
    {
        OutNeighborGraph.SetNum(LODData.GetNumVertices());

        for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
        {
            if (!Section.IsValid())
            {
                continue;
            }

            const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
            const int32 LastIndex = FMath::Min(FirstIndex + static_cast<int32>(Section.NumTriangles * 3), IndexBuffer.Num());

            for (int32 TriangleIndex = FirstIndex; TriangleIndex + 2 < LastIndex; TriangleIndex += 3)
            {
                const int32 Index0 = static_cast<int32>(IndexBuffer[TriangleIndex]);
                const int32 Index1 = static_cast<int32>(IndexBuffer[TriangleIndex + 1]);
                const int32 Index2 = static_cast<int32>(IndexBuffer[TriangleIndex + 2]);

                const bool b0 = VertexData.IsValidIndex(Index0) && VertexData[Index0].bIsWettable;
                const bool b1 = VertexData.IsValidIndex(Index1) && VertexData[Index1].bIsWettable;
                const bool b2 = VertexData.IsValidIndex(Index2) && VertexData[Index2].bIsWettable;

                if (b0 && b1)
                {
                    AddNeighbor(OutNeighborGraph, Index0, Index1);
                    AddNeighbor(OutNeighborGraph, Index1, Index0);
                }
                if (b0 && b2)
                {
                    AddNeighbor(OutNeighborGraph, Index0, Index2);
                    AddNeighbor(OutNeighborGraph, Index2, Index0);
                }
                if (b1 && b2)
                {
                    AddNeighbor(OutNeighborGraph, Index1, Index2);
                    AddNeighbor(OutNeighborGraph, Index2, Index1);
                }
            }
        }

        ConnectCoincidentPositionNeighbors(LODData, VertexData, OutNeighborGraph);
    }
    void FilterBoneOptimizationCacheByWettableVertices(
        FWetBoneOptimizationCache& RuntimeBoneOptimizationCache,
        const TArray<FWetClothingPrecomputedVertexData>& VertexData)
    {
        FWetBonePrimaryVertexCache& PrimaryCache = RuntimeBoneOptimizationCache.PrimaryVertexCache;
        if (PrimaryCache.BoneCount <= 0 || PrimaryCache.BoneStartOffsets.Num() != PrimaryCache.BoneCount + 1)
        {
            return;
        }

        TArray<int32> VertexCountsByBone;
        VertexCountsByBone.Init(0, PrimaryCache.BoneCount);

        for (int32 BoneIndex = 0; BoneIndex < PrimaryCache.BoneCount; ++BoneIndex)
        {
            const int32 StartOffset = PrimaryCache.BoneStartOffsets[BoneIndex];
            const int32 EndOffset = PrimaryCache.BoneStartOffsets[BoneIndex + 1];
            for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
            {
                if (!PrimaryCache.FlatVertexIndices.IsValidIndex(Offset))
                {
                    continue;
                }

                const int32 VertexIndex = PrimaryCache.FlatVertexIndices[Offset];
                if (VertexData.IsValidIndex(VertexIndex) && VertexData[VertexIndex].bIsWettable)
                {
                    ++VertexCountsByBone[BoneIndex];
                }
            }
        }

        TArray<int32> NewStartOffsets;
        NewStartOffsets.SetNumZeroed(PrimaryCache.BoneCount + 1);
        for (int32 BoneIndex = 0; BoneIndex < PrimaryCache.BoneCount; ++BoneIndex)
        {
            NewStartOffsets[BoneIndex + 1] = NewStartOffsets[BoneIndex] + VertexCountsByBone[BoneIndex];
        }

        TArray<int32> NewFlatVertexIndices;
        NewFlatVertexIndices.SetNumUninitialized(NewStartOffsets.Last());

        TArray<int32> WriteOffsets = NewStartOffsets;
        for (int32 BoneIndex = 0; BoneIndex < PrimaryCache.BoneCount; ++BoneIndex)
        {
            const int32 StartOffset = PrimaryCache.BoneStartOffsets[BoneIndex];
            const int32 EndOffset = PrimaryCache.BoneStartOffsets[BoneIndex + 1];
            for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
            {
                if (!PrimaryCache.FlatVertexIndices.IsValidIndex(Offset))
                {
                    continue;
                }

                const int32 VertexIndex = PrimaryCache.FlatVertexIndices[Offset];
                if (VertexData.IsValidIndex(VertexIndex) && VertexData[VertexIndex].bIsWettable)
                {
                    NewFlatVertexIndices[WriteOffsets[BoneIndex]++] = VertexIndex;
                }
            }
        }

        PrimaryCache.BoneStartOffsets = MoveTemp(NewStartOffsets);
        PrimaryCache.FlatVertexIndices = MoveTemp(NewFlatVertexIndices);
    }

} // namespace

void UWetClothingAsset::ClearPrecomputedSimulationData()
{
    PartData.PrecomputedSimulationData = FWetClothingPrecomputedSimulationData();
}


#if WITH_EDITOR
void UWetClothingAsset::PreSave(FObjectPreSaveContext SaveContext)
{
    Super::PreSave(SaveContext);

    if (TargetMesh == nullptr || IsPrecomputedSimulationDataValidForMesh(TargetMesh, 0))
    {
        return;
    }

    FString ErrorMessage;
    if (!RebuildPrecomputedSimulationData(&ErrorMessage, 0))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("WetClothingAsset: Failed to update precomputed simulation data before saving %s. %s"),
            *GetNameSafe(this),
            *ErrorMessage);
    }
}
#endif // WITH_EDITOR

bool UWetClothingAsset::IsPrecomputedSimulationDataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex) const
{
    if (!PartData.PrecomputedSimulationData.bIsValid || SkeletalMesh == nullptr)
    {
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    return PartData.PrecomputedSimulationData.DataVersion == 1 &&
           PartData.PrecomputedSimulationData.LODIndex == LODIndex &&
           PartData.PrecomputedSimulationData.VertexCount == LODData.GetNumVertices() &&
           PartData.PrecomputedSimulationData.MeshSignature == MakeMeshSignature(SkeletalMesh, LODData, LODIndex) &&
           PartData.PrecomputedSimulationData.SourceDataSignature == MakeSourceDataSignature(PartData.EditableWetPartData);
}

#if WITH_EDITOR
bool UWetClothingAsset::RebuildPrecomputedSimulationData(FString* OutErrorMessage, int32 LODIndex)
{
    ClearPrecomputedSimulationData();

    if (TargetMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No TargetMesh is assigned."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = TargetMesh->GetResourceForRendering();
    if (RenderData == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("TargetMesh render data is unavailable."));
        return false;
    }

    if (!RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The requested LOD render data is unavailable."));
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    TArray<uint32>                    IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

    if (LODData.GetNumVertices() <= 0 || IndexBuffer.Num() == 0)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("TargetMesh render data is empty."));
        return false;
    }

    PartData.GeneratedWetMaterialOverrides.RemoveAll(
        [this](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
        {
            return MaterialOverride.MaterialSlotIndex != INDEX_NONE &&
                   !IsMaterialSlotWettable(PartData.EditableWetPartData, MaterialOverride.MaterialSlotIndex);
        });

    PartData.PrecomputedSimulationData.bIsValid = true;
    PartData.PrecomputedSimulationData.LODIndex = LODIndex;
    PartData.PrecomputedSimulationData.VertexCount = LODData.GetNumVertices();
    PartData.PrecomputedSimulationData.MeshSignature = MakeMeshSignature(TargetMesh, LODData, LODIndex);
    PartData.PrecomputedSimulationData.SourceDataSignature = MakeSourceDataSignature(PartData.EditableWetPartData);
    PartData.PrecomputedSimulationData.DataVersion = 1;
    PartData.PrecomputedSimulationData.Vertices.SetNum(PartData.PrecomputedSimulationData.VertexCount);

    TMap<FWetPartScopeKey, TArray<int32>> EntryIndicesByScope;
    for (int32 EntryIndex = 0; EntryIndex < PartData.EditableWetPartData.WetPartEntries.Num(); ++EntryIndex)
    {
        const FWetClothingWetPartEntry& Entry = PartData.EditableWetPartData.WetPartEntries[EntryIndex];
        if (Entry.MaterialSlotIndex == INDEX_NONE ||
            Entry.UVChannelIndex == INDEX_NONE ||
            !IsMaterialSlotWettable(PartData.EditableWetPartData, Entry.MaterialSlotIndex))
        {
            continue;
        }

        FWetPartScopeKey ScopeKey;
        ScopeKey.MaterialSlotIndex = Entry.MaterialSlotIndex;
        ScopeKey.UVChannelIndex = Entry.UVChannelIndex;
        EntryIndicesByScope.FindOrAdd(ScopeKey).Add(EntryIndex);
    }

    for (const TPair<FWetPartScopeKey, TArray<int32>>& ScopePair : EntryIndicesByScope)
    {
        TArray<FRuntimeTriangle> RawTriangles;
        if (!BuildRawTriangles(
                TargetMesh,
                LODData,
                IndexBuffer,
                ScopePair.Key.UVChannelIndex,
                ScopePair.Key.MaterialSlotIndex,
                RawTriangles,
                OutErrorMessage))
        {
            ClearPrecomputedSimulationData();
            return false;
        }

        TArray<FRuntimeIsland> Islands;
        BuildIslands(RawTriangles, Islands);

        int32              DefaultEntryIndex = INDEX_NONE;
        TMap<int32, int32> AssignedUVIslandToEntryIndex;

        for (int32 EntryIndex : ScopePair.Value)
        {
            const FWetClothingWetPartEntry& Entry = PartData.EditableWetPartData.WetPartEntries[EntryIndex];
            if (Entry.WetPartID == 0)
            {
                DefaultEntryIndex = EntryIndex;
                continue;
            }

            for (int32 UVIslandID : Entry.AssignedUVIslandIDs)
            {
                AssignedUVIslandToEntryIndex.FindOrAdd(UVIslandID) = EntryIndex;
            }
        }

        for (const FRuntimeIsland& Island : Islands)
        {
            const int32* AssignedEntryIndex = AssignedUVIslandToEntryIndex.Find(Island.UVIslandID);
            const int32  EffectiveEntryIndex = AssignedEntryIndex != nullptr ? *AssignedEntryIndex : DefaultEntryIndex;

            if (!PartData.EditableWetPartData.WetPartEntries.IsValidIndex(EffectiveEntryIndex))
            {
                continue;
            }

            const FWetClothingWetPartEntry& Entry = PartData.EditableWetPartData.WetPartEntries[EffectiveEntryIndex];
            for (int32 VertexIndex : Island.VertexIndices)
            {
                if (!PartData.PrecomputedSimulationData.Vertices.IsValidIndex(VertexIndex))
                {
                    continue;
                }

                FWetClothingPrecomputedVertexData& VertexData = PartData.PrecomputedSimulationData.Vertices[VertexIndex];
                VertexData.WetPartID = Entry.WetPartID;
                VertexData.WetPartEntryIndex = EffectiveEntryIndex;
                VertexData.MaterialSlotIndex = ScopePair.Key.MaterialSlotIndex;
                VertexData.UVChannelIndex = ScopePair.Key.UVChannelIndex;
                VertexData.UVIslandID = Island.UVIslandID;
                VertexData.bIsWettable = true;
            }
        }
    }

    BuildNeighborGraph(
        LODData,
        IndexBuffer,
        PartData.PrecomputedSimulationData.Vertices,
        PartData.PrecomputedSimulationData.NeighborGraph);

    FWetBoneOptimizationCache   RuntimeBoneOptimizationCache;
    TArray<FWetBoneIncludeRule> IncludeRules;
    FString                     BoneCacheErrorMessage;
    if (FWetBoneOptimizationCacheBuilder::Build(
            TargetMesh,
            LODIndex,
            IncludeRules,
            RuntimeBoneOptimizationCache,
            &BoneCacheErrorMessage))
    {
        FilterBoneOptimizationCacheByWettableVertices(
            RuntimeBoneOptimizationCache,
            PartData.PrecomputedSimulationData.Vertices);

        PartData.PrecomputedSimulationData.BoneOptimizationCache.BuildFromRuntimeCache(
            TargetMesh,
            RuntimeBoneOptimizationCache,
            PartData.PrecomputedSimulationData.MeshSignature,
            nullptr);
    }
    else
    {
        PartData.PrecomputedSimulationData.BoneOptimizationCache.Reset();
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("WetClothingAsset: Failed to precompute bone optimization cache for %s. %s"),
            *GetNameSafe(TargetMesh),
            *BoneCacheErrorMessage);
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}
#endif // WITH_EDITOR
