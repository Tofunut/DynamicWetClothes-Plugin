#include "SWCAEditorPanel.h"

#include "DataAssets/WetClothingAsset.h"
#include "IDetailsView.h"
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
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "WCAEditorPanel"

namespace
{
    void RaiseIssueSeverity(FWCAEditorIssueStatus& Status, const EWCAEditorStatusSeverity Severity)
    {
        if (static_cast<uint8>(Severity) > static_cast<uint8>(Status.Severity))
        {
            Status.Severity = Severity;
        }
    }

    void AppendIssueSection(TArray<FString>& Sections, const TCHAR* Heading, const TArray<FString>& Messages)
    {
        if (!Messages.IsEmpty())
        {
            Sections.Add(FString::Printf(TEXT("%s\n%s"), Heading, *FString::Join(Messages, TEXT("\n"))));
        }
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
        ++Status.IssueCount;
        RaiseIssueSeverity(Status, ToEditorSeverity(Issue.Severity));

        TArray<FString>* TargetMessages = nullptr;
        switch (Issue.Section)
        {
        case EWCAValidationSection::DataUV:
            Status.bGeneratedDataUVIssue = true;
            TargetMessages = &Status.GeneratedDataUVMessages;
            break;
        case EWCAValidationSection::RuntimeData:
            Status.bRuntimeIssue = true;
            TargetMessages = &Status.RuntimeMessages;
            break;
        case EWCAValidationSection::GeneratedMaterials:
            Status.bGeneratedMaterialsIssue = true;
            TargetMessages = &Status.GeneratedMaterialMessages;
            break;
        case EWCAValidationSection::GPUSimulationMaps:
            Status.bGPUMapsIssue = true;
            TargetMessages = &Status.GPUMapMessages;
            break;
        case EWCAValidationSection::RenderProfileData:
            Status.bRenderProfileIssue = true;
            TargetMessages = &Status.RenderProfileMessages;
            break;
        case EWCAValidationSection::WrinkleMaps:
            Status.bWrinkleMapsIssue = true;
            TargetMessages = &Status.WrinkleMapMessages;
            break;
        case EWCAValidationSection::TransparencyMaps:
            Status.bTransparencyMapsIssue = true;
            TargetMessages = &Status.TransparencyMapMessages;
            break;
        case EWCAValidationSection::FailureDetails:
        default:
            Status.bFailure = true;
            TargetMessages = &Status.FailureMessages;
            break;
        }

        TargetMessages->Add(BuildIssueStatusMessage(Issue));
    }
}

FString FWCAEditorIssueStatus::BuildSummary() const
{
    TArray<FString> Sections;
    AppendIssueSection(Sections, TEXT("DWC UV Channel"), GeneratedDataUVMessages);
    AppendIssueSection(Sections, TEXT("Runtime Data"), RuntimeMessages);
    AppendIssueSection(Sections, TEXT("Generated Materials"), GeneratedMaterialMessages);
    AppendIssueSection(Sections, TEXT("GPU Runtime Data"), GPUMapMessages);
    AppendIssueSection(Sections, TEXT("Render Profile Lookup Texture"), RenderProfileMessages);
    AppendIssueSection(Sections, TEXT("Wrinkle Textures"), WrinkleMapMessages);
    AppendIssueSection(Sections, TEXT("Transparency Textures"), TransparencyMapMessages);
    AppendIssueSection(Sections, TEXT("Internal Failure"), FailureMessages);
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

    ChildSlot
    [
        SAssignNew(ModeContentBox, SBox)
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
    const int32 PreviousIssueCount = CachedIssueCount;
    const EWCAEditorStatusSeverity PreviousStatusSeverity = CachedStatusSeverity;

    const FWCAEditorIssueStatus Status = CollectIssueStatus(bRefreshAssetState, false);
    CachedIssueCount = Status.IssueCount;
    CachedStatusSeverity = Status.Severity;

    if (!bSuppressStatusChangedNotification &&
        OnStatusChanged.IsBound() &&
        (PreviousIssueCount != CachedIssueCount || PreviousStatusSeverity != CachedStatusSeverity))
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
        *OutSummary = PendingSections.IsEmpty() ? TEXT("Render Profile Lookup Texture is up to date.") : FString::Join(PendingSections, TEXT("\n\n"));
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
            Failures.Add(FString::Printf(TEXT("Render Profile Lookup Texture: %s"), *PartBakeSummary));
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

    OutSummary = Sections.IsEmpty() ? TEXT("Render Profile Lookup Texture is up to date.") : FString::Join(Sections, TEXT("\n\n"));
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
