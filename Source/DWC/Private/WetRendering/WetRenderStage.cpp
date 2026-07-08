// Fill out your copyright notice in the Description page of Project Settings.

#include "WetRendering/WetRenderStage.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "RuntimeData/WetClothingRuntimeData.h"
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
                if (!Receiver.WetMaterialInstances->IsValidIndex(MaterialSlotIndex))
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

    TArray<bool> bWrinkleNormalMapAssigned;
    bWrinkleNormalMapAssigned.Init(false, Receiver.WetMaterialInstances->Num());

    if (Receiver.WetClothingAsset != nullptr)
    {
        for (const FWetWrinkleBakedMapSet& BakedWrinkleMap : Receiver.WetClothingAsset->WrinkleData.BakedWrinkleMaps)
        {
            const int32 MaterialSlotIndex = BakedWrinkleMap.MaterialSlotIndex;
            if (BakedWrinkleMap.LODIndex != Receiver.LODIndex ||
                BakedWrinkleMap.BakedWrinkleNormalMap == nullptr ||
                !Receiver.WetMaterialInstances->IsValidIndex(MaterialSlotIndex) ||
                bWrinkleNormalMapAssigned[MaterialSlotIndex])
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
                MID->SetTextureParameterValue(Receiver.WrinkleNormalMapParameterName, BakedWrinkleMap.BakedWrinkleNormalMap);
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
            if (!Receiver.UseWrinkleNormalMapParameterName.IsNone())
            {
                MID->SetScalarParameterValue(Receiver.UseWrinkleNormalMapParameterName, 0.0f);
            }
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
