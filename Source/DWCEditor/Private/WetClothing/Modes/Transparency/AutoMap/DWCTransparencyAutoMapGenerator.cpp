//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"

#include "DataAssets/DWCBakeLayer.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingPartData.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialColorBakeCache.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyType1SourceProvider.h"
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyProjectionSourceProvider.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeProjection.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

namespace
{
    FDWCBakeResolvedLayer MakeResolvedLayer(
        USkeletalMesh* Mesh,
        const FName LayerId,
        const int32 LayerOrder,
        const float MaxRevealDistance)
    {
        FDWCBakeResolvedLayer Result;
        Result.LayerId = LayerId;
        Result.LayerOrder = LayerOrder;
        Result.ComponentDisplayName = Mesh != nullptr ? Mesh->GetFName() : NAME_None;
        Result.ComponentPath = GetPathNameSafe(Mesh);
        Result.SkeletalMesh = Mesh;
        Result.BakeTransform = FTransform::Identity;
        Result.bCanBeRevealSource = true;
        Result.bCanBeWetOuterLayer = true;
        Result.bBlocksReveal = false;
        Result.MaxRevealDistance = MaxRevealDistance;
        if (Mesh != nullptr)
        {
            for (const FSkeletalMaterial& Material : Mesh->GetMaterials())
            {
                Result.Materials.Add(Material.MaterialInterface);
            }
        }
        return Result;
    }

    FName MakeInnerSourceLayerId(const int32 PriorityIndex)
    {
        return FName(*FString::Printf(TEXT("DWCTransparencyInner_%d"), PriorityIndex));
    }

    int32 CountInvalidImportedBasisTriangles(const FDWCRevealBakeSurface& Surface)
    {
        int32 InvalidTriangleCount = 0;
        for (const FDWCRevealBakeSurfaceTriangle& Triangle : Surface.Triangles)
        {
            InvalidTriangleCount += Triangle.bHasValidImportedTangentBasis ? 0 : 1;
        }
        return InvalidTriangleCount;
    }

    void AppendImportedBasisWarning(
        const FDWCRevealBakeSurface& Surface,
        const FString& SurfaceLabel,
        TArray<FString>& InOutWarnings)
    {
        const int32 InvalidTriangleCount = CountInvalidImportedBasisTriangles(Surface);
        if (InvalidTriangleCount > 0)
        {
            InOutWarnings.Add(FString::Printf(
                TEXT("%s has %d triangle(s) with invalid imported tangent data; Reveal Normal falls back to flat on those hits."),
                *SurfaceLabel,
                InvalidTriangleCount));
        }
    }

    class FSameMeshSurfaceCache
    {
      public:
        FSameMeshSurfaceCache(
            USkeletalMesh* InMesh,
            const int32 InLODIndex,
            const FTransform& InBakeTransform = FTransform::Identity)
            : Mesh(InMesh)
            , LODIndex(InLODIndex)
            , BakeTransform(InBakeTransform)
        {
        }

        bool BuildSlotSurface(
            const FDWCBakeResolvedLayer& Layer,
            const int32 UVChannelIndex,
            const int32 MaterialSlotIndex,
            FDWCRevealBakeSurface& OutSurface,
            FString& OutErrorMessage)
        {
            const FDWCRevealBakeSurface* BaseSurface =
                FindOrBuildBaseSurface(UVChannelIndex, OutErrorMessage);
            if (BaseSurface == nullptr)
            {
                return false;
            }

            const TArray<int32>& SlotTriangleIndices =
                FindOrBuildSlotTriangleIndices(
                    UVChannelIndex,
                    MaterialSlotIndex,
                    *BaseSurface);
            if (SlotTriangleIndices.IsEmpty())
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Material slot %d has no triangles at LOD %d."),
                    MaterialSlotIndex,
                    LODIndex);
                return false;
            }

            OutSurface.Reset();
            OutSurface.LayerId = Layer.LayerId;
            OutSurface.LayerOrder = Layer.LayerOrder;
            OutSurface.LODIndex = LODIndex;
            OutSurface.UVChannelIndex = UVChannelIndex;
            OutSurface.SkeletalMesh = Mesh;
            OutSurface.bCanBeRevealSource = Layer.bCanBeRevealSource;
            OutSurface.bCanBeWetOuterLayer = Layer.bCanBeWetOuterLayer;
            OutSurface.bBlocksReveal = Layer.bBlocksReveal;
            OutSurface.MaxRevealDistance = Layer.MaxRevealDistance;
            OutSurface.Triangles.Reserve(SlotTriangleIndices.Num());
            for (const int32 TriangleIndex : SlotTriangleIndices)
            {
                const FDWCRevealBakeSurfaceTriangle& Triangle =
                    BaseSurface->Triangles[TriangleIndex];
                OutSurface.Bounds += Triangle.Bounds;
                OutSurface.Triangles.Add(Triangle);
            }
            OutErrorMessage.Reset();
            return true;
        }

      private:
        const FDWCRevealBakeSurface* FindOrBuildBaseSurface(
            const int32 UVChannelIndex,
            FString& OutErrorMessage)
        {
            if (const FDWCRevealBakeSurface* CachedSurface =
                    BaseSurfacesByUVChannel.Find(UVChannelIndex))
            {
                return CachedSurface;
            }

            const FDWCBakeResolvedLayer GeometryLayer = MakeResolvedLayer(
                Mesh,
                FName(TEXT("DWCTransparencyGeometryCache")),
                0,
                0.0f);
            FDWCBakeResolvedLayer TransformedGeometryLayer = GeometryLayer;
            TransformedGeometryLayer.BakeTransform = BakeTransform;
            FDWCRevealBakeSurface BuiltSurface;
            if (!FDWCRevealBakeSurfaceBuilder::BuildReferencePoseSurface(
                    TransformedGeometryLayer,
                    LODIndex,
                    UVChannelIndex,
                    BuiltSurface,
                    &OutErrorMessage))
            {
                return nullptr;
            }
            return &BaseSurfacesByUVChannel.Add(UVChannelIndex, MoveTemp(BuiltSurface));
        }

        const TArray<int32>& FindOrBuildSlotTriangleIndices(
            const int32 UVChannelIndex,
            const int32 MaterialSlotIndex,
            const FDWCRevealBakeSurface& BaseSurface)
        {
            const FIntPoint Key(UVChannelIndex, MaterialSlotIndex);
            if (const TArray<int32>* CachedIndices =
                    SlotTriangleIndicesByUVAndSlot.Find(Key))
            {
                return *CachedIndices;
            }

            TArray<int32> TriangleIndices;
            for (int32 TriangleIndex = 0;
                 TriangleIndex < BaseSurface.Triangles.Num();
                 ++TriangleIndex)
            {
                if (BaseSurface.Triangles[TriangleIndex].MaterialSlotIndex ==
                    MaterialSlotIndex)
                {
                    TriangleIndices.Add(TriangleIndex);
                }
            }
            return SlotTriangleIndicesByUVAndSlot.Add(Key, MoveTemp(TriangleIndices));
        }

        USkeletalMesh* Mesh = nullptr;
        int32 LODIndex = 0;
        FTransform BakeTransform = FTransform::Identity;
        TMap<int32, FDWCRevealBakeSurface> BaseSurfacesByUVChannel;
        TMap<FIntPoint, TArray<int32>> SlotTriangleIndicesByUVAndSlot;
    };

    bool BuildResolvedSlotSurface(
        const FDWCBakeResolvedLayer& Layer,
        const int32 LODIndex,
        const int32 UVChannelIndex,
        const int32 MaterialSlotIndex,
        FDWCRevealBakeSurface& OutSurface,
        FString& OutError)
    {
        FDWCRevealBakeSurface FullSurface;
        if (!FDWCRevealBakeSurfaceBuilder::BuildReferencePoseSurface(
                Layer, LODIndex, UVChannelIndex, FullSurface, &OutError))
        {
            return false;
        }
        OutSurface = MoveTemp(FullSurface);
        OutSurface.Triangles.RemoveAll(
            [MaterialSlotIndex](const FDWCRevealBakeSurfaceTriangle& Triangle)
            {
                return Triangle.MaterialSlotIndex != MaterialSlotIndex;
            });
        OutSurface.Bounds = FBox(ForceInit);
        for (const FDWCRevealBakeSurfaceTriangle& Triangle : OutSurface.Triangles)
        {
            OutSurface.Bounds += Triangle.Bounds;
        }
        if (OutSurface.Triangles.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Material slot %d has no LOD 0 triangles."), MaterialSlotIndex);
            return false;
        }
        return true;
    }

    /**
     * Wet Part authoring identifies original-UV islands. Transparency is
     * rasterized in the DWC Data UV, but both topologies retain the same
     * render triangle IDs. Build the target-side eligibility once from that
     * shared identity so source meshes remain completely independent.
     */
    bool BuildTargetWetPartEligibleTriangles(
        const UWetClothingAsset& WetClothingAsset,
        const int32 LODIndex,
        const int32 MaterialSlotIndex,
        TSet<int32>& OutEligibleTriangleIDs,
        FString& OutErrorMessage)
    {
        OutEligibleTriangleIDs.Reset();
        OutErrorMessage.Reset();

        const FWetClothingAuthoredMaterialSlot* WettableSlot =
            WetClothingAsset.Authored.PartData.EditableWetPartData.FindMaterialSlot(MaterialSlotIndex);
        if (WettableSlot == nullptr || !WettableSlot->bIsWettableSlot)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Transparency Target Part slot %d is not configured as a Wet Part material slot."),
                MaterialSlotIndex);
            return false;
        }

        TSet<int32> AssignedOriginalIslandIDs;
        for (const FWetClothingWetPartEntry& WetPart : WettableSlot->WetPartEntries)
        {
            for (const int32 IslandID : WetPart.AssignedUVIslandIDs)
            {
                if (IslandID != INDEX_NONE)
                {
                    AssignedOriginalIslandIDs.Add(IslandID);
                }
            }
        }
        if (AssignedOriginalIslandIDs.IsEmpty())
        {
            OutErrorMessage = FString::Printf(
                TEXT("Transparency Target Part slot %d has no Wet Part UV islands assigned."),
                MaterialSlotIndex);
            return false;
        }

