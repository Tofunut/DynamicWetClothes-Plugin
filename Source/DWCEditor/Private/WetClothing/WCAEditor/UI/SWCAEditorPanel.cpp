#include "SWCAEditorPanel.h"

#include "DataAssets/WetClothingAsset.h"
#include "IDetailsView.h"
#include "Styling/AppStyle.h"
#include "WetClothing/Modes/Part/Editor/SWetClothingPartEditorPanel.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetnessProfileMapBakeService.h"
#include "WetClothing/Modes/Transparency/Editor/SWetClothingTransparencyBakePanel.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"
#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleEditorPanel.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "WCAEditorPanel"

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

    void RaiseIssueSeverity(FWCAEditorIssueStatus& Status, const EWCAEditorStatusSeverity Severity)
    {
        if (static_cast<uint8>(Severity) > static_cast<uint8>(Status.Severity))
        {
            Status.Severity = Severity;
        }
    }

    EWCAEditorStatusSeverity GetSeverityForStatus(
        const EDWCBakeStatus Status,
        const EWCAEditorStatusSeverity NonFailedSeverity = EWCAEditorStatusSeverity::Warning)
    {
        return Status == EDWCBakeStatus::Failed
            ? EWCAEditorStatusSeverity::Error
            : NonFailedSeverity;
    }

    EWCAEditorStatusSeverity GetRuntimeSeverity(const EDWCBakeStatus Status, const bool bHasPayload)
    {
        if (Status == EDWCBakeStatus::Failed)
        {
            return EWCAEditorStatusSeverity::Error;
        }
        if (Status == EDWCBakeStatus::Required && !bHasPayload)
        {
            return EWCAEditorStatusSeverity::Info;
        }
        return EWCAEditorStatusSeverity::Warning;
    }

    FString BuildRuntimeDataMessage(
        const TCHAR* Label,
        const EDWCBakeStatus Status,
        const bool bHasPayload,
        const bool bWasEverGenerated,
        const bool bWasEverSaved,
        const bool bAssetHasUnsavedChanges,
        const bool bSavePending,
        const FString& FailureDetails)
    {
        const bool bHasPriorOutput = bHasPayload || bWasEverGenerated || bWasEverSaved;
        const bool bHasSavedOutput = bHasPayload || bWasEverSaved;
        if (DWCBuildStatus::IsUsable(Status) && bSavePending)
        {
            return FString::Printf(TEXT("%s: Generated and current, but not saved yet. Save the asset to persist it."), Label);
        }
        if (Status == EDWCBakeStatus::Required && !bHasPriorOutput)
        {
            return FString::Printf(TEXT("%s: Not generated yet. Save the asset to generate it."), Label);
        }
        if (Status == EDWCBakeStatus::Required && !bHasSavedOutput && bAssetHasUnsavedChanges)
        {
            return FString::Printf(TEXT("%s: Generated but not saved yet. Save the asset to persist it."), Label);
        }
        if (Status == EDWCBakeStatus::Required)
        {
            return FString::Printf(TEXT("%s: Missing from the saved runtime payload. Save the asset to rebuild it."), Label);
        }
        if (Status == EDWCBakeStatus::OutOfDate)
        {
            return FString::Printf(TEXT("%s: Out of date. Save the asset to rebuild it."), Label);
        }
        if (Status == EDWCBakeStatus::Failed)
        {
            if (!FailureDetails.IsEmpty())
            {
                return FString::Printf(TEXT("%s: Failed. %s"), Label, *FailureDetails);
            }
            return FString::Printf(TEXT("%s: Failed. Check the failure details, then save the asset to rebuild it."), Label);
        }
        return FString::Printf(TEXT("%s: %s. Save the asset to rebuild it."), Label, *BakeStatusToString(Status));
    }

    FString BuildMapDataMessage(
        const TCHAR* Label,
        const EDWCBakeStatus Status,
        const bool bWasEverGenerated,
        const bool bWasEverSaved,
        const bool bAssetHasUnsavedChanges,
        const bool bSavePending)
    {
        if (DWCBuildStatus::IsUsable(Status) && bSavePending)
        {
            return FString::Printf(TEXT("%s: Baked and current, but not saved yet. Save the asset to persist it."), Label);
        }
        if (Status == EDWCBakeStatus::Required && !bWasEverGenerated && !bWasEverSaved)
        {
            return FString::Printf(TEXT("%s: Not baked yet. Use Bake Maps to generate it."), Label);
        }
        if (Status == EDWCBakeStatus::Required && !bWasEverSaved && bAssetHasUnsavedChanges)
        {
            return FString::Printf(TEXT("%s: Baked but not saved yet. Save the asset to persist it."), Label);
        }
        if (Status == EDWCBakeStatus::Required)
        {
            return FString::Printf(TEXT("%s: Missing from the saved bake outputs. Use Bake Maps to rebuild it."), Label);
        }
        if (Status == EDWCBakeStatus::OutOfDate)
        {
            return FString::Printf(TEXT("%s: Out of date. Use Bake Maps to rebuild it."), Label);
        }
        if (Status == EDWCBakeStatus::Failed)
        {
            return FString::Printf(TEXT("%s: Failed. Use Bake Maps to rebuild it."), Label);
        }
        return FString::Printf(TEXT("%s: %s. Use Bake Maps to rebuild it."), Label, *BakeStatusToString(Status));
    }

    bool ContainsFailureIndicator(const FWCAEditorIssueStatus& Status)
    {
        if (Status.bFailure)
        {
            return true;
        }

        auto MessagesContainFailure = [](const TArray<FString>& Messages)
        {
            for (const FString& Message : Messages)
            {
                if (Message.Contains(TEXT("Failed"), ESearchCase::IgnoreCase) ||
                    Message.Contains(TEXT("Failure"), ESearchCase::IgnoreCase))
                {
                    return true;
                }
            }
            return false;
        };

        return MessagesContainFailure(Status.GeneratedDataUVMessages) ||
               MessagesContainFailure(Status.RuntimeMessages) ||
               MessagesContainFailure(Status.MapMessages) ||
               MessagesContainFailure(Status.MaterialMessages) ||
               MessagesContainFailure(Status.FailureMessages);
    }

    EWCAEditorStatusSeverity NormalizeIssueSeverity(const FWCAEditorIssueStatus& Status)
    {
        if (ContainsFailureIndicator(Status))
        {
            return EWCAEditorStatusSeverity::Error;
        }
        return Status.Severity;
    }

    bool HasWrinkleContent(const UWetClothingAsset& Asset)
    {
        if (!Asset.WrinkleData.BakedWrinkleMaps.IsEmpty())
        {
            return true;
        }
        for (const FWetWrinklePatchStroke& Stroke : Asset.WrinkleData.EditablePatchStrokes)
        {
            if (!Stroke.PatchPlacements.IsEmpty())
            {
                return true;
            }
        }
        return false;
    }

    void AppendIssueSection(TArray<FString>& Sections, const TCHAR* Heading, const TArray<FString>& Messages)
    {
        if (Messages.IsEmpty())
        {
            return;
        }
        Sections.Add(FString::Printf(TEXT("%s\n• %s"), Heading, *FString::Join(Messages, TEXT("\n• "))));
    }
}

