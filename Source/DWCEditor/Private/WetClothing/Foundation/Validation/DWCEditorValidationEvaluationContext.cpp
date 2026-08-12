// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluationContext.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingPartData.h"
#include "UObject/Package.h"

FDWCEditorValidationEvaluationContext::FDWCEditorValidationEvaluationContext(
    const UWetClothingAsset& InAsset,
    const bool bInDeepValidation)
    : Asset(InAsset)
    , Setup(InAsset.GetSetupSettings())
    , RuntimeMesh(InAsset.GetRuntimeSkeletalMesh())
    , bDeepValidation(bInDeepValidation)
    , bAssetDirty(InAsset.GetOutermost() != nullptr && InAsset.GetOutermost()->IsDirty())
    , bHasWettableSlots(InAsset.HasAnyWettableMaterialSlot())
    , bCPUBackendEnabled(Setup.bBuildCPUVertexSimulationData)
    , bGPUBackendEnabled(Setup.bBuildGPUWetnessMapSimulationData)
    , RuntimeLODIndex(InAsset.GetSimulationLODIndex())
{
    const FDWCDataUVLODMetadata* DataUVMetadata =
        InAsset.FindDataUVMetadataForLOD(RuntimeLODIndex);
    bDataUVReady = bDeepValidation
        ? InAsset.HasValidDataUVForLOD(RuntimeLODIndex)
        : (RuntimeMesh != nullptr &&
           DataUVMetadata != nullptr &&
           DataUVMetadata->bIsValid &&
           DataUVMetadata->UVChannelIndex == InAsset.GetDWCDataUVChannelIndex() &&
           DataUVMetadata->GeneratorVersion == DWCGeneratedDataVersion::DataUV &&
           DataUVMetadata->RenderVertexCount > 0);

    const FDWCEditorUVTopologyData* Topology = InAsset.FindOriginalUVTopologyForLOD(RuntimeLODIndex);
    bOriginalUVTopologyReady =
        Topology != nullptr &&
        Topology->bIsValid &&
        Topology->LODIndex == RuntimeLODIndex &&
        Topology->UVChannelIndex == InAsset.GetOriginalUVChannelIndex();

    for (const FWetClothingAuthoredMaterialSlot& Slot :
         InAsset.Authored.PartData.EditableWetPartData.MaterialSlots)
    {
        if (Slot.bIsWettableSlot && Slot.MaterialSlotIndex != INDEX_NONE)
        {
            WettableMaterialSlotIndices.AddUnique(Slot.MaterialSlotIndex);
        }
    }
    WettableMaterialSlotIndices.Sort();
}

bool FDWCEditorValidationEvaluationContext::IsBakeOutputSavePending(const int32 OutputMask) const
{
    return Asset.IsBakeOutputSavePending(OutputMask);
}