#if WITH_EDITORONLY_DATA
        const FDWCEditorUVTopologyData* OriginalTopology =
            WetClothingAsset.FindOriginalUVTopologyForLOD(LODIndex);
        if (OriginalTopology == nullptr || !OriginalTopology->bIsValid ||
            OriginalTopology->LODIndex != LODIndex ||
            OriginalTopology->UVChannelIndex != WetClothingAsset.GetOriginalUVChannelIndex())
        {
            OutErrorMessage = TEXT("Transparency Target Part Wet Part eligibility requires current Original UV topology.");
            return false;
        }

        for (const FDWCOriginalUVIslandTopology& Island : OriginalTopology->Islands)
        {
            if (Island.MaterialSlotIndex != MaterialSlotIndex ||
                !AssignedOriginalIslandIDs.Contains(Island.IslandID))
            {
                continue;
            }
            for (const int32 TriangleID : Island.TriangleIndices)
            {
                if (TriangleID != INDEX_NONE)
                {
                    OutEligibleTriangleIDs.Add(TriangleID);
                }
            }
        }
#else
        OutErrorMessage = TEXT("Transparency Target Part Wet Part eligibility is editor-only data.");
        return false;
#endif

        if (OutEligibleTriangleIDs.IsEmpty())
        {
            OutErrorMessage = FString::Printf(
                TEXT("Transparency Target Part slot %d Wet Part assignments do not contain any LOD %d triangles."),
                MaterialSlotIndex,
                LODIndex);
            return false;
        }
        return true;
    }

    int32 FilterSamplesToWetPartEligibility(
        TArray<FDWCRevealBakeTexelSample>& InOutSamples,
        const TSet<int32>& EligibleTriangleIDs)
    {
        InOutSamples.RemoveAll(
            [&EligibleTriangleIDs](const FDWCRevealBakeTexelSample& Sample)
            {
                return !EligibleTriangleIDs.Contains(Sample.TriangleIndex);
            });
        return InOutSamples.Num();
    }

    float CalculateAutoAlpha(const FWetClothingTransparencyRaySettings& Settings, const FDWCRevealBakeRayHit& Hit)
    {
        const float FullDistance = FMath::Max(Settings.FullTransparencyDistance, 0.0f);
        const float NoTransparencyDistance = FMath::Max(Settings.NoTransparencyDistance, FullDistance + UE_SMALL_NUMBER);
        const float DistanceAlpha = 1.0f - FMath::Clamp(
            (Hit.Distance - FullDistance) / (NoTransparencyDistance - FullDistance),
            0.0f,
            1.0f);
        return FMath::Clamp(DistanceAlpha * Hit.Confidence, 0.0f, 1.0f);
    }

    int32 ResolveOuterSampleUVIslandID(
        const FDWCDataUVLODMetadata* DataUVMetadata,
        const FDWCRevealBakeTexelSample& Sample)
    {
        if (Sample.UVIslandID != INDEX_NONE)
        {
            return Sample.UVIslandID;
        }
        if (DataUVMetadata != nullptr &&
            DataUVMetadata->DataUVIslandIDByTriangleID.IsValidIndex(Sample.TriangleIndex))
        {
            return DataUVMetadata->DataUVIslandIDByTriangleID[Sample.TriangleIndex];
        }
        return INDEX_NONE;
    }

    bool TryEncodeOuterSampleUVIslandID(
        const FDWCDataUVLODMetadata* DataUVMetadata,
        const FDWCRevealBakeTexelSample& Sample,
        uint16& OutEncodedIslandID,
        FString& OutErrorMessage)
    {
        const int32 IslandID = ResolveOuterSampleUVIslandID(DataUVMetadata, Sample);
        if (IslandID != INDEX_NONE && !FDWCTransparencySourcePayload::CanEncodeOuterIslandID(IslandID))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Transparency target UV island ID %d exceeds the supported range [0, %u]."),
                IslandID,
                static_cast<uint32>(FDWCTransparencySourcePayload::InvalidOuterIslandID) - 1u);
            return false;
        }
        OutEncodedIslandID = FDWCTransparencySourcePayload::EncodeOuterIslandID(IslandID);
        return true;
    }

    bool PassesTransparencyIslandClip(
        const FDWCTransparencySourcePayload& Result,
        const int32 PixelIndex,
        const int32 UVIslandID)
    {
        if (!Result.OuterCoverageBuffer.IsValidIndex(PixelIndex) ||
            Result.OuterCoverageBuffer[PixelIndex] == 0)
        {
            return false;
        }
        if (UVIslandID == INDEX_NONE)
        {
            return true;
        }
        return Result.OuterIslandIDBuffer.IsValidIndex(PixelIndex) &&
            FDWCTransparencySourcePayload::MatchesOuterIslandID(
                Result.OuterIslandIDBuffer[PixelIndex],
                UVIslandID);
    }

    void BuildTriangleLookup(
        const FDWCRevealBakeSurface& Surface,
        TMap<int32, const FDWCRevealBakeSurfaceTriangle*>& OutTrianglesByID)
    {
        OutTrianglesByID.Reset();
        OutTrianglesByID.Reserve(Surface.Triangles.Num());
        for (const FDWCRevealBakeSurfaceTriangle& Triangle : Surface.Triangles)
        {
            OutTrianglesByID.Add(Triangle.TriangleIndex, &Triangle);
        }
    }

    FColor EncodeReorientedRevealSurface(
        const FDWCTransparencyMaterialColorBakeResult& SourceMaterialSurface,
        const FDWCRevealBakeSurfaceTriangle& SourceTriangle,
        const FVector& SourceBarycentric,
        const FDWCRevealBakeSurfaceTriangle& OuterTriangle,
        const FVector& OuterBarycentric,
        const FVector2D& SourceUV)
    {
        FDWCRevealBakeSurfaceFrame SourceFrame;
        FDWCRevealBakeSurfaceFrame OuterFrame;
        const float Metallic = SourceMaterialSurface.SampleMetallic(SourceUV);
        if (!FDWCRevealBakeSurfaceFrameBuilder::BuildInterpolatedFrame(
                SourceTriangle, SourceBarycentric, SourceFrame) ||
            !FDWCRevealBakeSurfaceFrameBuilder::BuildInterpolatedFrame(
                OuterTriangle, OuterBarycentric, OuterFrame))
        {
            return FDWCRevealBakeSurfaceFrameBuilder::EncodeRevealSurface(
                FVector3f(0.0f, 0.0f, 1.0f), Metallic, true);
        }

        return FDWCRevealBakeSurfaceFrameBuilder::EncodeRevealSurface(
            FDWCRevealBakeSurfaceFrameBuilder::ReorientTangentNormal(
                SourceMaterialSurface.SampleTangentNormal(SourceUV), SourceFrame, OuterFrame),
            Metallic,
            true);
    }

}

