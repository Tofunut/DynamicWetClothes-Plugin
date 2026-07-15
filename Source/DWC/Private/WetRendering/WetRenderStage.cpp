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
#include "Materials/MaterialInterface.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationState.h"
#include "Utility/DWCLog.h"

namespace
{
    bool IsMaterialSlotWettableForRender(const UWetClothingAsset* WetClothingAsset, const int32 MaterialSlotIndex)
    {
        if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return false;
        }

        const FWetClothingWettableMaterialSlotState* State = WetClothingAsset->PartData.EditableWetPartData.WettableMaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingWettableMaterialSlotState& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });

        return State != nullptr && State->bIsWettableSlot;
    }

    bool HasTextureParameter(const UMaterialInterface* Material, const FName ParameterName)
    {
        if (Material == nullptr || ParameterName.IsNone())
        {
            return false;
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid> ParameterIds;
        Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
        return ParameterInfos.ContainsByPredicate(
            [ParameterName](const FMaterialParameterInfo& ParameterInfo)
            {
                return ParameterInfo.Name == ParameterName;
            });
    }

    bool HasScalarParameter(const UMaterialInterface* Material, const FName ParameterName)
    {
        if (Material == nullptr || ParameterName.IsNone())
        {
            return false;
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid> ParameterIds;
        Material->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
        return ParameterInfos.ContainsByPredicate(
            [ParameterName](const FMaterialParameterInfo& ParameterInfo)
            {
                return ParameterInfo.Name == ParameterName;
            });
    }

    const TCHAR* DescribeWrinkleMapMatchType(
        const FWetWrinkleBakedMapSet& BakedMap,
        const int32                   PreferredUVChannelIndex,
        const int32                   PreferredLODIndex)
    {
        if (PreferredUVChannelIndex != INDEX_NONE &&
            PreferredLODIndex != INDEX_NONE &&
            BakedMap.UVChannelIndex == PreferredUVChannelIndex &&
            BakedMap.LODIndex == PreferredLODIndex)
        {
            return TEXT("ExactSlotUvLod");
        }

        if (PreferredUVChannelIndex != INDEX_NONE &&
            BakedMap.UVChannelIndex == PreferredUVChannelIndex)
        {
            return TEXT("SlotUvFallbackLod");
        }

        return TEXT("SlotFallback");
    }

    const TCHAR* DescribeTransparencyMapMatchType(const EDWCTransparencyBakedMapMatch Match)
    {
        switch (Match)
        {
        case EDWCTransparencyBakedMapMatch::ExactSlotUVLOD:
            return TEXT("ExactSlotUvLod");
        case EDWCTransparencyBakedMapMatch::SlotUVFallback:
            return TEXT("SlotUvFallbackLod");
        case EDWCTransparencyBakedMapMatch::SlotFallback:
            return TEXT("SlotFallback");
        default:
            return TEXT("None");
        }
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

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
    {
        UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
        if (!MID)
        {
            continue;
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
        if (!Receiver.SurfaceWaterRTParameterName.IsNone())
        {
            FSurfaceWaterSimulationState* SlotState = nullptr;
            if (Receiver.SurfaceWaterStatesByMaterialSlot)
            {
                if (const TUniquePtr<FSurfaceWaterSimulationState>* Found = Receiver.SurfaceWaterStatesByMaterialSlot->Find(MaterialSlotIndex))
                {
                    SlotState = Found->Get();
                }
            }
            MID->SetTextureParameterValue(Receiver.SurfaceWaterRTParameterName, SlotState ? SlotState->GetDropletRenderTarget() : nullptr);
        }
        FSurfaceWaterSimulationState* SurfaceState = nullptr;
        if (Receiver.SurfaceWaterStatesByMaterialSlot)
        {
            if (const TUniquePtr<FSurfaceWaterSimulationState>* Found = Receiver.SurfaceWaterStatesByMaterialSlot->Find(MaterialSlotIndex))
            {
                SurfaceState = Found->Get();
            }
        }
        if (!Receiver.SurfaceDropletRTParameterName.IsNone())
        {
            MID->SetTextureParameterValue(Receiver.SurfaceDropletRTParameterName, SurfaceState ? SurfaceState->GetDropletRenderTarget() : nullptr);
        }
        if (!Receiver.SurfaceFlowRTParameterName.IsNone())
        {
            MID->SetTextureParameterValue(Receiver.SurfaceFlowRTParameterName, SurfaceState ? SurfaceState->GetFlowRenderTarget() : nullptr);
        }
        if (!Receiver.SurfaceWaterTimeParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.SurfaceWaterTimeParameterName, Receiver.SurfaceWaterTimeSeconds);
        }
        if (!Receiver.SurfaceWaterTexelSizeParameterName.IsNone())
        {
            MID->SetScalarParameterValue(
                Receiver.SurfaceWaterTexelSizeParameterName,
                SurfaceState && SurfaceState->GetResolution() > 0
                    ? 1.0f / static_cast<float>(SurfaceState->GetResolution())
                    : 0.0f);
        }
        if (!Receiver.SurfaceWaterNormalStrengthParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.SurfaceWaterNormalStrengthParameterName, FMath::Max(0.0f, SurfaceProfile->NormalStrength));
        }
        if (!Receiver.SurfaceWaterRoughnessParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.SurfaceWaterRoughnessParameterName, FMath::Clamp(SurfaceProfile->SurfaceRoughness, 0.0f, 1.0f));
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
        MID->SetScalarParameterValue(Receiver.SurfaceDropletTilingParameterName, FMath::Max(0.01f, SurfaceProfile->DropletTiling));
        MID->SetScalarParameterValue(Receiver.SurfaceAmountThresholdMinParameterName, ThresholdMin);
        MID->SetScalarParameterValue(Receiver.SurfaceAmountThresholdMaxParameterName, ThresholdMax);
        MID->SetScalarParameterValue(Receiver.SurfaceDropletMaskMinParameterName, MaskMin);
        MID->SetScalarParameterValue(Receiver.SurfaceDropletMaskMaxParameterName, MaskMax);
        MID->SetScalarParameterValue(Receiver.SurfaceFlowTilingParameterName, FMath::Max(0.01f, SurfaceProfile->FlowTiling));
        MID->SetScalarParameterValue(Receiver.SurfaceFlowPanningXParameterName, SurfaceProfile->FlowPanningX);
        MID->SetScalarParameterValue(Receiver.SurfaceFlowPanningYParameterName, SurfaceProfile->FlowPanningY);
        MID->SetScalarParameterValue(Receiver.SurfaceFlowNormalStrengthParameterName, FMath::Max(0.0f, SurfaceProfile->FlowNormalStrength));
        MID->SetScalarParameterValue(Receiver.SurfaceFlowRoughnessParameterName, FMath::Clamp(SurfaceProfile->FlowRoughness, 0.0f, 1.0f));
        MID->SetScalarParameterValue(Receiver.SurfaceFlowMaskMinParameterName, FlowMaskMin);
        MID->SetScalarParameterValue(Receiver.SurfaceFlowMaskMaxParameterName, FlowMaskMax);
        if (SurfaceProfile->DropletMaskTexture)
        {
            MID->SetTextureParameterValue(Receiver.SurfaceDropletMaskTextureParameterName, SurfaceProfile->DropletMaskTexture);
        }
        if (SurfaceProfile->DropletNormalTexture)
        {
            MID->SetTextureParameterValue(Receiver.SurfaceDropletNormalTextureParameterName, SurfaceProfile->DropletNormalTexture);
        }
        if (SurfaceProfile->FlowMaskTexture)
        {
            MID->SetTextureParameterValue(Receiver.SurfaceFlowMaskTextureParameterName, SurfaceProfile->FlowMaskTexture);
        }
        if (SurfaceProfile->FlowNormalTexture)
        {
            MID->SetTextureParameterValue(Receiver.SurfaceFlowNormalTextureParameterName, SurfaceProfile->FlowNormalTexture);
        }
        if (!Receiver.SurfaceWaterDebugModeParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.SurfaceWaterDebugModeParameterName, static_cast<float>(Receiver.SurfaceWaterDebugView));
        }
        if (!Receiver.WetPartDebugStrengthParameterName.IsNone())
        {
            MID->SetScalarParameterValue(
                Receiver.WetPartDebugStrengthParameterName,
                Receiver.bEnableWetPartDebugVertexColors ? 1.0f : 0.0f);
        }

        if (!Receiver.WetPartDebugUseWetnessMaskParameterName.IsNone())
        {
            MID->SetScalarParameterValue(
                Receiver.WetPartDebugUseWetnessMaskParameterName,
                Receiver.bWetPartDebugUseWetnessMask ? 1.0f : 0.0f);
        }

        if (!Receiver.UnderColorParameterName.IsNone())
        {
            MID->SetVectorParameterValue(Receiver.UnderColorParameterName, Receiver.UnderColor);
        }

        if (!Receiver.UnderColorBlendStrengthParameterName.IsNone())
        {
            MID->SetScalarParameterValue(
                Receiver.UnderColorBlendStrengthParameterName,
                FMath::Clamp(Receiver.UnderColorBlendStrength, 0.0f, 1.0f));
        }
    }

    ApplyWetnessProfileMapParameters(Receiver);
    ApplyWetWrinkleNormalMapParameters(Receiver);
    ApplyWetTransparencyMapParameters(Receiver);
}

