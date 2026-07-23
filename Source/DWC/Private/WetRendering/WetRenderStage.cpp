// Fill out your copyright notice in the Description page of Project Settings.

#include "WetRendering/WetRenderStage.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "RuntimeState/WetClothingRuntimeData.h"
#include "Core/WetClothingSettings.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "WetRendering/WetVertexColorBuffer.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "Utility/DWCProfiling.h"
#include "RHI.h"

namespace
{
    bool GetWetRenderStageLODRenderData(const USkeletalMeshComponent* TargetSkeletalMesh, const int32 LODIndex, FSkeletalMeshLODRenderData*& OutLODData)
    {
        OutLODData = nullptr;
        if (!TargetSkeletalMesh)
        {
            return false;
        }

        const USkeletalMesh* SkeletalMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
        if (!SkeletalMesh)
        {
            return false;
        }

        FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
        if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            return false;
        }

        OutLODData = &RenderData->LODRenderData[LODIndex];
        return true;
    }
} // namespace
#include "Runtime/Engine/Public/Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "Profiling/DWCStats.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationState.h"
#include "Utility/DWCLog.h"

namespace
{
    UTexture* ResolveOptionalSurfaceMaskTexture(UTexture* Texture)
    {
        // A texture sample cannot compile with a null texture. Use a semantically neutral
        // engine texture so an unassigned optional profile texture has no visual effect.
        static TWeakObjectPtr<UTexture> NeutralTexture;
        if (Texture == nullptr && !NeutralTexture.IsValid())
        {
            NeutralTexture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
        }
        return Texture != nullptr ? Texture : NeutralTexture.Get();
    }

    UTexture* ResolveOptionalSurfaceNormalTexture(UTexture* Texture)
    {
        static TWeakObjectPtr<UTexture> NeutralTexture;
        if (Texture == nullptr && !NeutralTexture.IsValid())
        {
            NeutralTexture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
        }
        return Texture != nullptr ? Texture : NeutralTexture.Get();
    }

    bool IsMaterialSlotWettableForRender(const UWetClothingAsset* WetClothingAsset, const int32 MaterialSlotIndex)
    {
        if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return false;
        }

        const FWetClothingWettableMaterialSlotState* State = WetClothingAsset->Authored.PartData.EditableWetPartData.WettableMaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingWettableMaterialSlotState& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });

        return State != nullptr && State->bIsWettableSlot;
    }

}

uint64 FWetRenderStage::GetAllocatedMemoryBytes() const
{
    return sizeof(*this) +
           WetMaterialInstances.GetAllocatedSize() +
           CachedWetVertexColors.GetAllocatedSize() +
           CachedWetPartDebugColorsByID.GetAllocatedSize() +
           TransparencyRuntimeBindings.GetAllocatedSize();
}

void FWetRenderStage::InvalidateTransparencyBindingCache()
{
    TransparencyRuntimeBindings.Reset();
    CachedTransparencyAsset.Reset();
    CachedTransparencyLODIndex = INDEX_NONE;
    CachedTransparencyUVChannelIndex = INDEX_NONE;
    CachedTransparencyWetnessMin = -1.0f;
    CachedTransparencyWetnessMax = -1.0f;
    bTransparencyBindingCacheValid = false;
}

bool FWetRenderStage::IsTransparencyBindingCacheCurrent(const FWetRenderStageArgs& Receiver) const
{
    if (!bTransparencyBindingCacheValid ||
        Receiver.WetMaterialInstances == nullptr ||
        CachedTransparencyAsset.Get() != Receiver.WetClothingAsset ||
        CachedTransparencyLODIndex != Receiver.LODIndex ||
        TransparencyRuntimeBindings.Num() != Receiver.WetMaterialInstances->Num())
    {
        return false;
    }

    const int32 ExpectedUVChannel = Receiver.WetClothingAsset != nullptr
        ? Receiver.WetClothingAsset->GetDWCDataUVChannelIndex()
        : INDEX_NONE;
    if (CachedTransparencyUVChannelIndex != ExpectedUVChannel)
    {
        return false;
    }

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < TransparencyRuntimeBindings.Num(); ++MaterialSlotIndex)
    {
        if (TransparencyRuntimeBindings[MaterialSlotIndex].MaterialInstance.Get() !=
            (*Receiver.WetMaterialInstances)[MaterialSlotIndex])
        {
            return false;
        }
    }

    return true;
}

