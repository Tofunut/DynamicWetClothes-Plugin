//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "DataAssets/WetClothingTransparencyData.h"

#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace
{
bool IsUsableBakedMap(const FWetClothingBakedTransparencyMap& Candidate)
{
    return Candidate.TransparencyMap != nullptr;
}

bool IsMaterialSlotValid(const USkeletalMesh& TargetMesh, int32 MaterialSlotIndex)
{
    return TargetMesh.GetMaterials().IsValidIndex(MaterialSlotIndex);
}

bool DoesStoredSlotNameMatch(const USkeletalMesh& TargetMesh, int32 MaterialSlotIndex, FName StoredSlotName)
{
    return StoredSlotName.IsNone() ||
           TargetMesh.GetMaterials()[MaterialSlotIndex].MaterialSlotName == StoredSlotName;
}
} // namespace

void FDWCTransparencyEditorStageCacheMetadata::MarkRevealStale()
{
    RevealSignature.Reset();
    bRevealReviewed = false;
    for (FDWCTransparencyTempArtifactReference& Artifact : Artifacts)
    {
        if (Artifact.Kind == EDWCTransparencyTempArtifactKind::CorrectedRevealColor)
        {
            Artifact.bObsolete = true;
        }
    }
}

void FDWCTransparencyEditorStageCacheMetadata::MarkSourceStale()
{
    SourceSignature.Reset();
    bSourceGenerated = false;
    for (FDWCTransparencyTempArtifactReference& Artifact : Artifacts)
    {
        Artifact.bObsolete = true;
    }
    MarkRevealStale();
}

void FWetClothingTransparencyLayerData::MarkAutoBakeStale()
{
    AutoBakeMetadata.AutoBakeGuid.Invalidate();
    AutoBakeMetadata.BuildSignature.Reset();
    AutoBakeMetadata.ValidHitCount = 0;
    AutoBakeMetadata.NoHitCount = 0;
#if WITH_EDITORONLY_DATA
    EditorStageCache.MarkSourceStale();
#endif
    MarkFinalBakeStale();
}

void FWetClothingTransparencyLayerData::MarkFinalBakeStale()
{
    for (FWetClothingBakedTransparencyMap& BakedMap : BakedMaps)
    {
        BakedMap.BakeGuid.Invalidate();
        BakedMap.BuildSignature.Reset();
    }
}

FWetClothingTransparencyLayerData* FWetClothingTransparencyData::FindTransparencyLayer(int32 MaterialSlotIndex)
{
    return TransparencyLayers.FindByPredicate(
        [MaterialSlotIndex](const FWetClothingTransparencyLayerData& Candidate)
        {
            return Candidate.TargetSurface.OuterMaterialSlotIndex == MaterialSlotIndex;
        });
}

const FWetClothingTransparencyLayerData* FWetClothingTransparencyData::FindTransparencyLayer(int32 MaterialSlotIndex) const
{
    return TransparencyLayers.FindByPredicate(
        [MaterialSlotIndex](const FWetClothingTransparencyLayerData& Candidate)
        {
            return Candidate.TargetSurface.OuterMaterialSlotIndex == MaterialSlotIndex;
        });
}

const FWetClothingBakedTransparencyMap* FWetClothingTransparencyData::FindBakedTransparencyMap(
    int32 MaterialSlotIndex) const
{
    const FWetClothingTransparencyLayerData* Layer = FindTransparencyLayer(MaterialSlotIndex);
    if (Layer == nullptr)
    {
        return nullptr;
    }

    return Layer->BakedMaps.FindByPredicate(
        [MaterialSlotIndex](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == MaterialSlotIndex && IsUsableBakedMap(Candidate);
        });
}

const FWetClothingBakedTransparencyMap* FWetClothingTransparencyData::FindRuntimeBakedTransparencyMap(
    const int32 MaterialSlotIndex) const
{
    if (MaterialSlotIndex == INDEX_NONE)
    {
        return nullptr;
    }

    const FWetClothingTransparencyLayerData* Layer = FindTransparencyLayer(MaterialSlotIndex);
    if (Layer == nullptr)
    {
        return nullptr;
    }

    return Layer->BakedMaps.FindByPredicate(
        [MaterialSlotIndex](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                   Candidate.IsRuntimeUsable();
        });
}

UTexture2D* FWetClothingTransparencyData::ResolveBakedTransparencyMap(int32 MaterialSlotIndex) const
{
    const FWetClothingBakedTransparencyMap* Match = FindBakedTransparencyMap(MaterialSlotIndex);
    return Match != nullptr ? Match->TransparencyMap.Get() : nullptr;
}