void FWetRenderStage::ApplyWetnessProfileMapParameters(FWetRenderStageArgs& Receiver)
{
    DWC_PROFILE_SCOPE(DWC_Render_ApplyWetnessProfileMapParameters);

    if (Receiver.WetnessProfileMap0ParameterName.IsNone() && Receiver.UseWetnessProfileMap0ParameterName.IsNone())
    {
        return;
    }

    TArray<bool> bWetnessProfileMapAssigned;
    bWetnessProfileMapAssigned.Init(false, Receiver.WetMaterialInstances->Num());

    if (Receiver.WetClothingAsset)
    {
        for (const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap : Receiver.WetClothingAsset->PartData.BakedWetnessProfileMaps)
        {
            if (BakedWetnessProfileMap.WetnessProfileMap0 == nullptr)
            {
                continue;
            }

            for (const int32 MaterialSlotIndex : BakedWetnessProfileMap.MaterialSlotIndices)
            {
                if (!Receiver.WetMaterialInstances->IsValidIndex(MaterialSlotIndex) ||
                    !IsMaterialSlotWettableForRender(Receiver.WetClothingAsset, MaterialSlotIndex))
                {
                    continue;
                }

                UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
                if (MID == nullptr)
                {
                    continue;
                }

                if (!Receiver.WetnessProfileMap0ParameterName.IsNone())
                {
                    MID->SetTextureParameterValue(Receiver.WetnessProfileMap0ParameterName, BakedWetnessProfileMap.WetnessProfileMap0);
                }

                if (!Receiver.UseWetnessProfileMap0ParameterName.IsNone())
                {
                    MID->SetScalarParameterValue(Receiver.UseWetnessProfileMap0ParameterName, 1.0f);
                }

                bWetnessProfileMapAssigned[MaterialSlotIndex] = true;
            }
        }
    }

    if (Receiver.UseWetnessProfileMap0ParameterName.IsNone())
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
            MID->SetScalarParameterValue(Receiver.UseWetnessProfileMap0ParameterName, 0.0f);
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

    if (Receiver.WrinkleNormalMapParameterName.IsNone() &&
        Receiver.UseWrinkleNormalMapParameterName.IsNone() &&
        Receiver.WrinkleStrengthParameterName.IsNone() &&
        Receiver.WrinkleWetnessMinParameterName.IsNone() &&
        Receiver.WrinkleWetnessMaxParameterName.IsNone())
    {
        return;
    }

    const float SafeWrinkleWetnessMin = FMath::Clamp(Receiver.WrinkleWetnessMin, 0.0f, 1.0f);
    const float SafeWrinkleWetnessMax = FMath::Max(SafeWrinkleWetnessMin, FMath::Clamp(Receiver.WrinkleWetnessMax, 0.0f, 1.0f));
    const float SafeWrinkleStrength = FMath::Max(0.0f, Receiver.WrinkleStrength);
    if (Receiver.bLogWrinkleRuntimeBindings && Receiver.WrinkleWetnessMax < Receiver.WrinkleWetnessMin)
    {
        UE_LOG(
            LogDWC,
            Log,
            TEXT("DWC wrinkle runtime: clamped wetness range on mesh '%s' from [%.3f, %.3f] to [%.3f, %.3f]."),
            *GetNameSafe(Receiver.TargetSkeletalMesh),
            Receiver.WrinkleWetnessMin,
            Receiver.WrinkleWetnessMax,
            SafeWrinkleWetnessMin,
            SafeWrinkleWetnessMax);
    }

    TArray<bool> bWrinkleNormalMapAssigned;
    bWrinkleNormalMapAssigned.Init(false, Receiver.WetMaterialInstances->Num());

    if (Receiver.WetClothingAsset != nullptr)
    {
        const int32 PreferredUVChannelIndex =
            Receiver.WetClothingAsset->WrinkleData.WrinkleUVChannelIndex != INDEX_NONE
                ? Receiver.WetClothingAsset->WrinkleData.WrinkleUVChannelIndex
                : 0;
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
        {
            if (!Receiver.WetMaterialInstances->IsValidIndex(MaterialSlotIndex) ||
                bWrinkleNormalMapAssigned[MaterialSlotIndex] ||
                !IsMaterialSlotWettableForRender(Receiver.WetClothingAsset, MaterialSlotIndex))
            {
                continue;
            }

            const FWetWrinkleBakedMapSet* BakedWrinkleMap =
                Receiver.WetClothingAsset->WrinkleData.FindBakedWrinkleMap(MaterialSlotIndex, PreferredUVChannelIndex, Receiver.LODIndex);
            if (BakedWrinkleMap == nullptr || BakedWrinkleMap->BakedWrinkleNormalMap == nullptr)
            {
                continue;
            }

            UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
            if (MID == nullptr)
            {
                continue;
            }

            if (!Receiver.WrinkleNormalMapParameterName.IsNone())
            {
                MID->SetTextureParameterValue(Receiver.WrinkleNormalMapParameterName, BakedWrinkleMap->BakedWrinkleNormalMap);
            }

            if (!Receiver.UseWrinkleNormalMapParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.UseWrinkleNormalMapParameterName, 1.0f);
            }

            if (!Receiver.WrinkleStrengthParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.WrinkleStrengthParameterName, SafeWrinkleStrength);
            }

            if (!Receiver.WrinkleWetnessMinParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.WrinkleWetnessMinParameterName, SafeWrinkleWetnessMin);
            }

            if (!Receiver.WrinkleWetnessMaxParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.WrinkleWetnessMaxParameterName, SafeWrinkleWetnessMax);
            }

            if (Receiver.bLogWrinkleRuntimeBindings)
            {
                const UMaterialInterface* CurrentMaterial =
                    Receiver.TargetSkeletalMesh != nullptr ? Receiver.TargetSkeletalMesh->GetMaterial(MaterialSlotIndex) : nullptr;
                const bool bHasWrinkleTextureParameter = HasTextureParameter(CurrentMaterial, Receiver.WrinkleNormalMapParameterName);
                const bool bHasUseWrinkleParameter = HasScalarParameter(CurrentMaterial, Receiver.UseWrinkleNormalMapParameterName);
                const bool bHasWrinkleStrengthParameter = HasScalarParameter(CurrentMaterial, Receiver.WrinkleStrengthParameterName);
                const bool bHasWrinkleWetnessMinParameter = HasScalarParameter(CurrentMaterial, Receiver.WrinkleWetnessMinParameterName);
                const bool bHasWrinkleWetnessMaxParameter = HasScalarParameter(CurrentMaterial, Receiver.WrinkleWetnessMaxParameterName);

                UE_LOG(
                    LogDWC,
                    Log,
                    TEXT("DWC wrinkle runtime: mesh '%s' slot %d assigned baked wrinkle map '%s' (match=%s, bakedUV=%d, bakedLOD=%d, strength=%.3f, wetnessRange=[%.3f, %.3f], material='%s', params={texture:%s,use:%s,strength:%s,min:%s,max:%s})."),
                    *GetNameSafe(Receiver.TargetSkeletalMesh),
                    MaterialSlotIndex,
                    *GetNameSafe(BakedWrinkleMap->BakedWrinkleNormalMap),
                    DescribeWrinkleMapMatchType(*BakedWrinkleMap, PreferredUVChannelIndex, Receiver.LODIndex),
                    BakedWrinkleMap->UVChannelIndex,
                    BakedWrinkleMap->LODIndex,
                    SafeWrinkleStrength,
                    SafeWrinkleWetnessMin,
                    SafeWrinkleWetnessMax,
                    *GetNameSafe(CurrentMaterial),
                    bHasWrinkleTextureParameter ? TEXT("true") : TEXT("false"),
                    bHasUseWrinkleParameter ? TEXT("true") : TEXT("false"),
                    bHasWrinkleStrengthParameter ? TEXT("true") : TEXT("false"),
                    bHasWrinkleWetnessMinParameter ? TEXT("true") : TEXT("false"),
                    bHasWrinkleWetnessMaxParameter ? TEXT("true") : TEXT("false"));
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
            if (!Receiver.WrinkleNormalMapParameterName.IsNone())
            {
                MID->SetTextureParameterValue(Receiver.WrinkleNormalMapParameterName, nullptr);
            }

            if (!Receiver.UseWrinkleNormalMapParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.UseWrinkleNormalMapParameterName, 0.0f);
            }

            if (!Receiver.WrinkleStrengthParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.WrinkleStrengthParameterName, 0.0f);
            }

            if (!Receiver.WrinkleWetnessMinParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.WrinkleWetnessMinParameterName, SafeWrinkleWetnessMin);
            }

            if (!Receiver.WrinkleWetnessMaxParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.WrinkleWetnessMaxParameterName, SafeWrinkleWetnessMax);
            }

            if (Receiver.bLogWrinkleRuntimeBindings)
            {
                const bool bHasAnyBakedEntryForSlot = Receiver.WetClothingAsset != nullptr &&
                                                      Receiver.WetClothingAsset->WrinkleData.BakedWrinkleMaps.ContainsByPredicate(
                                                          [MaterialSlotIndex](const FWetWrinkleBakedMapSet& Candidate)
                                                          {
                                                              return Candidate.MaterialSlotIndex == MaterialSlotIndex;
                                                          });
                const bool bHasAnyUsableNormalForSlot = Receiver.WetClothingAsset != nullptr &&
                                                        Receiver.WetClothingAsset->WrinkleData.BakedWrinkleMaps.ContainsByPredicate(
                                                            [MaterialSlotIndex](const FWetWrinkleBakedMapSet& Candidate)
                                                            {
                                                                return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                                                                       Candidate.BakedWrinkleNormalMap != nullptr;
                                                            });

                UE_LOG(
                    LogDWC,
                    Log,
                    TEXT("DWC wrinkle runtime: mesh '%s' slot %d disabled wrinkle normal apply (hasSlotMetadata=%s, hasUsableNormal=%s, preferredUV=%d, material='%s')."),
                    *GetNameSafe(Receiver.TargetSkeletalMesh),
                    MaterialSlotIndex,
                    bHasAnyBakedEntryForSlot ? TEXT("true") : TEXT("false"),
                    bHasAnyUsableNormalForSlot ? TEXT("true") : TEXT("false"),
                    Receiver.WetClothingAsset != nullptr && Receiver.WetClothingAsset->WrinkleData.WrinkleUVChannelIndex != INDEX_NONE
                        ? Receiver.WetClothingAsset->WrinkleData.WrinkleUVChannelIndex
                        : (Receiver.WetClothingAsset != nullptr ? 0 : INDEX_NONE),
                    *GetNameSafe(Receiver.TargetSkeletalMesh != nullptr ? Receiver.TargetSkeletalMesh->GetMaterial(MaterialSlotIndex) : nullptr));
            }

            continue;
        }

        if (!Receiver.WrinkleStrengthParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.WrinkleStrengthParameterName, SafeWrinkleStrength);
        }

        if (!Receiver.WrinkleWetnessMinParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.WrinkleWetnessMinParameterName, SafeWrinkleWetnessMin);
        }

        if (!Receiver.WrinkleWetnessMaxParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.WrinkleWetnessMaxParameterName, SafeWrinkleWetnessMax);
        }
    }
}

