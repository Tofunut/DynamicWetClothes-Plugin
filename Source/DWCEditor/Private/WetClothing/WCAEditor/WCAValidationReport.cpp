#include "WetClothing/WCAEditor/WCAValidationReport.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetPartDataTextureBaker.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"

namespace
{
    FString BakeStatusToString(const EDWCBakeStatus Status)
    {
        switch (Status)
        {
        case EDWCBakeStatus::Disabled: return TEXT("Disabled");
        case EDWCBakeStatus::Required: return TEXT("Required");
        case EDWCBakeStatus::Valid: return TEXT("Valid");
        case EDWCBakeStatus::ValidWithDiagnostics: return TEXT("Valid With Diagnostics");
        case EDWCBakeStatus::OutOfDate: return TEXT("Out of Date");
        case EDWCBakeStatus::Failed: return TEXT("Failed");
        default: return TEXT("Unknown");
        }
    }

    EWCAValidationSeverity SeverityForStatus(
        const EDWCBakeStatus Status,
        const EWCAValidationSeverity NonFailedSeverity = EWCAValidationSeverity::Warning)
    {
        return Status == EDWCBakeStatus::Failed ? EWCAValidationSeverity::Error : NonFailedSeverity;
    }

    EWCAValidationSeverity RuntimeSeverity(const EDWCBakeStatus Status, const bool bHasPayload)
    {
        if (Status == EDWCBakeStatus::Failed)
        {
            return EWCAValidationSeverity::Error;
        }
        if (Status == EDWCBakeStatus::Required && !bHasPayload)
        {
            return EWCAValidationSeverity::Info;
        }
        return EWCAValidationSeverity::Warning;
    }

    void AddIssue(
        FWCAValidationReport& Report,
        const FName IssueId,
        const EWCAValidationSeverity Severity,
        const EWCAValidationIssueCategory Category,
        const EWCAValidationFixKind FixKind,
        const FText& Title,
        const FText& Status,
        const FText& Detail,
        const FText& RequiredAction,
        const bool bFailed = false)
    {
        FWCAValidationIssue& Issue = Report.Issues.AddDefaulted_GetRef();
        Issue.IssueId = IssueId;
        Issue.Severity = Severity;
        Issue.Category = Category;
        Issue.FixKind = FixKind;
        Issue.Title = Title;
        Issue.Status = Status;
        Issue.Detail = Detail;
        Issue.RequiredAction = RequiredAction;
        Issue.bFailed = bFailed || Severity == EWCAValidationSeverity::Error;
    }

    bool IsActionRequiredStatus(const EDWCBakeStatus Status)
    {
        return Status == EDWCBakeStatus::Required ||
               Status == EDWCBakeStatus::OutOfDate ||
               Status == EDWCBakeStatus::Failed;
    }

    void AddBakeStatusIssueIfRequired(
        FWCAValidationReport& Report,
        const FName IssueId,
        const FText& Title,
        const EDWCBakeStatus Status,
        const EWCAValidationIssueCategory Category,
        const EWCAValidationFixKind FixKind,
        const FText& RequiredAction,
        const FString& Detail,
        const bool bSavePending = false)
    {
        if (!IsActionRequiredStatus(Status) && !bSavePending)
        {
            return;
        }

        AddIssue(
            Report,
            IssueId,
            SeverityForStatus(Status),
            Category,
            bSavePending && DWCBuildStatus::IsUsable(Status) ? EWCAValidationFixKind::Save : FixKind,
            Title,
            bSavePending && DWCBuildStatus::IsUsable(Status)
                ? NSLOCTEXT("WCAValidationReport", "SaveRequiredStatus", "Save Required")
                : FText::FromString(BakeStatusToString(Status)),
            FText::FromString(Detail),
            bSavePending && DWCBuildStatus::IsUsable(Status)
                ? NSLOCTEXT("WCAValidationReport", "SaveRequiredAction", "Save the asset to persist the current data.")
                : RequiredAction,
            Status == EDWCBakeStatus::Failed);
    }

