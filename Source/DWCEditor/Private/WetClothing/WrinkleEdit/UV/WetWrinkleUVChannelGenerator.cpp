#include "WetWrinkleUVChannelGenerator.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "MeshDescription.h"
#include "MeshUVChannelInfo.h"
#include "SkeletalMeshAttributes.h"

namespace WetWrinkleUVChannelGeneratorInternal
{
    struct FTriangleRecord
    {
        FTriangleID TriangleID; //Triangle ID for Identifying Triangle (in each Mesh)
        int32 MaterialSlotIndex = INDEX_NONE; //Which Material Slot it takes (in Mesh)
        FVertexInstanceID VertexInstances[3]; //The Datas That can be different by each same Vertex
        FVertexID Vertices[3]; // Vertex Indices
        FVector Positions[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector }; //Mesh Local Space Location of Vertex
        FVector2D SourceUVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };// Source Channel UV Coordinate
    };

    struct FIslandRecord
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        TArray<int32> TriangleIndices;
        TMap<int32, FVector2D> RawUVByVertexInstance;
        FBox2D RawBounds;
        double RawArea = 0.0;
        double WorldArea = 0.0;
        double WorldDensityScale = 1.0;
    };


    struct FSourceEdgeEndpointKey
    {
        FIntVector Position;
        FIntPoint UV;

        bool operator==(const FSourceEdgeEndpointKey& Other) const
        {
            return Position == Other.Position && UV == Other.UV;
        }
    };

    FORCEINLINE uint32 GetTypeHash(const FSourceEdgeEndpointKey& Key)
    {
        return HashCombine(GetTypeHash(Key.Position), GetTypeHash(Key.UV));
    }

    struct FSourceEdgeKey
    {
        FSourceEdgeEndpointKey A;
        FSourceEdgeEndpointKey B;

        bool operator==(const FSourceEdgeKey& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    FORCEINLINE uint32 GetTypeHash(const FSourceEdgeKey& Key)
    {
        return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
    }

    static FIntVector QuantizeWrinkleSourcePosition(const FVector& Position)
    {
        constexpr double PositionScale = 1000.0;
        return FIntVector(
            FMath::RoundToInt(Position.X * PositionScale),
            FMath::RoundToInt(Position.Y * PositionScale),
            FMath::RoundToInt(Position.Z * PositionScale));
    }

    static FIntPoint QuantizeWrinkleSourceUV(const FVector2D& UV)
    {
        constexpr double UVScale = 100000.0;
        return FIntPoint(
            FMath::RoundToInt(UV.X * UVScale),
            FMath::RoundToInt(UV.Y * UVScale));
    }

    static bool ShouldSwapWrinkleSourceEdgeEndpoints(const FSourceEdgeEndpointKey& A, const FSourceEdgeEndpointKey& B)
    {
        if (A.Position.X != B.Position.X) { return A.Position.X > B.Position.X; }
        if (A.Position.Y != B.Position.Y) { return A.Position.Y > B.Position.Y; }
        if (A.Position.Z != B.Position.Z) { return A.Position.Z > B.Position.Z; }
        if (A.UV.X != B.UV.X) { return A.UV.X > B.UV.X; }
        return A.UV.Y > B.UV.Y;
    }

    static FSourceEdgeKey MakeWrinkleSourceEdgeKey(
        const FTriangleRecord& Triangle,
        int32 CornerA,
        int32 CornerB)
    {
        FSourceEdgeEndpointKey A;
        A.Position = QuantizeWrinkleSourcePosition(Triangle.Positions[CornerA]);
        A.UV = QuantizeWrinkleSourceUV(Triangle.SourceUVs[CornerA]);

        FSourceEdgeEndpointKey B;
        B.Position = QuantizeWrinkleSourcePosition(Triangle.Positions[CornerB]);
        B.UV = QuantizeWrinkleSourceUV(Triangle.SourceUVs[CornerB]);

        if (ShouldSwapWrinkleSourceEdgeEndpoints(A, B))
        {
            Swap(A, B);
        }

        FSourceEdgeKey Key;
        Key.A = A;
        Key.B = B;
        return Key;
    }

    static void SetFailure(FWetWrinkleUVChannelGenerationResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
    }

    static int32 GetEditableUVChannelCount(USkeletalMesh* SkeletalMesh, int32 LODIndex)
    {
        if (SkeletalMesh == nullptr)
        {
            return 0;
        }

        FMeshDescription* MeshDescription = SkeletalMesh->GetMeshDescription(LODIndex);
        if (MeshDescription == nullptr)
        {
            return 0;
        }

        FSkeletalMeshAttributes Attributes(*MeshDescription);
        Attributes.Register();
        return Attributes.GetVertexInstanceUVs().GetNumChannels();
    }

    static int32 DeriveOriginalUVChannelCount(UWetClothingAsset* Asset, USkeletalMesh* TargetMesh, int32 LODIndex)
    {
        if (Asset == nullptr)
        {
            return INDEX_NONE;
        }

#if WITH_EDITORONLY_DATA
        if (Asset->WrinkleData.OriginalUVChannelCount != INDEX_NONE)
        {
            return Asset->WrinkleData.OriginalUVChannelCount;
        }
#endif

        const int32 CurrentUVChannelCount = GetEditableUVChannelCount(TargetMesh, LODIndex);
        int32 FirstKnownGeneratedChannel = TNumericLimits<int32>::Max();

        for (const FWetWrinkleGeneratedUVSlot& GeneratedSlot : Asset->WrinkleData.GeneratedWrinkleUVSlots)
        {
            if (GeneratedSlot.UVChannelIndex >= 0)
            {
                FirstKnownGeneratedChannel = FMath::Min(FirstKnownGeneratedChannel, GeneratedSlot.UVChannelIndex);
            }
        }

#if WITH_EDITORONLY_DATA
        if (Asset->WrinkleData.bHasGeneratedWrinkleUV && Asset->WrinkleData.WrinkleUVChannelIndex >= 0)
        {
            FirstKnownGeneratedChannel = FMath::Min(FirstKnownGeneratedChannel, Asset->WrinkleData.WrinkleUVChannelIndex);
        }
#endif

        if (FirstKnownGeneratedChannel != TNumericLimits<int32>::Max())
        {
            return FMath::Clamp(FirstKnownGeneratedChannel, 0, CurrentUVChannelCount);
        }

        return CurrentUVChannelCount;
    }

    template <typename ElementIDType>
    static bool IsValidElementID(ElementIDType ElementID)
    {
        return ElementID.GetValue() != INDEX_NONE;
    }

    static int32 FindParent(TArray<int32>& Parents, int32 Index)
    {
        if (!Parents.IsValidIndex(Index))
        {
            return INDEX_NONE;
        }

        if (Parents[Index] == Index)
        {
            return Index;
        }

        Parents[Index] = FindParent(Parents, Parents[Index]);
        return Parents[Index];
    }

    static void UnionParents(TArray<int32>& Parents, int32 A, int32 B)
    {
        const int32 RootA = FindParent(Parents, A);
        const int32 RootB = FindParent(Parents, B);
        if (RootA != INDEX_NONE && RootB != INDEX_NONE && RootA != RootB)
        {
            Parents[RootB] = RootA;
        }
    }

    static int32 ResolveMaterialSlotIndex(
        const USkeletalMesh* SkeletalMesh,
        const FMeshDescription& MeshDescription,
        FSkeletalMeshAttributes& Attributes,
        FTriangleID TriangleID)
    {
        const FPolygonID PolygonID = MeshDescription.GetTrianglePolygon(TriangleID);
        if (!IsValidElementID(PolygonID))
        {
            return INDEX_NONE;
        }

        const FPolygonGroupID PolygonGroupID = MeshDescription.GetPolygonPolygonGroup(PolygonID);
        int32 FallbackIndex = PolygonGroupID.GetValue();

        if (SkeletalMesh == nullptr || !IsValidElementID(PolygonGroupID))
        {
            return FallbackIndex;
        }

        const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
        const auto MaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
        const FName PolygonGroupMaterialName = IsValidElementID(PolygonGroupID) ? MaterialSlotNames[PolygonGroupID] : NAME_None;

        if (!PolygonGroupMaterialName.IsNone())
        {
            for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
            {
                const FSkeletalMaterial& Material = Materials[MaterialIndex];
                if (Material.MaterialSlotName == PolygonGroupMaterialName || Material.ImportedMaterialSlotName == PolygonGroupMaterialName)
                {
                    return MaterialIndex;
                }
            }
        }

        return Materials.IsValidIndex(FallbackIndex) ? FallbackIndex : INDEX_NONE;
    }

    static double ComputeTriangleArea2D(const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        return FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * 0.5;
    }

    static double ComputeTriangleArea3D(const FVector& A, const FVector& B, const FVector& C)
    {
        return FVector::CrossProduct(B - A, C - A).Length() * 0.5;
    }

    static void BuildConnectedIslandsForSlot(
        const TArray<FTriangleRecord>& Triangles,
        const TArray<int32>& SlotTriangleIndices,
        TArray<FIslandRecord>& OutIslands)
    {
        if (SlotTriangleIndices.Num() == 0)
        {
            return;
        }

        TArray<int32> Parents;
        Parents.SetNum(SlotTriangleIndices.Num());
        for (int32 LocalIndex = 0; LocalIndex < SlotTriangleIndices.Num(); ++LocalIndex)
        {
            Parents[LocalIndex] = LocalIndex;
        }

        // Group by source UV island rather than by individual polygon or by mesh topology alone.
        // The edge key includes both endpoint positions and source UVs, so duplicated vertices at
        // import seams can still weld, while intentional UV seams remain separate islands.
        TMap<FSourceEdgeKey, int32> EdgeToLocalTriangleIndex;
        for (int32 LocalIndex = 0; LocalIndex < SlotTriangleIndices.Num(); ++LocalIndex)
        {
            const FTriangleRecord& Triangle = Triangles[SlotTriangleIndices[LocalIndex]];
            const int32 EdgeCorners[3][2] = { {0, 1}, {1, 2}, {2, 0} };
            for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
            {
                const FSourceEdgeKey EdgeKey = MakeWrinkleSourceEdgeKey(Triangle, EdgeCorners[EdgeIndex][0], EdgeCorners[EdgeIndex][1]);
                if (int32* ExistingLocalTriangleIndex = EdgeToLocalTriangleIndex.Find(EdgeKey))
                {
                    UnionParents(Parents, *ExistingLocalTriangleIndex, LocalIndex);
                }
                else
                {
                    EdgeToLocalTriangleIndex.Add(EdgeKey, LocalIndex);
                }
            }
        }

        TMap<int32, int32> RootToIslandIndex;
        for (int32 LocalIndex = 0; LocalIndex < SlotTriangleIndices.Num(); ++LocalIndex)
        {
            const int32 Root = FindParent(Parents, LocalIndex);
            int32* ExistingIslandIndex = RootToIslandIndex.Find(Root);
            if (ExistingIslandIndex == nullptr)
            {
                FIslandRecord Island;
                Island.MaterialSlotIndex = Triangles[SlotTriangleIndices[LocalIndex]].MaterialSlotIndex;
                Island.RawBounds = FBox2D(ForceInit);
                const int32 NewIslandIndex = OutIslands.Add(Island);
                RootToIslandIndex.Add(Root, NewIslandIndex);
                ExistingIslandIndex = RootToIslandIndex.Find(Root);
            }

            OutIslands[*ExistingIslandIndex].TriangleIndices.Add(SlotTriangleIndices[LocalIndex]);
        }
    }

    static void BuildRawIslandUVs(const TArray<FTriangleRecord>& Triangles, FIslandRecord& Island)
    {
        Island.RawBounds = FBox2D(ForceInit);
        Island.RawArea = 0.0;
        Island.WorldArea = 0.0;
        Island.WorldDensityScale = 1.0;
        Island.RawUVByVertexInstance.Reset();

        for (int32 TriangleIndex : Island.TriangleIndices)
        {
            const FTriangleRecord& Triangle = Triangles[TriangleIndex];

            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                const FVector2D RawUV = Triangle.SourceUVs[CornerIndex];
                Island.RawUVByVertexInstance.FindOrAdd(Triangle.VertexInstances[CornerIndex].GetValue()) = RawUV;
                Island.RawBounds += RawUV;
            }

            Island.RawArea += ComputeTriangleArea2D(Triangle.SourceUVs[0], Triangle.SourceUVs[1], Triangle.SourceUVs[2]);
            Island.WorldArea += ComputeTriangleArea3D(Triangle.Positions[0], Triangle.Positions[1], Triangle.Positions[2]);
        }

        if (Island.RawArea > UE_DOUBLE_SMALL_NUMBER && Island.WorldArea > UE_DOUBLE_SMALL_NUMBER)
        {
            // Uniformly scale the source parameterization so its UV area equals
            // the island's local-space surface area. A later common scale then
            // maps one Unreal Unit to the same number of pixels for every island.
            Island.WorldDensityScale = FMath::Sqrt(Island.WorldArea / Island.RawArea);
        }

        if (!Island.RawBounds.bIsValid || Island.RawBounds.GetSize().IsNearlyZero())
        {
            Island.RawBounds = FBox2D(FVector2D(-0.5, -0.5), FVector2D(0.5, 0.5));
        }
    }

    static FVector2D GetDensityNormalizedIslandSize(const FIslandRecord& Island, bool bUseWorldDensity)
    {
        const double DensityScale = bUseWorldDensity ? Island.WorldDensityScale : 1.0;
        FVector2D Size = Island.RawBounds.GetSize() * DensityScale;
        Size.X = FMath::Max(Size.X, 1.0e-4);
        Size.Y = FMath::Max(Size.Y, 1.0e-4);
        return Size;
    }

    static void SortIslandsForPacking(TArray<FIslandRecord>& Islands, bool bUseWorldDensity)
    {
        Islands.Sort(
            [bUseWorldDensity](const FIslandRecord& A, const FIslandRecord& B)
            {
                const FVector2D ASize = GetDensityNormalizedIslandSize(A, bUseWorldDensity);
                const FVector2D BSize = GetDensityNormalizedIslandSize(B, bUseWorldDensity);
                if (!FMath::IsNearlyEqual(ASize.Y, BSize.Y)) return ASize.Y > BSize.Y;
                if (!FMath::IsNearlyEqual(ASize.X, BSize.X)) return ASize.X > BSize.X;
                return A.WorldArea > B.WorldArea;
            });
    }

    static bool TryShelfPack(
        const TArray<FIslandRecord>& Islands,
        double PaddingUV,
        double Scale,
        bool bUseWorldDensity,
        TArray<FVector2D>* OutPackedMins)
    {
        TArray<FVector2D> LocalPackedMins;
        LocalPackedMins.SetNum(Islands.Num());
        double CursorX = PaddingUV;
        double CursorY = PaddingUV;
        double RowHeight = 0.0;

        for (int32 IslandIndex = 0; IslandIndex < Islands.Num(); ++IslandIndex)
        {
            const FVector2D PackedSize = GetDensityNormalizedIslandSize(Islands[IslandIndex], bUseWorldDensity) * Scale;
            if (PackedSize.X > 1.0 - PaddingUV * 2.0 || PackedSize.Y > 1.0 - PaddingUV * 2.0)
            {
                return false;
            }
            if (CursorX + PackedSize.X + PaddingUV > 1.0)
            {
                CursorX = PaddingUV;
                CursorY += RowHeight + PaddingUV;
                RowHeight = 0.0;
            }
            if (CursorY + PackedSize.Y + PaddingUV > 1.0)
            {
                return false;
            }

            LocalPackedMins[IslandIndex] = FVector2D(CursorX, CursorY);
            CursorX += PackedSize.X + PaddingUV;
            RowHeight = FMath::Max(RowHeight, PackedSize.Y);
        }

        if (OutPackedMins) *OutPackedMins = MoveTemp(LocalPackedMins);
        return true;
    }

    static double FindMaximumPackingScale(
        const TArray<FIslandRecord>& Islands,
        double PaddingUV,
        bool bUseWorldDensity)
    {
        double MaxDimension = 1.0e-4;
        for (const FIslandRecord& Island : Islands)
        {
            const FVector2D Size = GetDensityNormalizedIslandSize(Island, bUseWorldDensity);
            MaxDimension = FMath::Max(MaxDimension, FMath::Max(Size.X, Size.Y));
        }

        double LowScale = 0.0;
        double HighScale = (1.0 - PaddingUV * 2.0) / MaxDimension;
        for (int32 Iteration = 0; Iteration < 40; ++Iteration)
        {
            const double CandidateScale = (LowScale + HighScale) * 0.5;
            if (TryShelfPack(Islands, PaddingUV, CandidateScale, bUseWorldDensity, nullptr)) LowScale = CandidateScale;
            else HighScale = CandidateScale;
        }
        return LowScale;
    }

    static bool PackIslandsIntoUnitSquare(
        const TArray<FTriangleRecord>& Triangles,
        TArray<FIslandRecord>& Islands,
        int32 Resolution,
        int32 PaddingPixels,
        TMap<int32, FVector2f>& OutPackedUVByVertexInstance,
        double TargetTexelsPerWorldUnit = 0.0)
    {
        OutPackedUVByVertexInstance.Reset();

        if (Islands.Num() == 0)
        {
            return false;
        }

        for (FIslandRecord& Island : Islands)
        {
            BuildRawIslandUVs(Triangles, Island);
        }

        const float RequestedPaddingUV = Resolution > 0 ? static_cast<float>(PaddingPixels) / static_cast<float>(Resolution) : 0.0f;
        const double PaddingUV = FMath::Clamp(static_cast<double>(RequestedPaddingUV), 0.0, 0.05);

        const bool bUseWorldDensity = TargetTexelsPerWorldUnit > 0.0;
        // Wrinkle UVs keep their established source-density behavior. Surface
        // Water supplies a shared world-density scale across material slots.
        SortIslandsForPacking(Islands, bUseWorldDensity);
        const double PackingScale = bUseWorldDensity
            ? TargetTexelsPerWorldUnit / FMath::Max(Resolution, 1)
            : FindMaximumPackingScale(Islands, PaddingUV, false);

        TArray<FVector2D> PackedMins;
        if (!TryShelfPack(Islands, PaddingUV, PackingScale, bUseWorldDensity, &PackedMins))
        {
            return false;
        }

        for (int32 IslandIndex = 0; IslandIndex < Islands.Num(); ++IslandIndex)
        {
            FIslandRecord& Island = Islands[IslandIndex];
            const double DensityScale = bUseWorldDensity ? Island.WorldDensityScale : 1.0;

            for (const TPair<int32, FVector2D>& Pair : Island.RawUVByVertexInstance)
            {
                FVector2D PackedUV = PackedMins[IslandIndex]
                    + (Pair.Value - Island.RawBounds.Min) * DensityScale * PackingScale;
                PackedUV.X = FMath::Clamp(PackedUV.X, 0.0, 1.0);
                PackedUV.Y = FMath::Clamp(PackedUV.Y, 0.0, 1.0);
                OutPackedUVByVertexInstance.Add(Pair.Key, FVector2f(static_cast<float>(PackedUV.X), static_cast<float>(PackedUV.Y)));
            }
        }
        return true;
    }
} // namespace WetWrinkleUVChannelGeneratorInternal

