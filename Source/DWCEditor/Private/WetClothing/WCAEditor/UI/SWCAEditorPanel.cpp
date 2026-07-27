#include "SWCAEditorPanel.h"

#include "DataAssets/WetClothingAsset.h"
#include "IDetailsView.h"
#include "Styling/AppStyle.h"
#include "WetClothing/Modes/Part/Editor/SWetClothingPartEditorPanel.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"
#include "WetClothing/Modes/Transparency/Editor/SWetClothingTransparencyBakePanel.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"
#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleEditorPanel.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"
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
        (void)bAssetHasUnsavedChanges;
        const bool bHasPriorOutput = bHasPayload || bWasEverGenerated || bWasEverSaved;
        if (DWCBuildStatus::IsUsable(Status) && bSavePending)
        {
            return FString::Printf(TEXT("%s: Generated and current, but not saved yet. Save the asset to persist it."), Label);
        }
        if (Status == EDWCBakeStatus::Required && !bHasPriorOutput)
        {
            return FString::Printf(TEXT("%s: Not generated yet. Save the asset to generate it."), Label);
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
        if (!Asset.Authored.WrinkleData.BakedWrinkleMaps.IsEmpty())
        {
            return true;
        }
        return !Asset.Authored.WrinkleData.EditablePatches.IsEmpty() ||
               !Asset.Authored.WrinkleData.EditableProceduralRidgeStrokes.IsEmpty();
    }

    void AppendIssueSection(TArray<FString>& Sections, const TCHAR* Heading, const TArray<FString>& Messages)
    {
        if (Messages.IsEmpty())
        {
            return;
        }

        Sections.Add(FString::Printf(TEXT("%s\n%s"), Heading, *FString::Join(Messages, TEXT("\n"))));
    }

    EWCAEditorStatusSeverity ToEditorSeverity(const EWCAValidationSeverity Severity)
    {
        switch (Severity)
        {
        case EWCAValidationSeverity::Error: return EWCAEditorStatusSeverity::Error;
        case EWCAValidationSeverity::Warning: return EWCAEditorStatusSeverity::Warning;
        default: return EWCAEditorStatusSeverity::Info;
        }
    }

    FString BuildIssueStatusMessage(const FWCAValidationIssue& Issue)
    {
        FString Message = Issue.Detail.IsEmpty() ? Issue.Title.ToString() : Issue.Detail.ToString();
        if (!Issue.RequiredAction.IsEmpty())
        {
            Message += FString::Printf(TEXT(" %s"), *Issue.RequiredAction.ToString());
        }
        return Message;
    }

    void AddReportIssueToStatus(FWCAEditorIssueStatus& Status, const FWCAValidationIssue& Issue)
    {
        RaiseIssueSeverity(Status, ToEditorSeverity(Issue.Severity));
        if (Issue.FixKind == EWCAValidationFixKind::FixSurfaceWaterProfile)
        {
            Status.bSurfaceWaterProfileIssue = true;
        }

        TArray<FString>* TargetMessages = nullptr;
        switch (Issue.Category)
        {
        case EWCAValidationIssueCategory::DataUV:
            Status.bGeneratedDataUVIssue = true;
            TargetMessages = &Status.GeneratedDataUVMessages;
            break;

        case EWCAValidationIssueCategory::Runtime:
            Status.bRuntimeIssue = true;
            TargetMessages = &Status.RuntimeMessages;
            break;

        case EWCAValidationIssueCategory::Material:
            Status.bMaterialIssue = true;
            TargetMessages = &Status.MaterialMessages;
            break;

        case EWCAValidationIssueCategory::Failure:
            Status.bFailure = true;
            TargetMessages = &Status.FailureMessages;
            break;

        case EWCAValidationIssueCategory::Map:
        default:
            Status.bMapIssue = true;
            TargetMessages = &Status.MapMessages;
            break;
        }

        if (TargetMessages != nullptr)
        {
            TargetMessages->Add(BuildIssueStatusMessage(Issue));
        }
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
    OnStatusChanged = InArgs._OnStatusChanged;
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

    {
        TGuardValue<bool> SuppressStatusChangedNotification(bSuppressStatusChangedNotification, true);
        SetEditorMode(EWCAEditorMode::PartEdit);
    }
    RegisterActiveTimer(
        0.5,
        FWidgetActiveTimerDelegate::CreateSP(this, &SWCAEditorPanel::HandleStatusRefreshTimer));
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

void SWCAEditorPanel::RefreshFromAsset(const bool bRebuildActiveModePreview)
{
    bRefreshPending = false;
    bPendingFullModeRefresh = false;
    UpdateCachedStatus();

    switch (ActiveMode)
    {
    case EWCAEditorMode::PartEdit:
        if (PartEditorPanel.IsValid()) PartEditorPanel->RefreshFromAsset();
        break;
    case EWCAEditorMode::WrinkleEdit:
        if (WrinkleEditorPanel.IsValid())
        {
            if (bRebuildActiveModePreview)
            {
                WrinkleEditorPanel->RefreshFromAsset();
            }
            else
            {
                WrinkleEditorPanel->RefreshFromAssetLightweight();
            }
        }
        break;
    case EWCAEditorMode::TransparencyBake:
        if (TransparencyBakePanel.IsValid()) TransparencyBakePanel->RefreshFromAsset();
        break;
    default:
        break;
    }
}

void SWCAEditorPanel::RefreshStatusFromAsset()
{
    // DWCEditorUtils::SaveAsset has already refreshed the asset bake state before
    // broadcasting save completion. Reuse that state instead of validating twice.
    UpdateCachedStatus(false);
}

void SWCAEditorPanel::RequestRefreshFromAsset(const bool bRebuildActiveModePreview)
{
    bPendingFullModeRefresh |= bRebuildActiveModePreview;
    if (bRefreshPending)
    {
        return;
    }
    bRefreshPending = true;
    RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SWCAEditorPanel::HandleDeferredRefresh));
}