    FString BuildRuntimeDetail(
        const TCHAR* Label,
        const EDWCBakeStatus Status,
        const bool bHasPayload,
        const bool bWasEverGenerated,
        const bool bWasEverSaved,
        const bool bAssetHasUnsavedChanges,
        const bool bSavePending,
        const FString& FailureDetails)
    {
        (void)bAssetHasUnsavedChanges;
        const bool bHasPriorOutput = bHasPayload || bWasEverGenerated || bWasEverSaved;
        if (DWCBuildStatus::IsUsable(Status) && bSavePending)
        {
            return FString::Printf(TEXT("%s: Generated and current, but not saved yet."), Label);
        }
        if (Status == EDWCBakeStatus::Required && !bHasPriorOutput)
        {
            return FString::Printf(TEXT("%s: Not generated yet."), Label);
        }
        if (Status == EDWCBakeStatus::OutOfDate)
        {
            return FString::Printf(TEXT("%s: Out of date."), Label);
        }
        if (Status == EDWCBakeStatus::Failed && !FailureDetails.IsEmpty())
        {
            return FString::Printf(TEXT("%s: Failed. %s"), Label, *FailureDetails);
        }
        return FString::Printf(TEXT("%s: %s."), Label, *BakeStatusToString(Status));
    }

    FString BuildMapDetail(
        const TCHAR* Label,
        const EDWCBakeStatus Status,
        const bool bSavePending)
    {
        if (DWCBuildStatus::IsUsable(Status) && bSavePending)
        {
            return FString::Printf(TEXT("%s: Baked and current, but not saved yet."), Label);
        }
        return FString::Printf(TEXT("%s: %s."), Label, *BakeStatusToString(Status));
    }

    void CollectWrinkleMaterialSlots(const UWetClothingAsset& Asset, TSet<int32>& OutMaterialSlots)
    {
        const FWetClothingWrinkleData& WrinkleData = Asset.Authored.WrinkleData;
        const int32 UVChannelIndex = Asset.GetDWCDataUVChannelIndex();
        for (const FWetWrinklePatchPlacement& Patch : WrinkleData.EditablePatches)
        {
            if ((!Patch.bEnabled && !WrinkleData.BakeSettings.bIncludeDisabledPatches) ||
                Patch.MaterialSlotIndex == INDEX_NONE ||
                Patch.UVChannelIndex != UVChannelIndex ||
                !Asset.IsMaterialSlotWettable(Patch.MaterialSlotIndex))
            {
                continue;
            }
            OutMaterialSlots.Add(Patch.MaterialSlotIndex);
        }
        for (const FWetProceduralRidgeStroke& Stroke : WrinkleData.EditableProceduralRidgeStrokes)
        {
            if ((!Stroke.bEnabled && !WrinkleData.BakeSettings.bIncludeDisabledPatches) ||
                Stroke.MaterialSlotIndex == INDEX_NONE ||
                Stroke.UVChannelIndex != UVChannelIndex ||
                !Asset.IsMaterialSlotWettable(Stroke.MaterialSlotIndex))
            {
                continue;
            }
            OutMaterialSlots.Add(Stroke.MaterialSlotIndex);
        }
        for (const FWetWrinkleBakedMapSet& BakedMap : WrinkleData.BakedWrinkleMaps)
        {
            if (BakedMap.MaterialSlotIndex != INDEX_NONE && Asset.IsMaterialSlotWettable(BakedMap.MaterialSlotIndex))
            {
                OutMaterialSlots.Add(BakedMap.MaterialSlotIndex);
            }
        }
    }

    constexpr float MinSurfaceWaterRepresentationFraction = 0.05f;
    constexpr float MinSurfaceWaterRejectedFraction = 0.05f;
    constexpr float MinSurfaceWaterDropletSpawnProbability = 0.05f;
    constexpr float MinSurfaceWaterDropletLifetimeSeconds = 0.25f;
    constexpr float MinSurfaceWaterDropletRadiusPixels = 1.0f;

    FString DescribeWetPartProfile(
        const FWetPartProfileAssignment* Profile,
        const int32 ProfileIndex)
    {
        if (Profile != nullptr && Profile->SourceProfile.IsValid())
        {
            return FString::Printf(TEXT("profile '%s'"), *Profile->SourceProfile.GetAssetName());
        }
        if (ProfileIndex == 0)
        {
            return TEXT("the default inline profile");
        }
        return FString::Printf(TEXT("profile %d"), ProfileIndex);
    }