bool FDWCTransparencyAutoMapGenerator::BuildTargetSurfaceBuffers(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyTargetSurface& TargetSurface,
    const int32 LODIndex,
    const FIntPoint Resolution,
    TArray<uint8>& OutCoverageBuffer,
    TArray<uint16>& OutIslandIDBuffer,
    int32* OutOuterSampleCount,
    int32* OutOverlappedPixelCount,
    FString& OutErrorMessage)
{
    OutCoverageBuffer.Reset();
    OutIslandIDBuffer.Reset();
    if (OutOuterSampleCount != nullptr)
    {
        *OutOuterSampleCount = 0;
    }
    if (OutOverlappedPixelCount != nullptr)
    {
        *OutOverlappedPixelCount = 0;
    }
    OutErrorMessage.Reset();

    USkeletalMesh* RuntimeMesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        OutErrorMessage = TEXT("Manual transparency generation requires a DWC runtime skeletal mesh.");
        return false;
    }
    if (TargetSurface.OuterMaterialSlotIndex == INDEX_NONE)
    {
        OutErrorMessage = TEXT("Select a Transparency Target Part before generating the transparency map.");
        return false;
    }
    if (Resolution.X <= 0 || Resolution.Y <= 0)
    {
        OutErrorMessage = TEXT("Transparency target surface buffers require a valid resolution.");
        return false;
    }

    const FDWCBakeResolvedLayer OuterLayer = MakeResolvedLayer(
        RuntimeMesh,
        FName(TEXT("DWCTransparencyTargetSurface")) ,
        MAX_int32 / 2,
        0.0f);
    FSameMeshSurfaceCache SurfaceCache(RuntimeMesh, LODIndex);
    FDWCRevealBakeSurface OuterSurface;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    if (!SurfaceCache.BuildSlotSurface(
            OuterLayer,
            DataUVChannelIndex,
            TargetSurface.OuterMaterialSlotIndex,
            OuterSurface,
            OutErrorMessage))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to build the transparency target surface: %s"),
            *OutErrorMessage);
        return false;
    }

    FDWCRevealBakeTexelSamplingSettings SamplingSettings;
    SamplingSettings.Resolution = Resolution;
    SamplingSettings.MaterialSlotIndex = TargetSurface.OuterMaterialSlotIndex;
    TArray<FDWCRevealBakeTexelSample> OuterSamples;
    int32 OverlappedPixelCount = 0;
    if (!FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            OuterSurface,
            SamplingSettings,
            OuterSamples,
            &OutErrorMessage,
            &OverlappedPixelCount))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to rasterize the transparency target UVs: %s"),
            *OutErrorMessage);
        return false;
    }
    if (OuterSamples.IsEmpty())
    {
        OutErrorMessage = TEXT("No target texel samples were generated. Check the selected target slot and DWC UV Channel.");
        return false;
    }

    TSet<int32> EligibleTriangleIDs;
    if (!BuildTargetWetPartEligibleTriangles(
            WetClothingAsset,
            LODIndex,
            TargetSurface.OuterMaterialSlotIndex,
            EligibleTriangleIDs,
            OutErrorMessage))
    {
        return false;
    }
    const int32 EligibleSampleCount = FilterSamplesToWetPartEligibility(
        OuterSamples,
        EligibleTriangleIDs);
    if (EligibleSampleCount == 0)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Transparency Target Part slot %d Wet Part UV islands did not rasterize any DWC Data UV texels."),
            TargetSurface.OuterMaterialSlotIndex);
        return false;
    }

    const int32 PixelCount = Resolution.X * Resolution.Y;
    OutCoverageBuffer.Init(0, PixelCount);
    OutIslandIDBuffer.Init(FDWCTransparencySourcePayload::InvalidOuterIslandID, PixelCount);
    const FDWCDataUVLODMetadata* DataUVMetadata =
        WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
    for (const FDWCRevealBakeTexelSample& Sample : OuterSamples)
    {
        if (Sample.Pixel.X < 0 || Sample.Pixel.Y < 0 ||
            Sample.Pixel.X >= Resolution.X || Sample.Pixel.Y >= Resolution.Y)
        {
            continue;
        }

        const int32 PixelIndex = Sample.Pixel.Y * Resolution.X + Sample.Pixel.X;
        OutCoverageBuffer[PixelIndex] = 1;
        uint16 EncodedIslandID = FDWCTransparencySourcePayload::InvalidOuterIslandID;
        if (!TryEncodeOuterSampleUVIslandID(
                DataUVMetadata,
                Sample,
                EncodedIslandID,
                OutErrorMessage))
        {
            OutCoverageBuffer.Reset();
            OutIslandIDBuffer.Reset();
            return false;
        }
        OutIslandIDBuffer[PixelIndex] = EncodedIslandID;
    }

    if (OutOuterSampleCount != nullptr)
    {
        *OutOuterSampleCount = EligibleSampleCount;
    }
    if (OutOverlappedPixelCount != nullptr)
    {
        *OutOverlappedPixelCount = OverlappedPixelCount;
    }
    return true;
}

