//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationTypes.h"

class UWetClothingAsset;
struct FWCAEditorValidationSnapshot;

enum class EWCAValidationMode : uint8
{
    Fast,
    Deep
};

/** User-facing validation groups. Keep the order in sync with the Validation Results dialog. */
enum class EWCAValidationSection : uint8
{
    DataUV,
    RuntimeData,
    GeneratedMaterials,
    GPUSimulationMaps,
    RenderProfileData,
    WrinkleMaps,
    TransparencyMaps,
    FailureDetails
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
};

struct FWCAValidationReport
{
    TArray<FWCAValidationIssue> Issues;
    FDWCTriangleValidationSummary Diagnostics;

    bool HasIssues() const { return !Issues.IsEmpty(); }
    bool HasManualIssues() const;
    bool HasAutoResolvableIssues() const;
    FString BuildSummary() const;
    FString BuildManualIssueSummary() const;
};

FWCAValidationReport BuildWCAValidationReport(
    UWetClothingAsset& Asset,
    EWCAValidationMode Mode,
    bool bRefreshAssetState);

FWCAEditorValidationSnapshot BuildWCAValidationSnapshot(
    UWetClothingAsset& Asset,
    EWCAValidationMode Mode,
    bool bRefreshAssetState);