void FWetRenderStage::RebuildTransparencyBindingCache(
    FWetRenderStageArgs& Receiver,
    const float WetnessMin,
    const float WetnessMax)
{
    InvalidateTransparencyBindingCache();

    if (Receiver.WetMaterialInstances == nullptr)
    {
        return;
    }

    UWetClothingAsset* WetClothingAsset = const_cast<UWetClothingAsset*>(Receiver.WetClothingAsset);
    CachedTransparencyAsset = WetClothingAsset;
    CachedTransparencyLODIndex = Receiver.LODIndex;
    CachedTransparencyUVChannelIndex = WetClothingAsset != nullptr
        ? WetClothingAsset->GetDWCDataUVChannelIndex()
        : INDEX_NONE;
    CachedTransparencyWetnessMin = WetnessMin;
    CachedTransparencyWetnessMax = WetnessMax;
    TransparencyRuntimeBindings.SetNum(Receiver.WetMaterialInstances->Num());

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < TransparencyRuntimeBindings.Num(); ++MaterialSlotIndex)
    {
        FTransparencyRuntimeBinding& Binding = TransparencyRuntimeBindings[MaterialSlotIndex];
        Binding.MaterialSlotIndex = MaterialSlotIndex;
        Binding.UVChannelIndex = CachedTransparencyUVChannelIndex;
        Binding.LODIndex = Receiver.LODIndex;
        Binding.MaterialInstance = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];

        UMaterialInstanceDynamic* MID = Binding.MaterialInstance.Get();
        if (MID == nullptr)
        {
            continue;
        }

        MID->SetScalarParameterValue(DWCWetMaterialParameters::UseTransparencyMap(), 0.0f);
        MID->SetScalarParameterValue(DWCWetMaterialParameters::TransparencyWetnessMin(), WetnessMin);
        MID->SetScalarParameterValue(DWCWetMaterialParameters::TransparencyWetnessMax(), WetnessMax);
    }

    bTransparencyBindingCacheValid = true;
    if (WetClothingAsset == nullptr)
    {
        return;
    }

    const FDWCDataUVLODMetadata* DataUVMetadata =
        WetClothingAsset->FindDataUVMetadataForLOD(Receiver.LODIndex);
    const bool bHasValidDataUV =
        CachedTransparencyUVChannelIndex >= 0 &&
        CachedTransparencyUVChannelIndex <= 7 &&
        DataUVMetadata != nullptr &&
        DataUVMetadata->bIsValid &&
        DataUVMetadata->UVChannelIndex == CachedTransparencyUVChannelIndex;
    if (!bHasValidDataUV)
    {
        const FString LogKey = FString::Printf(
            TEXT("InvalidDWCDataUV_%d_%d"),
            CachedTransparencyUVChannelIndex,
            Receiver.LODIndex);
        if (LastTransparencyBindingLogKeys.FindRef(INDEX_NONE) != LogKey)
        {
            LastTransparencyBindingLogKeys.Add(INDEX_NONE, LogKey);
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC transparency runtime: mesh '%s' has no valid DWC Data UV for channel %d, LOD%d. Transparency is disabled."),
                *GetNameSafe(Receiver.TargetSkeletalMesh),
                CachedTransparencyUVChannelIndex,
                Receiver.LODIndex);
        }
        return;
    }

    for (FTransparencyRuntimeBinding& Binding : TransparencyRuntimeBindings)
    {
        const int32 MaterialSlotIndex = Binding.MaterialSlotIndex;
        UMaterialInstanceDynamic* MID = Binding.MaterialInstance.Get();
        if (MID == nullptr ||
            !IsMaterialSlotWettableForRender(WetClothingAsset, MaterialSlotIndex))
        {
            continue;
        }

        const FWetClothingTransparencyLayerData* Layer =
            WetClothingAsset->Authored.TransparencyData.FindTransparencyLayer(
                MaterialSlotIndex,
                CachedTransparencyUVChannelIndex);
        if (Layer == nullptr)
        {
            continue;
        }

        const FWetClothingBakedTransparencyMap* BakedMap =
            WetClothingAsset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(
                MaterialSlotIndex,
                CachedTransparencyUVChannelIndex,
                Receiver.LODIndex);
        if (BakedMap == nullptr)
        {
            const FWetClothingBakedTransparencyMap* ExactStoredMap =
                Layer->BakedMaps.FindByPredicate(
                    [MaterialSlotIndex, this](const FWetClothingBakedTransparencyMap& Candidate)
                    {
                        return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                               Candidate.UVChannelIndex == CachedTransparencyUVChannelIndex &&
                               Candidate.LODIndex == CachedTransparencyLODIndex;
                    });
            const FString Reason = ExactStoredMap == nullptr
                ? TEXT("no exact baked map exists")
                : TEXT("the exact baked map is missing, stale, or incomplete");
            const FString LogKey = FString::Printf(
                TEXT("InvalidMap_%d_%d_%d_%s"),
                MaterialSlotIndex,
                CachedTransparencyUVChannelIndex,
                Receiver.LODIndex,
                *Reason);
            if (LastTransparencyBindingLogKeys.FindRef(MaterialSlotIndex) != LogKey)
            {
                LastTransparencyBindingLogKeys.Add(MaterialSlotIndex, LogKey);
                UE_LOG(
                    LogDWC,
                    Warning,
                    TEXT("DWC transparency runtime: mesh '%s' slot %d disabled because %s for DWC UV%d LOD%d."),
                    *GetNameSafe(Receiver.TargetSkeletalMesh),
                    MaterialSlotIndex,
                    *Reason,
                    CachedTransparencyUVChannelIndex,
                    Receiver.LODIndex);
            }
            continue;
        }

        MID->SetTextureParameterValue(
            DWCWetMaterialParameters::TransparencyMap(),
            BakedMap->TransparencyMap);
        Binding.TransparencyMap = BakedMap->TransparencyMap;
        Binding.BakeGuid = BakedMap->BakeGuid;
        Binding.bUsable = true;
    }
}