FString FWCAEditorIssueStatus::BuildSummary() const
{
    TArray<FString> Sections;
    AppendIssueSection(Sections, TEXT("DWC Data UV"), GeneratedDataUVMessages);
    AppendIssueSection(Sections, TEXT("Runtime Data"), RuntimeMessages);
    AppendIssueSection(Sections, TEXT("Texture Maps"), MapMessages);
    AppendIssueSection(Sections, TEXT("Generated Materials"), MaterialMessages);
    AppendIssueSection(Sections, TEXT("Failures"), FailureMessages);
    return FString::Join(Sections, TEXT("\n\n"));
}

SWCAEditorPanel::~SWCAEditorPanel()
{
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        FWCAGeneratedDataInvalidator::InvalidateAsset(*Asset);
    }
}

void SWCAEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;
    CachedStatusText = FText::GetEmpty();

    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
        [
            SNew(SBorder)
            .Visibility(this, &SWCAEditorPanel::GetRuntimeReadyWarningVisibility)
            .Padding(FMargin(10.0f, 6.0f))
            .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Panel")))
            .BorderBackgroundColor(this, &SWCAEditorPanel::GetRuntimeReadyStatusBackgroundColor)
            [
                SNew(STextBlock)
                .Text(this, &SWCAEditorPanel::GetRuntimeReadyWarningText)
                .ColorAndOpacity(this, &SWCAEditorPanel::GetRuntimeReadyStatusTextColor)
            ]
        ]
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        [SAssignNew(ModeContentBox, SBox)]
    ];

    SetEditorMode(EWCAEditorMode::PartEdit);
}

