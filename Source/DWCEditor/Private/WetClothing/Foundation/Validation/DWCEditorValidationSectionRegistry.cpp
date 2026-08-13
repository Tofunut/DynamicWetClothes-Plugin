// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCEditorValidationSectionRegistry.h"

#define LOCTEXT_NAMESPACE "DWCEditorValidationSectionRegistry"

namespace
{
const TArray<FDWCValidationSectionDescriptor>& GetSectionDescriptors()
{
    static const TArray<FDWCValidationSectionDescriptor> Descriptors = {
        {
            EWCAValidationSection::Asset,
            EDWCValidationSectionGroup::Asset,
            0,
            LOCTEXT("AssetTitle", "Asset State"),
            LOCTEXT("AssetNotApplicable", "No Wet Clothing Asset is available to validate."),
            TEXT("ClassIcon.DataAsset"),
            false,
            {
                LOCTEXT("AssetCheckConfiguration", "Asset setup and authoring configuration"),
                LOCTEXT("AssetCheckSave", "Unsaved authoring and generated-data changes")
            }
        },
        {
            EWCAValidationSection::DataUV,
            EDWCValidationSectionGroup::Authoring,
            10,
            LOCTEXT("DataUVTitle", "Prepared Mesh UV Layout"),
            LOCTEXT("DataUVNotApplicable", "No prepared mesh UV layout is required by the current asset configuration."),
            TEXT("ClassIcon.SkeletalMesh"),
            false,
            {
                LOCTEXT("DataUVCheckAvailability", "Prepared mesh UV layout availability and build version"),
                LOCTEXT("DataUVCheckOriginalTopology", "Original UV topology data"),
                LOCTEXT("DataUVCheckCompatibility", "Prepared mesh and UV-channel compatibility")
            }
        },
        {
            EWCAValidationSection::WetPart,
            EDWCValidationSectionGroup::Authoring,
            20,
            LOCTEXT("WetPartTitle", "Wet Part Authoring"),
            LOCTEXT("WetPartNotApplicable", "No material slots are configured for Wet Part authoring."),
            TEXT("DWCEditor.WetPartTool"),
            true,
            {
                LOCTEXT("WetPartCheckSlots", "Wettable material slot intent and authored Wet Part data"),
                LOCTEXT("WetPartCheckProfiles", "Wet Part profile assignments and source data")
            }
        },
        {
            EWCAValidationSection::RuntimeData,
            EDWCValidationSectionGroup::Runtime,
            30,
            LOCTEXT("RuntimeTitle", "Runtime Data"),
            LOCTEXT("RuntimeNotApplicable", "CPU and GPU runtime-data generation are disabled for this asset."),
            TEXT("ClassIcon.DataAsset"),
            false,
            {
                LOCTEXT("RuntimeCheckCPU", "CPU runtime data availability and freshness"),
                LOCTEXT("RuntimeCheckGPU", "GPU runtime data and simulation lookup availability and freshness"),
                LOCTEXT("RuntimeCheckSave", "Unsaved runtime payload changes")
            }
        },
        {
            EWCAValidationSection::GeneratedMaterials,
            EDWCValidationSectionGroup::GeneratedAssets,
            40,
            LOCTEXT("MaterialsTitle", "Generated Materials"),
            LOCTEXT("MaterialsNotApplicable", "No material slots are marked Wettable."),
            TEXT("ClassIcon.Material"),
            false,
            {
                LOCTEXT("MaterialsCheckAvailability", "Generated material availability"),
                LOCTEXT("MaterialsCheckSource", "Source material changes and parent consistency"),
                LOCTEXT("MaterialsCheckFunctions", "Required material functions and runtime parameters")
            }
        },
        {
            EWCAValidationSection::RenderProfileData,
            EDWCValidationSectionGroup::GeneratedAssets,
            50,
            LOCTEXT("RenderProfileTitle", "Render Profile Lookup Texture"),
            LOCTEXT("RenderProfileNotApplicable", "No wettable material slots require a Render Profile Lookup Texture."),
            TEXT("ClassIcon.Texture2D"),
            false,
            {
                LOCTEXT("RenderProfileCheckTexture", "Wet Part Data Texture and local profile mapping"),
                LOCTEXT("RenderProfileCheckWater", "Surface Water profile inputs"),
                LOCTEXT("RenderProfileCheckDroplets", "Prepared Droplet Normal and Mask references"),
                LOCTEXT("RenderProfileCheckSlots", "Material slot connections")
            }
        },
        {
            EWCAValidationSection::WrinkleMaps,
            EDWCValidationSectionGroup::GeneratedAssets,
            60,
            LOCTEXT("WrinkleTitle", "Wrinkle Textures"),
            LOCTEXT("WrinkleNotApplicable", "This asset has no authored or custom wrinkle texture intent."),
            TEXT("ClassIcon.Texture2D"),
            false,
            {
                LOCTEXT("WrinkleCheckBaked", "Baked wrinkle texture availability and freshness"),
                LOCTEXT("WrinkleCheckCustom", "Custom wrinkle texture assignments"),
                LOCTEXT("WrinkleCheckSlots", "Per-slot wrinkle texture output state")
            }
        },
        {
            EWCAValidationSection::TransparencyMaps,
            EDWCValidationSectionGroup::GeneratedAssets,
            70,
            LOCTEXT("TransparencyTitle", "Transparency Textures"),
            LOCTEXT("TransparencyNotApplicable", "This asset has no configured transparency layer intent."),
            TEXT("ClassIcon.Texture2D"),
            false,
            {
                LOCTEXT("TransparencyCheckInputs", "Transparency layer intent and inputs"),
                LOCTEXT("TransparencyCheckSlots", "Source and target material relationships"),
                LOCTEXT("TransparencyCheckOutputs", "Stored transparency texture availability and freshness")
            }
        },
        {
            EWCAValidationSection::FailureDetails,
            EDWCValidationSectionGroup::Internal,
            80,
            LOCTEXT("FailureTitle", "Internal Failure"),
            LOCTEXT("FailureNotApplicable", "No unclassified internal failures were recorded."),
            TEXT("DWCEditor.Validation.Failure"),
            true,
            {
                LOCTEXT("FailureCheckRecent", "Unclassified internal build and validation failures")
            }
        }
    };
    return Descriptors;
}
}