void FWetRenderStage::ApplyWetTransparencyMapParameters(FWetRenderStageArgs& Receiver)
{
    if (Receiver.WetMaterialInstances == nullptr)
    {
        return;
    }

    if (Receiver.TransparencyMapParameterName.IsNone() &&
        Receiver.UseTransparencyMapParameterName.IsNone() &&
        Receiver.TransparencyStrengthParameterName.IsNone() &&
        Receiver.TransparencyWetnessMinParameterName.IsNone() &&
        Receiver.TransparencyWetnessMaxParameterName.IsNone() &&
        Receiver.TransparencyUVChannelParameterName.IsNone() &&
        Receiver.WrinkleSuppressionStrengthParameterName.IsNone())
    {
        return;
    }

    const float SafeWetnessMin = FMath::Clamp(Receiver.TransparencyWetnessMin, 0.0f, 1.0f);
    const float SafeWetnessMax = FMath::Max(SafeWetnessMin, FMath::Clamp(Receiver.TransparencyWetnessMax, 0.0f, 1.0f));
    const float TransparencyStrength = Receiver.WetClothingAsset != nullptr
                                           ? FMath::Max(0.0f, Receiver.WetClothingAsset->TransparencyData.TransparencyPreviewStrength)
                                           : 0.0f;
    const float WrinkleSuppressionStrength = Receiver.WetClothingAsset != nullptr
                                                 ? FMath::Max(0.0f, Receiver.WetClothingAsset->TransparencyData.WrinkleSuppressionStrength)
                                                 : 0.0f;

    TArray<bool> bTransparencyMapAssigned;
    bTransparencyMapAssigned.Init(false, Receiver.WetMaterialInstances->Num());

    if (Receiver.WetClothingAsset != nullptr)
    {
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
        {
            if (!IsMaterialSlotWettableForRender(Receiver.WetClothingAsset, MaterialSlotIndex))
            {
                continue;
            }

            const FWetClothingTransparencyLayerData* Layer =
                Receiver.WetClothingAsset->TransparencyData.TransparencyLayers.FindByPredicate(
                    [MaterialSlotIndex](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.TargetSurface.OuterMaterialSlotIndex == MaterialSlotIndex;
                    });
            if (Layer == nullptr)
            {
                continue;
            }

            EDWCTransparencyBakedMapMatch Match = EDWCTransparencyBakedMapMatch::None;
            const FWetClothingBakedTransparencyMap* BakedMap =
                Receiver.WetClothingAsset->TransparencyData.FindBakedTransparencyMap(
                    MaterialSlotIndex,
                    Layer->TargetSurface.OuterUVChannel,
                    Receiver.LODIndex,
                    &Match);
            if (BakedMap == nullptr || BakedMap->TransparencyMap == nullptr)
            {
                continue;
            }

            if (BakedMap->UVChannelIndex < 0 || BakedMap->UVChannelIndex > 3)
            {
                if (Receiver.bLogTransparencyRuntimeBindings)
                {
                    UE_LOG(
                        LogDWC,
                        Warning,
                        TEXT("DWC transparency runtime: mesh '%s' slot %d skipped map '%s' because baked UV channel %d is outside the supported runtime range 0-3."),
                        *GetNameSafe(Receiver.TargetSkeletalMesh),
                        MaterialSlotIndex,
                        *GetNameSafe(BakedMap->TransparencyMap),
                        BakedMap->UVChannelIndex);
                }
                continue;
            }

            UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
            if (MID == nullptr)
            {
                continue;
            }

            if (!Receiver.TransparencyMapParameterName.IsNone())
            {
                MID->SetTextureParameterValue(Receiver.TransparencyMapParameterName, BakedMap->TransparencyMap);
            }
            if (!Receiver.UseTransparencyMapParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.UseTransparencyMapParameterName, 1.0f);
            }
            if (!Receiver.TransparencyStrengthParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.TransparencyStrengthParameterName, TransparencyStrength);
            }
            if (!Receiver.TransparencyWetnessMinParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.TransparencyWetnessMinParameterName, SafeWetnessMin);
            }
            if (!Receiver.TransparencyWetnessMaxParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.TransparencyWetnessMaxParameterName, SafeWetnessMax);
            }
            if (!Receiver.TransparencyUVChannelParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.TransparencyUVChannelParameterName, static_cast<float>(BakedMap->UVChannelIndex));
            }
            if (!Receiver.WrinkleSuppressionStrengthParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.WrinkleSuppressionStrengthParameterName, WrinkleSuppressionStrength);
            }

            if (Receiver.bLogTransparencyRuntimeBindings)
            {
                const UMaterialInterface* Material = Receiver.TargetSkeletalMesh != nullptr
                                                         ? Receiver.TargetSkeletalMesh->GetMaterial(MaterialSlotIndex)
                                                         : nullptr;
                UE_LOG(
                    LogDWC,
                    Log,
                    TEXT("DWC transparency runtime: mesh '%s' slot %d assigned packed map '%s' (match=%s, bakedUV=%d, bakedLOD=%d, strength=%.3f, wetnessRange=[%.3f, %.3f], wrinkleSuppression=%.3f, material='%s', params={texture:%s,use:%s,strength:%s,min:%s,max:%s,uv:%s,suppression:%s})."),
                    *GetNameSafe(Receiver.TargetSkeletalMesh),
                    MaterialSlotIndex,
                    *GetNameSafe(BakedMap->TransparencyMap),
                    DescribeTransparencyMapMatchType(Match),
                    BakedMap->UVChannelIndex,
                    BakedMap->LODIndex,
                    TransparencyStrength,
                    SafeWetnessMin,
                    SafeWetnessMax,
                    WrinkleSuppressionStrength,
                    *GetNameSafe(Material),
                    HasTextureParameter(Material, Receiver.TransparencyMapParameterName) ? TEXT("true") : TEXT("false"),
                    HasScalarParameter(Material, Receiver.UseTransparencyMapParameterName) ? TEXT("true") : TEXT("false"),
                    HasScalarParameter(Material, Receiver.TransparencyStrengthParameterName) ? TEXT("true") : TEXT("false"),
                    HasScalarParameter(Material, Receiver.TransparencyWetnessMinParameterName) ? TEXT("true") : TEXT("false"),
                    HasScalarParameter(Material, Receiver.TransparencyWetnessMaxParameterName) ? TEXT("true") : TEXT("false"),
                    HasScalarParameter(Material, Receiver.TransparencyUVChannelParameterName) ? TEXT("true") : TEXT("false"),
                    HasScalarParameter(Material, Receiver.WrinkleSuppressionStrengthParameterName) ? TEXT("true") : TEXT("false"));
            }

            bTransparencyMapAssigned[MaterialSlotIndex] = true;
        }
    }

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
    {
        UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
        if (MID == nullptr || bTransparencyMapAssigned[MaterialSlotIndex])
        {
            continue;
        }

        if (!Receiver.TransparencyMapParameterName.IsNone())
        {
            MID->SetTextureParameterValue(Receiver.TransparencyMapParameterName, nullptr);
        }
        if (!Receiver.UseTransparencyMapParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.UseTransparencyMapParameterName, 0.0f);
        }
        if (!Receiver.TransparencyStrengthParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.TransparencyStrengthParameterName, 0.0f);
        }
        if (!Receiver.TransparencyWetnessMinParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.TransparencyWetnessMinParameterName, SafeWetnessMin);
        }
        if (!Receiver.TransparencyWetnessMaxParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.TransparencyWetnessMaxParameterName, SafeWetnessMax);
        }
        if (!Receiver.TransparencyUVChannelParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.TransparencyUVChannelParameterName, 0.0f);
        }
        if (!Receiver.WrinkleSuppressionStrengthParameterName.IsNone())
        {
            MID->SetScalarParameterValue(Receiver.WrinkleSuppressionStrengthParameterName, 0.0f);
        }
    }
}

