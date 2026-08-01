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
        (void)bHasPayload;
        return Status == EDWCBakeStatus::Failed
            ? EWCAValidationSeverity::Error
            : EWCAValidationSeverity::Warning;
    }

    void AddIssue(
        FWCAValidationReport& Report,
        const FName IssueId,
        const EWCAValidationSeverity Severity,
        const EWCAValidationSection Section,
        const EWCAValidationFixKind FixKind,
        const FText& Title,
        const FText& Status,
        const FText& Detail,
        const FText& RequiredAction,
        const bool bFailed = false,
        const FText& ContextLabel = FText::GetEmpty())
    {
        FWCAValidationIssue& Issue = Report.Issues.AddDefaulted_GetRef();
        Issue.IssueId = IssueId;
        Issue.Severity = Severity;
        Issue.Section = Section;
        Issue.FixKind = FixKind;
        Issue.Title = Title;
        Issue.Status = Status;
        Issue.Detail = Detail;
        Issue.RequiredAction = RequiredAction;
        Issue.ContextLabel = ContextLabel;
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
        const EWCAValidationSection Section,
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
            Section,
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

    bool RequiresSurfaceWaterDropletMask(const FWetnessProfileParameters& Parameters)
    {
        const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
        return Surface.bEnabled;
    }

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


    FText BuildSlotContextLabel(const int32 MaterialSlotIndex)
    {
        return MaterialSlotIndex == INDEX_NONE
            ? FText::GetEmpty()
            : FText::FromString(FString::Printf(TEXT("Slot %d"), MaterialSlotIndex));
    }

    FText StatusForValidationDetail(const FString& Detail)
    {
        if (Detail.Contains(TEXT("failed"), ESearchCase::IgnoreCase))
        {
            return NSLOCTEXT("WCAValidationReport", "FailedStatus", "Failed");
        }
        if (Detail.Contains(TEXT("out of date"), ESearchCase::IgnoreCase) ||
            Detail.Contains(TEXT("outdated"), ESearchCase::IgnoreCase) ||
            Detail.Contains(TEXT("old authored data"), ESearchCase::IgnoreCase) ||
            Detail.Contains(TEXT("old DWC Data UV"), ESearchCase::IgnoreCase) ||
            Detail.Contains(TEXT("rebake"), ESearchCase::IgnoreCase) ||
            Detail.Contains(TEXT("regenerated"), ESearchCase::IgnoreCase))
        {
            return NSLOCTEXT("WCAValidationReport", "OutOfDateStatus", "Out of Date");
        }
        if (Detail.Contains(TEXT("missing"), ESearchCase::IgnoreCase) ||
            Detail.Contains(TEXT("not assigned"), ESearchCase::IgnoreCase) ||
            Detail.Contains(TEXT("could not be resolved"), ESearchCase::IgnoreCase))
        {
            return NSLOCTEXT("WCAValidationReport", "MissingStatus", "Missing");
        }
        if (Detail.Contains(TEXT("invalid"), ESearchCase::IgnoreCase) ||
            Detail.Contains(TEXT("out of range"), ESearchCase::IgnoreCase))
        {
            return NSLOCTEXT("WCAValidationReport", "InvalidStatus", "Invalid");
        }
        return NSLOCTEXT("WCAValidationReport", "RequiredStatus", "Required");
    }

    bool TryExtractSlotIndex(const FString& Detail, int32& OutMaterialSlotIndex)
    {
        OutMaterialSlotIndex = INDEX_NONE;
        if (!Detail.StartsWith(TEXT("Slot ")))
        {
            return false;
        }

        int32 ColonIndex = INDEX_NONE;
        if (!Detail.FindChar(TEXT(':'), ColonIndex) || ColonIndex <= 5)
        {
            return false;
        }

        const FString SlotText = Detail.Mid(5, ColonIndex - 5);
        if (!SlotText.IsNumeric())
        {
            return false;
        }

        OutMaterialSlotIndex = FCString::Atoi(*SlotText);
        return true;
    }

    FString ExtractProfileDisplayName(const FString& StableKey)
    {
        TArray<FString> Segments;
        StableKey.ParseIntoArray(Segments, TEXT("|"), true);
        for (const FString& Segment : Segments)
        {
            int32 SlashIndex = INDEX_NONE;
            if (!Segment.FindLastChar(TEXT('/'), SlashIndex))
            {
                continue;
            }

            FString Candidate = Segment.Mid(SlashIndex + 1);
            int32 DotIndex = INDEX_NONE;
            if (Candidate.FindChar(TEXT('.'), DotIndex))
            {
                Candidate.LeftInline(DotIndex);
            }
            if (!Candidate.IsEmpty())
            {
                return Candidate;
            }
        }

        return StableKey.IsEmpty() ? TEXT("Wetness Profile") : StableKey;
    }

    bool ExtractQuotedProfileKey(const FString& Detail, FString& OutStableKey)
    {
        OutStableKey.Reset();
        const FString Prefix(TEXT("Profile '"));
        const int32 StartIndex = Detail.Find(Prefix, ESearchCase::CaseSensitive);
        if (StartIndex == INDEX_NONE)
        {
            return false;
        }

        const int32 ValueStart = StartIndex + Prefix.Len();
        const int32 EndIndex = Detail.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
        if (EndIndex == INDEX_NONE || EndIndex <= ValueStart)
        {
            return false;
        }

        OutStableKey = Detail.Mid(ValueStart, EndIndex - ValueStart);
        return !OutStableKey.IsEmpty();
    }

    void AddGeneratedMaterialIssues(
        FWCAValidationReport& Report,
        const TArray<FString>& Messages)
    {
        for (int32 MessageIndex = 0; MessageIndex < Messages.Num(); ++MessageIndex)
        {
            const FString& Message = Messages[MessageIndex];
            int32 MaterialSlotIndex = INDEX_NONE;
            const bool bHasSlot = TryExtractSlotIndex(Message, MaterialSlotIndex);
            const bool bFailed = Message.Contains(TEXT("failed"), ESearchCase::IgnoreCase);
            const bool bFunctionMessage =
                Message.Contains(TEXT("MF_"), ESearchCase::CaseSensitive) ||
                Message.Contains(TEXT("function"), ESearchCase::IgnoreCase);
            AddIssue(
                Report,
                FName(*FString::Printf(TEXT("GeneratedMaterials_%d"), MessageIndex)),
                bFailed ? EWCAValidationSeverity::Error : EWCAValidationSeverity::Warning,
                EWCAValidationSection::GeneratedMaterials,
                EWCAValidationFixKind::GenerateMaterials,
                bHasSlot
                    ? NSLOCTEXT("WCAValidationReport", "GeneratedMaterialSlotTitle", "Generated Material Setup")
                    : (bFunctionMessage
                        ? NSLOCTEXT("WCAValidationReport", "GeneratedMaterialFunctionsTitle", "Generated Material Functions")
                        : NSLOCTEXT("WCAValidationReport", "GeneratedMaterialSetupTitle", "Generated Material Setup")),
                StatusForValidationDetail(Message),
                FText::FromString(Message),
                NSLOCTEXT("WCAValidationReport", "GeneratedMaterialsAction", "Use Build for Runtime > Generate Materials."),
                bFailed,
                bHasSlot ? BuildSlotContextLabel(MaterialSlotIndex) : FText::GetEmpty());
        }
    }

    bool TryExtractSlotIndexAnywhere(const FString& Detail, int32& OutMaterialSlotIndex)
    {
        OutMaterialSlotIndex = INDEX_NONE;
        const FString LowerDetail = Detail.ToLower();
        const int32 SlotTokenIndex = LowerDetail.Find(TEXT("slot "));
        if (SlotTokenIndex == INDEX_NONE)
        {
            return false;
        }

        const int32 NumberStart = SlotTokenIndex + 5;
        int32 NumberEnd = NumberStart;
        while (NumberEnd < Detail.Len() && FChar::IsDigit(Detail[NumberEnd]))
        {
            ++NumberEnd;
        }
        if (NumberEnd == NumberStart)
        {
            return false;
        }

        OutMaterialSlotIndex = FCString::Atoi(*Detail.Mid(NumberStart, NumberEnd - NumberStart));
        return true;
    }

    bool HasIssueForSectionAndContext(
        const FWCAValidationReport& Report,
        const EWCAValidationSection Section,
        const FText& ContextLabel)
    {
        return Report.Issues.ContainsByPredicate(
            [Section, &ContextLabel](const FWCAValidationIssue& Issue)
            {
                return Issue.Section == Section &&
                       (ContextLabel.IsEmpty() || Issue.ContextLabel.EqualTo(ContextLabel));
            });
    }

    void AddRenderProfileDataIssues(
        FWCAValidationReport& Report,
        const bool bDataUVLayoutLocked,
        const FString& VisualSummary)
    {
        TArray<FString> Lines;
        VisualSummary.ParseIntoArrayLines(Lines, true);
        int32 IssueIndex = 0;
        for (FString Line : Lines)
        {
            Line.TrimStartAndEndInline();
            if (Line.IsEmpty() || Line.Equals(TEXT("Pending Render Profile Bake:"), ESearchCase::IgnoreCase))
            {
                continue;
            }
            if (Line.StartsWith(TEXT("- ")))
            {
                Line = Line.RightChop(2);
                Line.TrimStartAndEndInline();
            }

            EWCAValidationSection Section = EWCAValidationSection::RenderProfileData;
            EWCAValidationFixKind FixKind = EWCAValidationFixKind::BakeRenderProfileData;
            FText Title = NSLOCTEXT("WCAValidationReport", "WetPartDataTextureTitle", "Wet Part Data Texture");
            FText RequiredAction = NSLOCTEXT(
                "WCAValidationReport",
                "WetPartDataTextureAction",
                "Use Build for Runtime > Bake Render Profile Lookup Texture.");
            FText ContextLabel;

            FString StableKey;
            if (ExtractQuotedProfileKey(Line, StableKey))
            {
                const FString ProfileDisplayName = ExtractProfileDisplayName(StableKey);
                ContextLabel = FText::FromString(ProfileDisplayName);
                Line.ReplaceInline(
                    *FString::Printf(TEXT("Profile '%s'"), *StableKey),
                    *FString::Printf(TEXT("Profile '%s'"), *ProfileDisplayName),
                    ESearchCase::CaseSensitive);
                Title = Line.Contains(TEXT("texture settings"), ESearchCase::IgnoreCase)
                    ? NSLOCTEXT("WCAValidationReport", "SurfaceWaterTextureSettingsTitle", "Surface Water Texture Settings")
                    : NSLOCTEXT("WCAValidationReport", "PreparedSurfaceTexturesTitle", "Prepared Surface Textures");

                if (Line.Contains(TEXT("invalid authored"), ESearchCase::IgnoreCase))
                {
                    FixKind = EWCAValidationFixKind::Manual;
                    RequiredAction = NSLOCTEXT(
                        "WCAValidationReport",
                        "SurfaceWaterTextureSettingsAction",
                        "Fix the Wetness Profile texture inputs, then use Build for Runtime > Bake Render Profile Lookup Texture.");
                    if (HasIssueForSectionAndContext(Report, Section, ContextLabel))
                    {
                        // A more specific manual input issue (for example a missing Droplet Mask)
                        // is already present for this profile.
                        continue;
                    }
                }
            }
            else if (Line.Contains(TEXT("profile that is missing"), ESearchCase::IgnoreCase))
            {
                Title = NSLOCTEXT("WCAValidationReport", "LocalRenderProfileTitle", "Local Render Profile");
            }

            int32 MaterialSlotIndex = INDEX_NONE;
            if (TryExtractSlotIndexAnywhere(Line, MaterialSlotIndex))
            {
                ContextLabel = BuildSlotContextLabel(MaterialSlotIndex);
            }

            if (Line.Contains(TEXT("Prepared DWC Skeletal Mesh is unavailable"), ESearchCase::IgnoreCase))
            {
                Section = EWCAValidationSection::DataUV;
                FixKind = EWCAValidationFixKind::Manual;
                Title = NSLOCTEXT("WCAValidationReport", "PreparedMeshTitle", "Prepared DWC Skeletal Mesh");
                RequiredAction = NSLOCTEXT(
                    "WCAValidationReport",
                    "PreparedMeshAction",
                    "The prepared mesh is missing. Create a new WCA from the intended Source Skeletal Mesh.");
            }
            else if (Line.Contains(TEXT("DWC Data UV must be rebuilt"), ESearchCase::IgnoreCase) ||
                     Line.Contains(TEXT("sealed DWC Data UV is invalid"), ESearchCase::IgnoreCase))
            {
                Section = EWCAValidationSection::DataUV;
                FixKind = bDataUVLayoutLocked ? EWCAValidationFixKind::Manual : EWCAValidationFixKind::InitializeDataUV;
                Title = NSLOCTEXT("WCAValidationReport", "DWCDataUVTitle", "DWC Data UV");
                RequiredAction = bDataUVLayoutLocked
                    ? NSLOCTEXT(
                        "WCAValidationReport",
                        "DWCDataUVLockedAction",
                        "The sealed UV layout cannot be rebuilt. Create a new WCA if its mesh topology changed.")
                    : NSLOCTEXT(
                        "WCAValidationReport",
                        "DWCDataUVInitializeAction",
                        "Initialize DWC Data UV for this asset.");
                if (HasIssueForSectionAndContext(Report, Section, FText::GetEmpty()))
                {
                    continue;
                }
            }
            else if (Line.Contains(TEXT("Unified wet material setup is required"), ESearchCase::IgnoreCase))
            {
                Section = EWCAValidationSection::GeneratedMaterials;
                FixKind = EWCAValidationFixKind::GenerateMaterials;
                Title = NSLOCTEXT("WCAValidationReport", "GeneratedMaterialSetupTitle", "Generated Material Setup");
                RequiredAction = NSLOCTEXT(
                    "WCAValidationReport",
                    "GeneratedMaterialsAction",
                    "Use Build for Runtime > Generate Materials.");
                if (HasIssueForSectionAndContext(Report, Section, ContextLabel))
                {
                    continue;
                }
            }
            else if (Line.Contains(TEXT("Material slot"), ESearchCase::IgnoreCase) &&
                     Line.Contains(TEXT("out of range"), ESearchCase::IgnoreCase))
            {
                Section = EWCAValidationSection::GeneratedMaterials;
                FixKind = EWCAValidationFixKind::Manual;
                Title = NSLOCTEXT("WCAValidationReport", "GeneratedMaterialSetupTitle", "Generated Material Setup");
                RequiredAction = NSLOCTEXT(
                    "WCAValidationReport",
                    "MaterialSlotRangeAction",
                    "Fix the wettable material-slot assignment before generating materials or baking maps.");
            }

            const bool bFailed = Line.Contains(TEXT("failed"), ESearchCase::IgnoreCase);
            const bool bManual = FixKind == EWCAValidationFixKind::Manual;
            AddIssue(
                Report,
                FName(*FString::Printf(TEXT("RenderProfileData_%d"), IssueIndex++)),
                bFailed ? EWCAValidationSeverity::Error : EWCAValidationSeverity::Warning,
                Section,
                FixKind,
                Title,
                bManual
                    ? NSLOCTEXT("WCAValidationReport", "ManualFixStatus", "Manual Fix")
                    : StatusForValidationDetail(Line),
                FText::FromString(Line),
                RequiredAction,
                bFailed,
                ContextLabel);
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
                if (Entry.WetPartID == 0 ||
                    Entry.AssignedUVIslandIDs.IsEmpty() ||
                    ReportedProfileIndices.Contains(Entry.ProfileIndex))
                {
                    continue;
                }

                const FWetPartProfileAssignment* Profile = EditableData.FindProfile(Entry);
                FWetnessProfileParameters Parameters;
                FWetClothingWetPartDataTextureBaker::ResolveProfileParameters(Profile, Parameters);
                if (!RequiresSurfaceWaterDropletMask(Parameters) ||
                    Parameters.SurfaceWater.DropletMaskTexture != nullptr)
                {
                    continue;
                }

                ReportedProfileIndices.Add(Entry.ProfileIndex);
                AddIssue(
                    Report,
                    FName(*FString::Printf(TEXT("SurfaceWaterMissingDropletMask_Profile%d"), Entry.ProfileIndex)),
                    EWCAValidationSeverity::Warning,
                    EWCAValidationSection::RenderProfileData,
                    EWCAValidationFixKind::Manual,
                    NSLOCTEXT("WCAValidationReport", "SurfaceWaterInputTitle", "Surface Water Input"),
                    NSLOCTEXT("WCAValidationReport", "ManualFixStatus", "Manual Fix"),
                    FText::FromString(FString::Printf(
                        TEXT("Surface Water: %s used by Wet Part %d in slot %d has no Droplet Mask Texture. Generated GPU materials mask-gate Surface Water coverage, so coverage resolves to zero."),
                        *DescribeWetPartProfile(Profile, Entry.ProfileIndex),
                        Entry.WetPartID,
                        Slot.MaterialSlotIndex)),
                    NSLOCTEXT("WCAValidationReport", "SurfaceWaterDropletMaskAction", "Assign a Droplet Mask Texture in the Wetness Profile, then use Build for Runtime > Bake Render Profile Lookup Texture."),
                    false,
                    Profile != nullptr && Profile->SourceProfile.IsValid()
                        ? FText::FromString(Profile->SourceProfile.GetAssetName())
                        : FText::FromString(FString::Printf(TEXT("Profile %d"), Entry.ProfileIndex)));
            }
        }
    }

    void AppendIssueSection(
        TArray<FString>& Sections,
        const TCHAR* Heading,
        const FWCAValidationReport& Report,
        const EWCAValidationSection Section,
        const bool bManualOnly)
    {
        TArray<FString> Lines;
        for (const FWCAValidationIssue& Issue : Report.Issues)
        {
            if (Issue.Section != Section ||
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
    AppendIssueSection(Sections, TEXT("DWC Data UV"), *this, EWCAValidationSection::DataUV, false);
    AppendIssueSection(Sections, TEXT("Runtime Data"), *this, EWCAValidationSection::RuntimeData, false);
    AppendIssueSection(Sections, TEXT("Generated Materials"), *this, EWCAValidationSection::GeneratedMaterials, false);
    AppendIssueSection(Sections, TEXT("GPU Runtime Data"), *this, EWCAValidationSection::GPUSimulationMaps, false);
    AppendIssueSection(Sections, TEXT("Render Profile Lookup Texture"), *this, EWCAValidationSection::RenderProfileData, false);
    AppendIssueSection(Sections, TEXT("Wrinkle Textures"), *this, EWCAValidationSection::WrinkleMaps, false);
    AppendIssueSection(Sections, TEXT("Transparency Textures"), *this, EWCAValidationSection::TransparencyMaps, false);
    AppendIssueSection(Sections, TEXT("Internal Failure"), *this, EWCAValidationSection::FailureDetails, false);
    return FString::Join(Sections, TEXT("\n\n"));
}

FString FWCAValidationReport::BuildManualIssueSummary() const
{
    TArray<FString> Sections;
    AppendIssueSection(Sections, TEXT("DWC Data UV"), *this, EWCAValidationSection::DataUV, true);
    AppendIssueSection(Sections, TEXT("Runtime Data"), *this, EWCAValidationSection::RuntimeData, true);
    AppendIssueSection(Sections, TEXT("Generated Materials"), *this, EWCAValidationSection::GeneratedMaterials, true);
    AppendIssueSection(Sections, TEXT("GPU Runtime Data"), *this, EWCAValidationSection::GPUSimulationMaps, true);
    AppendIssueSection(Sections, TEXT("Render Profile Lookup Texture"), *this, EWCAValidationSection::RenderProfileData, true);
    AppendIssueSection(Sections, TEXT("Wrinkle Textures"), *this, EWCAValidationSection::WrinkleMaps, true);
    AppendIssueSection(Sections, TEXT("Transparency Textures"), *this, EWCAValidationSection::TransparencyMaps, true);
    AppendIssueSection(Sections, TEXT("Internal Failure"), *this, EWCAValidationSection::FailureDetails, true);
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
    FString GPURuntimePreparationReason;
    const bool bCanBuildGPURuntimeData =
        Asset.CanPrepareRuntimeDataForEditorSave(&GPURuntimePreparationReason);
    const bool bHasRuntimeMesh = Asset.GetRuntimeSkeletalMesh() != nullptr;
    const bool bHasCurrentOriginalUVTopology = DWCBuildStatus::IsUsable(State.OriginalUVTopology);
    const bool bCanBuildCPURuntimeData = bHasRuntimeMesh && bHasCurrentOriginalUVTopology;
    const FString CPURuntimePreparationReason = !bHasRuntimeMesh
        ? FString(TEXT("No runtime skeletal mesh is assigned."))
        : (!bHasCurrentOriginalUVTopology
            ? FString(TEXT("Original UV topology is missing or out of date."))
            : FString());

    auto BuildRuntimeValidationDetail = [](
        const FString& BaseDetail,
        const EDWCBakeStatus Status,
        const bool bCanBuild,
        const FString& PreparationReason)
    {
        if (bCanBuild || DWCBuildStatus::IsUsable(Status) || PreparationReason.IsEmpty())
        {
            return BaseDetail;
        }
        return FString::Printf(
            TEXT("%s Runtime data cannot be built: %s"),
            *BaseDetail,
            *PreparationReason);
    };

    auto GetRuntimeFixKind = [](
        const EDWCBakeStatus Status,
        const bool bSavePending,
        const bool bCanBuild,
        const EWCAValidationFixKind BuildFixKind)
    {
        if (bSavePending && DWCBuildStatus::IsUsable(Status))
        {
            return EWCAValidationFixKind::Save;
        }
        return bCanBuild ? BuildFixKind : EWCAValidationFixKind::Manual;
    };

    auto GetRuntimeRequiredAction = [](
        const EDWCBakeStatus Status,
        const bool bSavePending,
        const bool bCanBuild,
        const FText& BuildAction,
        const FText& PrerequisiteAction)
    {
        if (bSavePending && DWCBuildStatus::IsUsable(Status))
        {
            return NSLOCTEXT("WCAValidationReport", "RuntimeDataSaveAction", "Save the asset to persist it.");
        }
        return bCanBuild ? BuildAction : PrerequisiteAction;
    };

    const bool bDataUVLayoutLocked = Asset.HasLockedDataUVLayout();
    AddBakeStatusIssueIfRequired(
        Report,
        TEXT("DWCDataUV"),
        NSLOCTEXT("WCAValidationReport", "DWCDataUVTitle", "DWC Data UV"),
        State.GeneratedDataUV,
        EWCAValidationSection::DataUV,
        bDataUVLayoutLocked ? EWCAValidationFixKind::Manual : EWCAValidationFixKind::InitializeDataUV,
        bDataUVLayoutLocked
            ? NSLOCTEXT("WCAValidationReport", "DWCDataUVLockedAction", "The sealed UV layout cannot be rebuilt. Create a new WCA if the prepared mesh or UV layout changed.")
            : NSLOCTEXT("WCAValidationReport", "DWCDataUVInitializeAction", "Initialize DWC Data UV for this asset."),
        FString::Printf(TEXT("DWC Data UV: %s.%s"),
            *BakeStatusToString(State.GeneratedDataUV),
            bDataUVLayoutLocked ? TEXT(" The stored packed layout is immutable") : TEXT("")));

    AddBakeStatusIssueIfRequired(
        Report,
        TEXT("OriginalUVTopology"),
        NSLOCTEXT("WCAValidationReport", "OriginalUVTopologyTitle", "Original UV Topology"),
        State.OriginalUVTopology,
        EWCAValidationSection::DataUV,
        bDataUVLayoutLocked ? EWCAValidationFixKind::Manual : EWCAValidationFixKind::InitializeDataUV,
        bDataUVLayoutLocked
            ? NSLOCTEXT("WCAValidationReport", "OriginalUVTopologyLockedAction", "Original UV island topology is locked. Create a new WCA to generate different topology.")
            : NSLOCTEXT("WCAValidationReport", "OriginalUVTopologyInitializeAction", "Initialize DWC Data UV and Original UV topology."),
        FString::Printf(TEXT("Original UV Topology: %s.%s"),
            *BakeStatusToString(State.OriginalUVTopology),
            bDataUVLayoutLocked ? TEXT(" The stored island identities are immutable") : TEXT("")));

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
                EWCAValidationSection::RuntimeData,
                GetRuntimeFixKind(State.CPURuntimeData, bSavePending, bCanBuildCPURuntimeData, EWCAValidationFixKind::PrepareRuntimeData),
                NSLOCTEXT("WCAValidationReport", "CPURuntimeDataTitle", "CPU Runtime Data"),
                bSavePending && DWCBuildStatus::IsUsable(State.CPURuntimeData) ? NSLOCTEXT("WCAValidationReport", "CPURuntimeSaveRequired", "Save Required") : FText::FromString(BakeStatusToString(State.CPURuntimeData)),
                FText::FromString(BuildRuntimeValidationDetail(
                    BuildRuntimeDetail(TEXT("CPU Runtime Data"), State.CPURuntimeData, bHasPayload, Asset.HasGeneratedBakeOutput(DWCBakeOutput::CPURuntimeData), Asset.HasSavedBakeOutput(DWCBakeOutput::CPURuntimeData), bAssetHasUnsavedChanges, bSavePending, State.LastFailure),
                    State.CPURuntimeData,
                    bCanBuildCPURuntimeData,
                    CPURuntimePreparationReason)),
                GetRuntimeRequiredAction(
                    State.CPURuntimeData,
                    bSavePending,
                    bCanBuildCPURuntimeData,
                    NSLOCTEXT("WCAValidationReport", "CPURuntimeDataAction", "Use Build for Runtime > Build CPU Runtime Data, or save the asset, to rebuild and persist it."),
                    NSLOCTEXT("WCAValidationReport", "CPURuntimeDataPrerequisiteAction", "Resolve the runtime-data prerequisite, then use Build for Runtime > Build CPU Runtime Data.")),
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
                EWCAValidationSection::RuntimeData,
                GetRuntimeFixKind(State.GPURuntimeData, bSavePending, bCanBuildGPURuntimeData, EWCAValidationFixKind::BakeGPUMaps),
                NSLOCTEXT("WCAValidationReport", "GPURuntimeDataTitle", "GPU Runtime Data"),
                bSavePending && DWCBuildStatus::IsUsable(State.GPURuntimeData) ? NSLOCTEXT("WCAValidationReport", "GPURuntimeSaveRequired", "Save Required") : FText::FromString(BakeStatusToString(State.GPURuntimeData)),
                FText::FromString(BuildRuntimeValidationDetail(
                    BuildRuntimeDetail(TEXT("GPU Runtime Data"), State.GPURuntimeData, bHasPayload, Asset.HasGeneratedBakeOutput(DWCBakeOutput::GPURuntimeData), Asset.HasSavedBakeOutput(DWCBakeOutput::GPURuntimeData), bAssetHasUnsavedChanges, bSavePending, State.LastFailure),
                    State.GPURuntimeData,
                    bCanBuildGPURuntimeData,
                    GPURuntimePreparationReason)),
                GetRuntimeRequiredAction(
                    State.GPURuntimeData,
                    bSavePending,
                    bCanBuildGPURuntimeData,
                    NSLOCTEXT("WCAValidationReport", "GPURuntimeDataAction", "Use Build for Runtime > Build GPU Runtime Data, or save the asset, to rebuild and persist it."),
                    NSLOCTEXT("WCAValidationReport", "GPURuntimeDataPrerequisiteAction", "Resolve the runtime-data prerequisite, then use Build for Runtime > Build GPU Runtime Data.")),
                State.GPURuntimeData == EDWCBakeStatus::Failed);
        }
    }

    AddBakeStatusIssueIfRequired(
        Report,
        TEXT("GPUMaps"),
        NSLOCTEXT("WCAValidationReport", "GPUMapsTitle", "GPU Runtime Data"),
        State.GPUMaps,
        EWCAValidationSection::GPUSimulationMaps,
        bCanBuildGPURuntimeData ? EWCAValidationFixKind::BakeGPUMaps : EWCAValidationFixKind::Manual,
        bCanBuildGPURuntimeData
            ? NSLOCTEXT("WCAValidationReport", "BuildGPUDataAction", "Use Build for Runtime > Build GPU Runtime Data.")
            : NSLOCTEXT("WCAValidationReport", "BuildGPUDataPrerequisiteAction", "Resolve the GPU runtime-data prerequisite, then use Build for Runtime > Build GPU Runtime Data."),
        BuildMapDetail(TEXT("GPU Runtime Data"), State.GPUMaps, Asset.IsBakeOutputSavePending(DWCBakeOutput::GPUMaps)),
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
        AddGeneratedMaterialIssues(Report, GeneratedMaterialMessages);
    }

    if (Asset.HasWrinkleBakeContent() && !DWCBuildStatus::IsUsable(State.WrinkleMaps))
    {
        AddIssue(
            Report,
            TEXT("WrinkleMaps"),
            SeverityForStatus(State.WrinkleMaps),
            EWCAValidationSection::WrinkleMaps,
            EWCAValidationFixKind::BakeWrinkleMaps,
            NSLOCTEXT("WCAValidationReport", "WrinkleMapsTitle", "Wrinkle Textures"),
            FText::FromString(BakeStatusToString(State.WrinkleMaps)),
            FText::FromString(BuildMapDetail(TEXT("Wrinkle Textures"), State.WrinkleMaps, false)),
            NSLOCTEXT("WCAValidationReport", "BakeWrinkleTexturesAction", "Use Build for Runtime > Bake Wrinkle Textures."),
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
                    EWCAValidationSection::WrinkleMaps,
                    EWCAValidationFixKind::BakeWrinkleMaps,
                    NSLOCTEXT("WCAValidationReport", "WrinkleMapsTitle", "Wrinkle Textures"),
                    NSLOCTEXT("WCAValidationReport", "OutOfDateStatus", "Out of Date"),
                    FText::FromString(FString::Printf(TEXT("Wrinkle Textures: Slot %d is missing or was built from old authored data."), MaterialSlotIndex)),
                    NSLOCTEXT("WCAValidationReport", "BakeWrinkleTexturesAction", "Use Build for Runtime > Bake Wrinkle Textures."),
                    false,
                    BuildSlotContextLabel(MaterialSlotIndex));
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
                EWCAValidationSection::WrinkleMaps,
                EWCAValidationFixKind::Manual,
                NSLOCTEXT("WCAValidationReport", "CustomWrinkleTextureTitle", "Wrinkle Textures"),
                NSLOCTEXT("WCAValidationReport", "ManualFixStatus", "Manual Fix"),
                FText::FromString(FString::Printf(TEXT("Wrinkle Textures: Slot %d uses Custom Wrinkle Map but no texture is assigned."), Source.MaterialSlotIndex)),
                NSLOCTEXT("WCAValidationReport", "CustomWrinkleTextureAction", "Assign a Custom Wrinkle Normal Map or switch the slot back to baked wrinkle textures."),
                false,
                BuildSlotContextLabel(Source.MaterialSlotIndex));
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
                    EWCAValidationSection::TransparencyMaps,
                    EWCAValidationFixKind::Manual,
                    NSLOCTEXT("WCAValidationReport", "TransparencyInputTitle", "Transparency Textures"),
                    NSLOCTEXT("WCAValidationReport", "ManualFixStatus", "Manual Fix"),
                    FText::FromString(FString::Printf(TEXT("Transparency Textures: %s"), *FString::Join(TransparencyErrors, TEXT("\n")))),
                    NSLOCTEXT("WCAValidationReport", "TransparencyInputAction", "Fix the Transparency layer inputs before baking textures."),
                    true,
                    BuildSlotContextLabel(Layer.TargetSurface.OuterMaterialSlotIndex));
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
                        EWCAValidationSection::TransparencyMaps,
                        EWCAValidationFixKind::BakeTransparencyMaps,
                        NSLOCTEXT("WCAValidationReport", "TransparencyMapsTitle", "Transparency Textures"),
                        NSLOCTEXT("WCAValidationReport", "OutOfDateStatus", "Out of Date"),
                        FText::FromString(CurrentnessReason.IsEmpty()
                            ? TEXT("Transparency Textures: stored outputs are missing or out of date.")
                            : FString::Printf(TEXT("Transparency Textures: %s"), *CurrentnessReason)),
                        NSLOCTEXT("WCAValidationReport", "BakeTransparencyTexturesAction", "Use Build for Runtime > Bake Transparency Textures."),
                        false,
                        BuildSlotContextLabel(Layer.TargetSurface.OuterMaterialSlotIndex));
                }
            }
        }

        if (!DWCBuildStatus::IsUsable(State.TransparencyMaps))
        {
            AddIssue(
                Report,
                TEXT("TransparencyMaps"),
                SeverityForStatus(State.TransparencyMaps),
                EWCAValidationSection::TransparencyMaps,
                EWCAValidationFixKind::BakeTransparencyMaps,
                NSLOCTEXT("WCAValidationReport", "TransparencyMapsTitle", "Transparency Textures"),
                FText::FromString(BakeStatusToString(State.TransparencyMaps)),
                FText::FromString(BuildMapDetail(TEXT("Transparency Textures"), State.TransparencyMaps, false)),
                NSLOCTEXT("WCAValidationReport", "BakeTransparencyTexturesAction", "Use Build for Runtime > Bake Transparency Textures."),
                State.TransparencyMaps == EDWCBakeStatus::Failed);
        }
    }

    AddSurfaceWaterInputIssues(Report, Asset);

    FString VisualSummary;
    if (Asset.HasAnyWettableMaterialSlot() &&
        FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(&Asset, &VisualSummary) &&
        !VisualSummary.IsEmpty())
    {
        AddRenderProfileDataIssues(Report, bDataUVLayoutLocked, VisualSummary);
    }

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
                EWCAValidationSection::FailureDetails,
                EWCAValidationFixKind::Manual,
                NSLOCTEXT("WCAValidationReport", "LastFailureTitle", "Internal Failure"),
                NSLOCTEXT("WCAValidationReport", "FailedStatus", "Failed"),
                FText::FromString(State.LastFailure),
                NSLOCTEXT("WCAValidationReport", "LastFailureAction", "Review the failed bake and retry the relevant action."),
                true);
        }
    }
#endif
    return Report;
}