TConstArrayView<FDWCValidationSectionDescriptor> FDWCEditorValidationSectionRegistry::GetSections()
{
    return GetSectionDescriptors();
}

const FDWCValidationSectionDescriptor* FDWCEditorValidationSectionRegistry::Find(
    const EWCAValidationSection Section)
{
    return GetSectionDescriptors().FindByPredicate(
        [Section](const FDWCValidationSectionDescriptor& Descriptor)
        {
            return Descriptor.Section == Section;
        });
}

EWCAValidationSection FDWCEditorValidationSectionRegistry::MapDomain(
    const EDWCEditorValidationDomain Domain)
{
    switch (Domain)
    {
    case EDWCEditorValidationDomain::Asset: return EWCAValidationSection::Asset;
    case EDWCEditorValidationDomain::DataUV: return EWCAValidationSection::DataUV;
    case EDWCEditorValidationDomain::WetPart: return EWCAValidationSection::WetPart;
    case EDWCEditorValidationDomain::RuntimeCPU:
    case EDWCEditorValidationDomain::RuntimeGPU:
    case EDWCEditorValidationDomain::GPUSimulationMap:
        return EWCAValidationSection::RuntimeData;
    case EDWCEditorValidationDomain::GeneratedMaterial: return EWCAValidationSection::GeneratedMaterials;
    case EDWCEditorValidationDomain::RenderProfile: return EWCAValidationSection::RenderProfileData;
    case EDWCEditorValidationDomain::Wrinkle: return EWCAValidationSection::WrinkleMaps;
    case EDWCEditorValidationDomain::Transparency: return EWCAValidationSection::TransparencyMaps;
    case EDWCEditorValidationDomain::Failure:
    default:
        return EWCAValidationSection::FailureDetails;
    }
}