TSharedRef<SWidget> SWCAEditorPanel::EnsureModeWidget(const EWCAEditorMode Mode)
{
    switch (Mode)
    {
    case EWCAEditorMode::PartEdit:
        if (!PartEditorPanel.IsValid())
        {
            SAssignNew(PartEditorPanel, SWetClothingPartEditorPanel)
                .WetClothingAsset(WetClothingAsset.Get())
                .DetailsView(DetailsView);
        }
        return PartEditorPanel.ToSharedRef();

    case EWCAEditorMode::WrinkleEdit:
        if (!WrinkleEditorPanel.IsValid())
        {
            SAssignNew(WrinkleEditorPanel, SWetWrinkleEditorPanel)
                .WetClothingAsset(WetClothingAsset.Get())
                .DetailsView(DetailsView);
        }
        return WrinkleEditorPanel.ToSharedRef();

    case EWCAEditorMode::TransparencyBake:
        if (!TransparencyBakePanel.IsValid())
        {
            SAssignNew(TransparencyBakePanel, SWetClothingTransparencyBakePanel)
                .WetClothingAsset(WetClothingAsset.Get())
                .DetailsView(DetailsView);
        }
        return TransparencyBakePanel.ToSharedRef();

    default:
        return SNullWidget::NullWidget;
    }
}

void SWCAEditorPanel::RefreshFromAsset()
{
    bRefreshPending = false;
    UpdateCachedStatus();

    switch (ActiveMode)
    {
    case EWCAEditorMode::PartEdit:
        if (PartEditorPanel.IsValid()) PartEditorPanel->RefreshFromAsset();
        break;
    case EWCAEditorMode::WrinkleEdit:
        if (WrinkleEditorPanel.IsValid()) WrinkleEditorPanel->RefreshFromAsset();
        break;
    case EWCAEditorMode::TransparencyBake:
        if (TransparencyBakePanel.IsValid()) TransparencyBakePanel->RefreshFromAsset();
        break;
    default:
        break;
    }
}

void SWCAEditorPanel::RequestRefreshFromAsset()
{
    if (bRefreshPending)
    {
        return;
    }
    bRefreshPending = true;
    RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SWCAEditorPanel::HandleDeferredRefresh));
}

EActiveTimerReturnType SWCAEditorPanel::HandleDeferredRefresh(double CurrentTime, float DeltaTime)
{
    RefreshFromAsset();
    return EActiveTimerReturnType::Stop;
}

FWCAEditorIssueStatus SWCAEditorPanel::CollectIssueStatus(
    const bool bRefreshAssetState,
    const bool bRunDeepValidation) const
{
    FWCAEditorIssueStatus Result;
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return Result;
    }

    if (bRefreshAssetState)
    {
        Asset->RefreshBakeState(bRunDeepValidation);
    }