void FWetRenderStage::ResetCachedVertexColors()
{
    CachedWetVertexColors.Reset();
}

void FWetRenderStage::InitializeCachedVertexColors(const int32 VertexCount)
{
    CachedWetVertexColors.Init(FColor::Black, VertexCount);
}

void FWetRenderStage::InitializeWetMaterialInstance(FWetRenderStageArgs& Receiver)
{
    DWC_PROFILE_SCOPE(DWC_Render_InitializeWetMaterialInstance);

    InvalidateTransparencyBindingCache();
    Receiver.WetMaterialInstances->Reset();

    if (!Receiver.TargetSkeletalMesh)
    {
        return;
    }

    const int32 MaterialCount = Receiver.TargetSkeletalMesh->GetNumMaterials();
    Receiver.WetMaterialInstances->SetNum(MaterialCount);

    for (int32 MaterialIdx = 0; MaterialIdx < MaterialCount; ++MaterialIdx)
    {
        UMaterialInstanceDynamic* MID =
            Receiver.TargetSkeletalMesh->CreateAndSetMaterialInstanceDynamic(MaterialIdx);

        (*Receiver.WetMaterialInstances)[MaterialIdx] = MID;
    }
}

void FWetRenderStage::ApplyWetMaterialParameters(FWetRenderStageArgs& Receiver)
{
    DWC_PROFILE_SCOPE(DWC_Render_ApplyWetMaterialParameters);

    uint32 UpdatedMaterialCount = 0;
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
    {
        UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
        if (!MID)
        {
            continue;
        }
        ++UpdatedMaterialCount;
        if (!DWCWetMaterialParameters::WetPartDebugStrength().IsNone())
        {
            MID->SetScalarParameterValue(
                DWCWetMaterialParameters::WetPartDebugStrength(),
                Receiver.bShowWetPartDebugColors ? 1.0f : 0.0f);
        }
        const FSurfaceWaterProfileParameters DefaultSurfaceProfile;
        const FSurfaceWaterProfileParameters* SurfaceProfile =
            Receiver.SurfaceWaterProfilesByMaterialSlot
                ? Receiver.SurfaceWaterProfilesByMaterialSlot->Find(MaterialSlotIndex)
                : nullptr;
        if (!SurfaceProfile)
        {
            SurfaceProfile = &DefaultSurfaceProfile;
        }
        if (!DWCWetMaterialParameters::SurfaceWaterRT().IsNone())
        {
            FSurfaceWaterSimulationState* SlotState = nullptr;
            if (Receiver.SurfaceWaterStatesByMaterialSlot)
            {
                if (const TUniquePtr<FSurfaceWaterSimulationState>* Found = Receiver.SurfaceWaterStatesByMaterialSlot->Find(MaterialSlotIndex))
                {
                    SlotState = Found->Get();
                }
            }
            MID->SetTextureParameterValue(DWCWetMaterialParameters::SurfaceWaterRT(), SlotState ? SlotState->GetDropletRenderTarget() : nullptr);
        }
        FSurfaceWaterSimulationState* SurfaceState = nullptr;
        if (Receiver.SurfaceWaterStatesByMaterialSlot)
        {
            if (const TUniquePtr<FSurfaceWaterSimulationState>* Found = Receiver.SurfaceWaterStatesByMaterialSlot->Find(MaterialSlotIndex))
            {
                SurfaceState = Found->Get();
            }
        }
        if (!DWCWetMaterialParameters::SurfaceDropletRT().IsNone())
        {
            MID->SetTextureParameterValue(DWCWetMaterialParameters::SurfaceDropletRT(), SurfaceState ? SurfaceState->GetDropletRenderTarget() : nullptr);
        }
        if (!DWCWetMaterialParameters::SurfaceFlowRT().IsNone())
        {
            MID->SetTextureParameterValue(DWCWetMaterialParameters::SurfaceFlowRT(), SurfaceState ? SurfaceState->GetFlowRenderTarget() : nullptr);
        }
        if (!DWCWetMaterialParameters::SurfaceWaterTime().IsNone())
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceWaterTime(), Receiver.SurfaceWaterTimeSeconds);
        }
        if (!DWCWetMaterialParameters::SurfaceWaterTexelSize().IsNone())
        {
            MID->SetScalarParameterValue(
                DWCWetMaterialParameters::SurfaceWaterTexelSize(),
                SurfaceState && SurfaceState->GetResolution() > 0
                    ? 1.0f / static_cast<float>(SurfaceState->GetResolution())
                    : 0.0f);
        }
        if (!DWCWetMaterialParameters::SurfaceWaterNormalStrength().IsNone())
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceWaterNormalStrength(), FMath::Max(0.0f, SurfaceProfile->NormalStrength));
        }
        if (!DWCWetMaterialParameters::SurfaceWaterRoughness().IsNone())
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceWaterRoughness(), FMath::Clamp(SurfaceProfile->SurfaceRoughness, 0.0f, 1.0f));
        }
        const float ThresholdMin = FMath::Clamp(
            FMath::Min(SurfaceProfile->SurfaceAmountThresholdMin, SurfaceProfile->SurfaceAmountThresholdMax), 0.0f, 1.0f);
        const float ThresholdMax = FMath::Clamp(
            FMath::Max(SurfaceProfile->SurfaceAmountThresholdMin, SurfaceProfile->SurfaceAmountThresholdMax), ThresholdMin + KINDA_SMALL_NUMBER, 1.0f);
        const float MaskMin = FMath::Clamp(
            FMath::Min(SurfaceProfile->DropletMaskMin, SurfaceProfile->DropletMaskMax), 0.0f, 1.0f);
        const float MaskMax = FMath::Clamp(
            FMath::Max(SurfaceProfile->DropletMaskMin, SurfaceProfile->DropletMaskMax), MaskMin + KINDA_SMALL_NUMBER, 1.0f);
        const float FlowMaskMin = FMath::Clamp(
            FMath::Min(SurfaceProfile->FlowMaskMin, SurfaceProfile->FlowMaskMax), 0.0f, 1.0f);
        const float FlowMaskMax = FMath::Clamp(
            FMath::Max(SurfaceProfile->FlowMaskMin, SurfaceProfile->FlowMaskMax), FlowMaskMin + KINDA_SMALL_NUMBER, 1.0f);
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceDropletTiling(), FMath::Max(0.01f, SurfaceProfile->DropletTiling));
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceAmountThresholdMin(), ThresholdMin);
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceAmountThresholdMax(), ThresholdMax);
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceDropletMaskMin(), MaskMin);
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceDropletMaskMax(), MaskMax);
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceFlowTiling(), FMath::Max(0.01f, SurfaceProfile->FlowTiling));
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceFlowPanningX(), SurfaceProfile->FlowPanningX);
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceFlowPanningY(), SurfaceProfile->FlowPanningY);
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceFlowNormalStrength(), FMath::Max(0.0f, SurfaceProfile->FlowNormalStrength));
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceFlowRoughness(), FMath::Clamp(SurfaceProfile->FlowRoughness, 0.0f, 1.0f));
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceFlowMaskMin(), FlowMaskMin);
        MID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceFlowMaskMax(), FlowMaskMax);
        // Always update optional textures. This clears a previous profile override when the
        // current profile leaves one unassigned instead of exposing authored function defaults.
        MID->SetTextureParameterValue(
            DWCWetMaterialParameters::SurfaceDropletMaskTexture(),
            ResolveOptionalSurfaceMaskTexture(SurfaceProfile->DropletMaskTexture));
        MID->SetTextureParameterValue(
            DWCWetMaterialParameters::SurfaceDropletNormalTexture(),
            ResolveOptionalSurfaceNormalTexture(SurfaceProfile->DropletNormalTexture));
        MID->SetTextureParameterValue(
            DWCWetMaterialParameters::SurfaceFlowMaskTexture(),
            ResolveOptionalSurfaceMaskTexture(SurfaceProfile->FlowMaskTexture));
        MID->SetTextureParameterValue(
            DWCWetMaterialParameters::SurfaceFlowNormalTexture(),
            ResolveOptionalSurfaceNormalTexture(SurfaceProfile->FlowNormalTexture));

        if (!DWCWetMaterialParameters::UnderColor().IsNone())
        {
            MID->SetVectorParameterValue(DWCWetMaterialParameters::UnderColor(), Receiver.UnderColor);
        }

        if (!DWCWetMaterialParameters::UnderColorBlendStrength().IsNone())
        {
            MID->SetScalarParameterValue(
                DWCWetMaterialParameters::UnderColorBlendStrength(),
                FMath::Clamp(Receiver.UnderColorBlendStrength, 0.0f, 1.0f));
        }
    }

    ApplyWetnessProfileMapParameters(Receiver);
    ApplyWetWrinkleNormalMapParameters(Receiver);
    ApplyWetTransparencyMapParameters(Receiver);
    FDWCWorkloadStats::RecordRenderUpdate(UpdatedMaterialCount);
}

