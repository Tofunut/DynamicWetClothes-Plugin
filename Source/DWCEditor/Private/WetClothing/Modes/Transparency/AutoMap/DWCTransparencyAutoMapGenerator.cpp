//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyTopologyCache.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyResolutionResolver.h"

#include "DataAssets/DWCBakeLayer.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingPartData.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Resources/DWCEditorAccountedMemory.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyStage2PhaseOperation.h"
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialColorBakeCache.h"
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialBakeResolutionResolver.h"
#include "WetClothing/Modes/Transparency/Diagnostics/DWCTransparencyBaselineDiagnostics.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyProjectionSourceProvider.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeProjection.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

namespace
{
    TAtomic<uint64> GNextTransparencyStage2OperationId{1};

    uint64 SaturatingMultiply(const uint64 A, const uint64 B)
    {
        return A != 0 && B > MAX_uint64 / A ? MAX_uint64 : A * B;
    }

    uint64 EstimatePayloadBytes(const FIntPoint Resolution)
    {
        const uint64 PixelCount = SaturatingMultiply(
            static_cast<uint64>(FMath::Max(Resolution.X, 0)),
            static_cast<uint64>(FMath::Max(Resolution.Y, 0)));
        // Color + reveal surface + alpha + coverage + island + valid bit +
        // distance + priority. Container overhead is covered by one cache line.
        const uint64 WholeByteChannels = SaturatingMultiply(PixelCount, 18ull);
        const uint64 ValidBits = (PixelCount + 7ull) / 8ull;
        return WholeByteChannels > MAX_uint64 - ValidBits - 64ull
            ? MAX_uint64
            : WholeByteChannels + ValidBits + 64ull;
    }

    uint64 EstimateSurfaceBytes(const int32 TriangleCount)
    {
        return SaturatingMultiply(
            static_cast<uint64>(FMath::Max(TriangleCount, 0)),
            static_cast<uint64>(sizeof(FDWCRevealBakeSurfaceTriangle)));
    }

    int32 CountMeshTriangles(
        const USkeletalMesh* Mesh,
        const int32 MaterialSlotIndex = INDEX_NONE)
    {
        const FSkeletalMeshRenderData* RenderData =
            Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
        {
            return 0;
        }
        int64 TriangleCount = 0;
        for (const FSkelMeshRenderSection& Section : RenderData->LODRenderData[0].RenderSections)
        {
            if (MaterialSlotIndex == INDEX_NONE || Section.MaterialIndex == MaterialSlotIndex)
            {
                TriangleCount += Section.NumTriangles;
            }
        }
        return static_cast<int32>(FMath::Min<int64>(TriangleCount, MAX_int32));
    }

    uint64 EstimateRasterSampleBytes(
        const FDWCRevealBakeSurface& Surface,
        const FIntPoint Resolution)
    {
        double CoveredTexels = 0.0;
        const double PixelCount = static_cast<double>(FMath::Max(Resolution.X, 0)) *
            static_cast<double>(FMath::Max(Resolution.Y, 0));
        for (const FDWCRevealBakeSurfaceTriangle& Triangle : Surface.Triangles)
        {
            const FVector2D EdgeA = Triangle.UVs[1] - Triangle.UVs[0];
            const FVector2D EdgeB = Triangle.UVs[2] - Triangle.UVs[0];
            const double UVArea = FMath::Abs(EdgeA.X * EdgeB.Y - EdgeA.Y * EdgeB.X) * 0.5;
            const FVector2D EdgeC = Triangle.UVs[2] - Triangle.UVs[1];
            const double EdgeTexels =
                EdgeA.Size() * FMath::Max(Resolution.X, Resolution.Y) +
                EdgeB.Size() * FMath::Max(Resolution.X, Resolution.Y) +
                EdgeC.Size() * FMath::Max(Resolution.X, Resolution.Y);
            CoveredTexels += UVArea * PixelCount + EdgeTexels * 0.5 + 1.0;
        }
        // Conservative sub-pixel/edge allowance without reserving the entire
        // square texture for sparse target slots.
        const uint64 EstimatedSamples = static_cast<uint64>(FMath::CeilToDouble(
            FMath::Min(PixelCount, CoveredTexels * 1.25 + Surface.Triangles.Num() * 2.0)));
        return SaturatingMultiply(EstimatedSamples, sizeof(FDWCRevealBakeTexelSample));
    }

    FDWCEditorAsyncOperationIdentity MakeStage2Owner(
        const FDWCTransparencyStage2ExecutionOptions& Options,
        const FWetClothingTransparencyLayerData& Layer)
    {
        if (Options.ResourceOwner.IsValid())
        {
            return Options.ResourceOwner;
        }
        FDWCEditorAsyncOperationIdentity Owner;
        Owner.Key.Namespace = TEXT("DWC.Transparency.Stage2");
        Owner.Key.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
        Owner.Key.ResourceGuid = Layer.LayerGuid;
        Owner.SessionEpoch = FGuid::NewGuid();
        Owner.OperationId = GNextTransparencyStage2OperationId.IncrementExchange();
        Owner.Generation = 1;
        Owner.Domain = EDWCEditorAuthoringDomain::Transparency;
        return Owner;
    }

    void ReportGenerationProgress(
        const FDWCTransparencyGenerationProgressCallback* ProgressCallback,
        const EDWCTransparencyGenerationPhase Phase,
        const double OverallFraction,
        const int32 CompletedItems = 0,
        const int32 TotalItems = 0,
        const FName SourceName = NAME_None,
        const int32 MaterialSlotIndex = INDEX_NONE)
    {
        if (ProgressCallback == nullptr)
        {
            return;
        }

        FDWCTransparencyGenerationProgress Progress;
        Progress.Phase = Phase;
        Progress.OverallFraction = FMath::Clamp(OverallFraction, 0.0, 1.0);
        Progress.CompletedItems = FMath::Max(0, CompletedItems);
        Progress.TotalItems = FMath::Max(0, TotalItems);
        Progress.SourceName = SourceName;
        Progress.MaterialSlotIndex = MaterialSlotIndex;
        (*ProgressCallback)(Progress);
    }

    bool AbortCanceledGeneration(
        const FDWCEditorCancellationToken* CancellationToken,
        FString& OutErrorMessage)
    {
        if (CancellationToken == nullptr || !CancellationToken->IsCanceled())
        {
            return false;
        }

        OutErrorMessage = TEXT("Transparency ray projection was canceled.");
        return true;
    }

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

