#include "DWCDataUVGenerator.h"

#include "DWCDataUVChartBuilder.h"
#include "DWCDataUVGenerationTypes.h"
#include "DWCDataUVPacker.h"
#include "DWCDataUVValidator.h"

#include "Engine/SkeletalMesh.h"
#include "MeshDescription.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshAttributes.h"
#include "WetClothing/Foundation/UV/DWCUVGeometry.h"

namespace DWCDataUVGeneratorInternal
{
    static constexpr int32 InternalPackingResolution = 4096;
    static constexpr int32 InternalPaddingPixels = 32; // Same normalized padding as the previous 8 / 1024 default.

    static void SetFailure(FDWCDataUVGenerationResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
    }


    template <typename ElementIDType>
    static bool IsValidElementID(ElementIDType ElementID)
    {
        return ElementID.GetValue() != INDEX_NONE;
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


} // namespace DWCDataUVGeneratorInternal

FDWCDataUVGenerationResult FDWCDataUVGenerator::GenerateForSkeletalMesh(
    USkeletalMesh* SkeletalMesh,
    int32 LODIndex,
    int32 SourceUVChannelIndex,
    int32 PreferredUVChannelIndex,
    bool bAllowOverwriteExistingChannel,
    int32 TargetMaterialSlotIndex)
{
    using namespace DWCDataUVGeneratorInternal;

    FDWCDataUVGenerationResult Result;

    if (SkeletalMesh == nullptr)
    {
        SetFailure(Result, TEXT("No skeletal mesh is assigned."));
        return Result;
    }

    FMeshDescription* MeshDescription = SkeletalMesh->GetMeshDescription(LODIndex);
    if (MeshDescription == nullptr)
    {
        SetFailure(Result, FString::Printf(TEXT("The target skeletal mesh does not expose editable mesh description data for LOD %d."), LODIndex));
        return Result;
    }

    SkeletalMesh->Modify();

    FSkeletalMeshAttributes Attributes(*MeshDescription);
    Attributes.Register();

    auto VertexPositions = Attributes.GetVertexPositions();
    auto VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

    const int32 ExistingUVChannelCount = VertexInstanceUVs.GetNumChannels();
    const int32 SafeSourceUVChannelIndex = FMath::Clamp(SourceUVChannelIndex, 0, 7);
    if (SafeSourceUVChannelIndex >= ExistingUVChannelCount)
    {
        SetFailure(Result, FString::Printf(
            TEXT("Source UV Channel %d does not exist. A DWC Data UV channel needs an existing material UV channel to preserve material-slot UV islands."),
            SafeSourceUVChannelIndex));
        return Result;
    }

    const int32 SafePreferredUVChannelIndex = FMath::Clamp(PreferredUVChannelIndex, 0, 7);

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
        if (ExistingUVChannelCount >= 8)
        {
            SetFailure(Result, FString::Printf(
                TEXT("UV Channel %d already exists and is not marked as generated by DWC. The target mesh also already has 8 UV channels, so a new safe DWC Data UV channel cannot be appended."),
                SafePreferredUVChannelIndex));
            return Result;
        }

        NewUVChannelIndex = ExistingUVChannelCount;
        bAppendedBecausePreferredChannelWasOccupied = true;
        VertexInstanceUVs.SetNumChannels(ExistingUVChannelCount + 1);
    }

    TArray<FDWCDataUVTriangle> Triangles;
    TMap<int32, TArray<int32>> SlotToTriangleIndices;
    TSet<int32> ExcludedVertexInstanceIDs;