FWetWrinkleUVChannelGenerationResult FWetWrinkleUVChannelGenerator::GenerateForAsset(
    UWetClothingAsset* Asset,
    const FWetWrinkleUVChannelGenerationSettings& Settings)
{
    FWetWrinkleUVChannelGenerationResult Result;

    if (Asset == nullptr)
    {
        WetWrinkleUVChannelGeneratorInternal::SetFailure(Result, TEXT("No Wet Clothing Asset is assigned."));
        return Result;
    }

    USkeletalMesh* TargetMesh = Asset->GetDWCSkeletalMesh();
    if (TargetMesh == nullptr)
    {
        WetWrinkleUVChannelGeneratorInternal::SetFailure(Result, TEXT("No Target Mesh is assigned."));
        return Result;
    }

#if WITH_EDITORONLY_DATA
    if (Asset->WrinkleData.OriginalUVChannelCount == INDEX_NONE)
    {
        Asset->Modify();
        Asset->WrinkleData.OriginalUVChannelCount = WetWrinkleUVChannelGeneratorInternal::DeriveOriginalUVChannelCount(Asset, TargetMesh, Settings.LODIndex);
    }
#endif

    bool bCanOverwriteRecordedGeneratedChannel =
        Settings.bAllowOverwriteExistingGeneratedChannel &&
        Asset->WrinkleData.WrinkleUVChannelIndex != INDEX_NONE;
#if WITH_EDITORONLY_DATA
    bCanOverwriteRecordedGeneratedChannel = bCanOverwriteRecordedGeneratedChannel && Asset->WrinkleData.bHasGeneratedWrinkleUV;
#endif

    const int32 PreferredUVChannelIndex = bCanOverwriteRecordedGeneratedChannel
                                           ? Asset->WrinkleData.WrinkleUVChannelIndex
                                           : Settings.PreferredUVChannelIndex;

    Result = GenerateForSkeletalMesh(
        TargetMesh,
        Settings.LODIndex,
        Settings.Resolution,
        Settings.PaddingPixels,
        Settings.SourceUVChannelIndex,
        PreferredUVChannelIndex,
        bCanOverwriteRecordedGeneratedChannel,
        Settings.TargetMaterialSlotIndex);
    if (Result.bSucceeded)
    {
        Asset->Modify();
        Asset->WrinkleData.WrinkleUVChannelIndex = Result.UVChannelIndex;
#if WITH_EDITORONLY_DATA
        Asset->WrinkleData.bHasGeneratedWrinkleUV = true;
        Asset->WrinkleData.GeneratedWrinkleUVBuildGuid = FGuid::NewGuid();
#endif

        if (Settings.TargetMaterialSlotIndex != INDEX_NONE)
        {
            FWetWrinkleGeneratedUVSlot* ExistingSlot = Asset->WrinkleData.GeneratedWrinkleUVSlots.FindByPredicate(
                [&Settings](const FWetWrinkleGeneratedUVSlot& Candidate)
                {
                    return Candidate.MaterialSlotIndex == Settings.TargetMaterialSlotIndex;
                });
            if (ExistingSlot == nullptr)
            {
                ExistingSlot = &Asset->WrinkleData.GeneratedWrinkleUVSlots.AddDefaulted_GetRef();
            }

            ExistingSlot->MaterialSlotIndex = Settings.TargetMaterialSlotIndex;
            ExistingSlot->UVChannelIndex = Result.UVChannelIndex;
            ExistingSlot->SourceUVChannelIndex = Settings.SourceUVChannelIndex;
            ExistingSlot->LODIndex = Settings.LODIndex;
#if WITH_EDITORONLY_DATA
            ExistingSlot->GeneratedUVBuildGuid = FGuid::NewGuid();
#endif
        }

        Asset->MarkPackageDirty();
    }

    return Result;
}