EWCAValidationSection FDWCEditorValidationSectionRegistry::MapAction(
    const EDWCEditorBuildAction Action)
{
    switch (Action)
    {
    case EDWCEditorBuildAction::SaveAsset: return EWCAValidationSection::Asset;
    case EDWCEditorBuildAction::InitializeDataUV: return EWCAValidationSection::DataUV;
    case EDWCEditorBuildAction::BuildCPURuntimeData:
    case EDWCEditorBuildAction::BuildGPURuntimeData:
        return EWCAValidationSection::RuntimeData;
    case EDWCEditorBuildAction::GenerateMaterials: return EWCAValidationSection::GeneratedMaterials;
    case EDWCEditorBuildAction::BakeRenderProfileData: return EWCAValidationSection::RenderProfileData;
    case EDWCEditorBuildAction::BakeWrinkleTextures: return EWCAValidationSection::WrinkleMaps;
    case EDWCEditorBuildAction::BakeTransparencyTextures:
    case EDWCEditorBuildAction::RebakeAffectedTransparencyMaps:
        return EWCAValidationSection::TransparencyMaps;
    default:
        return EWCAValidationSection::FailureDetails;
    }
}

int32 FDWCEditorValidationSectionRegistry::GetStatePriority(
    const EDWCEditorValidationOverallState State)
{
    switch (State)
    {
    case EDWCEditorValidationOverallState::Failed: return 140;
    case EDWCEditorValidationOverallState::Blocked: return 130;
    case EDWCEditorValidationOverallState::Invalid: return 120;
    case EDWCEditorValidationOverallState::Missing: return 110;
    case EDWCEditorValidationOverallState::Running: return 105;
    case EDWCEditorValidationOverallState::Stale: return 100;
    case EDWCEditorValidationOverallState::Partial: return 90;
    case EDWCEditorValidationOverallState::Cancelled: return 80;
    case EDWCEditorValidationOverallState::SavePending: return 60;
    case EDWCEditorValidationOverallState::Draft: return 50;
    case EDWCEditorValidationOverallState::Current: return 40;
    case EDWCEditorValidationOverallState::Disabled: return 30;
    case EDWCEditorValidationOverallState::NotConfigured: return 20;
    case EDWCEditorValidationOverallState::NotApplicable:
    default:
        return 10;
    }
}

EDWCValidationPresentationState FDWCEditorValidationSectionRegistry::MapPresentationState(
    const EDWCEditorValidationOverallState State)
{
    switch (State)
    {
    case EDWCEditorValidationOverallState::Failed:
    case EDWCEditorValidationOverallState::Blocked:
    case EDWCEditorValidationOverallState::Invalid:
    case EDWCEditorValidationOverallState::Missing:
        return EDWCValidationPresentationState::Error;
    case EDWCEditorValidationOverallState::Stale:
    case EDWCEditorValidationOverallState::Partial:
    case EDWCEditorValidationOverallState::Cancelled:
    case EDWCEditorValidationOverallState::SavePending:
        return EDWCValidationPresentationState::Warning;
    case EDWCEditorValidationOverallState::Running:
    case EDWCEditorValidationOverallState::Draft:
        return EDWCValidationPresentationState::Info;
    case EDWCEditorValidationOverallState::Current:
        return EDWCValidationPresentationState::Success;
    case EDWCEditorValidationOverallState::Disabled:
    case EDWCEditorValidationOverallState::NotConfigured:
    case EDWCEditorValidationOverallState::NotApplicable:
    default:
        return EDWCValidationPresentationState::Neutral;
    }
}

