//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSectionRegistry.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationTypes.h"

class UWetClothingAsset;
struct FWCAEditorValidationSnapshot;

enum class EWCAValidationMode : uint8
{
    MetadataOnly,
    ExactPayload
};

enum class EWCAValidationSeverity : uint8
{
    Info,
    Warning,
    Error
};

struct FWCAValidationIssue
{
    FName IssueId;
    EWCAValidationSeverity Severity = EWCAValidationSeverity::Info;
    EWCAValidationSection Section = EWCAValidationSection::RenderProfileData;
    EDWCEditorValidationRemediation Remediation = EDWCEditorValidationRemediation::None;
    TOptional<EDWCEditorBuildAction> BuildAction;
    FText Title;
    FText Status;
    FText Detail;
    FText RequiredAction;
    /** Optional short context such as "Slot 3", "WP_Metal", or a transparency layer name. */
    FText ContextLabel;
    /** Structured identity used by the canonical snapshot. Display text is never parsed for ownership. */
    FDWCEditorValidationTargetKey Target;
    bool bFailed = false;

    /** Identity used only for grouping validation rows with identical user-facing meaning. */
    FString BuildDisplayGroupKey() const;
};

struct FWCAValidationSectionResult
{
    EWCAValidationSection Section = EWCAValidationSection::Asset;
    EDWCEditorValidationOverallState OverallState = EDWCEditorValidationOverallState::NotApplicable;
    EDWCValidationPresentationState PresentationState = EDWCValidationPresentationState::Neutral;
    TArray<int32> IssueIndices;
    TArray<EDWCEditorBuildAction> SuggestedActions;
    int32 ErrorCount = 0;
    int32 WarningCount = 0;
    bool bApplicable = false;

    bool HasIssues() const { return !IssueIndices.IsEmpty(); }
};

struct FWCAValidationReport
{
    TArray<FWCAValidationIssue> Issues;
    TArray<FWCAValidationSectionResult> Sections;
    FDWCTriangleValidationSummary Diagnostics;

    bool HasIssues() const;
    bool HasManualIssues() const;
    bool HasAutoResolvableIssues() const;
    bool HasErrors() const;
    int32 GetDisplayIssueCount() const;
    const FWCAValidationSectionResult* FindSection(EWCAValidationSection Section) const;
    FString BuildSummary() const;
    FString BuildManualIssueSummary() const;
};

FWCAValidationReport BuildWCAValidationReport(
    UWetClothingAsset& Asset,
    EWCAValidationMode Mode);

FWCAEditorValidationSnapshot BuildWCAValidationSnapshot(
    UWetClothingAsset& Asset,
    EWCAValidationMode Mode);
