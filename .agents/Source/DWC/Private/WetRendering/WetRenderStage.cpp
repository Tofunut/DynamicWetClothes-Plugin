// Fill out your copyright notice in the Description page of Project Settings.

#include "WetRendering/WetRenderStage.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "RuntimeState/WetClothingRuntimeData.h"
#include "Core/WetClothingSettings.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "WetRendering/WetVertexColorBuffer.h"
#include "WetRendering/DWCGPUResourceSubsystem.h"
#include "Runtime/Engine/Public/Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
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
#include "Engine/TextureRenderTarget2D.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "Profiling/DWCStats.h"
#include "Utility/DWCLog.h"

namespace
{
    bool IsMaterialSlotWettableForRender(const UWetClothingAsset* WetClothingAsset, const int32 MaterialSlotIndex)
    {
        if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return false;
        }

        const FWetClothingAuthoredMaterialSlot* Slot =
            WetClothingAsset->Authored.PartData.EditableWetPartData.FindMaterialSlot(MaterialSlotIndex);
        return Slot != nullptr && Slot->bIsWettableSlot;
    }

}

uint64 FWetRenderStage::GetAllocatedMemoryBytes() const
{
    return sizeof(*this) +
           WetMaterialInstances.GetAllocatedSize() +
           CachedWetVertexColors.GetAllocatedSize() +
           CachedWetPartDebugColorsByID.GetAllocatedSize();
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
        UMaterialInterface* ParentMaterial = Receiver.TargetSkeletalMesh->GetMaterial(MaterialIdx);
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(
            ParentMaterial,
            Receiver.TargetSkeletalMesh);
        if (MID != nullptr)
        {
            Receiver.TargetSkeletalMesh->SetMaterial(MaterialIdx, MID);
        }

        (*Receiver.WetMaterialInstances)[MaterialIdx] = MID;
    }
}