FText FDWCEditorValidationSectionRegistry::GetStateLabel(
    const EDWCEditorValidationOverallState State)
{
    switch (State)
    {
    case EDWCEditorValidationOverallState::NotApplicable: return LOCTEXT("StateNotApplicable", "Not Applicable");
    case EDWCEditorValidationOverallState::NotConfigured: return LOCTEXT("StateNotConfigured", "Not Configured");
    case EDWCEditorValidationOverallState::Draft: return LOCTEXT("StateDraft", "Draft");
    case EDWCEditorValidationOverallState::Disabled: return LOCTEXT("StateDisabled", "Disabled");
    case EDWCEditorValidationOverallState::Current: return LOCTEXT("StateCurrent", "Current");
    case EDWCEditorValidationOverallState::SavePending: return LOCTEXT("StateSavePending", "Save Pending");
    case EDWCEditorValidationOverallState::Partial: return LOCTEXT("StatePartial", "Partial");
    case EDWCEditorValidationOverallState::Stale: return LOCTEXT("StateStale", "Out of Date");
    case EDWCEditorValidationOverallState::Missing: return LOCTEXT("StateMissing", "Missing");
    case EDWCEditorValidationOverallState::Invalid: return LOCTEXT("StateInvalid", "Invalid");
    case EDWCEditorValidationOverallState::Blocked: return LOCTEXT("StateBlocked", "Blocked");
    case EDWCEditorValidationOverallState::Running: return LOCTEXT("StateRunning", "Building");
    case EDWCEditorValidationOverallState::Failed: return LOCTEXT("StateFailed", "Failed");
    case EDWCEditorValidationOverallState::Cancelled: return LOCTEXT("StateCancelled", "Cancelled");
    default: return LOCTEXT("StateUnknown", "Unknown");
    }
}

FText FDWCEditorValidationSectionRegistry::GetStateDescription(
    const EDWCEditorValidationOverallState State)
{
    switch (State)
    {
    case EDWCEditorValidationOverallState::Current:
        return LOCTEXT("StateCurrentDescription", "No active issues. All checks in this section passed.");
    case EDWCEditorValidationOverallState::NotConfigured:
        return LOCTEXT("StateNotConfiguredDescription", "This feature has not been configured for the asset.");
    case EDWCEditorValidationOverallState::Draft:
        return LOCTEXT("StateDraftDescription", "Authoring is in progress and no runtime output is required yet.");
    case EDWCEditorValidationOverallState::Disabled:
        return LOCTEXT("StateDisabledDescription", "This feature is disabled for the asset.");
    case EDWCEditorValidationOverallState::Running:
        return LOCTEXT("StateRunningDescription", "A build operation is currently updating this section.");
    case EDWCEditorValidationOverallState::SavePending:
        return LOCTEXT("StateSavePendingDescription", "The current result is valid but has unsaved changes.");
    case EDWCEditorValidationOverallState::Cancelled:
        return LOCTEXT("StateCancelledDescription", "The last operation was cancelled. Refresh or rebuild when ready.");
    default:
        return FText::GetEmpty();
    }
}

FText FDWCEditorValidationSectionRegistry::GetGroupTitle(
    const EDWCValidationSectionGroup Group)
{
    switch (Group)
    {
    case EDWCValidationSectionGroup::Asset: return LOCTEXT("GroupAsset", "ASSET");
    case EDWCValidationSectionGroup::Authoring: return LOCTEXT("GroupAuthoring", "AUTHORING & UV");
    case EDWCValidationSectionGroup::Runtime: return LOCTEXT("GroupRuntime", "RUNTIME DATA");
    case EDWCValidationSectionGroup::GeneratedAssets: return LOCTEXT("GroupGenerated", "GENERATED ASSETS");
    case EDWCValidationSectionGroup::Internal: return LOCTEXT("GroupInternal", "INTERNAL");
    default: return FText::GetEmpty();
    }
}

#undef LOCTEXT_NAMESPACE