FLinearColor FWetRenderStage::MakeWetVertexColor(
    const FWetRenderStageArgs& Receiver,
    const int32                VertexIndex,
    const float                Wetness) const
{
    if (Receiver.RuntimeData == nullptr || !Receiver.RuntimeData->IsVertexWettable(VertexIndex))
    {
        return FLinearColor::Black;
    }

    if (!Receiver.bEnableWetPartDebugVertexColors)
    {
        const float TransparencyStrength = Receiver.RuntimeData->VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
                                               ? Receiver.RuntimeData->VertexWetnessProfileParameters[VertexIndex].GetTransparencyStrength()
                                               : 0.0f;

        return FLinearColor(Wetness, FMath::Clamp(TransparencyStrength, 0.0f, 1.0f), 0.0f, 1.0f);
    }

    const FLinearColor DebugColor = Receiver.RuntimeData->VertexWetPartDebugColors.IsValidIndex(VertexIndex)
                                        ? Receiver.RuntimeData->VertexWetPartDebugColors[VertexIndex]
                                        : Receiver.UnassignedWetPartDebugColor;

    return FLinearColor(
        Wetness,
        FMath::Clamp(DebugColor.R, 0.0f, 1.0f),
        FMath::Clamp(DebugColor.G, 0.0f, 1.0f),
        FMath::Clamp(DebugColor.B, 0.0f, 1.0f));
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
    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != VertexCount)
    {
        // Vertex SafeCode: 렌더링 단계에서는 RuntimeData를 재빌드하지 않고 SimulationState 크기만 방어적으로 맞춘다.
        Receiver.SimulationState->AbsorbedWetnessPerVertex.SetNumZeroed(VertexCount);
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
            if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex) ||
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
            const float Wetness = FMath::Clamp(
                Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex] / SafeVisualSaturationWetness,
                0.0f,
                1.0f);

            CachedWetVertexColors[VertexIndex] = MakeWetVertexColor(Receiver, VertexIndex, Wetness).ToFColor(false);
        }
    }

    Receiver.SimulationState->ClearDirtyWetVertexIndices();

    FWetVertexColorBuffer::ApplyVertexColorOverride(
        *Receiver.TargetSkeletalMesh,
        Receiver.LODIndex,
        CachedWetVertexColors);
}
