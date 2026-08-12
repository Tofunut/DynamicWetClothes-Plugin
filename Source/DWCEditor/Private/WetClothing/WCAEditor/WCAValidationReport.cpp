// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/WCAEditor/WCAValidationReport.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Validation/DWCAssetValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluationContext.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationReportAdapter.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"
#include "WetClothing/Foundation/Validation/DWCGeneratedMaterialValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCRenderProfileValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCRuntimeValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCTransparencyCookValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCTransparencyLayerValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCWetPartValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCWrinkleValidationEvaluator.h"
#include "UObject/Package.h"

namespace
{
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
            (bManualOnly && Issue.Remediation != EDWCEditorValidationRemediation::Manual))
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
        Sections.Add(FString::Printf(
            TEXT("%s\n%s"),
            Heading,
            *FString::Join(Lines, TEXT("\n"))));
    }
}

void AppendAllIssueSections(
    TArray<FString>& Sections,
    const FWCAValidationReport& Report,
    const bool bManualOnly)
{
    AppendIssueSection(Sections, TEXT("Prepared Mesh UV Layout"), Report, EWCAValidationSection::DataUV, bManualOnly);
    AppendIssueSection(Sections, TEXT("Runtime Data"), Report, EWCAValidationSection::RuntimeData, bManualOnly);
    AppendIssueSection(Sections, TEXT("Generated Materials"), Report, EWCAValidationSection::GeneratedMaterials, bManualOnly);
    AppendIssueSection(Sections, TEXT("GPU Runtime Data"), Report, EWCAValidationSection::GPUSimulationMaps, bManualOnly);
    AppendIssueSection(Sections, TEXT("Render Profile Lookup Texture"), Report, EWCAValidationSection::RenderProfileData, bManualOnly);
    AppendIssueSection(Sections, TEXT("Wrinkle Textures"), Report, EWCAValidationSection::WrinkleMaps, bManualOnly);
    AppendIssueSection(Sections, TEXT("Transparency Textures"), Report, EWCAValidationSection::TransparencyMaps, bManualOnly);
    AppendIssueSection(Sections, TEXT("Internal Failure"), Report, EWCAValidationSection::FailureDetails, bManualOnly);
}
}

bool FWCAValidationReport::HasManualIssues() const
{
    return Issues.ContainsByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return Issue.Remediation == EDWCEditorValidationRemediation::Manual;
        });
}

bool FWCAValidationReport::HasAutoResolvableIssues() const
{
    return Issues.ContainsByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return Issue.Remediation == EDWCEditorValidationRemediation::BuildAction &&
                   Issue.BuildAction.IsSet();
        });
}

FString FWCAValidationReport::BuildSummary() const
{
    TArray<FString> Sections;
    AppendAllIssueSections(Sections, *this, false);
    return FString::Join(Sections, TEXT("\n\n"));
}

FString FWCAValidationReport::BuildManualIssueSummary() const
{
    TArray<FString> Sections;
    AppendAllIssueSections(Sections, *this, true);
    return FString::Join(Sections, TEXT("\n\n"));
}

FWCAEditorValidationSnapshot BuildWCAValidationSnapshot(
    UWetClothingAsset& Asset,
    const EWCAValidationMode Mode,
    const bool bRefreshAssetState)
{
#if WITH_EDITORONLY_DATA
    if (bRefreshAssetState)
    {
        Asset.RefreshBakeState(Mode == EWCAValidationMode::Deep);
    }

    FWCAEditorValidationSnapshot Snapshot;
    Snapshot.AssetPath = Asset.GetPathName();
    Snapshot.bAssetDirty = Asset.GetOutermost() != nullptr &&
        Asset.GetOutermost()->IsDirty();
    Snapshot.bDeepValidation = Mode == EWCAValidationMode::Deep;
    Snapshot.TriangleDiagnostics = Asset.GetValidationSummary();

    const FDWCEditorValidationEvaluationContext Context(
        Asset,
        Snapshot.bDeepValidation);
    FDWCAssetValidationEvaluator::AppendAssetAndDataUV(Asset, Snapshot);
    FDWCWetPartValidationEvaluator::AppendToSnapshot(Context, Snapshot);
    FDWCGeneratedMaterialValidationEvaluator::AppendToSnapshot(Context, Snapshot);
    FDWCRuntimeValidationEvaluator::AppendToSnapshot(Context, Snapshot);
    FDWCRenderProfileValidationEvaluator::AppendToSnapshot(Asset, Snapshot);
    FDWCWrinkleValidationEvaluator::AppendToSnapshot(
        Asset,
        Snapshot.bDeepValidation,
        Snapshot);
    FDWCTransparencyLayerValidationEvaluator::AppendToSnapshot(
        Asset,
        Snapshot.bDeepValidation,
        Snapshot);
    if (Snapshot.bDeepValidation)
    {
        FDWCTransparencyCookValidationEvaluator::AppendToSnapshot(Asset, Snapshot);
    }
    FDWCAssetValidationEvaluator::AppendUnownedFailure(Asset, Snapshot);
    return Snapshot;
#else
    return {};
#endif
}

FWCAValidationReport BuildWCAValidationReport(
    UWetClothingAsset& Asset,
    const EWCAValidationMode Mode,
    const bool bRefreshAssetState)
{
    return FDWCEditorValidationReportAdapter::BuildReport(
        BuildWCAValidationSnapshot(Asset, Mode, bRefreshAssetState));
}
