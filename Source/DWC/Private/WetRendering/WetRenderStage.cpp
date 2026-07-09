// Fill out your copyright notice in the Description page of Project Settings.

#include "WetRendering/WetRenderStage.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "RuntimeState/WetClothingRuntimeData.h"
#include "Core/WetClothingSettings.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"

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
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "Utility/DWCLog.h"

namespace
{
    bool IsMaterialSlotWettableForRender(const UWetClothingAsset* WetClothingAsset, const int32 MaterialSlotIndex, const FString& ComponentPath)
    {
        if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return false;
        }

        const FWetClothingWettableMaterialSlotState* State = WetClothingAsset->PartData.EditableWetPartData.WettableMaterialSlots.FindByPredicate(
            [MaterialSlotIndex, &ComponentPath](const FWetClothingWettableMaterialSlotState& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                       (Candidate.ComponentPath == ComponentPath || Candidate.ComponentPath.IsEmpty());
            });

        return State != nullptr && State->bIsWettableSlot;
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
}

void FWetRenderStage::ResetCachedVertexColors()
{
    CachedWetVertexColors.Reset();
}

void FWetRenderStage::InitializeCachedVertexColors(const int32 VertexCount)
{
    CachedWetVertexColors.Init(FLinearColor::Black, VertexCount);
}

void FWetRenderStage::InitializeWetMaterialInstance(FWetRenderStageArgs& Receiver)
{
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
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
    {
        UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
        if (!MID)
        {
            continue;
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
}

void FWetRenderStage::ApplyWetnessProfileMapParameters(FWetRenderStageArgs& Receiver)
{
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
            if (BakedWetnessProfileMap.ComponentPath != Receiver.ComponentPath ||
                BakedWetnessProfileMap.WetnessProfileMap0 == nullptr)
            {
                continue;
            }

            for (const int32 MaterialSlotIndex : BakedWetnessProfileMap.MaterialSlotIndices)
            {
                if (!Receiver.WetMaterialInstances->IsValidIndex(MaterialSlotIndex) ||
                    !IsMaterialSlotWettableForRender(Receiver.WetClothingAsset, MaterialSlotIndex, Receiver.ComponentPath))
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
        const int32 PreferredUVChannelIndex = 0;
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
        {
            if (!Receiver.WetMaterialInstances->IsValidIndex(MaterialSlotIndex) ||
                bWrinkleNormalMapAssigned[MaterialSlotIndex] ||
                !IsMaterialSlotWettableForRender(Receiver.WetClothingAsset, MaterialSlotIndex, Receiver.ComponentPath))
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
                UE_LOG(
                    LogDWC,
                    Log,
                    TEXT("DWC wrinkle runtime: mesh '%s' slot %d assigned baked wrinkle map '%s' (match=%s, bakedUV=%d, bakedLOD=%d, strength=%.3f, wetnessRange=[%.3f, %.3f], material='%s')."),
                    *GetNameSafe(Receiver.TargetSkeletalMesh),
                    MaterialSlotIndex,
                    *GetNameSafe(BakedWrinkleMap->BakedWrinkleNormalMap),
                    DescribeWrinkleMapMatchType(*BakedWrinkleMap, PreferredUVChannelIndex, Receiver.LODIndex),
                    BakedWrinkleMap->UVChannelIndex,
                    BakedWrinkleMap->LODIndex,
                    SafeWrinkleStrength,
                    SafeWrinkleWetnessMin,
                    SafeWrinkleWetnessMax,
                    *GetNameSafe(Receiver.TargetSkeletalMesh != nullptr ? Receiver.TargetSkeletalMesh->GetMaterial(MaterialSlotIndex) : nullptr));
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
                    Receiver.WetClothingAsset != nullptr ? 0 : INDEX_NONE,
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
        Receiver.SimulationState->DirtyWetVertexIndices.Reset();
        CachedWetVertexColors.Init(FLinearColor::Black, VertexCount);
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            Receiver.SimulationState->DirtyWetVertexIndices.Add(VertexIndex);
        }
    }

    if (CachedWetVertexColors.Num() != VertexCount)
    {
        CachedWetVertexColors.Init(FLinearColor::Black, VertexCount);
        Receiver.SimulationState->DirtyWetVertexIndices.Reset();
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            Receiver.SimulationState->DirtyWetVertexIndices.Add(VertexIndex);
        }
    }

    if (Receiver.SimulationState->DirtyWetVertexIndices.Num() == 0)
    {
        return;
    }

    for (int32 VertexIndex : Receiver.SimulationState->DirtyWetVertexIndices)
    {
        if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex) ||
            !CachedWetVertexColors.IsValidIndex(VertexIndex))
        {
            continue;
        }

        if (Receiver.RuntimeData == nullptr || !Receiver.RuntimeData->IsVertexWettable(VertexIndex))
        {
            CachedWetVertexColors[VertexIndex] = FLinearColor::Black;
            continue;
        }

        const float SafeVisualSaturationWetness = FMath::Max(Receiver.WetnessSettings->VisualSaturationWetness, KINDA_SMALL_NUMBER);
        const float Wetness = FMath::Clamp(
            Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex] / SafeVisualSaturationWetness,
            0.0f,
            1.0f);

        CachedWetVertexColors[VertexIndex] = MakeWetVertexColor(Receiver, VertexIndex, Wetness);
    }

    Receiver.SimulationState->DirtyWetVertexIndices.Reset();

    Receiver.TargetSkeletalMesh->SetVertexColorOverride_LinearColor(0, CachedWetVertexColors);
    Receiver.TargetSkeletalMesh->MarkRenderStateDirty();
}
