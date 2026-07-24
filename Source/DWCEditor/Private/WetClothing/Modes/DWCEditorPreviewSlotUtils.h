#pragma once

#include "DataAssets/WetClothingAsset.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WetRendering/DWCGPUResourceSubsystem.h"

/** Editor preview uses the CPU backend only; GPU material instances are not a prerequisite. */
namespace DWCEditorPreviewSlotUtils
{
    inline const FWetClothingGeneratedWetMaterialOverride* FindGeneratedWetMaterialOverride(
        const UWetClothingAsset* WetClothingAsset,
        const int32 MaterialSlotIndex)
    {
        if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return nullptr;
        }

        return WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& Entry)
            {
                return Entry.MaterialSlotIndex == MaterialSlotIndex;
            });
    }

    inline bool IsCpuPreviewReady(const UWetClothingAsset* WetClothingAsset, const int32 MaterialSlotIndex)
    {
        if (WetClothingAsset == nullptr || !WetClothingAsset->IsMaterialSlotWettable(MaterialSlotIndex))
        {
            return false;
        }

        const FWetClothingGeneratedWetMaterialOverride* Override =
            FindGeneratedWetMaterialOverride(WetClothingAsset, MaterialSlotIndex);
        return Override != nullptr &&
               Override->GeneratedMaterial != nullptr &&
               Override->CPUMaterialInstance != nullptr &&
               Override->CPUMaterialInstance->Parent == Override->GeneratedMaterial;
    }

    inline UMaterialInstanceConstant* ResolveCpuPreviewMaterial(
        const UWetClothingAsset* WetClothingAsset,
        const int32 MaterialSlotIndex)
    {
        return IsCpuPreviewReady(WetClothingAsset, MaterialSlotIndex)
                   ? FindGeneratedWetMaterialOverride(WetClothingAsset, MaterialSlotIndex)->CPUMaterialInstance.Get()
                   : nullptr;
    }

    inline void ApplyRenderProfileResources(
        UWetClothingAsset* WetClothingAsset,
        const int32 MaterialSlotIndex,
        const int32 MaterialSlotCount,
        UMaterialInstanceDynamic* PreviewMID,
        UWorld* World)
    {
        if (WetClothingAsset == nullptr ||
            MaterialSlotIndex == INDEX_NONE ||
            PreviewMID == nullptr ||
            World == nullptr)
        {
            return;
        }

        UDWCGPUResourceSubsystem* ResourceSubsystem = World->GetSubsystem<UDWCGPUResourceSubsystem>();
        if (ResourceSubsystem == nullptr)
        {
            return;
        }

        TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMIDs;
        PreviewMIDs.Init(nullptr, FMath::Max(MaterialSlotCount, MaterialSlotIndex + 1));
        PreviewMIDs[MaterialSlotIndex] = PreviewMID;
        ResourceSubsystem->ApplyResourcesToMaterials(WetClothingAsset, PreviewMIDs);
    }
}