EActiveTimerReturnType SWCAEditorPanel::HandleDeferredRefresh(double CurrentTime, float DeltaTime)
{
    const bool bRebuildActiveModePreview = bPendingFullModeRefresh;
    RefreshFromAsset(bRebuildActiveModePreview);
    return EActiveTimerReturnType::Stop;
}

EActiveTimerReturnType SWCAEditorPanel::HandleStatusRefreshTimer(double CurrentTime, float DeltaTime)
{
    UpdateCachedStatus(false);
    return EActiveTimerReturnType::Continue;
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

    const FWCAValidationReport Report = BuildWCAValidationReport(
        *Asset,
        bRunDeepValidation ? EWCAValidationMode::Deep : EWCAValidationMode::Fast,
        bRefreshAssetState);
    for (const FWCAValidationIssue& Issue : Report.Issues)
    {
        AddReportIssueToStatus(Result, Issue);
    }
    return Result;
}

void SWCAEditorPanel::UpdateCachedStatus(const bool bRefreshAssetState)
{
    const bool bPreviousStatusWarningVisible = bStatusWarningVisible;
    const EWCAEditorStatusSeverity PreviousStatusSeverity = CachedStatusSeverity;
    const FText PreviousStatusText = CachedStatusText;

    const FWCAEditorIssueStatus Status = CollectIssueStatus(bRefreshAssetState, false);
    bStatusWarningVisible = Status.HasIssues();
    CachedStatusSeverity = NormalizeIssueSeverity(Status);
    CachedStatusText = bStatusWarningVisible
        ? FText::FromString(Status.BuildSummary())
        : FText::GetEmpty();

    if (!bSuppressStatusChangedNotification &&
        OnStatusChanged.IsBound() &&
        (bPreviousStatusWarningVisible != bStatusWarningVisible ||
         PreviousStatusSeverity != CachedStatusSeverity ||
         !PreviousStatusText.EqualTo(CachedStatusText)))
    {
        OnStatusChanged.Execute();
    }
}

bool SWCAEditorPanel::HasPendingVisualBakeTasks(FString* OutSummary) const
{
    TArray<FString> PendingSections;
    FString PartSummary;
    if (FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(WetClothingAsset.Get(), &PartSummary))
    {
        PendingSections.Add(PartSummary);
    }
    if (OutSummary)
    {
        *OutSummary = PendingSections.IsEmpty() ? TEXT("Render profile data is up to date.") : FString::Join(PendingSections, TEXT("\n\n"));
    }
    return !PendingSections.IsEmpty();
}

bool SWCAEditorPanel::BakeWetVisualAssets(FString& OutSummary, bool* OutHadWarnings)
{
    return FWetClothingRenderProfileBakeService::BakeRenderProfileDataAndUpdateMaterials(WetClothingAsset.Get(), OutSummary, OutHadWarnings);
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
    if (FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(WetClothingAsset.Get(), &PartPendingSummary))
    {
        FString PartBakeSummary;
        bool bPartWarnings = false;
        if (FWetClothingRenderProfileBakeService::BakeRenderProfileDataAndUpdateMaterials(WetClothingAsset.Get(), PartBakeSummary, &bPartWarnings))
        {
            Sections.Add(PartBakeSummary);
            bHadWarnings |= bPartWarnings;
        }
        else
        {
            Failures.Add(FString::Printf(TEXT("Render Profile Data: %s"), *PartBakeSummary));
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

    OutSummary = Sections.IsEmpty() ? TEXT("Render profile data is up to date.") : FString::Join(Sections, TEXT("\n\n"));
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = bHadWarnings;
    }
    return true;
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

bool SWCAEditorPanel::SaveTransparencySetupAssets() const
{
    return FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(WetClothingAsset.Get());
}

bool SWCAEditorPanel::SaveBakedVisualAssets() const
{
    bool bSaved = true;
    bSaved &= FWetClothingRenderProfileBakeService::SaveBakedRenderProfileAssets(WetClothingAsset.Get());
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
        RefreshFromAsset(false);
    }
    else
    {
        bRefreshPending = false;
        UpdateCachedStatus();
    }
}

#undef LOCTEXT_NAMESPACE