void FWetRenderStage::ApplyWetnessProfileMapParameters(FWetRenderStageArgs& Receiver)
{
    DWC_PROFILE_SCOPE(DWC_Render_ApplyWetnessProfileMapParameters);

    if (Receiver.WetMaterialInstances == nullptr)
    {
        return;
    }

    if (DWCWetMaterialParameters::WetnessProfileMap0().IsNone() &&
        DWCWetMaterialParameters::UseWetnessProfileMap0().IsNone())
    {
        return;
    }

    TArray<bool> bWetnessProfileMapAssigned;
    bWetnessProfileMapAssigned.Init(false, Receiver.WetMaterialInstances->Num());

    if (Receiver.WetClothingAsset != nullptr)
    {
        for (const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap : Receiver.WetClothingAsset->Derived.Inline.BakedWetnessProfileMaps)
        {
            if (BakedWetnessProfileMap.WetnessProfileMap0 == nullptr)
            {
                continue;
            }

            for (const int32 MaterialSlotIndex : BakedWetnessProfileMap.MaterialSlotIndices)
            {
                if (!Receiver.WetMaterialInstances->IsValidIndex(MaterialSlotIndex) ||
                    bWetnessProfileMapAssigned[MaterialSlotIndex] ||
                    !IsMaterialSlotWettableForRender(Receiver.WetClothingAsset, MaterialSlotIndex))
                {
                    continue;
                }

                UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
                if (MID == nullptr)
                {
                    continue;
                }

                if (!DWCWetMaterialParameters::WetnessProfileMap0().IsNone())
                {
                    MID->SetTextureParameterValue(DWCWetMaterialParameters::WetnessProfileMap0(), BakedWetnessProfileMap.WetnessProfileMap0);
                }

                if (!DWCWetMaterialParameters::UseWetnessProfileMap0().IsNone())
                {
                    MID->SetScalarParameterValue(DWCWetMaterialParameters::UseWetnessProfileMap0(), 1.0f);
                }

                bWetnessProfileMapAssigned[MaterialSlotIndex] = true;
            }
        }
    }

    if (DWCWetMaterialParameters::UseWetnessProfileMap0().IsNone())
    {
        return;
    }

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
    {
        if (bWetnessProfileMapAssigned.IsValidIndex(MaterialSlotIndex) && bWetnessProfileMapAssigned[MaterialSlotIndex])
        {
            continue;
        }

        UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
        if (MID != nullptr)
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::UseWetnessProfileMap0(), 0.0f);
        }
    }
}