    void CollectSurfaceWaterProfileProblems(
        const FWetnessProfileParameters& Parameters,
        TArray<FString>& OutProblems)
    {
        OutProblems.Reset();

        const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
        if (!Surface.bEnabled)
        {
            return;
        }

        if (!Surface.bEnableDroplets)
        {
            OutProblems.Add(TEXT("bEnableDroplets is disabled"));
        }

        const float SurfaceRepresentationFraction =
            FMath::Clamp(Surface.SurfaceRepresentationFraction, 0.0f, 1.0f);
        const float RejectedWaterFraction =
            FMath::Clamp(Parameters.GetRejectedWaterFraction(), 0.0f, 1.0f);
        const float MaxSurfaceAmount = SurfaceRepresentationFraction * RejectedWaterFraction;

        if (SurfaceRepresentationFraction < MinSurfaceWaterRepresentationFraction)
        {
            OutProblems.Add(FString::Printf(
                TEXT("SurfaceRepresentationFraction %.3f is below %.3f"),
                Surface.SurfaceRepresentationFraction,
                MinSurfaceWaterRepresentationFraction));
        }
        if (RejectedWaterFraction < MinSurfaceWaterRejectedFraction)
        {
            OutProblems.Add(FString::Printf(
                TEXT("rejected water fraction %.3f is below %.3f because AbsorptionFraction is too high"),
                RejectedWaterFraction,
                MinSurfaceWaterRejectedFraction));
        }
        if (Surface.DropletSpawnProbability < MinSurfaceWaterDropletSpawnProbability)
        {
            OutProblems.Add(FString::Printf(
                TEXT("DropletSpawnProbability %.3f is below %.3f"),
                Surface.DropletSpawnProbability,
                MinSurfaceWaterDropletSpawnProbability));
        }
        if (Surface.DropletLifetimeSeconds < MinSurfaceWaterDropletLifetimeSeconds)
        {
            OutProblems.Add(FString::Printf(
                TEXT("DropletLifetimeSeconds %.3f is below %.3f"),
                Surface.DropletLifetimeSeconds,
                MinSurfaceWaterDropletLifetimeSeconds));
        }
        if (Surface.DropletRadiusPixels < MinSurfaceWaterDropletRadiusPixels)
        {
            OutProblems.Add(FString::Printf(
                TEXT("DropletRadiusPixels %.3f is below %.3f"),
                Surface.DropletRadiusPixels,
                MinSurfaceWaterDropletRadiusPixels));
        }
        if (MaxSurfaceAmount > UE_KINDA_SMALL_NUMBER &&
            Surface.SurfaceVisibilityThreshold >= MaxSurfaceAmount)
        {
            OutProblems.Add(FString::Printf(
                TEXT("SurfaceVisibilityThreshold %.3f is not below the maximum possible surface amount %.3f"),
                Surface.SurfaceVisibilityThreshold,
                MaxSurfaceAmount));
        }
    }

    void AddSurfaceWaterInputIssues(
        FWCAValidationReport& Report,
        const UWetClothingAsset& Asset)
    {
        const FWetClothingEditableWetPartData& EditableData = Asset.Authored.PartData.EditableWetPartData;
        TSet<int32> ReportedProfileIndices;

        for (const FWetClothingAuthoredMaterialSlot& Slot : EditableData.MaterialSlots)
        {
            if (!Slot.bIsWettableSlot || Slot.MaterialSlotIndex == INDEX_NONE)
            {
                continue;
            }

            for (const FWetClothingWetPartEntry& Entry : Slot.WetPartEntries)
            {
                if (Entry.AssignedUVIslandIDs.IsEmpty() ||
                    ReportedProfileIndices.Contains(Entry.ProfileIndex))
                {
                    continue;
                }

                const FWetPartProfileAssignment* Profile = EditableData.FindProfile(Entry);
                FWetnessProfileParameters Parameters;
                FWetClothingWetPartDataTextureBaker::ResolveProfileParameters(Profile, Parameters);
                TArray<FString> Problems;
                CollectSurfaceWaterProfileProblems(Parameters, Problems);
                if (Problems.IsEmpty())
                {
                    continue;
                }

                ReportedProfileIndices.Add(Entry.ProfileIndex);
                AddIssue(
                    Report,
                    FName(*FString::Printf(TEXT("SurfaceWaterProfileBounds_Profile%d"), Entry.ProfileIndex)),
                    EWCAValidationSeverity::Warning,
                    EWCAValidationIssueCategory::Map,
                    EWCAValidationFixKind::FixSurfaceWaterProfile,
                    NSLOCTEXT("WCAValidationReport", "SurfaceWaterInputTitle", "Surface Water Input"),
                    NSLOCTEXT("WCAValidationReport", "SurfaceWaterProfileFixStatus", "Fix Available"),
                    FText::FromString(FString::Printf(
                        TEXT("Surface Water: %s used by Wet Part %d in slot %d has values that can prevent droplet stamps from rendering: %s."),
                        *DescribeWetPartProfile(Profile, Entry.ProfileIndex),
                        Entry.WetPartID,
                        Slot.MaterialSlotIndex,
                        *FString::Join(Problems, TEXT("; ")))),
                    NSLOCTEXT("WCAValidationReport", "SurfaceWaterProfileFixAction", "Use Resolve to clamp the Surface Water profile to renderable minimum values, then rebuild dependent render data."));
            }
        }
    }