void FDWCTransparencyAutoMapGenerator::ApplyRevealColorPaintStrokes(
    const FDWCTransparencySourcePayload& SourcePayload,
    const TArray<FDWCTransparencyRevealColorStroke>& Strokes,
    const int32 MaterialSlotIndex,
    const FLinearColor&,
    TArray<FColor>& InOutRevealColorBuffer)
{
    const int32 Width = SourcePayload.Resolution.X;
    const int32 Height = SourcePayload.Resolution.Y;
    if (Width <= 0 || Height <= 0 || InOutRevealColorBuffer.Num() != Width * Height)
    {
        return;
    }

    const auto WrapIndex = [](int32 Value, int32 Size) { return (Value % Size + Size) % Size; };
    for (const FDWCTransparencyRevealColorStroke& Stroke : Strokes)
    {
        if (!Stroke.bEnabled || Stroke.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
        const FLinearColor PaintColor = Stroke.PaintColor.CopyWithNewOpacity(1.0f);
        for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
        {
            const float RadiusX = FMath::Max(Sample.RadiusUV * Width, 1.0f);
            const float RadiusY = FMath::Max(Sample.RadiusUV * Height, 1.0f);
            const FVector2D Center(Sample.PositionUV.X * Width, Sample.PositionUV.Y * Height);
            const int32 MinX = FMath::FloorToInt(Center.X - RadiusX - 1.0f);
            const int32 MaxX = FMath::CeilToInt(Center.X + RadiusX + 1.0f);
            const int32 MinY = FMath::FloorToInt(Center.Y - RadiusY - 1.0f);
            const int32 MaxY = FMath::CeilToInt(Center.Y + RadiusY + 1.0f);
            const int32 ClipUVIslandID = SourcePayload.ResolveOuterIslandIDAtUV(
                Sample.PositionUV,
                Sample.UVIslandID,
                bWrap);
            const float InnerRadius = 1.0f - FMath::Clamp(Stroke.Falloff, 0.0f, 1.0f);
            const bool bSmooth = Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::Smooth;
            const int32 SnapshotMinX = MinX - 1;
            const int32 SnapshotMinY = MinY - 1;
            const int32 SnapshotWidth = MaxX - MinX + 3;
            const int32 SnapshotHeight = MaxY - MinY + 3;
            TArray<FColor> SmoothSnapshot;
            if (bSmooth)
            {
                SmoothSnapshot.SetNumUninitialized(SnapshotWidth * SnapshotHeight);
                for (int32 SnapshotY = 0; SnapshotY < SnapshotHeight; ++SnapshotY)
                {
                    for (int32 SnapshotX = 0; SnapshotX < SnapshotWidth; ++SnapshotX)
                    {
                        int32 SourceX = SnapshotMinX + SnapshotX;
                        int32 SourceY = SnapshotMinY + SnapshotY;
                        if (bWrap)
                        {
                            SourceX = WrapIndex(SourceX, Width);
                            SourceY = WrapIndex(SourceY, Height);
                        }
                        else
                        {
                            SourceX = FMath::Clamp(SourceX, 0, Width - 1);
                            SourceY = FMath::Clamp(SourceY, 0, Height - 1);
                        }
                        SmoothSnapshot[SnapshotY * SnapshotWidth + SnapshotX] =
                            InOutRevealColorBuffer[SourceY * Width + SourceX];
                    }
                }
            }
            auto ReadColorAt = [Width, Height, bWrap, SnapshotMinX, SnapshotMinY, SnapshotWidth,
                                &WrapIndex, &SmoothSnapshot, &SourcePayload, &InOutRevealColorBuffer](int32 RawX, int32 RawY)
            {
                const int32 SnapshotIndex = (RawY - SnapshotMinY) * SnapshotWidth + (RawX - SnapshotMinX);
                if (SmoothSnapshot.IsValidIndex(SnapshotIndex))
                {
                    return FLinearColor(SmoothSnapshot[SnapshotIndex]);
                }
                if (bWrap)
                {
                    RawX = WrapIndex(RawX, Width);
                    RawY = WrapIndex(RawY, Height);
                }
                else
                {
                    RawX = FMath::Clamp(RawX, 0, Width - 1);
                    RawY = FMath::Clamp(RawY, 0, Height - 1);
                }
                return FLinearColor(InOutRevealColorBuffer[RawY * Width + RawX]);
            };
            for (int32 RawY = MinY; RawY <= MaxY; ++RawY)
            {
                for (int32 RawX = MinX; RawX <= MaxX; ++RawX)
                {
                    if (!bWrap && (RawX < 0 || RawX >= Width || RawY < 0 || RawY >= Height)) continue;
                    const float DX = (RawX + 0.5f - Center.X) / RadiusX;
                    const float DY = (RawY + 0.5f - Center.Y) / RadiusY;
                    const float Distance = FMath::Sqrt(DX * DX + DY * DY);
                    if (Distance > 1.0f) continue;
                    const float RadialWeight = Distance <= InnerRadius || Stroke.Falloff <= KINDA_SMALL_NUMBER
                        ? 1.0f : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
                    const float Weight = FMath::Clamp(RadialWeight * Sample.Strength, 0.0f, 1.0f);
                    if (Weight <= 0.0f) continue;
                    const int32 X = bWrap ? WrapIndex(RawX, Width) : RawX;
                    const int32 Y = bWrap ? WrapIndex(RawY, Height) : RawY;
                    const int32 PixelIndex = Y * Width + X;
                    if (!SourcePayload.OuterCoverageBuffer.IsValidIndex(PixelIndex) || SourcePayload.OuterCoverageBuffer[PixelIndex] == 0) continue;
                    if (!PassesTransparencyIslandClip(SourcePayload, PixelIndex, ClipUVIslandID)) continue;
                    FLinearColor TargetColor = PaintColor;
                    if (Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::EraseToBase)
                    {
                        TargetColor = FLinearColor(SourcePayload.InnerColorBuffer[PixelIndex]);
                        TargetColor.A = 1.0f;
                    }
                    else if (Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::Smooth)
                    {
                        TargetColor = FLinearColor::Black;
                        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                        {
                            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                            {
                                int32 NeighborX = RawX + OffsetX;
                                int32 NeighborY = RawY + OffsetY;
                                if (bWrap)
                                {
                                    NeighborX = WrapIndex(NeighborX, Width);
                                    NeighborY = WrapIndex(NeighborY, Height);
                                }
                                else
                                {
                                    NeighborX = FMath::Clamp(NeighborX, 0, Width - 1);
                                    NeighborY = FMath::Clamp(NeighborY, 0, Height - 1);
                                }
                                const int32 NeighborIndex = NeighborY * Width + NeighborX;
                                if (PassesTransparencyIslandClip(SourcePayload, NeighborIndex, ClipUVIslandID))
                                {
                                    TargetColor += ReadColorAt(RawX + OffsetX, RawY + OffsetY);
                                }
                                else
                                {
                                    TargetColor += FLinearColor(InOutRevealColorBuffer[PixelIndex]);
                                }
                            }
                        }
                        TargetColor /= 9.0f;
                        TargetColor.A = 1.0f;
                    }
                    InOutRevealColorBuffer[PixelIndex] = FMath::Lerp(
                        FLinearColor(InOutRevealColorBuffer[PixelIndex]),
                        TargetColor.CopyWithNewOpacity(1.0f),
                        Weight).ToFColor(true);
                }
            }
        }
    }
}
bool FDWCTransparencyAutoMapGenerator::GenerateBaseRevealColorMap(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencySourcePayload& OutResult,
    FString& OutSummary,
    TArray<FString>& OutWarnings)
{
    OutResult = FDWCTransparencySourcePayload();
    OutSummary.Reset();
    OutWarnings.Reset();

    if (Layer.SourceType != EDWCTransparencySourceType::ManualColorOrTexture)
    {
        OutSummary = TEXT("The selected Transparency Target Part is not configured for the manual base-color workflow.");
        return false;
    }

    USkeletalMesh* RuntimeMesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        OutSummary = TEXT("Manual transparency generation requires a DWC runtime skeletal mesh.");
        return false;
    }
    if (Layer.TargetSurface.OuterMaterialSlotIndex == INDEX_NONE)
    {
        OutSummary = TEXT("Select a Transparency Target Part before generating the transparency map.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(
        WetClothingAsset.Authored.TransparencyData.TransparencyBakeResolution,
        16,
        4096);
    constexpr int32 LODIndex = 0;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    const FIntPoint BakeResolution(Resolution, Resolution);
    const int32 PixelCount = Resolution * Resolution;

    const FDWCBakeResolvedLayer OuterLayer = MakeResolvedLayer(
        RuntimeMesh,
        FName(TEXT("DWCTransparencyManualOuter")),
        MAX_int32 / 2,
        0.0f);
    FSameMeshSurfaceCache SurfaceCache(RuntimeMesh, LODIndex);
    FDWCRevealBakeSurface OuterSurface;
    FString BuildError;
    if (!SurfaceCache.BuildSlotSurface(
            OuterLayer,
            DataUVChannelIndex,
            Layer.TargetSurface.OuterMaterialSlotIndex,
            OuterSurface,
            BuildError))
    {
        OutSummary = FString::Printf(
            TEXT("Failed to build the transparency target surface: %s"),
            *BuildError);
        return false;
    }

    FDWCRevealBakeTexelSamplingSettings SamplingSettings;
    SamplingSettings.Resolution = BakeResolution;
    SamplingSettings.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    TArray<FDWCRevealBakeTexelSample> OuterSamples;
    int32 OverlappedPixelCount = 0;
    if (!FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            OuterSurface,
            SamplingSettings,
            OuterSamples,
            &BuildError,
            &OverlappedPixelCount))
    {
        OutSummary = FString::Printf(
            TEXT("Failed to rasterize the transparency target UVs: %s"),
            *BuildError);
        return false;
    }
    if (OuterSamples.IsEmpty())
    {
        OutSummary = TEXT("No target texel samples were generated. Check the selected target slot and DWC UV Channel.");
        return false;
    }

    TSet<int32> EligibleTriangleIDs;
    if (!BuildTargetWetPartEligibleTriangles(
            WetClothingAsset,
            LODIndex,
            Layer.TargetSurface.OuterMaterialSlotIndex,
            EligibleTriangleIDs,
            OutSummary))
    {
        return false;
    }
    if (FilterSamplesToWetPartEligibility(OuterSamples, EligibleTriangleIDs) == 0)
    {
        OutSummary = FString::Printf(
            TEXT("Transparency Target Part slot %d Wet Part UV islands did not rasterize any DWC Data UV texels."),
            Layer.TargetSurface.OuterMaterialSlotIndex);
        return false;
    }

    OutResult.LayerGuid = Layer.LayerGuid;
    OutResult.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    OutResult.UVChannelIndex = DataUVChannelIndex;
    OutResult.LODIndex = LODIndex;
    OutResult.Resolution = BakeResolution;
    OutResult.OuterSampleCount = OuterSamples.Num();
    OutResult.OverlappedUVPixelCount = OverlappedPixelCount;
    OutResult.InnerColorBuffer.Init(FColor::Black, PixelCount);
    OutResult.RevealSurfaceAuthoring.Init(BakeResolution, FColor(128, 128, 0, 0));
    OutResult.AutoAlphaBuffer.Init(0, PixelCount);
    OutResult.OuterCoverageBuffer.Init(0, PixelCount);
    OutResult.ValidHitBuffer.Init(false, PixelCount);
    OutResult.HitDistanceBuffer.Init(0.0f, PixelCount);
    OutResult.SourcePriorityBuffer.Init(INDEX_NONE, PixelCount);
    OutResult.OuterIslandIDBuffer.Init(FDWCTransparencySourcePayload::InvalidOuterIslandID, PixelCount);

    const FDWCDataUVLODMetadata* DataUVMetadata =
        WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
    const FColor BaseRevealColor = Layer.ManualColorSource.BaseRevealColor
        .CopyWithNewOpacity(1.0f)
        .ToFColor(true);
    const uint8 InitialAlpha = static_cast<uint8>(FMath::RoundToInt(
        FMath::Clamp(Layer.ManualColorSource.InitialTransparencyAlpha, 0.0f, 1.0f) * 255.0f));
    for (const FDWCRevealBakeTexelSample& Sample : OuterSamples)
    {
        if (Sample.Pixel.X < 0 || Sample.Pixel.Y < 0 ||
            Sample.Pixel.X >= Resolution || Sample.Pixel.Y >= Resolution)
        {
            continue;
        }

        const int32 PixelIndex = Sample.Pixel.Y * Resolution + Sample.Pixel.X;
        OutResult.OuterCoverageBuffer[PixelIndex] = 1;
        uint16 EncodedIslandID = FDWCTransparencySourcePayload::InvalidOuterIslandID;
        FString IslandError;
        if (!TryEncodeOuterSampleUVIslandID(
                DataUVMetadata,
                Sample,
                EncodedIslandID,
                IslandError))
        {
            OutSummary = MoveTemp(IslandError);
            return false;
        }
        OutResult.OuterIslandIDBuffer[PixelIndex] = EncodedIslandID;
        OutResult.ValidHitBuffer[PixelIndex] = true;
        OutResult.InnerColorBuffer[PixelIndex] = BaseRevealColor;
        OutResult.AutoAlphaBuffer[PixelIndex] = InitialAlpha;
        ++OutResult.ValidHitCount;
    }

    FString MaterialBakeSignature;
    FString SignatureError;
    if (!FDWCTransparencySignatureService::BuildSourceSignature(
            WetClothingAsset,
            Layer,
            OutResult.BuildSignature,
            MaterialBakeSignature,
            SignatureError))
    {
        OutSummary = MoveTemp(SignatureError);
        return false;
    }

    if (OverlappedPixelCount > 0)
    {
        OutWarnings.Add(FString::Printf(
            TEXT("The target DWC UV Channel contains %d genuinely overlapping rasterized pixel(s). Rebuild and inspect the DWC UV Channel before editing transparency."),
            OverlappedPixelCount));
    }

    OutSummary = FString::Printf(
        TEXT("Generated %d target texels with Base Reveal Color and Initial Transparency Alpha. No ray projection was used."),
        OutResult.OuterSampleCount);
    return true;
}