    bool BuildResolvedSlotSurface(
        const UWetClothingAsset& OwnerAsset,
        const FDWCBakeResolvedLayer& Layer,
        const int32 LODIndex,
        const int32 UVChannelIndex,
        const int32 MaterialSlotIndex,
        const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
        FDWCRevealBakeSurface& OutSurface,
        FString& OutError)
    {
        return FDWCTransparencyTopologyCache::BuildSlotSurface(
            OwnerAsset,
            Layer,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            CacheStore,
            OutSurface,
            OutError);
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
        const FDWCEditorUVTopologyHandle OriginalTopologyHandle =
            WetClothingAsset.AcquireOriginalUVTopologyForLOD(LODIndex, &OutErrorMessage);
        const FDWCEditorUVTopologyData* OriginalTopology = OriginalTopologyHandle.Get();
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

    bool AnalyzeSourceSurface(
        const FDWCRevealBakeSurface& Surface,
        bool& bOutHasUsableUV)
    {
        bool bHasUsableGeometry = false;
        bOutHasUsableUV = false;
        for (const FDWCRevealBakeSurfaceTriangle& Triangle : Surface.Triangles)
        {
            const bool bFiniteGeometry =
                !Triangle.Positions[0].ContainsNaN() &&
                !Triangle.Positions[1].ContainsNaN() &&
                !Triangle.Positions[2].ContainsNaN() &&
                !Triangle.Normals[0].ContainsNaN() &&
                !Triangle.Normals[1].ContainsNaN() &&
                !Triangle.Normals[2].ContainsNaN();
            const bool bFiniteUV =
                FMath::IsFinite(Triangle.UVs[0].X) && FMath::IsFinite(Triangle.UVs[0].Y) &&
                FMath::IsFinite(Triangle.UVs[1].X) && FMath::IsFinite(Triangle.UVs[1].Y) &&
                FMath::IsFinite(Triangle.UVs[2].X) && FMath::IsFinite(Triangle.UVs[2].Y);
            if (!bFiniteGeometry || !bFiniteUV)
            {
                continue;
            }

            const FVector Edge01 = Triangle.Positions[1] - Triangle.Positions[0];
            const FVector Edge02 = Triangle.Positions[2] - Triangle.Positions[0];
            bHasUsableGeometry |=
                FVector::CrossProduct(Edge01, Edge02).SizeSquared() > UE_SMALL_NUMBER;
            const FVector2D UVEdge01 = Triangle.UVs[1] - Triangle.UVs[0];
            const FVector2D UVEdge02 = Triangle.UVs[2] - Triangle.UVs[0];
            bOutHasUsableUV |=
                FMath::Abs(UVEdge01.X * UVEdge02.Y - UVEdge01.Y * UVEdge02.X) >
                UE_SMALL_NUMBER;
        }
        return !Surface.Triangles.IsEmpty() && bHasUsableGeometry;
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
    FString& OutErrorMessage,
    const TSharedPtr<FDWCEditorCacheStore>& CacheStore)
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
    FDWCRevealBakeSurface OuterSurface;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    if (!FDWCTransparencyTopologyCache::BuildSlotSurface(
            WetClothingAsset,
            OuterLayer,
            LODIndex,
            DataUVChannelIndex,
            TargetSurface.OuterMaterialSlotIndex,
            CacheStore,
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

    TArray<int32> UVIslandIDs;
    int32 EligibleSampleCount = 0;
    int32 OverlappedPixelCount = 0;
    if (!FDWCRevealBakeTexelSampler::BuildOuterTexelMaskBuffers(
            OuterSurface,
            SamplingSettings,
            EligibleTriangleIDs,
            OutCoverageBuffer,
            UVIslandIDs,
            EligibleSampleCount,
            &OutErrorMessage,
            &OverlappedPixelCount))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to rasterize Transparency Target Part slot %d Wet Part UV islands: %s"),
            TargetSurface.OuterMaterialSlotIndex,
            *OutErrorMessage);
        return false;
    }

    const int32 PixelCount = UVIslandIDs.Num();
    OutIslandIDBuffer.Init(FDWCTransparencySourcePayload::InvalidOuterIslandID, PixelCount);
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const int32 UVIslandID = UVIslandIDs[PixelIndex];
        if (UVIslandID == INDEX_NONE)
        {
            continue;
        }
        if (!FDWCTransparencySourcePayload::CanEncodeOuterIslandID(UVIslandID))
        {
            OutCoverageBuffer.Reset();
            OutIslandIDBuffer.Reset();
            OutErrorMessage = FString::Printf(
                TEXT("Transparency target UV island ID %d exceeds the supported range [0, %u]."),
                UVIslandID,
                static_cast<uint32>(FDWCTransparencySourcePayload::InvalidOuterIslandID) - 1u);
            return false;
        }
        OutIslandIDBuffer[PixelIndex] =
            FDWCTransparencySourcePayload::EncodeOuterIslandID(UVIslandID);
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
    TArray<FDWCTransparencyBrushSample> DecodedSamples;
    for (const FDWCTransparencyRevealColorStroke& Stroke : Strokes)
    {
        if (!Stroke.bEnabled || Stroke.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
        const FLinearColor PaintColor = Stroke.PaintColor.CopyWithNewOpacity(1.0f);
        Stroke.DecodeSamples(DecodedSamples);
        for (const FDWCTransparencyBrushSample& Sample : DecodedSamples)
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
    TArray<FString>& OutWarnings,
    const TSharedPtr<FDWCEditorCacheStore>& CacheStore)
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

    const FDWCTransparencyResolvedOutputResolution ResolvedOutputResolution =
        FDWCTransparencyResolutionResolver::Resolve(WetClothingAsset, Layer);
    if (!ResolvedOutputResolution.IsValid())
    {
        OutSummary = TEXT("Could not resolve the Transparency output resolution.");
        return false;
    }
    const int32 Resolution = ResolvedOutputResolution.Size;
    constexpr int32 LODIndex = 0;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    const FIntPoint BakeResolution(Resolution, Resolution);
    const int32 PixelCount = Resolution * Resolution;

    const FDWCBakeResolvedLayer OuterLayer = MakeResolvedLayer(
        RuntimeMesh,
        FName(TEXT("DWCTransparencyManualOuter")),
        MAX_int32 / 2,
        0.0f);
    FDWCRevealBakeSurface OuterSurface;
    FString BuildError;
    if (!FDWCTransparencyTopologyCache::BuildSlotSurface(
            WetClothingAsset,
            OuterLayer,
            LODIndex,
            DataUVChannelIndex,
            Layer.TargetSurface.OuterMaterialSlotIndex,
            CacheStore,
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
    OutResult.OutputResolutionIdentity = ResolvedOutputResolution.Identity;
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
        OutResult.OuterCoverageBuffer[PixelIndex] = Sample.Coverage;
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

    FString SignatureError;
    if (!FDWCTransparencySignatureService::BuildSourceSignature(
            WetClothingAsset,
            Layer,
            ResolvedOutputResolution,
            OutResult.BuildSignature,
            OutResult.MaterialBakeSignature,
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
    TArray<FString>& OutWarnings,
    const FDWCTransparencyStage2ExecutionOptions& Options)
{
    OutResult = FDWCTransparencySourcePayload();
    OutSummary.Reset();
    OutWarnings.Reset();

    TSharedPtr<FDWCEditorResourceGovernor> Governor = Options.ResourceGovernor;
    if (!Governor.IsValid() && !Options.bResourcesOwnedByCaller)
    {
        Governor = FDWCEditorResourceBroker::Get()->GetResourceGovernor();
    }
    const FDWCEditorAsyncOperationIdentity ResourceOwner = MakeStage2Owner(Options, Layer);
    FDWCTransparencyStage2PhaseOperation PhaseOperation(
        Governor,
        ResourceOwner,
        Options.bResourcesOwnedByCaller);
    const auto PublishPhaseSnapshot = [&Options, &PhaseOperation]()
    {
        if (Options.OutPhaseGraphSnapshot != nullptr)
        {
            *Options.OutPhaseGraphSnapshot = PhaseOperation.GetSnapshot();
        }
    };

    FDWCTransparencyAutoMapSnapshot Snapshot;
    FString SnapshotError;
    if (!BuildProjectionSnapshotInternal(
            WetClothingAsset,
            Layer,
            Snapshot,
            SnapshotError,
            Options.BlueprintHierarchy,
            Options.CancellationToken,
            Options.ProgressCallback,
            &PhaseOperation,
            Options.CacheStore))
    {
        if (Options.CancellationToken != nullptr && Options.CancellationToken->IsCanceled())
        {
            PhaseOperation.Cancel(SnapshotError);
        }
        else
        {
            PhaseOperation.Fail(SnapshotError);
        }
        PublishPhaseSnapshot();
        OutSummary = MoveTemp(SnapshotError);
        return false;
    }

    FDWCTransparencyAutoMapComputedResult Computed =
        ComputeStreamingProjection(
            WetClothingAsset,
            Snapshot,
            Options.CancellationToken,
            Options.ProgressCallback,
            &PhaseOperation);
    OutWarnings = MoveTemp(Computed.Warnings);
    if (!Computed.bSucceeded)
    {
        if (Computed.bCanceled)
        {
            PhaseOperation.Cancel(Computed.Error);
        }
        else
        {
            PhaseOperation.Fail(Computed.Error);
        }
        PublishPhaseSnapshot();
        OutSummary = Computed.Error.IsEmpty()
            ? TEXT("Transparency ray projection failed.")
            : MoveTemp(Computed.Error);
        return false;
    }

    OutResult = MoveTemp(Computed.SourcePayload);
    OutSummary = MoveTemp(Computed.Summary);
    const uint64 ResultBytes = FMath::Max<uint64>(OutResult.GetAllocatedBytes(), 1);
    FDWCTransparencyStage2PhaseResources TransferResources;
    TransferResources.WorkerPeakBytes = Snapshot.GetEstimatedBytes();
    TransferResources.PreviewPeakBytes = ResultBytes;
    TransferResources.PreviewRetainedBytes = ResultBytes;
    FString PhaseError;
    if (!PhaseOperation.BeginPhase(
            EDWCTransparencyStage2OperationPhase::TransferResult,
            TransferResources,
            PhaseError) ||
        !PhaseOperation.Complete(PhaseError))
    {
        PhaseOperation.Fail(PhaseError);
        PublishPhaseSnapshot();
        OutSummary = FString::Printf(
            TEXT("Transparency Stage 2 could not transfer its result ownership: %s"),
            *PhaseError);
        OutResult = {};
        return false;
    }

    if (Governor.IsValid() && !Options.bResourcesOwnedByCaller)
    {
        FDWCEditorMemoryLease ResultLease = PhaseOperation.TakeRetainedLease(
            EDWCEditorResourcePool::PreviewWorkspaceCPU);
        TSharedPtr<FDWCEditorAccountedMemory, ESPMode::ThreadSafe> Account =
            MakeShared<FDWCEditorAccountedMemory, ESPMode::ThreadSafe>();
        Account->Configure(
            Governor,
            EDWCEditorResourcePool::PreviewWorkspaceCPU,
            ResourceOwner,
            TEXT("Transparency Stage 2 canonical source payload"));
        if (!Account->AdoptExistingLease(MoveTemp(ResultLease), ResultBytes, &PhaseError))
        {
            PublishPhaseSnapshot();
            OutSummary = FString::Printf(
                TEXT("Transparency Stage 2 could not retain its result payload: %s"),
                *PhaseError);
            OutResult = {};
            return false;
        }
        OutResult.PersistentMemoryAccount = MoveTemp(Account);
    }
    ReportGenerationProgress(
        Options.ProgressCallback,
        EDWCTransparencyGenerationPhase::CommittingResult,
        1.0);
    PublishPhaseSnapshot();
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

    const FDWCTransparencyResolvedOutputResolution ResolvedOutputResolution =
        FDWCTransparencyResolutionResolver::Resolve(WetClothingAsset, Layer);
    if (!ResolvedOutputResolution.IsValid())
    {
        OutErrorMessage = TEXT("Could not resolve the Transparency output resolution.");
        return false;
    }
    const int32 Resolution = ResolvedOutputResolution.Size;
    OutResult.LayerGuid = Layer.LayerGuid;
    OutResult.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    OutResult.UVChannelIndex = DataUVChannelIndex;
    OutResult.LODIndex = LODIndex;
    OutResult.Resolution = FIntPoint(Resolution, Resolution);
    OutResult.OutputResolutionIdentity = ResolvedOutputResolution.Identity;
    return FDWCTransparencySignatureService::BuildSourceSignature(
        WetClothingAsset,
        Layer,
        ResolvedOutputResolution,
        OutResult.BuildSignature,
        OutResult.MaterialBakeSignature,
        OutErrorMessage);
}

struct FDWCTransparencyAutoMapSnapshot::FImpl
{
    struct FMaterialSourceDescriptor
    {
        TObjectPtr<USkeletalMesh> SourceMesh = nullptr;
        TObjectPtr<UMaterialInterface> EffectiveMaterial = nullptr;
        FTransform BakeTransform = FTransform::Identity;
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 SourceUVChannel = 0;
        int32 SourceBakeResolution = 0;
        FName ComponentDisplayName;
        FName MaterialSlotName;
        bool bHasUsableUV = false;
    };

    FDWCRevealBakeSurface OuterSurface;
    TArray<FDWCRevealBakeSurface> SourceSurfaces;
    TArray<FDWCRevealBakeTexelSample> OuterSamples;
    FDWCRevealBakeRayProjectionSettings ProjectionSettings;
    FWetClothingTransparencyRaySettings RaySettings;
    TMap<FName, int32> PriorityBySourceLayerId;
    TMap<FName, int32> StatsIndexBySourceLayerId;
    TMap<FName, FMaterialSourceDescriptor> MaterialSourceDescriptorsByLayerId;
    TSharedPtr<FDWCTransparencyProjectionObjectLease> ProjectionObjectLease;
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

uint64 FDWCTransparencyAutoMapSnapshot::GetEstimatedBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedBytes : 0;
}

bool FDWCTransparencyAutoMapGenerator::BuildProjectionSnapshotInternal(
    UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyAutoMapSnapshot& OutSnapshot,
    FString& OutErrorMessage,
    const FDWCTransparencyBlueprintHierarchy* BlueprintHierarchy,
    const FDWCEditorCancellationToken* CancellationToken,
    const FDWCTransparencyGenerationProgressCallback* ProgressCallback,
    FDWCTransparencyStage2PhaseOperation* PhaseOperation,
    const TSharedPtr<FDWCEditorCacheStore>& CacheStore)
{
    check(IsInGameThread());
    ReportGenerationProgress(
        ProgressCallback, EDWCTransparencyGenerationPhase::PreparingTarget, 0.0);
    if (AbortCanceledGeneration(CancellationToken, OutErrorMessage))
    {
        return false;
    }
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

    const FDWCTransparencyResolvedOutputResolution ResolvedOutputResolution =
        FDWCTransparencyResolutionResolver::Resolve(WetClothingAsset, Layer);
    if (!ResolvedOutputResolution.IsValid())
    {
        OutErrorMessage = TEXT("Could not resolve the Transparency output resolution.");
        return false;
    }
    const int32 Resolution = ResolvedOutputResolution.Size;
    constexpr int32 LODIndex = 0;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    const FIntPoint BakeResolution(Resolution, Resolution);
    const int32 PixelCount = Resolution * Resolution;

    if (PhaseOperation != nullptr)
    {
        FDWCTransparencyStage2PhaseResources Resources;
        Resources.WorkerPeakBytes = FMath::Max<uint64>(
            EstimateSurfaceBytes(CountMeshTriangles(
                RuntimeMesh)),
            1);
        Resources.WorkerRetainedBytes = Resources.WorkerPeakBytes;
        if (!PhaseOperation->BeginPhase(
                EDWCTransparencyStage2OperationPhase::PrepareTarget,
                Resources,
                OutErrorMessage))
        {
            return false;
        }
    }

    FDWCTransparencyProjectionSourceSet ProviderSources;
    if (Layer.SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        const bool bBuiltSources = BlueprintHierarchy != nullptr
            ? FDWCTransparencyProjectionSourceProvider::BuildBlueprintSources(
                WetClothingAsset,
                Layer,
                *BlueprintHierarchy,
                ProviderSources,
                OutErrorMessage)
            : FDWCTransparencyProjectionSourceProvider::BuildBlueprintSources(
                WetClothingAsset,
                Layer,
                ProviderSources,
                OutErrorMessage);
        if (!bBuiltSources)
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
    Snapshot.ProjectionObjectLease = ProviderSources.ObjectLease;
    ReportGenerationProgress(
        ProgressCallback,
        EDWCTransparencyGenerationPhase::PreparingTarget,
        0.04);
    if (AbortCanceledGeneration(CancellationToken, OutErrorMessage))
    {
        return false;
    }

    FDWCBakeResolvedLayer OuterLayer = MakeResolvedLayer(
        RuntimeMesh,
        FName(TEXT("DWCTransparencyOuter")),
        MAX_int32 / 2,
        Layer.RaySettings.MaxRayDistance);
    OuterLayer.BakeTransform = ProviderSources.OuterBakeTransform;
    FString BuildError;
    if (!FDWCTransparencyTopologyCache::BuildSlotSurface(
            WetClothingAsset,
            OuterLayer,
            LODIndex,
            DataUVChannelIndex,
            Layer.TargetSurface.OuterMaterialSlotIndex,
            CacheStore,
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
    ReportGenerationProgress(
        ProgressCallback,
        EDWCTransparencyGenerationPhase::RasterizingTarget,
        0.08);
    if (AbortCanceledGeneration(CancellationToken, OutErrorMessage))
    {
        return false;
    }

    FDWCRevealBakeTexelSamplingSettings SamplingSettings;
    SamplingSettings.Resolution = BakeResolution;
    SamplingSettings.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    if (PhaseOperation != nullptr)
    {
        const uint64 BaseSurfaceCacheBytes = EstimateSurfaceBytes(
            CountMeshTriangles(RuntimeMesh));
        const uint64 TargetSurfaceBytes = Snapshot.OuterSurface.Triangles.GetAllocatedSize();
        FDWCTransparencyStage2PhaseResources Resources;
        Resources.WorkerPeakBytes = BaseSurfaceCacheBytes + TargetSurfaceBytes +
            EstimateRasterSampleBytes(Snapshot.OuterSurface, BakeResolution);
        Resources.WorkerRetainedBytes = Resources.WorkerPeakBytes;
        if (!PhaseOperation->BeginPhase(
                EDWCTransparencyStage2OperationPhase::RasterizeTarget,
                Resources,
                OutErrorMessage))
        {
            return false;
        }
    }
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
    ReportGenerationProgress(
        ProgressCallback,
        EDWCTransparencyGenerationPhase::PreparingSources,
        0.18,
        0,
        Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots
            ? Layer.SameMeshSource.InnerSlotPriority.Num()
            : ProviderSources.Sources.Num());
    if (AbortCanceledGeneration(CancellationToken, OutErrorMessage))
    {
        return false;
    }

    if (PhaseOperation != nullptr)
    {
        uint64 SourceSurfaceEstimate = 0;
        uint64 BuildCacheEstimate = EstimateSurfaceBytes(CountMeshTriangles(RuntimeMesh));
        if (Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
        {
            for (const FWetClothingTransparencyInnerSlot& InnerSlot :
                 Layer.SameMeshSource.InnerSlotPriority)
            {
                SourceSurfaceEstimate += EstimateSurfaceBytes(CountMeshTriangles(
                    RuntimeMesh, InnerSlot.MaterialSlotIndex));
            }
        }
        else
        {
            BuildCacheEstimate = 0;
            for (const FDWCTransparencyProjectionSource& Source : ProviderSources.Sources)
            {
                SourceSurfaceEstimate += EstimateSurfaceBytes(CountMeshTriangles(
                    Source.Layer.SkeletalMesh));
            }
        }
        const uint64 PreparedTargetBytes =
            Snapshot.OuterSurface.Triangles.GetAllocatedSize() +
            Snapshot.OuterSamples.GetAllocatedSize();
        FDWCTransparencyStage2PhaseResources Resources;
        Resources.WorkerPeakBytes = BuildCacheEstimate + PreparedTargetBytes + SourceSurfaceEstimate +
            EstimatePayloadBytes(BakeResolution);
        Resources.WorkerRetainedBytes = Resources.WorkerPeakBytes;
        if (!PhaseOperation->BeginPhase(
                EDWCTransparencyStage2OperationPhase::PrepareSources,
                Resources,
                OutErrorMessage))
        {
            return false;
        }
    }

    if (Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        Snapshot.SourceSurfaces.Reserve(Layer.SameMeshSource.InnerSlotPriority.Num());
        for (int32 PriorityIndex = 0; PriorityIndex < Layer.SameMeshSource.InnerSlotPriority.Num(); ++PriorityIndex)
        {
            if (AbortCanceledGeneration(CancellationToken, OutErrorMessage))
            {
                return false;
            }
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
            if (!FDWCTransparencyTopologyCache::BuildSlotSurface(
                    WetClothingAsset,
                    SourceLayer,
                    LODIndex,
                    InnerSlot.SourceUVChannel,
                    InnerSlot.MaterialSlotIndex,
                    CacheStore,
                    SourceSurface,
                    BuildError))
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

            USkeletalMesh* OriginalSourceMesh = WetClothingAsset.GetSourceSkeletalMesh();
            UMaterialInterface* EffectiveMaterial =
                OriginalSourceMesh != nullptr &&
                    OriginalSourceMesh->GetMaterials().IsValidIndex(InnerSlot.MaterialSlotIndex)
                ? OriginalSourceMesh->GetMaterials()[InnerSlot.MaterialSlotIndex].MaterialInterface
                : nullptr;
            bool bHasUsableUV = false;
            if (!AnalyzeSourceSurface(SourceSurface, bHasUsableUV))
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Inner Source Part '%s' has no usable LOD 0 surface triangles."),
                    *InnerSlot.MaterialSlotName.ToString());
                return false;
            }
            if (OriginalSourceMesh == nullptr || EffectiveMaterial == nullptr)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Inner Source Part '%s' has no effective source material."),
                    *InnerSlot.MaterialSlotName.ToString());
                return false;
            }

            const FDWCTransparencyResolvedMaterialBakeResolution SourceBakeResolution =
                FDWCTransparencyMaterialBakeResolutionResolver::Resolve(EffectiveMaterial);
            FDWCTransparencyAutoMapSnapshot::FImpl::FMaterialSourceDescriptor Descriptor;
            Descriptor.SourceMesh = OriginalSourceMesh;
            Descriptor.EffectiveMaterial = EffectiveMaterial;
            Descriptor.MaterialSlotIndex = InnerSlot.MaterialSlotIndex;
            Descriptor.SourceUVChannel = InnerSlot.SourceUVChannel;
            Descriptor.SourceBakeResolution = SourceBakeResolution.Resolution;
            Descriptor.ComponentDisplayName = OriginalSourceMesh->GetFName();
            Descriptor.MaterialSlotName = InnerSlot.MaterialSlotName;
            Descriptor.bHasUsableUV = bHasUsableUV;
            Snapshot.MaterialSourceDescriptorsByLayerId.Add(SourceLayerId, MoveTemp(Descriptor));

            FDWCTransparencySourceHitStats& Stats = Snapshot.SeedResult.SourceStats.AddDefaulted_GetRef();
            Stats.PriorityIndex = PriorityIndex;
            Stats.MaterialSlotIndex = InnerSlot.MaterialSlotIndex;
            Stats.MaterialSlotName = InnerSlot.MaterialSlotName;
            Stats.SourceBakeResolution = SourceBakeResolution.Resolution;
            Snapshot.StatsIndexBySourceLayerId.Add(SourceLayerId, Snapshot.SeedResult.SourceStats.Num() - 1);
            Snapshot.PriorityBySourceLayerId.Add(SourceLayerId, PriorityIndex);

            SourceSurface.SkeletalMesh = nullptr;
            Snapshot.SourceSurfaces.Add(MoveTemp(SourceSurface));
        }
        if (Snapshot.MaterialSourceDescriptorsByLayerId.IsEmpty())
        {
            OutErrorMessage = TEXT("No valid Type 1 Inner Source Part could be prepared.");
            return false;
        }
    }
    else
    {
        Snapshot.Warnings.Append(ProviderSources.Warnings);
        Snapshot.SourceSurfaces.Reserve(ProviderSources.Sources.Num());
        for (const FDWCTransparencyProjectionSource& Source : ProviderSources.Sources)
        {
            if (AbortCanceledGeneration(CancellationToken, OutErrorMessage))
            {
                return false;
            }
            FDWCRevealBakeSurface SourceSurface;
            if (!BuildResolvedSlotSurface(
                    WetClothingAsset,
                    Source.Layer,
                    LODIndex,
                    Source.Layer.SourceUVChannel,
                    Source.MaterialSlotIndex,
                    CacheStore,
                    SourceSurface,
                    BuildError))
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
                bool bHasUsableUV = false;
                if (!AnalyzeSourceSurface(SourceSurface, bHasUsableUV))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Source '%s' slot '%s' has no usable LOD 0 surface triangles."),
                        *Source.Layer.ComponentDisplayName.ToString(),
                        *Source.MaterialSlotName.ToString());
                    return false;
                }
                if (Source.Layer.SkeletalMesh == nullptr || Source.EffectiveMaterial == nullptr)
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Source '%s' slot '%s' has no effective source material."),
                        *Source.Layer.ComponentDisplayName.ToString(),
                        *Source.MaterialSlotName.ToString());
                    return false;
                }
                const FDWCTransparencyResolvedMaterialBakeResolution SourceBakeResolution =
                    FDWCTransparencyMaterialBakeResolutionResolver::Resolve(
                        Source.EffectiveMaterial);
                FDWCTransparencyAutoMapSnapshot::FImpl::FMaterialSourceDescriptor Descriptor;
                Descriptor.SourceMesh = Source.Layer.SkeletalMesh;
                Descriptor.EffectiveMaterial = Source.EffectiveMaterial;
                Descriptor.BakeTransform = Source.Layer.BakeTransform;
                Descriptor.MaterialSlotIndex = Source.MaterialSlotIndex;
                Descriptor.SourceUVChannel = Source.Layer.SourceUVChannel;
                Descriptor.SourceBakeResolution = SourceBakeResolution.Resolution;
                Descriptor.ComponentDisplayName = Source.Layer.ComponentDisplayName;
                Descriptor.MaterialSlotName = Source.MaterialSlotName;
                Descriptor.bHasUsableUV = bHasUsableUV;
                Snapshot.MaterialSourceDescriptorsByLayerId.Add(
                    Source.Layer.LayerId, MoveTemp(Descriptor));
                FDWCTransparencySourceHitStats& Stats = Snapshot.SeedResult.SourceStats.AddDefaulted_GetRef();
                Stats.PriorityIndex = Source.PriorityIndex;
                Stats.MaterialSlotIndex = Source.MaterialSlotIndex;
                Stats.MaterialSlotName = Source.MaterialSlotName;
                Stats.SourceBakeResolution = SourceBakeResolution.Resolution;
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
    SeedResult.OutputResolutionIdentity = ResolvedOutputResolution.Identity;
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
            SeedResult.OuterCoverageBuffer[PixelIndex] = Sample.Coverage;
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

    if (!FDWCTransparencySignatureService::BuildSourceSignature(
            WetClothingAsset,
            Layer,
            ResolvedOutputResolution,
            SeedResult.BuildSignature,
            SeedResult.MaterialBakeSignature,
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
    // Material surface readbacks retain a SharedCacheCPU lease. This snapshot
    // owns only shared references, so their pixel bytes must not be charged to
    // WorkerPrivateCPU a second time.
    Snapshot.bValid = true;
    FDWCTransparencyBaselineDiagnostics::RecordProjectionSnapshotBuild(
        Snapshot.SourceSurfaces.Num(),
        Snapshot.OuterSamples.Num());
    ReportGenerationProgress(
        ProgressCallback,
        EDWCTransparencyGenerationPhase::PreparingSources,
        0.25,
        Snapshot.SourceSurfaces.Num(),
        Snapshot.SourceSurfaces.Num());
    return true;
}

FDWCTransparencyAutoMapComputedResult FDWCTransparencyAutoMapGenerator::ComputeStreamingProjection(
    UWetClothingAsset& WetClothingAsset,
    FDWCTransparencyAutoMapSnapshot& SnapshotHandle,
    const FDWCEditorCancellationToken* CancellationToken,
    const FDWCTransparencyGenerationProgressCallback* ProgressCallback,
    FDWCTransparencyStage2PhaseOperation* PhaseOperation)
{
    check(IsInGameThread());
    FDWCTransparencyAutoMapComputedResult Result;
    if (!SnapshotHandle.IsValid())
    {
        Result.Error = TEXT("The transparency projection snapshot is invalid.");
        return Result;
    }

    FDWCTransparencyAutoMapSnapshot::FImpl& Snapshot = *SnapshotHandle.Impl;
    Result.SourcePayload = MoveTemp(Snapshot.SeedResult);
    Result.Warnings = Snapshot.Warnings;
    FDWCTransparencySourcePayload& SourcePayload = Result.SourcePayload;
    const int32 Resolution = SourcePayload.Resolution.X;
    const int32 PixelCount = Resolution * Resolution;

    if (PhaseOperation != nullptr)
    {
        const uint64 SnapshotBytes = FMath::Max<uint64>(Snapshot.EstimatedBytes, 1);
        const uint64 IndexScratchBytes =
            static_cast<uint64>(Snapshot.OuterSamples.Num()) * sizeof(int32) * 2ull +
            static_cast<uint64>(PixelCount) * sizeof(float);
        FDWCTransparencyStage2PhaseResources Resources;
        Resources.WorkerPeakBytes = SnapshotBytes + IndexScratchBytes;
        Resources.WorkerRetainedBytes = SnapshotBytes;
        FString PhaseError;
        if (!PhaseOperation->BeginPhase(
                EDWCTransparencyStage2OperationPhase::StreamProjection,
                Resources,
                PhaseError))
        {
            Result.Error = MoveTemp(PhaseError);
            return Result;
        }
    }

    TMap<int32, const FDWCRevealBakeSurfaceTriangle*> OuterTrianglesByID;
    BuildTriangleLookup(Snapshot.OuterSurface, OuterTrianglesByID);

    TMap<int32, TArray<int32>> SurfaceIndicesByPriority;
    bool bHasBlockers = false;
    for (int32 SurfaceIndex = 0; SurfaceIndex < Snapshot.SourceSurfaces.Num(); ++SurfaceIndex)
    {
        const FDWCRevealBakeSurface& Surface = Snapshot.SourceSurfaces[SurfaceIndex];
        SurfaceIndicesByPriority.FindOrAdd(Surface.LayerOrder).Add(SurfaceIndex);
        bHasBlockers |= Surface.bBlocksReveal && !Surface.bCanBeRevealSource;
    }
    TArray<int32> Priorities;
    SurfaceIndicesByPriority.GetKeys(Priorities);
    Priorities.Sort();

    TArray<int32> UnresolvedSampleIndices;
    UnresolvedSampleIndices.Reserve(Snapshot.OuterSamples.Num());
    for (int32 SampleIndex = 0; SampleIndex < Snapshot.OuterSamples.Num(); ++SampleIndex)
    {
        UnresolvedSampleIndices.Add(SampleIndex);
    }
    TArray<int32> NextUnresolvedSampleIndices;
    NextUnresolvedSampleIndices.Reserve(UnresolvedSampleIndices.Num());
    TArray<float> BlockerDistanceByPixel;
    if (bHasBlockers)
    {
        BlockerDistanceByPixel.Init(-1.0f, PixelCount);
    }

    const int32 TotalSourceCount = Snapshot.SourceSurfaces.Num();
    int32 ProcessedSourceCount = 0;

    const auto ResetCandidatePixel =
        [&SourcePayload, &BlockerDistanceByPixel](const int32 PixelIndex)
    {
        SourcePayload.ValidHitBuffer[PixelIndex] = false;
        SourcePayload.HitDistanceBuffer[PixelIndex] = 0.0f;
        SourcePayload.SourcePriorityBuffer[PixelIndex] = INDEX_NONE;
        if (BlockerDistanceByPixel.IsValidIndex(PixelIndex))
        {
            BlockerDistanceByPixel[PixelIndex] = -1.0f;
        }
    };

    for (const int32 Priority : Priorities)
    {
        if (UnresolvedSampleIndices.IsEmpty())
        {
            break;
        }
        if (CancellationToken != nullptr && CancellationToken->IsCanceled())
        {
            Result.bCanceled = true;
            Result.Error = TEXT("Transparency ray projection was canceled.");
            return Result;
        }

        for (const int32 SampleIndex : UnresolvedSampleIndices)
        {
            const FDWCRevealBakeTexelSample& Sample = Snapshot.OuterSamples[SampleIndex];
            if (Sample.Pixel.X >= 0 && Sample.Pixel.Y >= 0 &&
                Sample.Pixel.X < Resolution && Sample.Pixel.Y < Resolution)
            {
                ResetCandidatePixel(Sample.Pixel.Y * Resolution + Sample.Pixel.X);
            }
        }

        const TArray<int32>& SurfaceIndices = SurfaceIndicesByPriority.FindChecked(Priority);
        for (const int32 SurfaceIndex : SurfaceIndices)
        {
            const FDWCRevealBakeSurface& SourceSurface =
                Snapshot.SourceSurfaces[SurfaceIndex];
            const double SourceStartFraction = 0.25 +
                0.65 * static_cast<double>(ProcessedSourceCount) /
                    FMath::Max(1, TotalSourceCount);
            const double SourceEndFraction = 0.25 +
                0.65 * static_cast<double>(ProcessedSourceCount + 1) /
                    FMath::Max(1, TotalSourceCount);
            const double SourceFractionRange = SourceEndFraction - SourceStartFraction;
            const FDWCTransparencyAutoMapSnapshot::FImpl::FMaterialSourceDescriptor*
                Descriptor = Snapshot.MaterialSourceDescriptorsByLayerId.Find(SourceSurface.LayerId);
            const FName SourceDisplayName = Descriptor != nullptr
                ? Descriptor->ComponentDisplayName
                : SourceSurface.LayerId;
            const int32 SourceMaterialSlotIndex = Descriptor != nullptr
                ? Descriptor->MaterialSlotIndex
                : INDEX_NONE;
            TSharedPtr<const FDWCTransparencyMaterialColorBakeResult> MaterialSurface;
            uint64 MaterialSurfaceBytes = 0;
            if (SourceSurface.bCanBeRevealSource)
            {
                ReportGenerationProgress(
                    ProgressCallback,
                    EDWCTransparencyGenerationPhase::BakingSourceMaterial,
                    SourceStartFraction,
                    ProcessedSourceCount,
                    TotalSourceCount,
                    SourceDisplayName,
                    SourceMaterialSlotIndex);
                if (Descriptor == nullptr || Descriptor->SourceMesh == nullptr ||
                    Descriptor->EffectiveMaterial == nullptr)
                {
                    Result.Error = FString::Printf(
                        TEXT("Source layer '%s' has no material surface descriptor."),
                        *SourceSurface.LayerId.ToString());
                    return Result;
                }

                FString BakeError;
                MaterialSurface = FDWCTransparencyMaterialColorBakeCache::ResolveOrBake(
                    WetClothingAsset,
                    *Descriptor->SourceMesh,
                    *Descriptor->EffectiveMaterial,
                    Descriptor->BakeTransform,
                    Descriptor->MaterialSlotIndex,
                    Descriptor->SourceUVChannel,
                    Descriptor->SourceBakeResolution,
                    BakeError);
                if (!MaterialSurface.IsValid())
                {
                    Result.Error = FString::Printf(
                        TEXT("Source '%s' slot '%s' could not stream its material surface: %s"),
                        *Descriptor->ComponentDisplayName.ToString(),
                        *Descriptor->MaterialSlotName.ToString(),
                        *BakeError);
                    return Result;
                }
                if (MaterialSurface->PayloadKind ==
                        EDWCTransparencyMaterialColorPayloadKind::Texture &&
                    !Descriptor->bHasUsableUV)
                {
                    const uint64 ReclaimBytes = MaterialSurface->AllocatedBytes;
                    MaterialSurface.Reset();
                    FDWCTransparencyMaterialColorBakeCache::ReclaimUnleasedBytes(
                        &WetClothingAsset, ReclaimBytes);
                    Result.Error = FString::Printf(
                        TEXT("Source '%s' slot '%s' uses a texture surface but its selected UV channel has no usable area."),
                        *Descriptor->ComponentDisplayName.ToString(),
                        *Descriptor->MaterialSlotName.ToString());
                    return Result;
                }
                MaterialSurfaceBytes = MaterialSurface->AllocatedBytes;
            }

            ReportGenerationProgress(
                ProgressCallback,
                EDWCTransparencyGenerationPhase::ProjectingSamples,
                SourceStartFraction + SourceFractionRange * 0.35,
                0,
                UnresolvedSampleIndices.Num(),
                SourceDisplayName,
                SourceMaterialSlotIndex);

            TMap<int32, const FDWCRevealBakeSurfaceTriangle*> SourceTrianglesByID;
            BuildTriangleLookup(SourceSurface, SourceTrianglesByID);
            const auto ConsumeSourceHit =
                [&SourcePayload,
                 &Snapshot,
                 &OuterTrianglesByID,
                 &SourceTrianglesByID,
                 &BlockerDistanceByPixel,
                 &MaterialSurface,
                 Resolution](const FDWCRevealBakeRayHit& Hit)
            {
                if (Hit.Pixel.X < 0 || Hit.Pixel.Y < 0 ||
                    Hit.Pixel.X >= Resolution || Hit.Pixel.Y >= Resolution)
                {
                    return;
                }
                const int32 PixelIndex = Hit.Pixel.Y * Resolution + Hit.Pixel.X;
                if (Hit.bBlocked)
                {
                    float& BlockerDistance = BlockerDistanceByPixel[PixelIndex];
                    if (BlockerDistance < 0.0f || Hit.Distance < BlockerDistance)
                    {
                        BlockerDistance = Hit.Distance;
                    }
                    return;
                }
                if (!Hit.bHit || !MaterialSurface.IsValid())
                {
                    return;
                }
                if (SourcePayload.ValidHitBuffer[PixelIndex] &&
                    SourcePayload.HitDistanceBuffer[PixelIndex] <= Hit.Distance)
                {
                    return;
                }

                SourcePayload.ValidHitBuffer[PixelIndex] = true;
                SourcePayload.HitDistanceBuffer[PixelIndex] = Hit.Distance;
                const int32 StatsIndex = Snapshot.StatsIndexBySourceLayerId.FindRef(
                    Hit.SourceLayerId);
                SourcePayload.SourcePriorityBuffer[PixelIndex] = static_cast<int16>(
                    FMath::Clamp(StatsIndex, 0, static_cast<int32>(MAX_int16)));
                SourcePayload.InnerColorBuffer[PixelIndex] =
                    MaterialSurface->Sample(Hit.SourceUV).ToFColor(true);

                const FDWCRevealBakeSurfaceTriangle* const* SourceTriangle =
                    SourceTrianglesByID.Find(Hit.SourceTriangleIndex);
                const FDWCRevealBakeSurfaceTriangle* const* OuterTriangle =
                    OuterTrianglesByID.Find(Hit.OuterTriangleIndex);
                if (SourceTriangle != nullptr && OuterTriangle != nullptr)
                {
                    SourcePayload.RevealSurfaceAuthoring[PixelIndex] =
                        EncodeReorientedRevealSurface(
                            *MaterialSurface,
                            **SourceTriangle,
                            Hit.SourceBarycentric,
                            **OuterTriangle,
                            Hit.OuterBarycentric,
                            Hit.SourceUV);
                }
            };

            FString ProjectionError;
            const FDWCRevealBakeProjectionProgressCallback ProjectionProgress =
                [ProgressCallback,
                 SourceStartFraction,
                 SourceFractionRange,
                 SourceDisplayName,
                 SourceMaterialSlotIndex](const int32 CompletedSamples, const int32 TotalSamples)
            {
                const double SampleFraction = TotalSamples > 0
                    ? static_cast<double>(CompletedSamples) / TotalSamples
                    : 1.0;
                ReportGenerationProgress(
                    ProgressCallback,
                    EDWCTransparencyGenerationPhase::ProjectingSamples,
                    SourceStartFraction + SourceFractionRange * (0.35 + 0.65 * SampleFraction),
                    CompletedSamples,
                    TotalSamples,
                    SourceDisplayName,
                    SourceMaterialSlotIndex);
            };
            if (!FDWCRevealBakeRayProjector::ProjectSamplesToSources(
                    Snapshot.OuterSurface,
                    MakeArrayView(&SourceSurface, 1),
                    Snapshot.OuterSamples,
                    Snapshot.ProjectionSettings,
                    ConsumeSourceHit,
                    &ProjectionError,
                    CancellationToken,
                    UnresolvedSampleIndices,
                    ProgressCallback != nullptr ? &ProjectionProgress : nullptr))
            {
                MaterialSurface.Reset();
                if (MaterialSurfaceBytes > 0)
                {
                    FDWCTransparencyMaterialColorBakeCache::ReclaimUnleasedBytes(
                        &WetClothingAsset, MaterialSurfaceBytes);
                }
                Result.bCanceled = CancellationToken != nullptr && CancellationToken->IsCanceled();
                Result.Error = MoveTemp(ProjectionError);
                return Result;
            }

            MaterialSurface.Reset();
            if (MaterialSurfaceBytes > 0)
            {
                FDWCTransparencyMaterialColorBakeCache::ReclaimUnleasedBytes(
                    &WetClothingAsset, MaterialSurfaceBytes);
            }
            ++ProcessedSourceCount;
        }

        NextUnresolvedSampleIndices.Reset();
        for (const int32 SampleIndex : UnresolvedSampleIndices)
        {
            const FDWCRevealBakeTexelSample& Sample = Snapshot.OuterSamples[SampleIndex];
            if (Sample.Pixel.X < 0 || Sample.Pixel.Y < 0 ||
                Sample.Pixel.X >= Resolution || Sample.Pixel.Y >= Resolution)
            {
                continue;
            }
            const int32 PixelIndex = Sample.Pixel.Y * Resolution + Sample.Pixel.X;
            const bool bHasReveal = SourcePayload.ValidHitBuffer[PixelIndex];
            const float BlockerDistance = BlockerDistanceByPixel.IsValidIndex(PixelIndex)
                ? BlockerDistanceByPixel[PixelIndex]
                : -1.0f;
            const bool bBlocked = BlockerDistance >= 0.0f &&
                (!bHasReveal || BlockerDistance < SourcePayload.HitDistanceBuffer[PixelIndex]);
            if (bBlocked)
            {
                SourcePayload.ValidHitBuffer[PixelIndex] = false;
                SourcePayload.HitDistanceBuffer[PixelIndex] = 0.0f;
                SourcePayload.SourcePriorityBuffer[PixelIndex] = INDEX_NONE;
                SourcePayload.InnerColorBuffer[PixelIndex] = FColor::Black;
                SourcePayload.RevealSurfaceAuthoring[PixelIndex] = FColor(128, 128, 0, 0);
                SourcePayload.AutoAlphaBuffer[PixelIndex] = 0;
                continue;
            }
            if (!bHasReveal)
            {
                NextUnresolvedSampleIndices.Add(SampleIndex);
                continue;
            }

            FDWCRevealBakeRayHit FinalHit;
            FinalHit.bHit = true;
            FinalHit.Distance = SourcePayload.HitDistanceBuffer[PixelIndex];
            const float MaxRevealDistance = FMath::Max(
                Snapshot.OuterSurface.MaxRevealDistance *
                    FMath::Max(Snapshot.ProjectionSettings.RayLengthScale, 0.0f),
                UE_SMALL_NUMBER);
            FinalHit.Confidence = FMath::Clamp(
                1.0f - FinalHit.Distance / MaxRevealDistance,
                0.0f,
                1.0f);
            SourcePayload.AutoAlphaBuffer[PixelIndex] = static_cast<uint8>(FMath::RoundToInt(
                CalculateAutoAlpha(Snapshot.RaySettings, FinalHit) * 255.0f));
            const int32 StatsIndex = SourcePayload.SourcePriorityBuffer[PixelIndex];
            if (SourcePayload.SourceStats.IsValidIndex(StatsIndex))
            {
                ++SourcePayload.SourceStats[StatsIndex].HitCount;
            }
            SourcePayload.SourcePriorityBuffer[PixelIndex] = static_cast<int16>(
                FMath::Clamp(Priority, 0, static_cast<int32>(MAX_int16)));
            ++SourcePayload.ValidHitCount;
        }
        UnresolvedSampleIndices = MoveTemp(NextUnresolvedSampleIndices);
        NextUnresolvedSampleIndices.Reserve(UnresolvedSampleIndices.Num());
    }

    SourcePayload.NoHitCount = FMath::Max(
        0,
        SourcePayload.OuterSampleCount - SourcePayload.ValidHitCount);
    OuterTrianglesByID.Empty();
    SurfaceIndicesByPriority.Empty();
    Priorities.Empty();
    UnresolvedSampleIndices.Empty();
    NextUnresolvedSampleIndices.Empty();
    BlockerDistanceByPixel.Empty();
    if (PhaseOperation != nullptr)
    {
        FDWCTransparencyStage2PhaseResources Resources;
        Resources.WorkerPeakBytes = FMath::Max<uint64>(Snapshot.EstimatedBytes, 1);
        Resources.WorkerRetainedBytes = Resources.WorkerPeakBytes;
        FString PhaseError;
        if (!PhaseOperation->BeginPhase(
                EDWCTransparencyStage2OperationPhase::ComposeResult,
                Resources,
                PhaseError))
        {
            Result.Error = MoveTemp(PhaseError);
            return Result;
        }
    }
    ReportGenerationProgress(
        ProgressCallback,
        EDWCTransparencyGenerationPhase::ComposingResult,
        0.97,
        SourcePayload.OuterSampleCount,
        SourcePayload.OuterSampleCount);
    Result.Summary = FString::Printf(
        TEXT("Generated %d outer samples: %d valid hit(s), %d no-hit sample(s), with source material surfaces streamed by priority layer."),
        SourcePayload.OuterSampleCount,
        SourcePayload.ValidHitCount,
        SourcePayload.NoHitCount);
    Result.ResultBytes =
        SourcePayload.InnerColorBuffer.GetAllocatedSize() +
        SourcePayload.RevealSurfaceAuthoring.GetAllocatedBytes() +
        SourcePayload.AutoAlphaBuffer.GetAllocatedSize() +
        SourcePayload.OuterCoverageBuffer.GetAllocatedSize() +
        SourcePayload.OuterIslandIDBuffer.GetAllocatedSize() +
        SourcePayload.ValidHitBuffer.GetAllocatedSize() +
        SourcePayload.HitDistanceBuffer.GetAllocatedSize() +
        SourcePayload.SourcePriorityBuffer.GetAllocatedSize();
    Result.bSucceeded = true;
    return Result;
}