    void AppendIssueSection(
        TArray<FString>& Sections,
        const TCHAR* Heading,
        const FWCAValidationReport& Report,
        const EWCAValidationIssueCategory Category,
        const bool bManualOnly)
    {
        TArray<FString> Lines;
        for (const FWCAValidationIssue& Issue : Report.Issues)
        {
            if (Issue.Category != Category ||
                (bManualOnly && Issue.FixKind != EWCAValidationFixKind::Manual))
            {
                continue;
            }

            FString Line = Issue.Detail.IsEmpty()
                ? Issue.Title.ToString()
                : Issue.Detail.ToString();
            if (!Issue.RequiredAction.IsEmpty())
            {
                Line += FString::Printf(TEXT(" %s"), *Issue.RequiredAction.ToString());
            }
            Lines.Add(Line);
        }
        if (!Lines.IsEmpty())
        {
            Sections.Add(FString::Printf(TEXT("%s\n%s"), Heading, *FString::Join(Lines, TEXT("\n"))));
        }
    }
}

bool FWCAValidationReport::HasManualIssues() const
{
    return Issues.ContainsByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return Issue.FixKind == EWCAValidationFixKind::Manual;
        });
}

bool FWCAValidationReport::HasAutoResolvableIssues() const
{
    return Issues.ContainsByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return Issue.FixKind != EWCAValidationFixKind::None &&
                   Issue.FixKind != EWCAValidationFixKind::Manual;
        });
}

FString FWCAValidationReport::BuildSummary() const
{
    TArray<FString> Sections;
    AppendIssueSection(Sections, TEXT("DWC Data UV"), *this, EWCAValidationIssueCategory::DataUV, false);
    AppendIssueSection(Sections, TEXT("Runtime Data"), *this, EWCAValidationIssueCategory::Runtime, false);
    AppendIssueSection(Sections, TEXT("Texture Maps"), *this, EWCAValidationIssueCategory::Map, false);
    AppendIssueSection(Sections, TEXT("Generated Materials"), *this, EWCAValidationIssueCategory::Material, false);
    AppendIssueSection(Sections, TEXT("Failures"), *this, EWCAValidationIssueCategory::Failure, false);
    return FString::Join(Sections, TEXT("\n\n"));
}

FString FWCAValidationReport::BuildManualIssueSummary() const
{
    TArray<FString> Sections;
    AppendIssueSection(Sections, TEXT("Manual Fix Required"), *this, EWCAValidationIssueCategory::DataUV, true);
    AppendIssueSection(Sections, TEXT("Manual Fix Required"), *this, EWCAValidationIssueCategory::Runtime, true);
    AppendIssueSection(Sections, TEXT("Manual Fix Required"), *this, EWCAValidationIssueCategory::Map, true);
    AppendIssueSection(Sections, TEXT("Manual Fix Required"), *this, EWCAValidationIssueCategory::Material, true);
    AppendIssueSection(Sections, TEXT("Manual Fix Required"), *this, EWCAValidationIssueCategory::Failure, true);
    return FString::Join(Sections, TEXT("\n\n"));
}