bool FDWCTransparencyAutoMapGenerator::GenerateSameMesh(
    UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencySourcePayload& OutResult,
    FString& OutSummary,
    TArray<FString>& OutWarnings)
{
    OutResult = FDWCTransparencySourcePayload();
    OutSummary.Reset();
    OutWarnings.Reset();

    FDWCTransparencyAutoMapSnapshot Snapshot;
    FString SnapshotError;
    if (!BuildProjectionSnapshot(WetClothingAsset, Layer, Snapshot, SnapshotError))
    {
        OutSummary = MoveTemp(SnapshotError);
        return false;
    }

    FDWCTransparencyAutoMapComputedResult Computed = ComputeSameMeshSnapshot(Snapshot);
    OutWarnings = MoveTemp(Computed.Warnings);
    if (!Computed.bSucceeded)
    {
        OutSummary = Computed.Error.IsEmpty()
            ? TEXT("Transparency ray projection failed.")
            : MoveTemp(Computed.Error);
        return false;
    }

    OutResult = MoveTemp(Computed.SourcePayload);
    OutSummary = MoveTemp(Computed.Summary);
    return true;
}

bool FDWCTransparencyAutoMapGenerator::BuildSignatureOnlyResult(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencySourcePayload& OutResult,
    FString& OutErrorMessage)
{
    check(IsInGameThread());
    OutResult = FDWCTransparencySourcePayload();
    OutErrorMessage.Reset();

    USkeletalMesh* RuntimeMesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        OutErrorMessage = TEXT("Transparency validation requires a DWC runtime skeletal mesh.");
        return false;
    }
    if (!RuntimeMesh->GetMaterials().IsValidIndex(Layer.TargetSurface.OuterMaterialSlotIndex))
    {
        OutErrorMessage = TEXT("The Transparency Target Part no longer references a valid material slot.");
        return false;
    }
    if (Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots &&
        Layer.SameMeshSource.InnerSlotPriority.IsEmpty())
    {
        OutErrorMessage = TEXT("Add at least one Inner Source Part before validating the transparency map.");
        return false;
    }

    constexpr int32 LODIndex = 0;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    const FDWCDataUVLODMetadata* DataUVMetadata = WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
    if (DataUVChannelIndex == INDEX_NONE || DataUVMetadata == nullptr ||
        DataUVMetadata->DataUVOutputSignature.IsEmpty())
    {
        OutErrorMessage = TEXT("Transparency validation requires valid DWC Data UV metadata for LOD 0.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(
        WetClothingAsset.Authored.TransparencyData.TransparencyBakeResolution,
        16,
        4096);
    OutResult.LayerGuid = Layer.LayerGuid;
    OutResult.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    OutResult.UVChannelIndex = DataUVChannelIndex;
    OutResult.LODIndex = LODIndex;
    OutResult.Resolution = FIntPoint(Resolution, Resolution);
    FString MaterialBakeSignature;
    return FDWCTransparencySignatureService::BuildSourceSignature(
        WetClothingAsset,
        Layer,
        OutResult.BuildSignature,
        MaterialBakeSignature,
        OutErrorMessage);
}

struct FDWCTransparencyAutoMapSnapshot::FImpl
{
    FDWCRevealBakeSurface OuterSurface;
    TArray<FDWCRevealBakeSurface> SourceSurfaces;
    TArray<FDWCRevealBakeTexelSample> OuterSamples;
    FDWCRevealBakeRayProjectionSettings ProjectionSettings;
    FWetClothingTransparencyRaySettings RaySettings;
    TMap<FName, int32> PriorityBySourceLayerId;
    TMap<FName, int32> StatsIndexBySourceLayerId;
    TMap<FName, TSharedPtr<const FDWCTransparencyMaterialColorBakeResult>> SourceSurfacesByLayerId;
    FDWCTransparencySourcePayload SeedResult;
    TArray<FString> Warnings;
    uint64 EstimatedBytes = 0;
    bool bValid = false;
};