FWetWrinkleUVChannelGenerationResult FWetWrinkleUVChannelGenerator::DeleteUVChannelForAsset(
    UWetClothingAsset* Asset,
    int32 LODIndex,
    int32 UVChannelIndex)
{
    using namespace WetWrinkleUVChannelGeneratorInternal;

    FWetWrinkleUVChannelGenerationResult Result;
    Result.UVChannelIndex = UVChannelIndex;

    if (Asset == nullptr)
    {
        SetFailure(Result, TEXT("No Wet Clothing Asset is assigned."));
        return Result;
    }

    USkeletalMesh* TargetMesh = Asset->GetDWCSkeletalMesh();
    if (TargetMesh == nullptr)
    {
        SetFailure(Result, TEXT("No Target Mesh is assigned."));
        return Result;
    }

    FMeshDescription* MeshDescription = TargetMesh->GetMeshDescription(LODIndex);
    if (MeshDescription == nullptr)
    {
        SetFailure(Result, FString::Printf(TEXT("The target skeletal mesh does not expose editable mesh description data for LOD %d."), LODIndex));
        return Result;
    }

    FSkeletalMeshAttributes Attributes(*MeshDescription);
    Attributes.Register();
    auto VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

    const int32 NumUVChannels = VertexInstanceUVs.GetNumChannels();
    if (UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
    {
        SetFailure(Result, FString::Printf(TEXT("UV Channel %d does not exist."), UVChannelIndex));
        return Result;
    }

    int32 OriginalUVChannelCount = DeriveOriginalUVChannelCount(Asset, TargetMesh, LODIndex);
#if WITH_EDITORONLY_DATA
    if (Asset->WrinkleData.OriginalUVChannelCount == INDEX_NONE)
    {
        Asset->Modify();
        Asset->WrinkleData.OriginalUVChannelCount = OriginalUVChannelCount;
    }
#endif

    if (UVChannelIndex < OriginalUVChannelCount)
    {
        SetFailure(Result, FString::Printf(
            TEXT("UV Channel %d is protected because it belongs to the original mesh data. Only DWC-added/generated UV channels can be deleted here."),
            UVChannelIndex));
        return Result;
    }

    TargetMesh->Modify();
    Asset->Modify();

    for (const FVertexInstanceID VertexInstanceID : MeshDescription->VertexInstances().GetElementIDs())
    {
        for (int32 ChannelIndex = UVChannelIndex; ChannelIndex < NumUVChannels - 1; ++ChannelIndex)
        {
            const FVector2f NextUV = VertexInstanceUVs.Get(VertexInstanceID, ChannelIndex + 1);
            VertexInstanceUVs.Set(VertexInstanceID, ChannelIndex, NextUV);
        }
    }

    VertexInstanceUVs.SetNumChannels(NumUVChannels - 1);

    auto ShiftOrInvalidateChannel = [UVChannelIndex](int32& ChannelIndex)
    {
        if (ChannelIndex == UVChannelIndex)
        {
            ChannelIndex = INDEX_NONE;
        }
        else if (ChannelIndex > UVChannelIndex)
        {
            --ChannelIndex;
        }
    };

    for (int32 SlotIndex = Asset->WrinkleData.GeneratedWrinkleUVSlots.Num() - 1; SlotIndex >= 0; --SlotIndex)
    {
        FWetWrinkleGeneratedUVSlot& GeneratedSlot = Asset->WrinkleData.GeneratedWrinkleUVSlots[SlotIndex];
        if (GeneratedSlot.UVChannelIndex == UVChannelIndex)
        {
            Asset->WrinkleData.GeneratedWrinkleUVSlots.RemoveAt(SlotIndex);
        }
        else if (GeneratedSlot.UVChannelIndex > UVChannelIndex)
        {
            --GeneratedSlot.UVChannelIndex;
        }
    }

    int32 RemovedPatchCount = 0;
    for (FWetWrinklePatchStroke& Stroke : Asset->WrinkleData.EditablePatchStrokes)
    {
        for (int32 PatchIndex = Stroke.PatchPlacements.Num() - 1; PatchIndex >= 0; --PatchIndex)
        {
            FWetWrinklePatchPlacement& Patch = Stroke.PatchPlacements[PatchIndex];
            if (Patch.UVChannelIndex == UVChannelIndex)
            {
                Stroke.PatchPlacements.RemoveAt(PatchIndex);
                ++RemovedPatchCount;
            }
            else if (Patch.UVChannelIndex > UVChannelIndex)
            {
                --Patch.UVChannelIndex;
            }
        }
    }

    for (int32 BakedIndex = Asset->WrinkleData.BakedWrinkleMaps.Num() - 1; BakedIndex >= 0; --BakedIndex)
    {
        FWetWrinkleBakedMapSet& BakedMap = Asset->WrinkleData.BakedWrinkleMaps[BakedIndex];
        if (BakedMap.UVChannelIndex == UVChannelIndex)
        {
            Asset->WrinkleData.BakedWrinkleMaps.RemoveAt(BakedIndex);
        }
        else if (BakedMap.UVChannelIndex > UVChannelIndex)
        {
            --BakedMap.UVChannelIndex;
        }
    }

    ShiftOrInvalidateChannel(Asset->WrinkleData.WrinkleUVChannelIndex);

#if WITH_EDITORONLY_DATA
    Asset->WrinkleData.bHasGeneratedWrinkleUV = Asset->WrinkleData.GeneratedWrinkleUVSlots.Num() > 0;
    Asset->WrinkleData.GeneratedWrinkleUVBuildGuid = FGuid::NewGuid();
#endif

    TargetMesh->CommitMeshDescription(LODIndex);
    TargetMesh->PostEditChange();
    TargetMesh->MarkPackageDirty();
    Asset->MarkPackageDirty();

    Result.bSucceeded = true;
    Result.MaterialSlotIndex = INDEX_NONE;
    Result.Message = FString::Printf(
        TEXT("Deleted UV Channel %d. Removed %d wrinkle patch(es) that used the deleted channel and shifted higher channel indices down."),
        UVChannelIndex,
        RemovedPatchCount);
    return Result;
}

bool FWetWrinkleUVChannelGenerator::CalculateSharedWorldTexelDensity(
    USkeletalMesh* SkeletalMesh,
    int32 LODIndex,
    int32 Resolution,
    int32 PaddingPixels,
    int32 SourceUVChannelIndex,
    const TArray<int32>& TargetMaterialSlotIndices,
    double& OutTexelsPerWorldUnit,
    FString& OutError)
{
    using namespace WetWrinkleUVChannelGeneratorInternal;

    OutTexelsPerWorldUnit = 0.0;
    OutError.Reset();
    if (SkeletalMesh == nullptr || TargetMaterialSlotIndices.IsEmpty())
    {
        OutError = TEXT("A skeletal mesh and at least one material slot are required.");
        return false;
    }

    FMeshDescription* MeshDescription = SkeletalMesh->GetMeshDescription(LODIndex);
    if (MeshDescription == nullptr)
    {
        OutError = FString::Printf(TEXT("LOD %d has no editable mesh description."), LODIndex);
        return false;
    }

    FSkeletalMeshAttributes Attributes(*MeshDescription);
    Attributes.Register();
    const auto VertexPositions = Attributes.GetVertexPositions();
    const auto VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
    if (SourceUVChannelIndex < 0 || SourceUVChannelIndex >= VertexInstanceUVs.GetNumChannels())
    {
        OutError = FString::Printf(TEXT("Source UV Channel %d does not exist."), SourceUVChannelIndex);
        return false;
    }

    TSet<int32> TargetSlots;
    TargetSlots.Reserve(TargetMaterialSlotIndices.Num());
    for (const int32 MaterialSlotIndex : TargetMaterialSlotIndices)
    {
        TargetSlots.Add(MaterialSlotIndex);
    }
    TArray<FTriangleRecord> Triangles;
    TMap<int32, TArray<int32>> SlotToTriangleIndices;
    for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
    {
        const int32 MaterialSlotIndex = ResolveMaterialSlotIndex(SkeletalMesh, *MeshDescription, Attributes, TriangleID);
        if (!TargetSlots.Contains(MaterialSlotIndex))
        {
            continue;
        }

        const auto VertexInstances = MeshDescription->GetTriangleVertexInstances(TriangleID);
        if (VertexInstances.Num() < 3)
        {
            continue;
        }

        FTriangleRecord Triangle;
        Triangle.TriangleID = TriangleID;
        Triangle.MaterialSlotIndex = MaterialSlotIndex;
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Triangle.VertexInstances[CornerIndex] = VertexInstances[CornerIndex];
            Triangle.Vertices[CornerIndex] = MeshDescription->GetVertexInstanceVertex(VertexInstances[CornerIndex]);
            Triangle.Positions[CornerIndex] = FVector(VertexPositions[Triangle.Vertices[CornerIndex]]);
            const FVector2f SourceUV = VertexInstanceUVs.Get(VertexInstances[CornerIndex], SourceUVChannelIndex);
            Triangle.SourceUVs[CornerIndex] = FVector2D(SourceUV.X, SourceUV.Y);
        }

        const int32 TriangleIndex = Triangles.Add(Triangle);
        SlotToTriangleIndices.FindOrAdd(MaterialSlotIndex).Add(TriangleIndex);
    }

    const double PaddingUV = FMath::Clamp(
        Resolution > 0 ? static_cast<double>(PaddingPixels) / static_cast<double>(Resolution) : 0.0,
        0.0,
        0.05);
    double SharedMaximumPackingScale = TNumericLimits<double>::Max();
    for (const int32 MaterialSlotIndex : TargetMaterialSlotIndices)
    {
        const TArray<int32>* SlotTriangles = SlotToTriangleIndices.Find(MaterialSlotIndex);
        if (SlotTriangles == nullptr || SlotTriangles->IsEmpty())
        {
            OutError = FString::Printf(TEXT("Material Slot %d has no triangles to pack."), MaterialSlotIndex);
            return false;
        }

        TArray<FIslandRecord> SlotIslands;
        BuildConnectedIslandsForSlot(Triangles, *SlotTriangles, SlotIslands);
        for (FIslandRecord& Island : SlotIslands)
        {
            BuildRawIslandUVs(Triangles, Island);
        }
        SortIslandsForPacking(SlotIslands, true);
        SharedMaximumPackingScale = FMath::Min(
            SharedMaximumPackingScale,
            FindMaximumPackingScale(SlotIslands, PaddingUV, true));
    }

    if (!FMath::IsFinite(SharedMaximumPackingScale) || SharedMaximumPackingScale <= UE_DOUBLE_SMALL_NUMBER)
    {
        OutError = TEXT("No positive shared world-space texel density fits the requested material slots.");
        return false;
    }

    // Unreal mesh positions are centimeters by convention, so the world-unit
    // density reported here is texels per centimeter.
    OutTexelsPerWorldUnit = SharedMaximumPackingScale * FMath::Max(Resolution, 1);
    return true;
}