void FWetRenderStage::ApplyWetMaterialParameters(FWetRenderStageArgs& Receiver)
{
    DWC_PROFILE_SCOPE(DWC_Render_ApplyWetMaterialParameters);

    if (Receiver.TargetSkeletalMesh != nullptr &&
        Receiver.WetClothingAsset != nullptr &&
        Receiver.WetMaterialInstances != nullptr)
    {
        if (UWorld* World = Receiver.TargetSkeletalMesh->GetWorld())
        {
            if (UDWCGPUResourceSubsystem* ResourceSubsystem = World->GetSubsystem<UDWCGPUResourceSubsystem>())
            {
                ResourceSubsystem->ApplyResourcesToMaterials(
                    const_cast<UWetClothingAsset*>(Receiver.WetClothingAsset),
                    *Receiver.WetMaterialInstances,
                    Receiver.bGPUWetnessMode
                        ? EDWCRenderResourceUsage::FullGPU
                        : EDWCRenderResourceUsage::AbsorbedOnly);
            }
        }
    }

    uint32 UpdatedMaterialCount = 0;
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
    {
        UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
        if (!MID)
        {
            continue;
        }
        ++UpdatedMaterialCount;
        if (!DWCWetMaterialParameters::UseGPUBackend().IsNone())
        {
            MID->SetScalarParameterValue(
                DWCWetMaterialParameters::UseGPUBackend(),
                Receiver.bGPUWetnessMode ? 1.0f : 0.0f);
        }
        if (!DWCWetMaterialParameters::WetPartDebugStrength().IsNone())
        {
            MID->SetScalarParameterValue(
                DWCWetMaterialParameters::WetPartDebugStrength(),
                Receiver.bShowWetPartDebugColors ? 1.0f : 0.0f);
        }
        if (!DWCWetMaterialParameters::Droplet1RenderingEnabled().IsNone())
        {
            MID->SetScalarParameterValue(
                DWCWetMaterialParameters::Droplet1RenderingEnabled(),
                Receiver.bDroplet1RenderingEnabled ? 1.0f : 0.0f);
        }
        if (!DWCWetMaterialParameters::Droplet2RenderingEnabled().IsNone())
        {
            MID->SetScalarParameterValue(
                DWCWetMaterialParameters::Droplet2RenderingEnabled(),
                Receiver.bDroplet2RenderingEnabled ? 1.0f : 0.0f);
        }
        if (Receiver.bGPUWetnessMode)
        {
            if (!DWCWetMaterialParameters::SurfaceWaterDebugStrength().IsNone())
            {
                MID->SetScalarParameterValue(
                    DWCWetMaterialParameters::SurfaceWaterDebugStrength(),
                    Receiver.bShowSurfaceWaterDebugColors ? 1.0f : 0.0f);
            }
            if (!DWCWetMaterialParameters::SurfaceWaterDebugDropletColor().IsNone())
            {
                MID->SetVectorParameterValue(
                    DWCWetMaterialParameters::SurfaceWaterDebugDropletColor(),
                    FLinearColor(1.0f, 1.0f, 0.0f, 1.0f));
            }
        }
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

    ApplyWetWrinkleNormalMapParameters(Receiver);
    ApplyWetTransparencyMapParameters(Receiver);
    FDWCWorkloadStats::RecordRenderUpdate(UpdatedMaterialCount);
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
                    MaterialSlotIndex);
            if (!ResolvedWrinkleMap.IsValid() || ResolvedWrinkleMap.Texture == nullptr)
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
        DWCWetMaterialParameters::TransparencyWetnessMax().IsNone() &&
        DWCWetMaterialParameters::TransparencyUVChannel().IsNone())
    {
        return;
    }

    if (!Receiver.bEnableTransparency)
    {
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver.WetMaterialInstances->Num(); ++MaterialSlotIndex)
        {
            UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
            if (MID == nullptr)
            {
                continue;
            }

            if (!DWCWetMaterialParameters::TransparencyMap().IsNone())
            {
                MID->SetTextureParameterValue(DWCWetMaterialParameters::TransparencyMap(), nullptr);
            }
            if (!DWCWetMaterialParameters::UseTransparencyMap().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::UseTransparencyMap(), 0.0f);
            }
            if (!DWCWetMaterialParameters::TransparencyUVChannel().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::TransparencyUVChannel(), 0.0f);
            }
        }
        return;
    }

    const float SafeWetnessMin = FMath::Clamp(Receiver.TransparencyWetnessMin, 0.0f, 1.0f);
    const float SafeWetnessMax = FMath::Max(SafeWetnessMin, FMath::Clamp(Receiver.TransparencyWetnessMax, 0.0f, 1.0f));
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
                Receiver.WetClothingAsset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [MaterialSlotIndex](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.TargetSurface.OuterMaterialSlotIndex == MaterialSlotIndex;
                    });
            if (Layer == nullptr)
            {
                continue;
            }

            const FWetClothingBakedTransparencyMap* BakedMap =
                Receiver.WetClothingAsset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(
                    MaterialSlotIndex);
            if (BakedMap == nullptr || BakedMap->TransparencyMap == nullptr)
            {
                continue;
            }

            UMaterialInstanceDynamic* MID = (*Receiver.WetMaterialInstances)[MaterialSlotIndex];
            if (MID == nullptr)
            {
                continue;
            }

            if (!DWCWetMaterialParameters::TransparencyMap().IsNone())
            {
                MID->SetTextureParameterValue(DWCWetMaterialParameters::TransparencyMap(), BakedMap->TransparencyMap);
            }
            if (!DWCWetMaterialParameters::UseTransparencyMap().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::UseTransparencyMap(), 1.0f);
            }
            if (!DWCWetMaterialParameters::TransparencyWetnessMin().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::TransparencyWetnessMin(), SafeWetnessMin);
            }
            if (!DWCWetMaterialParameters::TransparencyWetnessMax().IsNone())
            {
                MID->SetScalarParameterValue(DWCWetMaterialParameters::TransparencyWetnessMax(), SafeWetnessMax);
            }
            if (!DWCWetMaterialParameters::TransparencyUVChannel().IsNone())
            {
                MID->SetScalarParameterValue(
                    DWCWetMaterialParameters::TransparencyUVChannel(),
                    static_cast<float>(Receiver.WetClothingAsset->GetDWCDataUVChannelIndex()));
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

        if (!DWCWetMaterialParameters::TransparencyMap().IsNone())
        {
            MID->SetTextureParameterValue(DWCWetMaterialParameters::TransparencyMap(), nullptr);
        }
        if (!DWCWetMaterialParameters::UseTransparencyMap().IsNone())
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::UseTransparencyMap(), 0.0f);
        }
        if (!DWCWetMaterialParameters::TransparencyWetnessMin().IsNone())
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::TransparencyWetnessMin(), SafeWetnessMin);
        }
        if (!DWCWetMaterialParameters::TransparencyWetnessMax().IsNone())
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::TransparencyWetnessMax(), SafeWetnessMax);
        }
        if (!DWCWetMaterialParameters::TransparencyUVChannel().IsNone())
        {
            MID->SetScalarParameterValue(DWCWetMaterialParameters::TransparencyUVChannel(), 0.0f);
        }
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
            for (const FWetClothingAuthoredMaterialSlot& Slot :
                 Receiver.WetClothingAsset->Authored.PartData.EditableWetPartData.MaterialSlots)
            {
                for (const FWetClothingWetPartEntry& Entry : Slot.WetPartEntries)
                {
                    CachedWetPartDebugColorsByID.FindOrAdd(Entry.WetPartID, Entry.Color);
                }
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