FDWCTransparencyAutoMapSnapshot::FDWCTransparencyAutoMapSnapshot()
    : Impl(MakeUnique<FImpl>())
{
}

FDWCTransparencyAutoMapSnapshot::~FDWCTransparencyAutoMapSnapshot() = default;
FDWCTransparencyAutoMapSnapshot::FDWCTransparencyAutoMapSnapshot(FDWCTransparencyAutoMapSnapshot&&) = default;
FDWCTransparencyAutoMapSnapshot& FDWCTransparencyAutoMapSnapshot::operator=(FDWCTransparencyAutoMapSnapshot&&) = default;

bool FDWCTransparencyAutoMapSnapshot::IsValid() const
{
    return Impl.IsValid() && Impl->bValid;
}

int32 FDWCTransparencyAutoMapSnapshot::GetMaterialSlotIndex() const
{
    return Impl.IsValid() ? Impl->SeedResult.MaterialSlotIndex : INDEX_NONE;
}

FGuid FDWCTransparencyAutoMapSnapshot::GetLayerGuid() const
{
    return Impl.IsValid() ? Impl->SeedResult.LayerGuid : FGuid();
}

uint64 FDWCTransparencyAutoMapSnapshot::GetEstimatedBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedBytes : 0;
}

bool FDWCTransparencyAutoMapGenerator::BuildSameMeshSnapshot(
    UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyAutoMapSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    if (Layer.SourceType != EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        OutErrorMessage = TEXT("BuildSameMeshSnapshot only accepts Type 1 transparency layers.");
        return false;
    }
    return BuildProjectionSnapshot(WetClothingAsset, Layer, OutSnapshot, OutErrorMessage);
}