#if WITH_EDITORONLY_DATA
    const FDWCAssetBakeState& State = Asset->GetBakeState();
    const FDWCWetClothingAssetSetupSettings& Setup = Asset->GetSetupSettings();
    const bool bAssetHasUnsavedChanges = Asset->GetOutermost() != nullptr && Asset->GetOutermost()->IsDirty();

    if (State.GeneratedDataUV != EDWCBakeStatus::Valid)
    {
        Result.bGeneratedDataUVIssue = true;
        RaiseIssueSeverity(Result, GetSeverityForStatus(State.GeneratedDataUV));
        Result.GeneratedDataUVMessages.Add(FString::Printf(
            TEXT("DWC Data UV: %s. Use DWC Data UV on the toolbar to rebuild it."),
            *BakeStatusToString(State.GeneratedDataUV)));
    }
    if (State.OriginalUVTopology != EDWCBakeStatus::Valid)
    {
        Result.bGeneratedDataUVIssue = true;
        RaiseIssueSeverity(Result, GetSeverityForStatus(State.OriginalUVTopology));
        Result.GeneratedDataUVMessages.Add(FString::Printf(
            TEXT("Original UV Topology: %s. Rebuild the DWC Data UV."),
            *BakeStatusToString(State.OriginalUVTopology)));
    }

    const bool bCPURuntimeSavePending = Asset->IsBakeOutputSavePending(DWCBakeOutput::CPURuntimeData);
    if ((Setup.bBuildCPUVertexSimulationData || Asset->HasCPURuntimeDataPayload()) &&
        State.CPURuntimeData != EDWCBakeStatus::Disabled &&
        (!DWCBuildStatus::IsUsable(State.CPURuntimeData) || bCPURuntimeSavePending))
    {
        Result.bRuntimeIssue = true;
        const bool bHasPayload = Asset->HasCPURuntimeDataPayload();
        const bool bWasEverGenerated = Asset->HasGeneratedBakeOutput(DWCBakeOutput::CPURuntimeData);
        const bool bWasEverSaved = Asset->HasSavedBakeOutput(DWCBakeOutput::CPURuntimeData);
        RaiseIssueSeverity(Result, GetRuntimeSeverity(State.CPURuntimeData, bHasPayload || bWasEverGenerated || bWasEverSaved));
        Result.RuntimeMessages.Add(BuildRuntimeDataMessage(
            TEXT("CPU Runtime Data"),
            State.CPURuntimeData,
            bHasPayload,
            bWasEverGenerated,
            bWasEverSaved,
            bAssetHasUnsavedChanges,
            bCPURuntimeSavePending,
            State.LastFailure));
    }
    const bool bGPURuntimeSavePending = Asset->IsBakeOutputSavePending(DWCBakeOutput::GPURuntimeData);
    if ((Setup.bBuildGPUWetnessMapSimulationData || Asset->HasGPURuntimeDataPayload()) &&
        State.GPURuntimeData != EDWCBakeStatus::Disabled &&
        (!DWCBuildStatus::IsUsable(State.GPURuntimeData) || bGPURuntimeSavePending))
    {
        Result.bRuntimeIssue = true;
        const bool bHasPayload = Asset->HasGPURuntimeDataPayload();
        const bool bWasEverGenerated = Asset->HasGeneratedBakeOutput(DWCBakeOutput::GPURuntimeData);
        const bool bWasEverSaved = Asset->HasSavedBakeOutput(DWCBakeOutput::GPURuntimeData);
        RaiseIssueSeverity(Result, GetRuntimeSeverity(State.GPURuntimeData, bHasPayload || bWasEverGenerated || bWasEverSaved));
        Result.RuntimeMessages.Add(BuildRuntimeDataMessage(
            TEXT("GPU Runtime Data"),
            State.GPURuntimeData,
            bHasPayload,
            bWasEverGenerated,
            bWasEverSaved,
            bAssetHasUnsavedChanges,
            bGPURuntimeSavePending,
            State.LastFailure));
    }

    const bool bGPUMapSavePending = Asset->IsBakeOutputSavePending(DWCBakeOutput::GPUMaps);
    if ((Setup.bBuildGPUWetnessMapSimulationData || Asset->HasGPUMapDataPayload()) &&
        State.GPUMaps != EDWCBakeStatus::Disabled &&
        (!DWCBuildStatus::IsUsable(State.GPUMaps) || bGPUMapSavePending))
    {
        Result.bMapIssue = true;
        RaiseIssueSeverity(Result, GetSeverityForStatus(State.GPUMaps));
        const bool bWasEverGenerated = Asset->HasGeneratedBakeOutput(DWCBakeOutput::GPUMaps);
        const bool bWasEverSaved = Asset->HasGPUMapDataPayload() || Asset->HasSavedBakeOutput(DWCBakeOutput::GPUMaps);
        Result.MapMessages.Add(BuildMapDataMessage(
            TEXT("GPU Simulation Maps"),
            State.GPUMaps,
            bWasEverGenerated,
            bWasEverSaved,
            bAssetHasUnsavedChanges,
            bGPUMapSavePending));
    }

    TArray<FString> GeneratedMaterialMessages;
    if (Asset->HasAnyWettableMaterialSlot())
    {
        if (bRunDeepValidation)
        {
            FWCAMaterialGenerator::ValidateGeneratedMaterialOverrides(Asset, GeneratedMaterialMessages);
        }
        else
        {
            FWCAMaterialGenerator::ValidateGeneratedMaterialOverrideReferences(Asset, GeneratedMaterialMessages);
        }
    }
    if (!GeneratedMaterialMessages.IsEmpty())
    {
        Result.bMaterialIssue = true;
        RaiseIssueSeverity(Result, EWCAEditorStatusSeverity::Warning);
        Result.MaterialMessages = MoveTemp(GeneratedMaterialMessages);
    }

    if (Asset->HasWrinkleBakeContent() && !DWCBuildStatus::IsUsable(State.WrinkleMaps))
    {
        Result.bMapIssue = true;
        RaiseIssueSeverity(Result, GetSeverityForStatus(State.WrinkleMaps));
        Result.MapMessages.Add(BuildMapDataMessage(
            TEXT("Wrinkle Maps"),
            State.WrinkleMaps,
            Asset->HasGeneratedBakeOutput(DWCBakeOutput::WrinkleMaps),
            Asset->HasSavedBakeOutput(DWCBakeOutput::WrinkleMaps),
            bAssetHasUnsavedChanges,
            false));
    }
    if (Asset->HasTransparencyBakeContent() && !DWCBuildStatus::IsUsable(State.TransparencyMaps))
    {
        Result.bMapIssue = true;
        RaiseIssueSeverity(Result, GetSeverityForStatus(State.TransparencyMaps));
        Result.MapMessages.Add(BuildMapDataMessage(
            TEXT("Transparency Maps"),
            State.TransparencyMaps,
            Asset->HasGeneratedBakeOutput(DWCBakeOutput::TransparencyMaps),
            Asset->HasSavedBakeOutput(DWCBakeOutput::TransparencyMaps),
            bAssetHasUnsavedChanges,
            false));
    }

    FString VisualSummary;
    if (Asset->HasAnyWettableMaterialSlot() && HasPendingVisualBakeTasks(&VisualSummary) && !VisualSummary.IsEmpty())
    {
        Result.bMapIssue = true;
        RaiseIssueSeverity(Result, EWCAEditorStatusSeverity::Warning);
        Result.MapMessages.Add(VisualSummary);
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
        const auto ContainsFailureDetail = [&State](const TArray<FString>& Messages)
        {
            return Messages.ContainsByPredicate(
                [&State](const FString& Message)
                {
                    return Message.Contains(State.LastFailure) || State.LastFailure.Contains(Message);
                });
        };
        const bool bFailureAlreadyShown =
            ContainsFailureDetail(Result.RuntimeMessages) ||
            ContainsFailureDetail(Result.MapMessages) ||
            ContainsFailureDetail(Result.MaterialMessages) ||
            ContainsFailureDetail(Result.GeneratedDataUVMessages);
        if (!bFailureAlreadyShown)
        {
            Result.bFailure = true;
            RaiseIssueSeverity(Result, EWCAEditorStatusSeverity::Error);
            Result.FailureMessages.Add(State.LastFailure);
        }
    }
