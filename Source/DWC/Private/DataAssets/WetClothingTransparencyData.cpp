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

void FWetClothingTransparencyLayerData::MarkAutoBakeStale()
{
    AutoBakeMetadata.AutoBakeGuid.Invalidate();
    AutoBakeMetadata.BuildSignature.Reset();
    AutoBakeMetadata.ValidHitCount = 0;
    AutoBakeMetadata.NoHitCount = 0;
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

FWetClothingTransparencyLayerData* FWetClothingTransparencyData::FindTransparencyLayer(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex)
{
    return TransparencyLayers.FindByPredicate(
        [MaterialSlotIndex, UVChannelIndex](const FWetClothingTransparencyLayerData& Candidate)
        {
            return Candidate.TargetSurface.OuterMaterialSlotIndex == MaterialSlotIndex &&
                   Candidate.TargetSurface.OuterUVChannel == UVChannelIndex;
        });
}

const FWetClothingTransparencyLayerData* FWetClothingTransparencyData::FindTransparencyLayer(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex) const
{
    return TransparencyLayers.FindByPredicate(
        [MaterialSlotIndex, UVChannelIndex](const FWetClothingTransparencyLayerData& Candidate)
        {
            return Candidate.TargetSurface.OuterMaterialSlotIndex == MaterialSlotIndex &&
                   Candidate.TargetSurface.OuterUVChannel == UVChannelIndex;
        });
}

const FWetClothingBakedTransparencyMap* FWetClothingTransparencyData::FindBakedTransparencyMap(
    int32 MaterialSlotIndex,
    int32 PreferredUVChannelIndex,
    int32 PreferredLODIndex,
    EDWCTransparencyBakedMapMatch* OutMatch) const
{
    if (OutMatch != nullptr)
    {
        *OutMatch = EDWCTransparencyBakedMapMatch::None;
    }

    const auto FindMatch = [this](const TFunctionRef<bool(const FWetClothingBakedTransparencyMap&)>& Predicate)
        -> const FWetClothingBakedTransparencyMap*
    {
        for (const FWetClothingTransparencyLayerData& Layer : TransparencyLayers)
        {
            if (const FWetClothingBakedTransparencyMap* Match = Layer.BakedMaps.FindByPredicate(
                    [&Predicate](const FWetClothingBakedTransparencyMap& Candidate)
                    {
                        return IsUsableBakedMap(Candidate) && Predicate(Candidate);
                    }))
            {
                return Match;
            }
        }

        return nullptr;
    };

    if (PreferredUVChannelIndex != INDEX_NONE && PreferredLODIndex != INDEX_NONE)
    {
        if (const FWetClothingBakedTransparencyMap* Match = FindMatch(
                [MaterialSlotIndex, PreferredUVChannelIndex, PreferredLODIndex](const FWetClothingBakedTransparencyMap& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                           Candidate.UVChannelIndex == PreferredUVChannelIndex &&
                           Candidate.LODIndex == PreferredLODIndex;
                }))
        {
            if (OutMatch != nullptr)
            {
                *OutMatch = EDWCTransparencyBakedMapMatch::ExactSlotUVLOD;
            }
            return Match;
        }
    }

    if (PreferredUVChannelIndex != INDEX_NONE)
    {
        if (const FWetClothingBakedTransparencyMap* Match = FindMatch(
                [MaterialSlotIndex, PreferredUVChannelIndex](const FWetClothingBakedTransparencyMap& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                           Candidate.UVChannelIndex == PreferredUVChannelIndex;
                }))
        {
            if (OutMatch != nullptr)
            {
                *OutMatch = EDWCTransparencyBakedMapMatch::SlotUVFallback;
            }
            return Match;
        }
    }

    if (const FWetClothingBakedTransparencyMap* Match = FindMatch(
            [MaterialSlotIndex](const FWetClothingBakedTransparencyMap& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            }))
    {
        if (OutMatch != nullptr)
        {
            *OutMatch = EDWCTransparencyBakedMapMatch::SlotFallback;
        }
        return Match;
    }

    return nullptr;
}

UTexture2D* FWetClothingTransparencyData::ResolveBakedTransparencyMap(
    int32 MaterialSlotIndex,
    int32 PreferredUVChannelIndex,
    int32 PreferredLODIndex) const
{
    const FWetClothingBakedTransparencyMap* Match =
        FindBakedTransparencyMap(MaterialSlotIndex, PreferredUVChannelIndex, PreferredLODIndex);
    return Match != nullptr ? Match->TransparencyMap.Get() : nullptr;
}

bool FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
    const USkeletalMesh* TargetMesh,
    const FWetClothingTransparencyLayerData& Layer,
    TArray<FString>& OutErrors,
    int32 LODIndex)
{
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

    if (RaySettings.MinHitDistance >= RaySettings.MaxRayDistance)
    {
        OutErrors.Add(TEXT("Minimum Hit Distance must be smaller than Maximum Ray Distance."));
    }

    if (RaySettings.FullTransparencyDistance > RaySettings.NoTransparencyDistance)
    {
        OutErrors.Add(TEXT("Full Transparency Distance must not exceed No Transparency Distance."));
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
        if (TargetSurface.OuterUVChannel < 0 || TargetSurface.OuterUVChannel >= NumTexCoords)
        {
            OutErrors.Add(FString::Printf(
                TEXT("Outer UV Channel %d is invalid for LOD %d, which has %d UV channel(s)."),
                TargetSurface.OuterUVChannel,
                LODIndex,
                NumTexCoords));
        }
    }

    TSet<int32> SeenInnerSlots;
    int32 EnabledInnerSlotCount = 0;
    if (Layer.SourceType != EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        return OutErrors.IsEmpty();
    }

    const TArray<FWetClothingTransparencyInnerSlot>& InnerSlotPriority = Layer.SameMeshSource.InnerSlotPriority;
    for (int32 PriorityIndex = 0; PriorityIndex < InnerSlotPriority.Num(); ++PriorityIndex)
    {
        const FWetClothingTransparencyInnerSlot& InnerSlot = InnerSlotPriority[PriorityIndex];
        if (!InnerSlot.bEnabled)
        {
            continue;
        }

        ++EnabledInnerSlotCount;
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

        if (InnerSlot.MaxHitDistance <= 0.0f)
        {
            OutErrors.Add(FString::Printf(
                TEXT("Inner Material Slot %d must have a positive Maximum Hit Distance."),
                InnerSlot.MaterialSlotIndex));
        }
    }

    if (EnabledInnerSlotCount == 0)
    {
        OutErrors.Add(TEXT("At least one enabled Inner Material Slot is required."));
    }

    return OutErrors.IsEmpty();
}