//For one Target Slot
FWetWrinkleUVChannelGenerationResult FWetWrinkleUVChannelGenerator::GenerateForSkeletalMesh(
    USkeletalMesh* SkeletalMesh,
    int32 LODIndex,
    int32 Resolution,
    int32 PaddingPixels,
    int32 SourceUVChannelIndex,
    int32 PreferredUVChannelIndex,
    bool bAllowOverwriteExistingChannel,
    int32 TargetMaterialSlotIndex,
    double TargetTexelsPerWorldUnit)
{
    using namespace WetWrinkleUVChannelGeneratorInternal;

    FWetWrinkleUVChannelGenerationResult Result;

    //Error : If Skeletal Mesh is nullptr
    if (SkeletalMesh == nullptr)
    {
        SetFailure(Result, TEXT("No skeletal mesh is assigned."));
        return Result;
    }

    //Error : If There is no Appropriate LOD INdex
    FMeshDescription* MeshDescription = SkeletalMesh->GetMeshDescription(LODIndex);
    if (MeshDescription == nullptr)
    {
        SetFailure(Result, FString::Printf(TEXT("The target skeletal mesh does not expose editable mesh description data for LOD %d."), LODIndex));
        return Result;
    }

    //Tell Unreal That I`m gonna Change Skeletal Mesh Information
    SkeletalMesh->Modify();

    FSkeletalMeshAttributes Attributes(*MeshDescription);
    Attributes.Register();

    //1. Setup New UV Coordinate get in
    auto VertexPositions = Attributes.GetVertexPositions();
    auto VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
    /*
     * 
    [ Vertex Instance UVs ]
                   0번 채널      1번 채널      2번 채널 (Wet/Wrinkle)
                 ┌────────────┬────────────┬────────────┐
VertexInstance_0 │ (0.0, 0.0) │ (1.0, 0.5) │ (0.1, 0.2) │
VertexInstance_1 │ (0.5, 0.0) │ (0.0, 0.0) │ (0.4, 0.8) │
VertexInstance_2 │ (1.0, 1.0) │ (0.5, 0.5) │ (0.9, 0.1) │
VertexInstance_3 │ ...        │ ...        │ ...        │
                 └────────────┴────────────┴────────────┘
                 
                 
                 * SetNumChannels(Current+1)은 가로로 채널 하나를 더 늘림
    * */

    const int32 ExistingUVChannelCount = VertexInstanceUVs.GetNumChannels(); // How many UVChannels Is this Skeletal Mesh Using?
    const int32 SafeSourceUVChannelIndex = FMath::Clamp(SourceUVChannelIndex, 0, MAX_TEXCOORDS - 1); //For safty Clamp Between Unreal MAX_UVChannel Count
    if (SafeSourceUVChannelIndex >= ExistingUVChannelCount)
    {
        SetFailure(Result, FString::Printf(
            TEXT("Source UV Channel %d does not exist. A wrinkle UV channel needs an existing material UV channel to preserve material-slot UV islands."),
            SafeSourceUVChannelIndex));
        return Result;
    }

    const int32 SafePreferredUVChannelIndex = FMath::Clamp(PreferredUVChannelIndex, 0, MAX_TEXCOORDS - 1);

    int32 NewUVChannelIndex = INDEX_NONE;
    bool bOverwritingExistingChannel = false;
    bool bAppendedBecausePreferredChannelWasOccupied = false;

    if (SafePreferredUVChannelIndex >= ExistingUVChannelCount)
    {
        NewUVChannelIndex = SafePreferredUVChannelIndex;
        VertexInstanceUVs.SetNumChannels(NewUVChannelIndex + 1);
    }
    else if (bAllowOverwriteExistingChannel)
    {
        NewUVChannelIndex = SafePreferredUVChannelIndex;
        bOverwritingExistingChannel = true;
    }
    else
    {
        if (ExistingUVChannelCount >= MAX_TEXCOORDS)
        {
            SetFailure(Result, FString::Printf(
                TEXT("UV Channel %d already exists and is not marked as generated by DWC. The target mesh also already has the maximum %d UV channels, so a new safe wrinkle UV channel cannot be appended."),
                SafePreferredUVChannelIndex,
                MAX_TEXCOORDS));
            return Result;
        }

        NewUVChannelIndex = ExistingUVChannelCount;
        bAppendedBecausePreferredChannelWasOccupied = true;
        VertexInstanceUVs.SetNumChannels(ExistingUVChannelCount + 1);
    }

    //2. Collect Geometry and Source UV Datas
    TArray<FTriangleRecord> Triangles; // Polygon Triangles
    TMap<int32, TArray<int32>> SlotToTriangleIndices; //Slot -> Trignale

    for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
    {
        //Find Triangle's Associated Material Slot
        //TODO : Maybe this Can be Improved by O(n)
        const int32 MaterialSlotIndex = ResolveMaterialSlotIndex(SkeletalMesh, *MeshDescription, Attributes, TriangleID);
        if (MaterialSlotIndex == INDEX_NONE)
        {
            continue;
        }

        if (TargetMaterialSlotIndex != INDEX_NONE && MaterialSlotIndex != TargetMaterialSlotIndex)
        {
            continue;
        }

        const auto VertexInstances = MeshDescription->GetTriangleVertexInstances(TriangleID);
        if (VertexInstances.Num() < 3)
        {
            continue;
        }

        FTriangleRecord Triangle;
        Triangle.TriangleID = TriangleID;
        Triangle.MaterialSlotIndex = MaterialSlotIndex;

        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Triangle.VertexInstances[CornerIndex] = VertexInstances[CornerIndex];
            Triangle.Vertices[CornerIndex] = MeshDescription->GetVertexInstanceVertex(VertexInstances[CornerIndex]);
            Triangle.Positions[CornerIndex] = FVector(VertexPositions[Triangle.Vertices[CornerIndex]]);
            const FVector2f SourceUV = VertexInstanceUVs.Get(VertexInstances[CornerIndex], SafeSourceUVChannelIndex);
            Triangle.SourceUVs[CornerIndex] = FVector2D(SourceUV.X, SourceUV.Y);
        }

        const int32 TriangleArrayIndex = Triangles.Add(Triangle);
        SlotToTriangleIndices.FindOrAdd(MaterialSlotIndex).Add(TriangleArrayIndex);
    }

    if (Triangles.Num() == 0)
    {
        if (TargetMaterialSlotIndex != INDEX_NONE)
        {
            SetFailure(Result, FString::Printf(TEXT("Material Slot %d does not contain triangles that can be unwrapped."), TargetMaterialSlotIndex));
        }
        else
        {
            SetFailure(Result, TEXT("The target mesh does not contain triangles that can be unwrapped."));
        }
        return Result;
    }

    
    //3. Make Island. (Triangles for Mesh is all collected.) 
    TArray<FIslandRecord> ResultIslands;
    TArray<int32> SortedSlotIndices;
    SlotToTriangleIndices.GenerateKeyArray(SortedSlotIndices);
    SortedSlotIndices.Sort();

    for (int32 MaterialSlotIndex : SortedSlotIndices)
    {
        const TArray<int32>* SlotTriangleIndices = SlotToTriangleIndices.Find(MaterialSlotIndex);
        if (SlotTriangleIndices != nullptr)
        {
            BuildConnectedIslandsForSlot(Triangles, *SlotTriangleIndices, ResultIslands);
        }
    }

    if (ResultIslands.Num() == 0)
    {
        SetFailure(Result, TEXT("No connected surface islands could be generated."));
        return Result;
    }

    TMap<int32, FVector2f> PackedUVByVertexInstance;
    if (!PackIslandsIntoUnitSquare(
            Triangles,
            ResultIslands,
            Resolution,
            PaddingPixels,
            PackedUVByVertexInstance,
            TargetTexelsPerWorldUnit))
    {
        SetFailure(Result, FString::Printf(
            TEXT("The requested %.3f texels per world unit could not be packed for Material Slot %d."),
            TargetTexelsPerWorldUnit,
            TargetMaterialSlotIndex));
        return Result;
    }

    for (const TPair<int32, FVector2f>& Pair : PackedUVByVertexInstance)
    {
        const FVertexInstanceID VertexInstanceID(Pair.Key);
        if (IsValidElementID(VertexInstanceID))
        {
            VertexInstanceUVs.Set(VertexInstanceID, NewUVChannelIndex, Pair.Value);
        }
    }

    SkeletalMesh->CommitMeshDescription(LODIndex);
    SkeletalMesh->PostEditChange();
    SkeletalMesh->MarkPackageDirty();

    Result.bSucceeded = true;
    Result.UVChannelIndex = NewUVChannelIndex;
    Result.MaterialSlotIndex = TargetMaterialSlotIndex;
    Result.PackedIslandCount = ResultIslands.Num();

    const FString TargetLabel = TargetMaterialSlotIndex != INDEX_NONE
                                    ? FString::Printf(TEXT("Material Slot %d"), TargetMaterialSlotIndex)
                                    : FString(TEXT("all material slots"));
    if (bOverwritingExistingChannel)
    {
        Result.Message = FString::Printf(
            TEXT("Regenerated %s in DWC-owned wrinkle UV channel %d with %d packed source UV island(s)."),
            *TargetLabel,
            NewUVChannelIndex,
            ResultIslands.Num());
    }
    else if (bAppendedBecausePreferredChannelWasOccupied)
    {
        Result.Message = FString::Printf(
            TEXT("Preferred UV Channel %d already existed and was not marked as DWC-generated, so created safe wrinkle UV channel %d and generated %s with %d packed source UV island(s)."),
            SafePreferredUVChannelIndex,
            NewUVChannelIndex,
            *TargetLabel,
            ResultIslands.Num());
    }
    else
    {
        Result.Message = FString::Printf(
            TEXT("Created wrinkle UV channel %d and generated %s with %d packed source UV island(s)."),
            NewUVChannelIndex,
            *TargetLabel,
            ResultIslands.Num());
    }
    return Result;
}
