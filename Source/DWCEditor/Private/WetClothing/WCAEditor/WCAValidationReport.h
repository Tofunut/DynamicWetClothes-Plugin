#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"

class UWetClothingAsset;

enum class EWCAValidationMode : uint8
{
    Fast,
    Deep
};

enum class EWCAValidationIssueCategory : uint8
{
    DataUV,
    Runtime,
    Map,
    Material,
    Failure
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
    RebuildDataUV,
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
    FName IssueId;
    EWCAValidationSeverity Severity = EWCAValidationSeverity::Info;
    EWCAValidationIssueCategory Category = EWCAValidationIssueCategory::Map;
    EWCAValidationFixKind FixKind = EWCAValidationFixKind::None;
    FText Title;
    FText Status;
    FText Detail;
    FText RequiredAction;
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