#endif
    return Result;
}

void SWCAEditorPanel::UpdateCachedStatus()
{
    const FWCAEditorIssueStatus Status = CollectIssueStatus(true, false);
    bStatusWarningVisible = Status.HasIssues();
    CachedStatusSeverity = NormalizeIssueSeverity(Status);
    CachedStatusText = bStatusWarningVisible
        ? FText::FromString(Status.BuildSummary())
        : FText::GetEmpty();
}

bool SWCAEditorPanel::HasPendingVisualBakeTasks(FString* OutSummary) const
{
    TArray<FString> PendingSections;
    FString PartSummary;
    if (FWetClothingWetnessProfileMapBakeService::HasPendingVisualBakeTasks(WetClothingAsset.Get(), &PartSummary))
    {
        PendingSections.Add(PartSummary);
    }
    FString TransparencySummary;
    if (FDWCTransparencyAssetBakeService::HasPendingTransparencySetup(WetClothingAsset.Get(), &TransparencySummary))
    {
        PendingSections.Add(TransparencySummary);
    }
    if (OutSummary)
    {
        *OutSummary = PendingSections.IsEmpty() ? TEXT("Visual maps are up to date.") : FString::Join(PendingSections, TEXT("\n\n"));
    }
    return !PendingSections.IsEmpty();
}