bool FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
    const USkeletalMesh* TargetMesh,
    const FWetClothingTransparencyLayerData& Layer,
    TArray<FString>& OutErrors,
    int32 DWCDataUVChannelIndex)
{
    constexpr int32 LODIndex = 0;
    OutErrors.Reset();
    const FWetClothingTransparencyTargetSurface& TargetSurface = Layer.TargetSurface;
    const FWetClothingTransparencyRaySettings& RaySettings = Layer.RaySettings;

    if (TargetMesh == nullptr)
    {
        OutErrors.Add(TEXT("Target Skeletal Mesh is not set."));
        return false;
    }

    if (!IsMaterialSlotValid(*TargetMesh, TargetSurface.OuterMaterialSlotIndex))
    {
        OutErrors.Add(FString::Printf(TEXT("Outer Material Slot %d does not exist on the target mesh."), TargetSurface.OuterMaterialSlotIndex));
    }
    else if (!DoesStoredSlotNameMatch(*TargetMesh, TargetSurface.OuterMaterialSlotIndex, TargetSurface.OuterMaterialSlotName))
    {
        OutErrors.Add(FString::Printf(
            TEXT("Outer Material Slot %d no longer matches the stored slot name '%s'."),
            TargetSurface.OuterMaterialSlotIndex,
            *TargetSurface.OuterMaterialSlotName.ToString()));
    }

    const FSkeletalMeshRenderData* RenderData = TargetMesh->GetResourceForRendering();
    int32 NumTexCoords = 0;
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        OutErrors.Add(FString::Printf(TEXT("LOD %d render data is not available on the target mesh."), LODIndex));
    }
    else
    {
        NumTexCoords = RenderData->LODRenderData[LODIndex].StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
        if (DWCDataUVChannelIndex < 0 || DWCDataUVChannelIndex >= NumTexCoords)
        {
            OutErrors.Add(FString::Printf(
                TEXT("DWC Data UV Channel %d is invalid for LOD %d, which has %d UV channel(s)."),
                DWCDataUVChannelIndex,
                LODIndex,
                NumTexCoords));
        }
    }

    if (Layer.SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
    {
        return OutErrors.IsEmpty();
    }

    if (RaySettings.MinHitDistance >= RaySettings.MaxRayDistance)
    {
        OutErrors.Add(TEXT("Minimum Hit Distance must be smaller than Maximum Ray Distance."));
    }

    if (RaySettings.FullTransparencyDistance > RaySettings.NoTransparencyDistance)
    {
        OutErrors.Add(TEXT("Full Transparency Distance must not exceed No Transparency Distance."));
    }

    if (Layer.SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        return OutErrors.IsEmpty();
    }

    if (Layer.SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh)
    {
        const USkeletalMesh* ExternalMesh = Layer.ExternalMeshSource.SkeletalMesh;
        if (ExternalMesh == nullptr)
        {
            OutErrors.Add(TEXT("External Skeletal Mesh is not set."));
            return false;
        }
        const FSkeletalMeshRenderData* ExternalRenderData = ExternalMesh->GetResourceForRendering();
        const int32 ExternalUVCount = ExternalRenderData != nullptr &&
            ExternalRenderData->LODRenderData.IsValidIndex(LODIndex)
                ? ExternalRenderData->LODRenderData[LODIndex].StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()
                : 0;
        for (const FWetClothingTransparencyInnerSlot& SourceSlot :
             Layer.ExternalMeshSource.SourceSlotPriority)
        {
            if (!IsMaterialSlotValid(*ExternalMesh, SourceSlot.MaterialSlotIndex))
            {
                OutErrors.Add(FString::Printf(
                    TEXT("External source material slot %d is unavailable."),
                    SourceSlot.MaterialSlotIndex));
            }
            if (SourceSlot.SourceUVChannel < 0 || SourceSlot.SourceUVChannel >= ExternalUVCount)
            {
                OutErrors.Add(FString::Printf(
                    TEXT("External source slot %d uses unavailable UV channel %d."),
                    SourceSlot.MaterialSlotIndex, SourceSlot.SourceUVChannel));
            }
        }
        return OutErrors.IsEmpty();
    }

    TSet<int32> SeenInnerSlots;
    int32 InnerSlotCount = 0;

    const TArray<FWetClothingTransparencyInnerSlot>& InnerSlotPriority = Layer.SameMeshSource.InnerSlotPriority;
    for (int32 PriorityIndex = 0; PriorityIndex < InnerSlotPriority.Num(); ++PriorityIndex)
    {
        const FWetClothingTransparencyInnerSlot& InnerSlot = InnerSlotPriority[PriorityIndex];
        ++InnerSlotCount;
        if (!IsMaterialSlotValid(*TargetMesh, InnerSlot.MaterialSlotIndex))
        {
            OutErrors.Add(FString::Printf(
                TEXT("Inner Material Slot at priority %d references missing slot %d."),
                PriorityIndex,
                InnerSlot.MaterialSlotIndex));
            continue;
        }

        if (InnerSlot.MaterialSlotIndex == TargetSurface.OuterMaterialSlotIndex)
        {
            OutErrors.Add(FString::Printf(
                TEXT("Inner Material Slot at priority %d is the same as the Outer Material Slot."),
                PriorityIndex));
        }

        if (SeenInnerSlots.Contains(InnerSlot.MaterialSlotIndex))
        {
            OutErrors.Add(FString::Printf(
                TEXT("Inner Material Slot %d is registered more than once."),
                InnerSlot.MaterialSlotIndex));
        }
        SeenInnerSlots.Add(InnerSlot.MaterialSlotIndex);

        if (!DoesStoredSlotNameMatch(*TargetMesh, InnerSlot.MaterialSlotIndex, InnerSlot.MaterialSlotName))
        {
            OutErrors.Add(FString::Printf(
                TEXT("Inner Material Slot %d no longer matches the stored slot name '%s'."),
                InnerSlot.MaterialSlotIndex,
                *InnerSlot.MaterialSlotName.ToString()));
        }

        if (NumTexCoords > 0 && (InnerSlot.SourceUVChannel < 0 || InnerSlot.SourceUVChannel >= NumTexCoords))
        {
            OutErrors.Add(FString::Printf(
                TEXT("Inner Material Slot %d uses invalid Source UV Channel %d for LOD %d."),
                InnerSlot.MaterialSlotIndex,
                InnerSlot.SourceUVChannel,
                LODIndex));
        }

    }

    if (InnerSlotCount == 0)
    {
        OutErrors.Add(TEXT("At least one Inner Material Slot is required."));
    }

    return OutErrors.IsEmpty();
}