FWCAValidationReport BuildWCAValidationReport(
    UWetClothingAsset& Asset,
    const EWCAValidationMode Mode,
    const bool bRefreshAssetState)
{
    FWCAValidationReport Report;
#if WITH_EDITORONLY_DATA
    if (bRefreshAssetState)
    {
        Asset.RefreshBakeState(Mode == EWCAValidationMode::Deep);
    }

    Report.Diagnostics = Asset.GetValidationSummary();
    const FDWCAssetBakeState& State = Asset.GetBakeState();
    const FDWCWetClothingAssetSetupSettings& Setup = Asset.GetSetupSettings();
    const bool bAssetHasUnsavedChanges = Asset.GetOutermost() != nullptr && Asset.GetOutermost()->IsDirty();
    FString RuntimePreparationReason;
    const bool bCanPrepareRuntimeDataForSave =
        Asset.CanPrepareRuntimeDataForEditorSave(&RuntimePreparationReason);

    auto BuildRuntimeValidationDetail = [&RuntimePreparationReason, bCanPrepareRuntimeDataForSave](
        const FString& BaseDetail,
        const EDWCBakeStatus Status)
    {
        if (bCanPrepareRuntimeDataForSave || DWCBuildStatus::IsUsable(Status) || RuntimePreparationReason.IsEmpty())
        {
            return BaseDetail;
        }
        return FString::Printf(
            TEXT("%s Runtime data cannot be prepared on save: %s"),
            *BaseDetail,
            *RuntimePreparationReason);
    };

    auto GetRuntimeFixKind = [bCanPrepareRuntimeDataForSave](
        const EDWCBakeStatus Status,
        const bool bSavePending)
    {
        if (bSavePending && DWCBuildStatus::IsUsable(Status))
        {
            return EWCAValidationFixKind::Save;
        }
        return bCanPrepareRuntimeDataForSave
            ? EWCAValidationFixKind::PrepareRuntimeData
            : EWCAValidationFixKind::Manual;
    };

    auto GetRuntimeRequiredAction = [bCanPrepareRuntimeDataForSave](
        const EDWCBakeStatus Status,
        const bool bSavePending)
    {
        if (bSavePending && DWCBuildStatus::IsUsable(Status))
        {
            return NSLOCTEXT("WCAValidationReport", "RuntimeDataSaveAction", "Save the asset to persist it.");
        }
        return bCanPrepareRuntimeDataForSave
            ? NSLOCTEXT("WCAValidationReport", "RuntimeDataAction", "Save the asset to rebuild or persist it.")
            : NSLOCTEXT("WCAValidationReport", "RuntimeDataPrerequisiteAction", "Resolve the runtime-data prerequisite, then save the asset.");
    };

    AddBakeStatusIssueIfRequired(
        Report,
        TEXT("DWCDataUV"),
        NSLOCTEXT("WCAValidationReport", "DWCDataUVTitle", "DWC Data UV"),
        State.GeneratedDataUV,
        EWCAValidationIssueCategory::DataUV,
        EWCAValidationFixKind::RebuildDataUV,
        NSLOCTEXT("WCAValidationReport", "DWCDataUVAction", "Use Rebuild DWC Data UV on the toolbar to rebuild it."),
        FString::Printf(TEXT("DWC Data UV: %s."), *BakeStatusToString(State.GeneratedDataUV)));

    AddBakeStatusIssueIfRequired(
        Report,
        TEXT("OriginalUVTopology"),
        NSLOCTEXT("WCAValidationReport", "OriginalUVTopologyTitle", "Original UV Topology"),
        State.OriginalUVTopology,
        EWCAValidationIssueCategory::DataUV,
        EWCAValidationFixKind::RebuildDataUV,
        NSLOCTEXT("WCAValidationReport", "OriginalUVTopologyAction", "Rebuild DWC Data UV."),
        FString::Printf(TEXT("Original UV Topology: %s."), *BakeStatusToString(State.OriginalUVTopology)));

    if ((Setup.bBuildCPUVertexSimulationData || Asset.HasCPURuntimeDataPayload()) &&
        State.CPURuntimeData != EDWCBakeStatus::Disabled)
    {
        const bool bSavePending = Asset.IsBakeOutputSavePending(DWCBakeOutput::CPURuntimeData);
        if (!DWCBuildStatus::IsUsable(State.CPURuntimeData) || bSavePending)
        {
            const bool bHasPayload = Asset.HasCPURuntimeDataPayload();
            AddIssue(
                Report,
                TEXT("CPURuntimeData"),
                RuntimeSeverity(State.CPURuntimeData, bHasPayload || Asset.HasGeneratedBakeOutput(DWCBakeOutput::CPURuntimeData) || Asset.HasSavedBakeOutput(DWCBakeOutput::CPURuntimeData)),
                EWCAValidationIssueCategory::Runtime,
                GetRuntimeFixKind(State.CPURuntimeData, bSavePending),
                NSLOCTEXT("WCAValidationReport", "CPURuntimeDataTitle", "CPU Runtime Data"),
                bSavePending && DWCBuildStatus::IsUsable(State.CPURuntimeData) ? NSLOCTEXT("WCAValidationReport", "CPURuntimeSaveRequired", "Save Required") : FText::FromString(BakeStatusToString(State.CPURuntimeData)),
                FText::FromString(BuildRuntimeValidationDetail(
                    BuildRuntimeDetail(TEXT("CPU Runtime Data"), State.CPURuntimeData, bHasPayload, Asset.HasGeneratedBakeOutput(DWCBakeOutput::CPURuntimeData), Asset.HasSavedBakeOutput(DWCBakeOutput::CPURuntimeData), bAssetHasUnsavedChanges, bSavePending, State.LastFailure),
                    State.CPURuntimeData)),
                GetRuntimeRequiredAction(State.CPURuntimeData, bSavePending),
                State.CPURuntimeData == EDWCBakeStatus::Failed);
        }
    }

    if ((Setup.bBuildGPUWetnessMapSimulationData || Asset.HasGPURuntimeDataPayload()) &&
        State.GPURuntimeData != EDWCBakeStatus::Disabled)
    {
        const bool bSavePending = Asset.IsBakeOutputSavePending(DWCBakeOutput::GPURuntimeData);
        if (!DWCBuildStatus::IsUsable(State.GPURuntimeData) || bSavePending)
        {
            const bool bHasPayload = Asset.HasGPURuntimeDataPayload();
            AddIssue(
                Report,
                TEXT("GPURuntimeData"),
                RuntimeSeverity(State.GPURuntimeData, bHasPayload || Asset.HasGeneratedBakeOutput(DWCBakeOutput::GPURuntimeData) || Asset.HasSavedBakeOutput(DWCBakeOutput::GPURuntimeData)),
                EWCAValidationIssueCategory::Runtime,
                GetRuntimeFixKind(State.GPURuntimeData, bSavePending),
                NSLOCTEXT("WCAValidationReport", "GPURuntimeDataTitle", "GPU Runtime Data"),
                bSavePending && DWCBuildStatus::IsUsable(State.GPURuntimeData) ? NSLOCTEXT("WCAValidationReport", "GPURuntimeSaveRequired", "Save Required") : FText::FromString(BakeStatusToString(State.GPURuntimeData)),
                FText::FromString(BuildRuntimeValidationDetail(
                    BuildRuntimeDetail(TEXT("GPU Runtime Data"), State.GPURuntimeData, bHasPayload, Asset.HasGeneratedBakeOutput(DWCBakeOutput::GPURuntimeData), Asset.HasSavedBakeOutput(DWCBakeOutput::GPURuntimeData), bAssetHasUnsavedChanges, bSavePending, State.LastFailure),
                    State.GPURuntimeData)),
                GetRuntimeRequiredAction(State.GPURuntimeData, bSavePending),
                State.GPURuntimeData == EDWCBakeStatus::Failed);
        }
    }

    AddBakeStatusIssueIfRequired(
        Report,
        TEXT("GPUMaps"),
        NSLOCTEXT("WCAValidationReport", "GPUMapsTitle", "GPU Simulation Maps"),
        State.GPUMaps,
        EWCAValidationIssueCategory::Map,
        EWCAValidationFixKind::BakeGPUMaps,
        NSLOCTEXT("WCAValidationReport", "BakeMapsAction", "Use Bake Maps to rebuild it."),
        BuildMapDetail(TEXT("GPU Simulation Maps"), State.GPUMaps, Asset.IsBakeOutputSavePending(DWCBakeOutput::GPUMaps)),
        Asset.IsBakeOutputSavePending(DWCBakeOutput::GPUMaps));

    TArray<FString> GeneratedMaterialMessages;
    if (Asset.HasAnyWettableMaterialSlot())
    {
        if (Mode == EWCAValidationMode::Deep)
        {
            FWCAMaterialGenerator::ValidateGeneratedMaterialOverrides(&Asset, GeneratedMaterialMessages);
        }
        else
        {
            FWCAMaterialGenerator::ValidateGeneratedMaterialOverrideReferences(&Asset, GeneratedMaterialMessages);
        }
    }
    if (!GeneratedMaterialMessages.IsEmpty())
    {
        AddIssue(
            Report,
            TEXT("GeneratedMaterials"),
            EWCAValidationSeverity::Warning,
            EWCAValidationIssueCategory::Material,
            EWCAValidationFixKind::GenerateMaterials,
            NSLOCTEXT("WCAValidationReport", "GeneratedMaterialsTitle", "Generated Materials"),
            FText::Format(NSLOCTEXT("WCAValidationReport", "GeneratedMaterialsStatus", "{0} issue(s)"), FText::AsNumber(GeneratedMaterialMessages.Num())),
            FText::FromString(FString::Join(GeneratedMaterialMessages, TEXT("\n"))),
            NSLOCTEXT("WCAValidationReport", "GeneratedMaterialsAction", "Use Generate Materials."));
    }

    if (Asset.HasWrinkleBakeContent() && !DWCBuildStatus::IsUsable(State.WrinkleMaps))
    {
        AddIssue(
            Report,
            TEXT("WrinkleMaps"),
            SeverityForStatus(State.WrinkleMaps),
            EWCAValidationIssueCategory::Map,
            EWCAValidationFixKind::BakeWrinkleMaps,
            NSLOCTEXT("WCAValidationReport", "WrinkleMapsTitle", "Wrinkle Maps"),
            FText::FromString(BakeStatusToString(State.WrinkleMaps)),
            FText::FromString(BuildMapDetail(TEXT("Wrinkle Maps"), State.WrinkleMaps, false)),
            NSLOCTEXT("WCAValidationReport", "BakeWrinkleMapsAction", "Use Bake Maps to rebuild it."),
            State.WrinkleMaps == EDWCBakeStatus::Failed);
    }

    if (Mode == EWCAValidationMode::Deep && Asset.HasWrinkleBakeContent())
    {
        TSet<int32> WrinkleMaterialSlots;
        CollectWrinkleMaterialSlots(Asset, WrinkleMaterialSlots);
        for (const int32 MaterialSlotIndex : WrinkleMaterialSlots)
        {
            if (!Asset.Authored.WrinkleData.IsUsingCustomWrinkleNormalMap(MaterialSlotIndex, Asset.GetDWCDataUVChannelIndex(), UWetClothingAsset::RuntimeSimulationLODIndex) &&
                !FWetWrinkleNormalMapBaker::IsMaterialSlotBakeCurrent(&Asset, MaterialSlotIndex))
            {
                AddIssue(
                    Report,
                    FName(*FString::Printf(TEXT("WrinkleMapsStale_Slot%d"), MaterialSlotIndex)),
                    EWCAValidationSeverity::Warning,
                    EWCAValidationIssueCategory::Map,
                    EWCAValidationFixKind::BakeWrinkleMaps,
                    NSLOCTEXT("WCAValidationReport", "WrinkleMapsTitle", "Wrinkle Maps"),
                    NSLOCTEXT("WCAValidationReport", "OutOfDateStatus", "Out of Date"),
                    FText::FromString(FString::Printf(TEXT("Wrinkle Maps: Slot %d is missing or was built from old authored data."), MaterialSlotIndex)),
                    NSLOCTEXT("WCAValidationReport", "BakeWrinkleMapsAction", "Use Bake Maps to rebuild it."));
            }
        }
    }

    for (const FWetWrinkleRuntimeNormalSource& Source : Asset.Authored.WrinkleData.RuntimeNormalSources)
    {
        if (Source.Source == EDWCWrinkleNormalSource::CustomTexture &&
            Asset.IsMaterialSlotWettable(Source.MaterialSlotIndex) &&
            Source.CustomWrinkleNormalMap == nullptr)
        {
            AddIssue(
                Report,
                FName(*FString::Printf(TEXT("CustomWrinkleTextureMissing_Slot%d"), Source.MaterialSlotIndex)),
                EWCAValidationSeverity::Warning,
                EWCAValidationIssueCategory::Map,
                EWCAValidationFixKind::Manual,
                NSLOCTEXT("WCAValidationReport", "CustomWrinkleTextureTitle", "Wrinkle Maps"),
                NSLOCTEXT("WCAValidationReport", "ManualFixStatus", "Manual Fix"),
                FText::FromString(FString::Printf(TEXT("Wrinkle Maps: Slot %d uses Custom Wrinkle Map but no texture is assigned."), Source.MaterialSlotIndex)),
                NSLOCTEXT("WCAValidationReport", "CustomWrinkleTextureAction", "Assign a Custom Wrinkle Normal Map or switch the slot back to baked wrinkle maps."));
        }
    }

    if (Asset.HasTransparencyBakeContent())
    {
        for (const FWetClothingTransparencyLayerData& Layer : Asset.Authored.TransparencyData.TransparencyLayers)
        {
            if (Layer.SourceType != EDWCTransparencySourceType::SameMeshMaterialSlots ||
                Layer.TargetSurface.OuterMaterialSlotIndex == INDEX_NONE ||
                !Asset.IsMaterialSlotWettable(Layer.TargetSurface.OuterMaterialSlotIndex))
            {
                continue;
            }

            TArray<FString> TransparencyErrors;
            if (!FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
                    Asset.GetDWCSkeletalMesh(),
                    Layer,
                    TransparencyErrors,
                    UWetClothingAsset::RuntimeSimulationLODIndex))
            {
                AddIssue(
                    Report,
                    FName(*FString::Printf(TEXT("TransparencyInput_%s"), *Layer.LayerGuid.ToString(EGuidFormats::Digits))),
                    EWCAValidationSeverity::Error,
                    EWCAValidationIssueCategory::Map,
                    EWCAValidationFixKind::Manual,
                    NSLOCTEXT("WCAValidationReport", "TransparencyInputTitle", "Transparency Maps"),
                    NSLOCTEXT("WCAValidationReport", "ManualFixStatus", "Manual Fix"),
                    FText::FromString(FString::Printf(TEXT("Transparency Maps: %s"), *FString::Join(TransparencyErrors, TEXT("\n")))),
                    NSLOCTEXT("WCAValidationReport", "TransparencyInputAction", "Fix the Transparency layer inputs before baking."));
            }
            else if (Mode == EWCAValidationMode::Deep && DWCBuildStatus::IsUsable(State.TransparencyMaps))
            {
                FString CurrentnessReason;
                if (!FDWCTransparencyEditedMapBaker::IsLayerBakeCurrent(Asset, Layer, &CurrentnessReason))
                {
                    AddIssue(
                        Report,
                        FName(*FString::Printf(TEXT("TransparencyStale_%s"), *Layer.LayerGuid.ToString(EGuidFormats::Digits))),
                        EWCAValidationSeverity::Warning,
                        EWCAValidationIssueCategory::Map,
                        EWCAValidationFixKind::BakeTransparencyMaps,
                        NSLOCTEXT("WCAValidationReport", "TransparencyMapsTitle", "Transparency Maps"),
                        NSLOCTEXT("WCAValidationReport", "OutOfDateStatus", "Out of Date"),
                        FText::FromString(CurrentnessReason.IsEmpty()
                            ? TEXT("Transparency Maps: stored outputs are missing or out of date.")
                            : FString::Printf(TEXT("Transparency Maps: %s"), *CurrentnessReason)),
                        NSLOCTEXT("WCAValidationReport", "BakeTransparencyMapsAction", "Use Bake Maps to rebuild it."));
                }
            }
        }

        if (!DWCBuildStatus::IsUsable(State.TransparencyMaps))
        {
            AddIssue(
                Report,
                TEXT("TransparencyMaps"),
                SeverityForStatus(State.TransparencyMaps),
                EWCAValidationIssueCategory::Map,
                EWCAValidationFixKind::BakeTransparencyMaps,
                NSLOCTEXT("WCAValidationReport", "TransparencyMapsTitle", "Transparency Maps"),
                FText::FromString(BakeStatusToString(State.TransparencyMaps)),
                FText::FromString(BuildMapDetail(TEXT("Transparency Maps"), State.TransparencyMaps, false)),
                NSLOCTEXT("WCAValidationReport", "BakeTransparencyMapsAction", "Use Bake Maps to rebuild it."),
                State.TransparencyMaps == EDWCBakeStatus::Failed);
        }
    }

    FString VisualSummary;
    if (Asset.HasAnyWettableMaterialSlot() &&
        FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(&Asset, &VisualSummary) &&
        !VisualSummary.IsEmpty())
    {
        AddIssue(
            Report,
            TEXT("WetPartDataTexture"),
            EWCAValidationSeverity::Warning,
            EWCAValidationIssueCategory::Map,
            EWCAValidationFixKind::BakeRenderProfileData,
            NSLOCTEXT("WCAValidationReport", "WetPartDataTextureTitle", "Wet Part Data Texture"),
            NSLOCTEXT("WCAValidationReport", "RequiredStatus", "Required"),
            FText::FromString(VisualSummary),
            NSLOCTEXT("WCAValidationReport", "WetPartDataTextureAction", "Use Bake Render Profile Data to rebuild it."));
    }

    AddSurfaceWaterInputIssues(Report, Asset);

    const bool bHasFailedState =
        State.GeneratedDataUV == EDWCBakeStatus::Failed ||
        State.OriginalUVTopology == EDWCBakeStatus::Failed ||
        State.CPURuntimeData == EDWCBakeStatus::Failed ||
        State.GPURuntimeData == EDWCBakeStatus::Failed ||
        State.GPUMaps == EDWCBakeStatus::Failed ||
        State.WrinkleMaps == EDWCBakeStatus::Failed ||
        State.TransparencyMaps == EDWCBakeStatus::Failed;
    if (bHasFailedState && !State.LastFailure.IsEmpty())
    {
        const bool bAlreadyShown = Report.Issues.ContainsByPredicate(
            [&State](const FWCAValidationIssue& Issue)
            {
                const FString Detail = Issue.Detail.ToString();
                return Detail.Contains(State.LastFailure) || State.LastFailure.Contains(Detail);
            });
        if (!bAlreadyShown)
        {
            AddIssue(
                Report,
                TEXT("LastFailure"),
                EWCAValidationSeverity::Error,
                EWCAValidationIssueCategory::Failure,
                EWCAValidationFixKind::Manual,
                NSLOCTEXT("WCAValidationReport", "LastFailureTitle", "Failure Details"),
                NSLOCTEXT("WCAValidationReport", "FailedStatus", "Failed"),
                FText::FromString(State.LastFailure),
                NSLOCTEXT("WCAValidationReport", "LastFailureAction", "Review the failed bake and retry the relevant action."),
                true);
        }
    }
#endif
    return Report;
}
