// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCWetPartValidationEvaluator.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetPartDataTextureBaker.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluationContext.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"

namespace
{
FText SlotLabel(const int32 SlotIndex)
{
    return FText::Format(
        NSLOCTEXT("DWCWetPartValidation", "SlotContext", "Material Slot {0}"),
        FText::AsNumber(SlotIndex));
}

void AddManualIssue(
    FWCAEditorValidationSnapshot& Snapshot,
    FDWCEditorValidationNode& Node,
    const FName Code,
    const FString& Detail)
{
    Node.Input = EDWCEditorValidationInputState::Invalid;
    Node.Artifact = EDWCEditorValidationArtifactState::Stale;
    DWCEditorValidation::AddDiagnostic(
        Snapshot,
        Node,
        Code,
        EDWCEditorValidationSeverity::Error,
        NSLOCTEXT("DWCWetPartValidation", "Title", "Wet Part Setup"),
        NSLOCTEXT("DWCWetPartValidation", "Invalid", "Invalid"),
        FText::FromString(Detail),
        NSLOCTEXT("DWCWetPartValidation", "ManualAction", "Fix the Wet Part assignment in Wet Part Editor."),
        EDWCEditorValidationRemediation::Manual,
        {},
        true,
        SlotLabel(Node.Key.MaterialSlotIndex));
}
}