void FWetRenderStage::ApplyWetWrinkleNormalMapParameters(FWetRenderStageArgs& Receiver)
{
    DWC_PROFILE_SCOPE(DWC_Render_ApplyWetWrinkleNormalMapParameters);

    if (Receiver.WetMaterialInstances == nullptr)
    {
        return;
    }

    if (DWCWetMaterialParameters::WrinkleNormalMap().IsNone() &&
        DWCWetMaterialParameters::UseWrinkleNormalMap().IsNone() &&
        DWCWetMaterialParameters::WrinkleStrength().IsNone() &&
        DWCWetMaterialParameters::WrinkleWetnessMin().IsNone() &&
        DWCWetMaterialParameters::WrinkleWetnessMax().IsNone())
    {
        return;
    }

    if (!Receiver.bEnableWrinkle)
    {
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
        {
            UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
            if (MID == nullptr)
            {
                continue;
            }

            if (!DWCWetMaterialParameters::WrinkleNormalMap().IsNone())
            {
                MID->SetTextureParameterValue(DWCWetMaterialParameters::WrinkleNormalMap(), nullptr);
            }

            if (!DWCWetMaterialParameters::UseWrinkleNormalMap().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::UseWrinkleNormalMap(), 0.0f);
            }

            if (!DWCWetMaterialParameters::WrinkleStrength().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::WrinkleStrength(), 0.0f);
            }
        }
        return;
    }

    const float SafeWrinkleWetnessMin = FMath::Clamp(Receiver.WrinkleWetnessMin, 0.0f, 1.0f);
    const float SafeWrinkleWetnessMax = FMath::Max(SafeWrinkleWetnessMin, FMath::Clamp(Receiver.WrinkleWetnessMax, 0.0f, 1.0f));
    const float SafeWrinkleStrength = FMath::Max(0.0f, Receiver.WrinkleStrength);

    TArray<bool> bWrinkleNormalMapAssigned;
    bWrinkleNormalMapAssigned.Init(false, Receiver.WetMaterialInstances->Num());

    if (Receiver.WetClothingAsset != nullptr)
    {
        const int32 PreferredUVChannelIndex =
            Receiver.WetClothingAsset->Authored.WrinkleData.WrinkleUVChannelIndex != INDEX_NONE
                ? Receiver.WetClothingAsset->Authored.WrinkleData.WrinkleUVChannelIndex
                : 0;
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
        {
            if (!Receiver.WetMaterialInstances->IsValidIndex(MaterialSlotIndex) ||
                bWrinkleNormalMapAssigned[MaterialSlotIndex] ||
                !IsMaterialSlotWettableForRender(Receiver.WetClothingAsset, MaterialSlotIndex))
            {
                continue;
            }

            const FWetWrinkleResolvedNormalMap ResolvedWrinkleMap =
                Receiver.WetClothingAsset->Authored.WrinkleData.ResolveRuntimeWrinkleNormalMap(
                    MaterialSlotIndex,
                    PreferredUVChannelIndex,
                    Receiver.LODIndex);
            if (!ResolvedWrinkleMap.IsValid())
            {
                continue;
            }

            UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
            if (MID == nullptr)
            {
                continue;
            }

            if (!DWCWetMaterialParameters::WrinkleNormalMap().IsNone())
            {
                MID->SetTextureParameterValue(DWCWetMaterialParameters::WrinkleNormalMap(), ResolvedWrinkleMap.Texture);
            }

            if (!DWCWetMaterialParameters::UseWrinkleNormalMap().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::UseWrinkleNormalMap(), 1.0f);
            }

            if (!DWCWetMaterialParameters::WrinkleStrength().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::WrinkleStrength(), SafeWrinkleStrength);
            }

            if (!DWCWetMaterialParameters::WrinkleWetnessMin().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::WrinkleWetnessMin(), SafeWrinkleWetnessMin);
            }

            if (!DWCWetMaterialParameters::WrinkleWetnessMax().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::WrinkleWetnessMax(), SafeWrinkleWetnessMax);
            }



            bWrinkleNormalMapAssigned[MaterialSlotIndex] = true;
        }
    }

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
    {
        UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
        if (MID == nullptr)
        {
            continue;
        }

        if (!bWrinkleNormalMapAssigned.IsValidIndex(MaterialSlotIndex) || !bWrinkleNormalMapAssigned[MaterialSlotIndex])
        {
            if (!DWCWetMaterialParameters::WrinkleNormalMap().IsNone())
            {
                MID->SetTextureParameterValue(DWCWetMaterialParameters::WrinkleNormalMap(), nullptr);
            }

            if (!DWCWetMaterialParameters::UseWrinkleNormalMap().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::UseWrinkleNormalMap(), 0.0f);
            }

            if (!DWCWetMaterialParameters::WrinkleStrength().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::WrinkleStrength(), 0.0f);
            }

            if (!DWCWetMaterialParameters::WrinkleWetnessMin().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::WrinkleWetnessMin(), SafeWrinkleWetnessMin);
            }

            if (!DWCWetMaterialParameters::WrinkleWetnessMax().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::WrinkleWetnessMax(), SafeWrinkleWetnessMax);
            }



            continue;
        }

        if (!DWCWetMaterialParameters::WrinkleStrength().IsNone())
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::WrinkleStrength(), SafeWrinkleStrength);
        }

        if (!DWCWetMaterialParameters::WrinkleWetnessMin().IsNone())
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::WrinkleWetnessMin(), SafeWrinkleWetnessMin);
        }

        if (!DWCWetMaterialParameters::WrinkleWetnessMax().IsNone())
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::WrinkleWetnessMax(), SafeWrinkleWetnessMax);
        }
    }
}