bool SWCAEditorPanel::BakeWetVisualAssets(FString& OutSummary, bool* OutHadWarnings)
{
    return FWetClothingWetnessProfileMapBakeService::BakeWetnessProfileMapsAndUpdateMaterials(WetClothingAsset.Get(), OutSummary, OutHadWarnings);
}

bool SWCAEditorPanel::BakePendingVisualAssets(FString& OutSummary, bool* OutHadWarnings)
{
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = false;
    }

    TArray<FString> Sections;
    TArray<FString> Failures;
    bool bHadWarnings = false;

    FString PartPendingSummary;
    if (FWetClothingWetnessProfileMapBakeService::HasPendingVisualBakeTasks(WetClothingAsset.Get(), &PartPendingSummary))
    {
        FString PartBakeSummary;
        bool bPartWarnings = false;
        if (FWetClothingWetnessProfileMapBakeService::BakeWetnessProfileMapsAndUpdateMaterials(WetClothingAsset.Get(), PartBakeSummary, &bPartWarnings))
        {
            Sections.Add(PartBakeSummary);
            bHadWarnings |= bPartWarnings;
        }
        else
        {
            Failures.Add(FString::Printf(TEXT("Wetness Profile Maps: %s"), *PartBakeSummary));
        }
    }

    FString TransparencyPendingSummary;
    if (FDWCTransparencyAssetBakeService::HasPendingTransparencySetup(WetClothingAsset.Get(), &TransparencyPendingSummary))
    {
        FString TransparencyBakeSummary;
        bool bTransparencyWarnings = false;
        if (BakeTransparencyRevealAssets(TransparencyBakeSummary, &bTransparencyWarnings))
        {
            Sections.Add(TransparencyBakeSummary);
            bHadWarnings |= bTransparencyWarnings;
        }
        else
        {
            Failures.Add(FString::Printf(TEXT("Transparency Maps: %s"), *TransparencyBakeSummary));
        }
    }

    if (!Failures.IsEmpty())
    {
        OutSummary = FString::Join(Failures, TEXT("\n\n"));
        if (OutHadWarnings != nullptr)
        {
            *OutHadWarnings = true;
        }
        return false;
    }

    OutSummary = Sections.IsEmpty() ? TEXT("Visual maps are up to date.") : FString::Join(Sections, TEXT("\n\n"));
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = bHadWarnings;
    }
    return true;
}

bool SWCAEditorPanel::BakeTransparencyRevealAssets(FString& OutSummary, bool* OutHadWarnings)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    const bool bSucceeded = FDWCTransparencyAssetBakeService::BakeTransparencyRevealAssets(Asset, OutSummary, OutHadWarnings);
    if (Asset != nullptr)
    {
        Asset->SetTransparencyBakeStatus(
            bSucceeded ? EDWCBakeStatus::Valid : EDWCBakeStatus::Failed,
            bSucceeded ? FString() : OutSummary);
    }
    return bSucceeded;
}