void FDWCWetPartValidationEvaluator::AppendToSnapshot(
    const FDWCEditorValidationEvaluationContext& Context,
    FWCAEditorValidationSnapshot& InOutSnapshot)
{
    const UWetClothingAsset& Asset = Context.Asset;
    const FWetClothingEditableWetPartData& Data = Asset.Authored.PartData.EditableWetPartData;
    const int32 RuntimeMaterialCount = Context.RuntimeMesh != nullptr
        ? Context.RuntimeMesh->GetMaterials().Num()
        : 0;

    TMap<int32, int32> SlotRecordCounts;
    for (const FWetClothingAuthoredMaterialSlot& Slot : Data.MaterialSlots)
    {
        ++SlotRecordCounts.FindOrAdd(Slot.MaterialSlotIndex);
    }

    const FDWCEditorUVTopologyData* Topology =
        Asset.FindOriginalUVTopologyForLOD(Context.RuntimeLODIndex);
    TSet<int32> EvaluatedSlots;
    TSet<int32> ReportedMissingMaskProfiles;
    for (const FWetClothingAuthoredMaterialSlot& Slot : Data.MaterialSlots)
    {
        const FDWCEditorValidationTargetKey Key{
            EDWCEditorValidationDomain::WetPart,
            Slot.MaterialSlotIndex};
        FDWCEditorValidationNode& Node =
            DWCEditorValidation::FindOrAddNode(InOutSnapshot, Key);
        Node.Intent = Slot.bIsWettableSlot
            ? EDWCEditorValidationIntentState::Enabled
            : EDWCEditorValidationIntentState::Disabled;
        Node.Artifact = Slot.bIsWettableSlot
            ? EDWCEditorValidationArtifactState::Current
            : EDWCEditorValidationArtifactState::NotRequired;

        if (!Slot.bIsWettableSlot)
        {
            continue;
        }

        if (EvaluatedSlots.Contains(Slot.MaterialSlotIndex))
        {
            continue;
        }
        EvaluatedSlots.Add(Slot.MaterialSlotIndex);

        if (Context.RuntimeMesh == nullptr)
        {
            Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
            Node.Artifact = EDWCEditorValidationArtifactState::Missing;
            DWCEditorValidation::AddDiagnostic(
                InOutSnapshot,
                Node,
                TEXT("WetPartRuntimeMeshMissing"),
                EDWCEditorValidationSeverity::Warning,
                NSLOCTEXT("DWCWetPartValidation", "Title", "Wet Part Setup"),
                NSLOCTEXT("DWCWetPartValidation", "Blocked", "Blocked"),
                NSLOCTEXT("DWCWetPartValidation", "RuntimeMeshMissingDetail", "The prepared runtime mesh is not available, so this Wet Part slot cannot be validated."),
                NSLOCTEXT("DWCWetPartValidation", "RuntimeMeshMissingAction", "Complete Asset Setup before validating Wet Part assignments."),
                EDWCEditorValidationRemediation::Manual,
                {});
            continue;
        }

        if (Slot.MaterialSlotIndex == INDEX_NONE ||
            Slot.MaterialSlotIndex < 0 ||
            Slot.MaterialSlotIndex >= RuntimeMaterialCount)
        {
            AddManualIssue(
                InOutSnapshot,
                Node,
                TEXT("WetPartSlotOutOfRange"),
                FString::Printf(
                    TEXT("Wet Part material slot %d is not available on the runtime mesh."),
                    Slot.MaterialSlotIndex));
            continue;
        }
        if (SlotRecordCounts.FindRef(Slot.MaterialSlotIndex) > 1)
        {
            AddManualIssue(
                InOutSnapshot,
                Node,
                TEXT("WetPartDuplicateSlotRecord"),
                FString::Printf(
                    TEXT("Material slot %d has multiple authored Wet Part slot records."),
                    Slot.MaterialSlotIndex));
        }

        TSet<int32> ValidIslandIds;
        if (Topology != nullptr && Topology->bIsValid)
        {
            for (const FDWCOriginalUVIslandTopology& Island : Topology->Islands)
            {
                if (Island.MaterialSlotIndex == Slot.MaterialSlotIndex)
                {
                    ValidIslandIds.Add(Island.IslandID);
                }
            }
        }
        else
        {
            Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
        }

        TSet<int32> SeenPartIds;
        TMap<int32, int32> IslandOwnerById;
        bool bHasAssignedWetPart = false;
        for (const FWetClothingWetPartEntry& Entry : Slot.WetPartEntries)
        {
            if (Entry.WetPartID < 0 || SeenPartIds.Contains(Entry.WetPartID))
            {
                AddManualIssue(
                    InOutSnapshot,
                    Node,
                    TEXT("WetPartInvalidOrDuplicateId"),
                    FString::Printf(
                        TEXT("Material slot %d contains an invalid or duplicate Wet Part ID %d."),
                        Slot.MaterialSlotIndex,
                        Entry.WetPartID));
            }
            SeenPartIds.Add(Entry.WetPartID);

            if (!Data.Profiles.IsValidIndex(Entry.ProfileIndex))
            {
                AddManualIssue(
                    InOutSnapshot,
                    Node,
                    TEXT("WetPartProfileOutOfRange"),
                    FString::Printf(
                        TEXT("Wet Part %d in material slot %d references missing profile index %d."),
                        Entry.WetPartID,
                        Slot.MaterialSlotIndex,
                        Entry.ProfileIndex));
            }

            if (Entry.WetPartID == 0)
            {
                continue;
            }
            bHasAssignedWetPart |= !Entry.AssignedUVIslandIDs.IsEmpty();
            for (const int32 IslandId : Entry.AssignedUVIslandIDs)
            {
                if (const int32* ExistingOwner = IslandOwnerById.Find(IslandId))
                {
                    AddManualIssue(
                        InOutSnapshot,
                        Node,
                        TEXT("WetPartDuplicateIslandAssignment"),
                        FString::Printf(
                            TEXT("UV island %d in material slot %d is assigned to Wet Parts %d and %d."),
                            IslandId,
                            Slot.MaterialSlotIndex,
                            *ExistingOwner,
                            Entry.WetPartID));
                }
                else
                {
                    IslandOwnerById.Add(IslandId, Entry.WetPartID);
                }

                if (Topology != nullptr && Topology->bIsValid && !ValidIslandIds.Contains(IslandId))
                {
                    AddManualIssue(
                        InOutSnapshot,
                        Node,
                        TEXT("WetPartIslandMissing"),
                        FString::Printf(
                            TEXT("Wet Part %d in material slot %d references UV island %d, which is not present in the current topology."),
                            Entry.WetPartID,
                            Slot.MaterialSlotIndex,
                            IslandId));
                }
            }

            if (!Data.Profiles.IsValidIndex(Entry.ProfileIndex))
            {
                continue;
            }
            const FWetPartProfileAssignment& Profile = Data.Profiles[Entry.ProfileIndex];
            FWetnessProfileParameters Parameters = Profile.Parameters;
            if (Context.bDeepValidation)
            {
                FWetClothingWetPartDataTextureBaker::ResolveProfileParameters(&Profile, Parameters);
            }
            if (Parameters.SurfaceWater.bEnabled &&
                Parameters.SurfaceWater.DropletMaskTexture == nullptr &&
                !ReportedMissingMaskProfiles.Contains(Entry.ProfileIndex))
            {
                ReportedMissingMaskProfiles.Add(Entry.ProfileIndex);
                DWCEditorValidation::AddDiagnostic(
                    InOutSnapshot,
                    Node,
                    FName(*FString::Printf(TEXT("SurfaceWaterMissingMask_Profile%d"), Entry.ProfileIndex)),
                    EDWCEditorValidationSeverity::Warning,
                    NSLOCTEXT("DWCWetPartValidation", "SurfaceWaterTitle", "Surface Water Input"),
                    NSLOCTEXT("DWCWetPartValidation", "ManualFix", "Manual Fix"),
                    FText::FromString(FString::Printf(
                        TEXT("Wet Part %d in material slot %d enables Surface Water but its profile has no Droplet Mask Texture."),
                        Entry.WetPartID,
                        Slot.MaterialSlotIndex)),
                    NSLOCTEXT("DWCWetPartValidation", "SurfaceWaterAction", "Assign a Droplet Mask Texture, then bake the Render Profile Lookup Texture."),
                    EDWCEditorValidationRemediation::Manual,
                    {},
                    false,
                    SlotLabel(Slot.MaterialSlotIndex));
            }
        }

        if (Node.Input != EDWCEditorValidationInputState::Invalid)
        {
            Node.Input = EDWCEditorValidationInputState::Valid;
        }
        if (Node.Input != EDWCEditorValidationInputState::Invalid && !bHasAssignedWetPart)
        {
            Node.Intent = EDWCEditorValidationIntentState::Draft;
            Node.Artifact = EDWCEditorValidationArtifactState::NotRequired;
        }
    }
}