void FWetRenderStage::ApplyWetTransparencyMapParameters(FWetRenderStageArgs& Receiver)
{
    if (Receiver.WetMaterialInstances == nullptr)
    {
        return;
    }

    if (DWCWetMaterialParameters::TransparencyMap().IsNone() &&
        DWCWetMaterialParameters::UseTransparencyMap().IsNone() &&
        DWCWetMaterialParameters::TransparencyWetnessMin().IsNone() &&
        DWCWetMaterialParameters::TransparencyWetnessMax().IsNone())
    {
        return;
    }

    const float ClampedWetnessA = FMath::Clamp(Receiver.TransparencyWetnessMin, 0.0f, 1.0f);
    const float ClampedWetnessB = FMath::Clamp(Receiver.TransparencyWetnessMax, 0.0f, 1.0f);
    const float SafeWetnessMin = FMath::Min(ClampedWetnessA, ClampedWetnessB);
    const float SafeWetnessMax = FMath::Max(ClampedWetnessA, ClampedWetnessB);

    if (!IsTransparencyBindingCacheCurrent(Receiver))
    {
        RebuildTransparencyBindingCache(Receiver, SafeWetnessMin, SafeWetnessMax);
    }

    if (!FMath::IsNearlyEqual(CachedTransparencyWetnessMin, SafeWetnessMin) ||
        !FMath::IsNearlyEqual(CachedTransparencyWetnessMax, SafeWetnessMax))
    {
        for (FTransparencyRuntimeBinding& Binding : TransparencyRuntimeBindings)
        {
            UMaterialInstanceDynamic* MID = Binding.MaterialInstance.Get();
            if (MID == nullptr)
            {
                continue;
            }

            MID->SetScalarParameterValue(
                DWCWetMaterialParameters::TransparencyWetnessMin(),
                SafeWetnessMin);
            MID->SetScalarParameterValue(
                DWCWetMaterialParameters::TransparencyWetnessMax(),
                SafeWetnessMax);
        }
        CachedTransparencyWetnessMin = SafeWetnessMin;
        CachedTransparencyWetnessMax = SafeWetnessMax;
    }

    for (FTransparencyRuntimeBinding& Binding : TransparencyRuntimeBindings)
    {
        UMaterialInstanceDynamic* MID = Binding.MaterialInstance.Get();
        if (MID == nullptr || !Binding.bUsable)
        {
            continue;
        }

        const bool bShouldEnable = Receiver.bEnableTransparency;
        if (Binding.bAppliedEnabled != bShouldEnable)
        {
            MID->SetScalarParameterValue(
                DWCWetMaterialParameters::UseTransparencyMap(),
                bShouldEnable ? 1.0f : 0.0f);
            Binding.bAppliedEnabled = bShouldEnable;
        }

        const FString BindingKey = FString::Printf(
            TEXT("%s_%s_%d_%d_%d_%.3f_%.3f"),
            bShouldEnable ? TEXT("Enabled") : TEXT("QualityDisabled"),
            *Binding.BakeGuid.ToString(EGuidFormats::Digits),
            Binding.MaterialSlotIndex,
            Binding.UVChannelIndex,
            Binding.LODIndex,
            SafeWetnessMin,
            SafeWetnessMax);
        if (LastTransparencyBindingLogKeys.FindRef(Binding.MaterialSlotIndex) == BindingKey)
        {
            continue;
        }

        LastTransparencyBindingLogKeys.Add(Binding.MaterialSlotIndex, BindingKey);
        UE_LOG(
            LogDWC,
            Log,
            TEXT("DWC transparency runtime: mesh '%s' slot %d %s map '%s' with exact DWC UV%d LOD%d (wetnessRange=[%.3f, %.3f], backend=%s, mid='%s')."),
            *GetNameSafe(Receiver.TargetSkeletalMesh),
            Binding.MaterialSlotIndex,
            bShouldEnable ? TEXT("enabled") : TEXT("quality-disabled"),
            *GetNameSafe(Binding.TransparencyMap.Get()),
            Binding.UVChannelIndex,
            Binding.LODIndex,
            SafeWetnessMin,
            SafeWetnessMax,
            Receiver.bGPUWetnessMode ? TEXT("GPU") : TEXT("CPU"),
            *GetNameSafe(MID));
    }
}