bool FDWCTransparencyAutoMapGenerator::BuildProjectionSnapshot(
    UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyAutoMapSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    check(IsInGameThread());
    OutSnapshot = FDWCTransparencyAutoMapSnapshot();
    FDWCTransparencyAutoMapSnapshot::FImpl& Snapshot = *OutSnapshot.Impl;
    OutErrorMessage.Reset();

    USkeletalMesh* RuntimeMesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        OutErrorMessage = TEXT("Transparency auto-map requires a DWC runtime skeletal mesh.");
        return false;
    }
    if (Layer.TargetSurface.OuterMaterialSlotIndex == INDEX_NONE)
    {
        OutErrorMessage = TEXT("Select a Transparency Target Part before generating the transparency map.");
        return false;
    }
    if (Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots &&
        Layer.SameMeshSource.InnerSlotPriority.IsEmpty())
    {
        OutErrorMessage = TEXT("Add at least one Inner Source Part before generating the transparency map.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(
        WetClothingAsset.Authored.TransparencyData.TransparencyBakeResolution,
        16,
        4096);
    constexpr int32 LODIndex = 0;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    const FIntPoint BakeResolution(Resolution, Resolution);
    const int32 PixelCount = Resolution * Resolution;

    FDWCTransparencyProjectionSourceSet ProviderSources;
    if (Layer.SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        if (!FDWCTransparencyProjectionSourceProvider::BuildBlueprintSources(
                WetClothingAsset, Layer, ProviderSources, OutErrorMessage))
        {
            return false;
        }
    }
    else if (Layer.SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh)
    {
        if (!FDWCTransparencyProjectionSourceProvider::BuildExternalMeshSources(
                WetClothingAsset, Layer, ProviderSources, OutErrorMessage))
        {
            return false;
        }
    }
    else if (Layer.SourceType != EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        OutErrorMessage = TEXT("This transparency source type does not use ray projection.");
        return false;
    }

    FDWCBakeResolvedLayer OuterLayer = MakeResolvedLayer(
        RuntimeMesh,
        FName(TEXT("DWCTransparencyOuter")),
        MAX_int32 / 2,
        Layer.RaySettings.MaxRayDistance);
    OuterLayer.BakeTransform = ProviderSources.OuterBakeTransform;
    FSameMeshSurfaceCache SurfaceCache(RuntimeMesh, LODIndex, OuterLayer.BakeTransform);
    FString BuildError;
    if (!SurfaceCache.BuildSlotSurface(
            OuterLayer,
            DataUVChannelIndex,
            Layer.TargetSurface.OuterMaterialSlotIndex,
            Snapshot.OuterSurface,
            BuildError))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to build the transparency target surface: %s"),
            *BuildError);
        return false;
    }
    AppendImportedBasisWarning(
        Snapshot.OuterSurface,
        TEXT("Transparency target surface"),
        Snapshot.Warnings);

    FDWCRevealBakeTexelSamplingSettings SamplingSettings;
    SamplingSettings.Resolution = BakeResolution;
    SamplingSettings.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    int32 OverlappedPixelCount = 0;
    if (!FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            Snapshot.OuterSurface,
            SamplingSettings,
            Snapshot.OuterSamples,
            &BuildError,
            &OverlappedPixelCount))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to rasterize the transparency target UVs: %s"),
            *BuildError);
        return false;
    }
    if (Snapshot.OuterSamples.IsEmpty())
    {
        OutErrorMessage = TEXT("No outer texel samples were generated. Check the selected target slot and DWC Data UV channel.");
        return false;
    }

    TSet<int32> EligibleTriangleIDs;
    if (!BuildTargetWetPartEligibleTriangles(
            WetClothingAsset,
            LODIndex,
            Layer.TargetSurface.OuterMaterialSlotIndex,
            EligibleTriangleIDs,
            OutErrorMessage))
    {
        return false;
    }
    if (FilterSamplesToWetPartEligibility(Snapshot.OuterSamples, EligibleTriangleIDs) == 0)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Transparency Target Part slot %d Wet Part UV islands did not rasterize any DWC Data UV texels."),
            Layer.TargetSurface.OuterMaterialSlotIndex);
        return false;
    }

    if (Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        FDWCTransparencyType1SourceBindings SourceBindings;
        Snapshot.SourceSurfaces.Reserve(Layer.SameMeshSource.InnerSlotPriority.Num());
        for (int32 PriorityIndex = 0; PriorityIndex < Layer.SameMeshSource.InnerSlotPriority.Num(); ++PriorityIndex)
        {
            const FWetClothingTransparencyInnerSlot& InnerSlot =
                Layer.SameMeshSource.InnerSlotPriority[PriorityIndex];
            if (InnerSlot.MaterialSlotIndex == INDEX_NONE ||
                !RuntimeMesh->GetMaterials().IsValidIndex(InnerSlot.MaterialSlotIndex))
            {
                continue;
            }

            const FName SourceLayerId = MakeInnerSourceLayerId(PriorityIndex);
            const FDWCBakeResolvedLayer SourceLayer = MakeResolvedLayer(
                RuntimeMesh, SourceLayerId, PriorityIndex, Layer.RaySettings.MaxRayDistance);
            FDWCRevealBakeSurface SourceSurface;
            if (!SurfaceCache.BuildSlotSurface(
                    SourceLayer, InnerSlot.SourceUVChannel, InnerSlot.MaterialSlotIndex,
                    SourceSurface, BuildError))
            {
                Snapshot.Warnings.Add(FString::Printf(
                    TEXT("Inner Source Part '%s' was skipped: %s"),
                    *InnerSlot.MaterialSlotName.ToString(), *BuildError));
                continue;
            }
            AppendImportedBasisWarning(
                SourceSurface,
                FString::Printf(TEXT("Inner Source Part '%s'"), *InnerSlot.MaterialSlotName.ToString()),
                Snapshot.Warnings);

            FString BindingError;
            if (!FDWCTransparencyType1SourceProvider::AddValidatedBinding(
                    WetClothingAsset, InnerSlot, PriorityIndex, SourceSurface,
                    SourceBindings, BindingError))
            {
                OutErrorMessage = BindingError;
                return false;
            }

            FDWCTransparencySourceHitStats& Stats = Snapshot.SeedResult.SourceStats.AddDefaulted_GetRef();
            Stats.PriorityIndex = PriorityIndex;
            Stats.MaterialSlotIndex = InnerSlot.MaterialSlotIndex;
            Stats.MaterialSlotName = InnerSlot.MaterialSlotName;
            Snapshot.StatsIndexBySourceLayerId.Add(SourceLayerId, Snapshot.SeedResult.SourceStats.Num() - 1);
            Snapshot.PriorityBySourceLayerId.Add(SourceLayerId, PriorityIndex);

            SourceSurface.SkeletalMesh = nullptr;
            Snapshot.SourceSurfaces.Add(MoveTemp(SourceSurface));
        }
        if (SourceBindings.SurfacesBySourceLayerId.IsEmpty())
        {
            OutErrorMessage = TEXT("No valid Type 1 Inner Source Part could be prepared.");
            return false;
        }
        Snapshot.Warnings.Append(MoveTemp(SourceBindings.Warnings));
        Snapshot.SourceSurfacesByLayerId = MoveTemp(SourceBindings.SurfacesBySourceLayerId);
    }
    else
    {
        Snapshot.Warnings.Append(ProviderSources.Warnings);
        Snapshot.SourceSurfaces.Reserve(ProviderSources.Sources.Num());
        const int32 SourceColorResolution = Resolution;
        for (const FDWCTransparencyProjectionSource& Source : ProviderSources.Sources)
        {
            FDWCRevealBakeSurface SourceSurface;
            if (!BuildResolvedSlotSurface(
                    Source.Layer, LODIndex, Source.Layer.SourceUVChannel,
                    Source.MaterialSlotIndex, SourceSurface, BuildError))
            {
                Snapshot.Warnings.Add(FString::Printf(
                    TEXT("Source '%s' slot '%s' was skipped: %s"),
                    *Source.Layer.ComponentDisplayName.ToString(),
                    *Source.MaterialSlotName.ToString(), *BuildError));
                continue;
            }
            AppendImportedBasisWarning(
                SourceSurface,
                FString::Printf(
                    TEXT("Source '%s' slot '%s'"),
                    *Source.Layer.ComponentDisplayName.ToString(),
                    *Source.MaterialSlotName.ToString()),
                Snapshot.Warnings);
            if (Source.Layer.bCanBeRevealSource)
            {
                FString BakeError;
                TSharedPtr<const FDWCTransparencyMaterialColorBakeResult> SourceMaterialSurface =
                    FDWCTransparencyMaterialColorBakeCache::ResolveOrBake(
                        WetClothingAsset, *Source.Layer.SkeletalMesh, *Source.EffectiveMaterial,
                        Source.Layer.BakeTransform, Source.MaterialSlotIndex,
                        Source.Layer.SourceUVChannel, SourceColorResolution, BakeError);
                if (!SourceMaterialSurface.IsValid())
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Source '%s' slot '%s' could not bake Base Color after its surface was validated: %s"),
                        *Source.Layer.ComponentDisplayName.ToString(),
                        *Source.MaterialSlotName.ToString(), *BakeError);
                    return false;
                }
                Snapshot.SourceSurfacesByLayerId.Add(Source.Layer.LayerId, MoveTemp(SourceMaterialSurface));
                FDWCTransparencySourceHitStats& Stats = Snapshot.SeedResult.SourceStats.AddDefaulted_GetRef();
                Stats.PriorityIndex = Source.PriorityIndex;
                Stats.MaterialSlotIndex = Source.MaterialSlotIndex;
                Stats.MaterialSlotName = Source.MaterialSlotName;
                Snapshot.StatsIndexBySourceLayerId.Add(
                    Source.Layer.LayerId, Snapshot.SeedResult.SourceStats.Num() - 1);
                Snapshot.PriorityBySourceLayerId.Add(Source.Layer.LayerId, Source.PriorityIndex);
            }
            SourceSurface.SkeletalMesh = nullptr;
            Snapshot.SourceSurfaces.Add(MoveTemp(SourceSurface));
        }
    }
    if (Snapshot.SourceSurfaces.IsEmpty())
    {
        OutErrorMessage = TEXT("No eligible Inner Source Part surface could be built for transparency ray projection.");
        return false;
    }

    Snapshot.OuterSurface.SkeletalMesh = nullptr;
    Snapshot.ProjectionSettings.RayStartOffset = Layer.RaySettings.RayStartOffset;
    Snapshot.ProjectionSettings.RayLengthScale = 1.0f;
    Snapshot.ProjectionSettings.MinHitDistance = Layer.RaySettings.MinHitDistance;
    Snapshot.ProjectionSettings.bRespectSourceLayerOrder = true;
    Snapshot.ProjectionSettings.bRespectBlockers =
        Layer.SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
        Layer.SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh;
    Snapshot.ProjectionSettings.bPreferLowerSourceLayerOrder = true;
    Snapshot.ProjectionSettings.bRespectPerSourceMaxDistance = true;
    Snapshot.ProjectionSettings.bUseNormalAlignmentConfidence = false;
    Snapshot.RaySettings = Layer.RaySettings;

    FDWCTransparencySourcePayload& SeedResult = Snapshot.SeedResult;
    SeedResult.LayerGuid = Layer.LayerGuid;
    SeedResult.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    SeedResult.UVChannelIndex = DataUVChannelIndex;
    SeedResult.LODIndex = LODIndex;
    SeedResult.Resolution = BakeResolution;
    SeedResult.OuterSampleCount = Snapshot.OuterSamples.Num();
    SeedResult.OverlappedUVPixelCount = OverlappedPixelCount;
    SeedResult.InnerColorBuffer.Init(FColor::Black, PixelCount);
    SeedResult.RevealSurfaceAuthoring.Init(
        SeedResult.Resolution,
        FColor(128, 128, 0, 0));
    SeedResult.AutoAlphaBuffer.Init(0, PixelCount);
    SeedResult.OuterCoverageBuffer.Init(0, PixelCount);
    SeedResult.ValidHitBuffer.Init(false, PixelCount);
    SeedResult.HitDistanceBuffer.Init(0.0f, PixelCount);
    SeedResult.SourcePriorityBuffer.Init(INDEX_NONE, PixelCount);
    SeedResult.OuterIslandIDBuffer.Init(FDWCTransparencySourcePayload::InvalidOuterIslandID, PixelCount);

    const FDWCDataUVLODMetadata* DataUVMetadata = WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
    for (const FDWCRevealBakeTexelSample& Sample : Snapshot.OuterSamples)
    {
        if (Sample.Pixel.X >= 0 && Sample.Pixel.Y >= 0 &&
            Sample.Pixel.X < Resolution && Sample.Pixel.Y < Resolution)
        {
            const int32 PixelIndex = Sample.Pixel.Y * Resolution + Sample.Pixel.X;
            SeedResult.OuterCoverageBuffer[PixelIndex] = 1;
            uint16 EncodedIslandID = FDWCTransparencySourcePayload::InvalidOuterIslandID;
            if (!TryEncodeOuterSampleUVIslandID(
                    DataUVMetadata,
                    Sample,
                    EncodedIslandID,
                    OutErrorMessage))
            {
                return false;
            }
            SeedResult.OuterIslandIDBuffer[PixelIndex] = EncodedIslandID;
        }
    }

    FString MaterialBakeSignature;
    if (!FDWCTransparencySignatureService::BuildSourceSignature(
            WetClothingAsset,
            Layer,
            SeedResult.BuildSignature,
            MaterialBakeSignature,
            OutErrorMessage))
    {
        return false;
    }
    if (OverlappedPixelCount > 0)
    {
        Snapshot.Warnings.Add(FString::Printf(
            TEXT("The target DWC Data UV contains %d genuinely overlapping rasterized pixel(s). Rebuild and inspect the DWC Data UV before editing transparency."),
            OverlappedPixelCount));
    }

    Snapshot.EstimatedBytes =
        Snapshot.OuterSurface.Triangles.GetAllocatedSize() +
        Snapshot.OuterSamples.GetAllocatedSize() +
        SeedResult.InnerColorBuffer.GetAllocatedSize() +
        SeedResult.RevealSurfaceAuthoring.GetAllocatedBytes() +
        SeedResult.AutoAlphaBuffer.GetAllocatedSize() +
        SeedResult.OuterCoverageBuffer.GetAllocatedSize() +
        SeedResult.OuterIslandIDBuffer.GetAllocatedSize() +
        SeedResult.ValidHitBuffer.GetAllocatedSize() +
        SeedResult.HitDistanceBuffer.GetAllocatedSize() +
        SeedResult.SourcePriorityBuffer.GetAllocatedSize();
    for (const FDWCRevealBakeSurface& SourceSurface : Snapshot.SourceSurfaces)
    {
        Snapshot.EstimatedBytes += SourceSurface.Triangles.GetAllocatedSize();
    }
    TSet<const FDWCTransparencyMaterialColorBakeResult*> UniqueSourceSurfaces;
    for (const TPair<FName, TSharedPtr<const FDWCTransparencyMaterialColorBakeResult>>& Pair : Snapshot.SourceSurfacesByLayerId)
    {
        if (Pair.Value.IsValid() && !UniqueSourceSurfaces.Contains(Pair.Value.Get()))
        {
            UniqueSourceSurfaces.Add(Pair.Value.Get());
            Snapshot.EstimatedBytes += Pair.Value->AllocatedBytes;
        }
    }
    Snapshot.bValid = true;
    return true;
}