    for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
    {
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

        FDWCDataUVTriangle Triangle;
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

        const bool bSourceUVIsFinite =
            FDWCUVGeometry::IsFiniteReasonableUV(Triangle.SourceUVs[0]) &&
            FDWCUVGeometry::IsFiniteReasonableUV(Triangle.SourceUVs[1]) &&
            FDWCUVGeometry::IsFiniteReasonableUV(Triangle.SourceUVs[2]);
        if (!bSourceUVIsFinite)
        {
            ++Result.InvalidSourceUVTriangleCount;
            for (const FVertexInstanceID VertexInstanceID : Triangle.VertexInstances)
            {
                ExcludedVertexInstanceIDs.Add(VertexInstanceID.GetValue());
            }
            continue;
        }

        // Degenerate geometry and UV triangles are filtered before connectivity/overlap analysis.
        // Point/line triangles would otherwise create false conflicts and cannot be rasterized.
        if (FDWCUVGeometry::ComputeTriangleDoubleArea3D(Triangle.Positions[0], Triangle.Positions[1], Triangle.Positions[2]) <= 1.0e-10)
        {
            ++Result.Degenerate3DTriangleCount;
            for (const FVertexInstanceID VertexInstanceID : Triangle.VertexInstances)
            {
                ExcludedVertexInstanceIDs.Add(VertexInstanceID.GetValue());
            }
            continue;
        }

        if (FDWCUVGeometry::ComputeTriangleArea2D(Triangle.SourceUVs[0], Triangle.SourceUVs[1], Triangle.SourceUVs[2]) <= 1.0e-12)
        {
            ++Result.DegenerateSourceUVTriangleCount;
            for (const FVertexInstanceID VertexInstanceID : Triangle.VertexInstances)
            {
                ExcludedVertexInstanceIDs.Add(VertexInstanceID.GetValue());
            }
            continue;
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

    TArray<FDWCDataUVChart> OriginalUVIslands;
    FDWCDataUVChartBuilder::BuildOriginalUVIslands(
        Triangles,
        SlotToTriangleIndices,
        OriginalUVIslands);

    if (OriginalUVIslands.Num() == 0)
    {
        SetFailure(Result, TEXT("No valid Original-UV islands could be generated after degenerate triangles were excluded."));
        return Result;
    }

    Result.OriginalUVIslandCount = OriginalUVIslands.Num();

    TArray<FDWCDataUVChart> DataUVCharts;
    FDWCDataUVChartBuilder::BuildNonOverlappingCharts(
        Triangles,
        OriginalUVIslands,
        DataUVCharts,
        Result.SplitOriginalUVIslandCount,
        Result.SelfOverlapPairCount);

    if (DataUVCharts.Num() == 0)
    {
        SetFailure(Result, TEXT("Original-UV islands were found, but no non-overlapping Data UV charts could be generated."));
        return Result;
    }

    TMap<int32, FVector2f> PackedUVByVertexInstance;
    FDWCDataUVPacker::Pack(Triangles, DataUVCharts, InternalPackingResolution, InternalPaddingPixels, PackedUVByVertexInstance);

    TSet<int32> ProblemMaterialSlots;
    FString PackedValidationError;
    if (!FDWCDataUVValidator::Validate(Triangles, DataUVCharts, PackedUVByVertexInstance, ProblemMaterialSlots, PackedValidationError))
    {
        TArray<FDWCDataUVChart> FallbackCharts;
        FDWCDataUVChartBuilder::BuildTriangleFallbackCharts(
            Triangles,
            DataUVCharts,
            ProblemMaterialSlots,
            FallbackCharts,
            Result.TriangleFallbackChartCount);
        DataUVCharts = MoveTemp(FallbackCharts);

        FDWCDataUVPacker::Pack(Triangles, DataUVCharts, InternalPackingResolution, InternalPaddingPixels, PackedUVByVertexInstance);
        ProblemMaterialSlots.Reset();
        PackedValidationError.Reset();
        if (!FDWCDataUVValidator::Validate(Triangles, DataUVCharts, PackedUVByVertexInstance, ProblemMaterialSlots, PackedValidationError))
        {
            SetFailure(Result, FString::Printf(
                TEXT("DWC Data UV generation failed final non-overlap validation: %s"),
                *PackedValidationError));
            return Result;
        }
    }

    for (const TPair<int32, FVector2f>& Pair : PackedUVByVertexInstance)
    {
        const FVertexInstanceID VertexInstanceID(Pair.Key);
        if (IsValidElementID(VertexInstanceID))
        {
            VertexInstanceUVs.Set(VertexInstanceID, NewUVChannelIndex, Pair.Value);
        }
    }

    // When regenerating an existing DWC-owned channel, explicitly clear corners belonging only
    // to excluded triangles so stale UVs cannot make them appear valid to later GPU builders.
    for (const int32 ExcludedVertexInstanceValue : ExcludedVertexInstanceIDs)
    {
        if (PackedUVByVertexInstance.Contains(ExcludedVertexInstanceValue))
        {
            continue;
        }

        const FVertexInstanceID VertexInstanceID(ExcludedVertexInstanceValue);
        if (IsValidElementID(VertexInstanceID))
        {
            VertexInstanceUVs.Set(VertexInstanceID, NewUVChannelIndex, FVector2f(0.0f, 0.0f));
        }
    }

    SkeletalMesh->CommitMeshDescription(LODIndex);
    SkeletalMesh->PostEditChange();
    SkeletalMesh->MarkPackageDirty();

    Result.bSucceeded = true;
    Result.UVChannelIndex = NewUVChannelIndex;
    Result.MaterialSlotIndex = TargetMaterialSlotIndex;
    Result.DataUVChartCount = DataUVCharts.Num();

    const FString TargetLabel = TargetMaterialSlotIndex != INDEX_NONE
                                    ? FString::Printf(TEXT("Material Slot %d"), TargetMaterialSlotIndex)
                                    : FString(TEXT("all material slots"));
    if (bOverwritingExistingChannel)
    {
        Result.Message = FString::Printf(
            TEXT("Regenerated %s in DWC-owned DWC Data UV channel %d with %d packed Data UV chart(s)."),
            *TargetLabel,
            NewUVChannelIndex,
            DataUVCharts.Num());
    }
    else if (bAppendedBecausePreferredChannelWasOccupied)
    {
        Result.Message = FString::Printf(
            TEXT("Preferred UV Channel %d already existed and was not marked as DWC-generated, so created safe DWC Data UV channel %d and generated %s with %d packed Data UV chart(s)."),
            SafePreferredUVChannelIndex,
            NewUVChannelIndex,
            *TargetLabel,
            DataUVCharts.Num());
    }
    else
    {
        Result.Message = FString::Printf(
            TEXT("Created DWC Data UV channel %d and generated %s with %d packed Data UV chart(s)."),
            NewUVChannelIndex,
            *TargetLabel,
            DataUVCharts.Num());
    }

    if (Result.HasWarnings())
    {
        Result.Message += FString::Printf(
            TEXT(" Warnings: excluded %d degenerate source-UV triangle(s) and %d invalid source-UV triangle(s); split %d self-overlapping Original-UV island(s) across %d overlap pair(s); triangle fallback charts: %d. The source Skeletal Mesh was not modified."),
            Result.DegenerateSourceUVTriangleCount,
            Result.InvalidSourceUVTriangleCount,
            Result.SplitOriginalUVIslandCount,
            Result.SelfOverlapPairCount,
            Result.TriangleFallbackChartCount);
    }

    return Result;
}