FLinearColor FWetRenderStage::MakeWetVertexColor(
    const FWetRenderStageArgs& Receiver,
    const int32 VertexIndex,
    const float Wetness) const
{
    if (Receiver.RuntimeData == nullptr || !Receiver.RuntimeData->IsVertexWettable(VertexIndex))
    {
        return FLinearColor::Black;
    }

    FLinearColor WetPartColor = FLinearColor::White;
    const int32 WetPartID = Receiver.RuntimeData->VertexWetPartIDs.IsValidIndex(VertexIndex)
        ? Receiver.RuntimeData->VertexWetPartIDs[VertexIndex]
        : INDEX_NONE;
    if (const FLinearColor* FoundColor = CachedWetPartDebugColorsByID.Find(WetPartID))
    {
        WetPartColor = *FoundColor;
    }

    // VertexColor.R is reserved for CPU wetness. GBA stores the RGB Wet Part debug color.
    // GPU rendering reads wetness from the existing wetness texture and uses the same GBA color.
    return FLinearColor(
        Wetness,
        FMath::Clamp(WetPartColor.R, 0.0f, 1.0f),
        FMath::Clamp(WetPartColor.G, 0.0f, 1.0f),
        FMath::Clamp(WetPartColor.B, 0.0f, 1.0f));
}

