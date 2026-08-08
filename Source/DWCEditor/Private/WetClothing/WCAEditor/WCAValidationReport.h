// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"

class UWetClothingAsset;

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

enum class EWCAValidationFixKind : uint8
{
    None,
    Save,
    InitializeDataUV,
    PrepareRuntimeData,
    BakeGPUMaps,
    BakeRenderProfileData,
    BakeWrinkleMaps,
    BakeTransparencyMaps,
    GenerateMaterials,
    Manual
};

struct FWCAValidationIssue
{
    FName                  IssueId;
    EWCAValidationSeverity Severity = EWCAValidationSeverity::Info;
    EWCAValidationSection  Section = EWCAValidationSection::RenderProfileData;
    EWCAValidationFixKind  FixKind = EWCAValidationFixKind::None;
    FText                  Title;
    FText                  Status;
    FText                  Detail;
    FText                  RequiredAction;
    /** Optional short context such as "Slot 3", "WP_Metal", or a transparency layer name. */
    FText ContextLabel;
    bool  bFailed = false;
};

struct FWCAValidationReport
{
    TArray<FWCAValidationIssue>   Issues;
    FDWCTriangleValidationSummary Diagnostics;

    bool    HasIssues() const { return !Issues.IsEmpty(); }
    bool    HasManualIssues() const;
    bool    HasAutoResolvableIssues() const;
    FString BuildSummary() const;
    FString BuildManualIssueSummary() const;
};

FWCAValidationReport BuildWCAValidationReport(
    UWetClothingAsset& Asset,
    EWCAValidationMode Mode,
    bool               bRefreshAssetState);