bool SWCAEditorPanel::BakeAllWrinkleMaps(FString& OutSummary, bool* OutHadWarnings)
{
    return FWetWrinkleBakeService::BakeAllWrinkleMaps(WetClothingAsset.Get(), OutSummary, OutHadWarnings);
}

FReply SWCAEditorPanel::BakeSelectedWrinkleNormalMap()
{
    EnsureModeWidget(EWCAEditorMode::WrinkleEdit);
    return WrinkleEditorPanel.IsValid()
               ? WrinkleEditorPanel->BakeSelectedWrinkleNormalMap()
               : FReply::Handled();
}

FReply SWCAEditorPanel::BakeSelectedWrinkleMask()
{
    EnsureModeWidget(EWCAEditorMode::WrinkleEdit);
    return WrinkleEditorPanel.IsValid()
               ? WrinkleEditorPanel->BakeSelectedWrinkleMask()
               : FReply::Handled();
}

bool SWCAEditorPanel::SaveTransparencySetupAssets() const
{
    return FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(WetClothingAsset.Get());
}

bool SWCAEditorPanel::SaveBakedVisualAssets() const
{
    bool bSaved = true;
    bSaved &= FWetClothingWetnessProfileMapBakeService::SaveBakedWetnessAssets(WetClothingAsset.Get());
    bSaved &= FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(WetClothingAsset.Get());
    return bSaved;
}

EVisibility SWCAEditorPanel::GetRuntimeReadyWarningVisibility() const
{
    return bStatusWarningVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SWCAEditorPanel::GetRuntimeReadyWarningText() const
{
    return CachedStatusText;
}

FSlateColor SWCAEditorPanel::GetRuntimeReadyStatusTextColor() const
{
    switch (CachedStatusSeverity)
    {
    case EWCAEditorStatusSeverity::Error:
        return FSlateColor(FLinearColor(1.0f, 0.28f, 0.28f, 1.0f));
    case EWCAEditorStatusSeverity::Warning:
        return FSlateColor(FLinearColor(1.0f, 0.72f, 0.24f, 1.0f));
    case EWCAEditorStatusSeverity::Info:
    default:
        return FSlateColor(FLinearColor(0.54f, 0.72f, 1.0f, 1.0f));
    }
}

FSlateColor SWCAEditorPanel::GetRuntimeReadyStatusBackgroundColor() const
{
    switch (CachedStatusSeverity)
    {
    case EWCAEditorStatusSeverity::Error:
        return FSlateColor(FLinearColor(0.24f, 0.03f, 0.03f, 1.0f));
    case EWCAEditorStatusSeverity::Warning:
        return FSlateColor(FLinearColor(0.22f, 0.14f, 0.02f, 1.0f));
    case EWCAEditorStatusSeverity::Info:
    default:
        return FSlateColor(FLinearColor(0.04f, 0.09f, 0.18f, 1.0f));
    }
}

void SWCAEditorPanel::SetEditorMode(const EWCAEditorMode NewMode)
{
    ActiveMode = NewMode;
    const bool bHadModeWidget =
        (NewMode == EWCAEditorMode::PartEdit && PartEditorPanel.IsValid()) ||
        (NewMode == EWCAEditorMode::WrinkleEdit && WrinkleEditorPanel.IsValid()) ||
        (NewMode == EWCAEditorMode::TransparencyBake && TransparencyBakePanel.IsValid());

    if (ModeContentBox.IsValid())
    {
        ModeContentBox->SetContent(EnsureModeWidget(NewMode));
    }

    if (bHadModeWidget)
    {
        RefreshFromAsset();
    }
    else
    {
        bRefreshPending = false;
        UpdateCachedStatus();
    }
}

#undef LOCTEXT_NAMESPACE