void FWetRenderStage::ApplyWetnessToMaterial(FWetRenderStageArgs& Receiver)
{
    DWC_PROFILE_SCOPE(DWC_Render_ApplyWetnessToMaterial);

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetWetRenderStageLODRenderData(Receiver.TargetSkeletalMesh, Receiver.LODIndex, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();

    if (CachedWetPartDebugColorAsset.Get() != Receiver.WetClothingAsset)
    {
        CachedWetPartDebugColorAsset = const_cast<UWetClothingAsset*>(Receiver.WetClothingAsset);
        CachedWetPartDebugColorsByID.Reset();
        if (Receiver.WetClothingAsset != nullptr)
        {
            for (const FWetClothingWetPartEntry& Entry :
                 Receiver.WetClothingAsset->Authored.PartData.EditableWetPartData.WetPartEntries)
            {
                CachedWetPartDebugColorsByID.FindOrAdd(Entry.WetPartID, Entry.Color);
            }
        }
    }
    TArray<float>* WetnessValues = &Receiver.SimulationState->AbsorbedWetnessPerVertex;
    if (!Receiver.bGPUWetnessMode && WetnessValues->Num() != VertexCount)
    {
        // CPU vertex-wetness rendering owns this state and can repair its size defensively.
        WetnessValues->SetNumZeroed(VertexCount);
        CachedWetVertexColors.Init(FColor::Black, VertexCount);
        Receiver.SimulationState->MarkAllWetVertexColorsDirty();
    }

    if (CachedWetVertexColors.Num() != VertexCount)
    {
        CachedWetVertexColors.Init(FColor::Black, VertexCount);
        Receiver.SimulationState->MarkAllWetVertexColorsDirty();
    }

    if (Receiver.SimulationState->DirtyWetVertexIndices.Num() == 0)
    {
        return;
    }

    {
        DWC_PROFILE_SCOPE(DWC_Render_UpdateWetVertexColors);

        for (int32 VertexIndex : Receiver.SimulationState->DirtyWetVertexIndices)
        {
            if ((!Receiver.bGPUWetnessMode && !WetnessValues->IsValidIndex(VertexIndex)) ||
                !CachedWetVertexColors.IsValidIndex(VertexIndex))
            {
                continue;
            }

            if (Receiver.RuntimeData == nullptr || !Receiver.RuntimeData->IsVertexWettable(VertexIndex))
            {
                CachedWetVertexColors[VertexIndex] = FColor::Black;
                continue;
            }

            const float SafeVisualSaturationWetness = FMath::Max(Receiver.WetnessSettings->VisualSaturationWetness, KINDA_SMALL_NUMBER);
            const float Wetness = Receiver.bGPUWetnessMode
                ? 0.0f
                : FMath::Clamp((*WetnessValues)[VertexIndex] / SafeVisualSaturationWetness, 0.0f, 1.0f);

            CachedWetVertexColors[VertexIndex] = MakeWetVertexColor(Receiver, VertexIndex, Wetness).ToFColor(false);
        }
    }

    Receiver.SimulationState->ClearDirtyWetVertexIndices();

    FWetVertexColorBuffer::ApplyVertexColorOverride(
        *Receiver.TargetSkeletalMesh,
        Receiver.LODIndex,
        CachedWetVertexColors);
    FDWCWorkloadStats::RecordRenderUpdate(0);
}