FDWCTransparencyAutoMapComputedResult FDWCTransparencyAutoMapGenerator::ComputeSameMeshSnapshot(
    FDWCTransparencyAutoMapSnapshot& SnapshotHandle,
    const FDWCEditorCancellationToken* CancellationToken)
{
    FDWCTransparencyAutoMapComputedResult Result;
    if (!SnapshotHandle.IsValid())
    {
        Result.Error = TEXT("The transparency projection snapshot is invalid.");
        return Result;
    }
    FDWCTransparencyAutoMapSnapshot::FImpl& Snapshot = *SnapshotHandle.Impl;
    Result.SourcePayload = MoveTemp(Snapshot.SeedResult);
    Result.Warnings = Snapshot.Warnings;
    const int32 Resolution = Result.SourcePayload.Resolution.X;

    TMap<int32, const FDWCRevealBakeSurfaceTriangle*> OuterTrianglesByID;
    BuildTriangleLookup(Snapshot.OuterSurface, OuterTrianglesByID);
    TMap<FName, TMap<int32, const FDWCRevealBakeSurfaceTriangle*>> SourceTrianglesByLayerID;
    for (const FDWCRevealBakeSurface& SourceSurface : Snapshot.SourceSurfaces)
    {
        BuildTriangleLookup(SourceSurface, SourceTrianglesByLayerID.FindOrAdd(SourceSurface.LayerId));
    }

    const auto ConsumeHit =
        [&Result, &Snapshot, &OuterTrianglesByID, &SourceTrianglesByLayerID, Resolution](
            const FDWCRevealBakeRayHit& Hit)
    {
        if (Hit.Pixel.X < 0 || Hit.Pixel.Y < 0 ||
            Hit.Pixel.X >= Resolution || Hit.Pixel.Y >= Resolution)
        {
            return;
        }
        FDWCTransparencySourcePayload& SourcePayload = Result.SourcePayload;
        const int32 PixelIndex = Hit.Pixel.Y * Resolution + Hit.Pixel.X;
        if (!Hit.bHit)
        {
            ++SourcePayload.NoHitCount;
            return;
        }

        ++SourcePayload.ValidHitCount;
        SourcePayload.ValidHitBuffer[PixelIndex] = true;
        SourcePayload.HitDistanceBuffer[PixelIndex] = Hit.Distance;
        SourcePayload.AutoAlphaBuffer[PixelIndex] = static_cast<uint8>(FMath::RoundToInt(
            CalculateAutoAlpha(Snapshot.RaySettings, Hit) * 255.0f));
        if (const int32* PriorityIndex = Snapshot.PriorityBySourceLayerId.Find(Hit.SourceLayerId))
        {
            SourcePayload.SourcePriorityBuffer[PixelIndex] = static_cast<int16>(
                FMath::Clamp(*PriorityIndex, 0, static_cast<int32>(MAX_int16)));
        }
        if (const int32* StatsIndex = Snapshot.StatsIndexBySourceLayerId.Find(Hit.SourceLayerId);
            StatsIndex != nullptr && SourcePayload.SourceStats.IsValidIndex(*StatsIndex))
        {
            ++SourcePayload.SourceStats[*StatsIndex].HitCount;
        }
        if (const TSharedPtr<const FDWCTransparencyMaterialColorBakeResult>* SourceSurface =
                Snapshot.SourceSurfacesByLayerId.Find(Hit.SourceLayerId);
            SourceSurface != nullptr && SourceSurface->IsValid())
        {
            SourcePayload.InnerColorBuffer[PixelIndex] =
                (*SourceSurface)->Sample(Hit.SourceUV).ToFColor(true);

            const TMap<int32, const FDWCRevealBakeSurfaceTriangle*>* SourceTriangles =
                SourceTrianglesByLayerID.Find(Hit.SourceLayerId);
            const FDWCRevealBakeSurfaceTriangle* const* SourceTriangle =
                SourceTriangles != nullptr ? SourceTriangles->Find(Hit.SourceTriangleIndex) : nullptr;
            const FDWCRevealBakeSurfaceTriangle* const* OuterTriangle =
                OuterTrianglesByID.Find(Hit.OuterTriangleIndex);
            if (SourceTriangle != nullptr && OuterTriangle != nullptr)
            {
                SourcePayload.RevealSurfaceAuthoring[PixelIndex] = EncodeReorientedRevealSurface(
                    **SourceSurface,
                    **SourceTriangle,
                    Hit.SourceBarycentric,
                    **OuterTriangle,
                    Hit.OuterBarycentric,
                    Hit.SourceUV);
            }
        }
        else
        {
            // BuildSameMeshSnapshot rejects missing exact source bakes. This is
            // only a defensive guard for stale/corrupt snapshots.
            SourcePayload.InnerColorBuffer[PixelIndex] = FColor::Black;
        }
    };

    FString ProjectionError;
    if (!FDWCRevealBakeRayProjector::ProjectSamplesToSources(
            Snapshot.OuterSurface,
            Snapshot.SourceSurfaces,
            Snapshot.OuterSamples,
            Snapshot.ProjectionSettings,
            ConsumeHit,
            &ProjectionError,
            CancellationToken))
    {
        Result.bCanceled = CancellationToken != nullptr && CancellationToken->IsCanceled();
        Result.Error = ProjectionError;
        return Result;
    }

    Result.Summary = FString::Printf(
        TEXT("Generated %d outer samples: %d valid hit(s), %d no-hit sample(s)."),
        Result.SourcePayload.OuterSampleCount,
        Result.SourcePayload.ValidHitCount,
        Result.SourcePayload.NoHitCount);
    Result.ResultBytes =
        Result.SourcePayload.InnerColorBuffer.GetAllocatedSize() +
        Result.SourcePayload.RevealSurfaceAuthoring.GetAllocatedBytes() +
        Result.SourcePayload.AutoAlphaBuffer.GetAllocatedSize() +
        Result.SourcePayload.OuterCoverageBuffer.GetAllocatedSize() +
        Result.SourcePayload.OuterIslandIDBuffer.GetAllocatedSize() +
        Result.SourcePayload.ValidHitBuffer.GetAllocatedSize() +
        Result.SourcePayload.HitDistanceBuffer.GetAllocatedSize() +
        Result.SourcePayload.SourcePriorityBuffer.GetAllocatedSize();
    Result.bSucceeded = true;
    return Result;
}
