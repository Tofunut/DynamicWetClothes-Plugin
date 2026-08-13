// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluationContext.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingPartData.h"
#include "UObject/Package.h"

FDWCEditorValidationEvaluationContext::FDWCEditorValidationEvaluationContext(
    const UWetClothingAsset& InAsset,
    const EDWCEditorValidationAccess InAccess)
    : Asset(InAsset)
    , Setup(InAsset.GetSetupSettings())
    , RuntimeMesh(InAsset.GetRuntimeSkeletalMesh())
    , Access(InAccess)
    , bDeepValidation(InAccess == EDWCEditorValidationAccess::ExactPayload)
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

    const FDWCEditorUVTopologyDescriptor* Topology =
        InAsset.FindOriginalUVTopologyDescriptorForLOD(RuntimeLODIndex);
    const bool bHasSerializedTopologyBulk =
        InAsset.GetSerializedOriginalUVTopologyBytesForEditor() > 0;
    bHasOriginalUVTopologyPayload = Topology != nullptr && bHasSerializedTopologyBulk;
    bOriginalUVTopologyMetadataValid =
        Topology != nullptr &&
        Topology->bIsValid &&
        Topology->LODIndex == RuntimeLODIndex &&
        Topology->UVChannelIndex == InAsset.GetOriginalUVChannelIndex() &&
        Topology->GeneratorVersion == DWCGeneratedDataVersion::OriginalUVTopology &&
        !Topology->BuildSignature.IsEmpty() &&
        Topology->IslandCount > 0 &&
        Topology->TriangleReferenceCount > 0 &&
        Topology->SerializedPayloadBytes > 0 &&
        bHasSerializedTopologyBulk;
    bOriginalUVTopologySignatureCurrent = bOriginalUVTopologyMetadataValid;
    if (bOriginalUVTopologyMetadataValid && bDeepValidation)
    {
        const FDWCEditorUVTopologyHandle TopologyHandle =
            InAsset.AcquireOriginalUVTopologyForLOD(RuntimeLODIndex);
        const FString CurrentSignature = UWetClothingAsset::BuildMeshContentSignature(
            RuntimeMesh,
            RuntimeLODIndex,
            InAsset.GetOriginalUVChannelIndex());
        bOriginalUVTopologySignatureCurrent =
            TopologyHandle.IsValid() &&
            !TopologyHandle->Islands.IsEmpty() &&
            !CurrentSignature.IsEmpty() &&
            Topology->BuildSignature == CurrentSignature;
    }
    bOriginalUVTopologyReady =
        bOriginalUVTopologyMetadataValid && bOriginalUVTopologySignatureCurrent;

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
