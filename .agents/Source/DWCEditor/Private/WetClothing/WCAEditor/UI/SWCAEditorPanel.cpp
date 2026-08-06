#include "SWCAEditorPanel.h"

#include "DataAssets/WetClothingAsset.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Modes/Part/Editor/SWetClothingPartEditorPanel.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"
#include "WetClothing/Modes/Transparency/Editor/SWetClothingTransparencyBakePanel.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"
#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"
#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleEditorPanel.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "WCAEditorPanel"

namespace
{
    FDWCEditorAuthoringIndex BuildAuthoringIndex(const UWetClothingAsset* Asset)
    {
        FDWCEditorAuthoringIndex Index;
        if (Asset == nullptr)
        {
            return Index;
        }

        for (const FWetWrinklePatchPlacement& Patch : Asset->Authored.WrinkleData.EditablePatches)
        {
            Index.WrinkleMaterialSlots.Add(Patch.MaterialSlotIndex);
            if (Patch.PatchGuid.IsValid())
            {
                Index.WrinkleElementGuids.Add(Patch.PatchGuid);
            }
        }
        for (const FWetProceduralRidgeStroke& Stroke : Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
        {
            Index.WrinkleMaterialSlots.Add(Stroke.MaterialSlotIndex);
            if (Stroke.StrokeGuid.IsValid())
            {
                Index.WrinkleElementGuids.Add(Stroke.StrokeGuid);
            }
        }
        for (const FWetClothingTransparencyLayerData& Layer : Asset->Authored.TransparencyData.TransparencyLayers)
        {
            Index.TransparencyLayerGuids.Add(Layer.LayerGuid);
        }
        return Index;
    }

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
    if (PreBeginPIEHandle.IsValid())
    {
        FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
        PreBeginPIEHandle.Reset();
    }
    if (EndPIEHandle.IsValid())
    {
        FEditorDelegates::EndPIE.Remove(EndPIEHandle);
        EndPIEHandle.Reset();
    }
    SuspendAllPreviewModes(EDWCEditorPreviewSuspendReason::EditorClosing);
    if (PreviewCommitCoordinator.IsValid())
    {
        FDWCEditorPreviewDiagnostics::UnregisterCommitCoordinator(PreviewCommitCoordinator.Get());
        PreviewCommitCoordinator->Shutdown();
    }
    BakeCoordinator.Reset();
    if (WorkerJobScheduler.IsValid())
    {
        FDWCEditorPreviewDiagnostics::UnregisterWorkerScheduler(WorkerJobScheduler.Get());
        WorkerJobScheduler->Shutdown();
        WorkerJobScheduler.Reset();
    }
    ResourceGovernor.Reset();

    // Release viewport-owned texture leases before shutting down the upload
    // queue and workspace that service them.
    if (ModeContentBox.IsValid())
    {
        ModeContentBox->SetContent(SNullWidget::NullWidget);
    }
    WrinkleEditorPanel.Reset();
    TransparencyBakePanel.Reset();
    PreviewCommitCoordinator.Reset();
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->Shutdown();
    }
    TextureWorkspace.Reset();
    RenderUploadQueue.Reset();
    if (AuthoringDocument.IsValid())
    {
        AuthoringDocument->OnChanged().RemoveAll(this);
    }
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        FWCAGeneratedDataInvalidator::InvalidateAsset(*Asset);
    }
}

void SWCAEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    AuthoringDocument = MakeShared<FDWCEditorAuthoringDocument>(WetClothingAsset.Get());
    CacheStore = MakeShared<FDWCEditorCacheStore>();
    SpatialQueryService = MakeShared<FDWCEditorSpatialQueryService>(CacheStore.ToSharedRef());
    RenderUploadQueue = MakeShared<FDWCEditorRenderUploadQueue>();
    TextureWorkspace = MakeShared<FDWCEditorTextureWorkspace>(RenderUploadQueue.ToSharedRef());
    SessionStore = MakeShared<FDWCEditorSessionStore>();
    ResourceGovernor = MakeShared<FDWCEditorResourceGovernor>();
    WorkerJobScheduler = MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(
        ResourceGovernor.ToSharedRef());
    PreviewCommitCoordinator = MakeShared<FDWCEditorPreviewCommitCoordinator>(
        TextureWorkspace.ToSharedRef(),
        WorkerJobScheduler->GetSessionEpoch());
    FDWCEditorPreviewDiagnostics::RegisterCommitCoordinator(PreviewCommitCoordinator.Get());
    FDWCEditorPreviewDiagnostics::RegisterWorkerScheduler(WorkerJobScheduler.Get());
    TWeakPtr<FDWCEditorSessionStore> WeakSessionStore = SessionStore;
    WorkerJobScheduler->SetDomainRevisionProvider(
        [WeakSessionStore](const EDWCEditorAuthoringDomain Domain)
        {
            const TSharedPtr<FDWCEditorSessionStore> Store = WeakSessionStore.Pin();
            return Store.IsValid() ? GetDWCEditorDomainRevision(Store->GetState(), Domain) : MAX_uint64;
        });
    BakeCoordinator = MakeShared<FDWCEditorBakeCoordinator>(
        WetClothingAsset.Get(),
        WorkerJobScheduler.ToSharedRef());
    AuthoringDocument->OnChanged().AddSP(this, &SWCAEditorPanel::HandleAuthoringDocumentChanged);
    FDWCReconcileAuthoringAction InitialReconcile;
    InitialReconcile.AuthoringRevision = AuthoringDocument->GetRevision();
    InitialReconcile.Index = BuildAuthoringIndex(WetClothingAsset.Get());
    SessionStore->Dispatch(InitialReconcile);
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
    RegisterActiveTimer(
        0.0,
        FWidgetActiveTimerDelegate::CreateSP(this, &SWCAEditorPanel::HandleTextureUploadTimer));

    PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddSP(
        this,
        &SWCAEditorPanel::HandlePreBeginPIE);
    EndPIEHandle = FEditorDelegates::EndPIE.AddSP(
        this,
        &SWCAEditorPanel::HandleEndPIE);
}

EActiveTimerReturnType SWCAEditorPanel::HandleTextureUploadTimer(double, float)
{
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->Flush();
    }
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->ProcessRetiredGPUResources();
    }
    return EActiveTimerReturnType::Continue;
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
                .AuthoringDocument(AuthoringDocument)
                .SessionStore(SessionStore)
                .WorkerJobScheduler(WorkerJobScheduler)
                .BakeCoordinator(BakeCoordinator)
                .SpatialQueryService(SpatialQueryService)
                .TextureWorkspace(TextureWorkspace)
                .PreviewCommitCoordinator(PreviewCommitCoordinator)
                .RenderUploadQueue(RenderUploadQueue)
                .DetailsView(DetailsView);
        }
        return WrinkleEditorPanel.ToSharedRef();

    case EWCAEditorMode::TransparencyBake:
        if (!TransparencyBakePanel.IsValid())
        {
            SAssignNew(TransparencyBakePanel, SWetClothingTransparencyBakePanel)
                .WetClothingAsset(WetClothingAsset.Get())
                .AuthoringDocument(AuthoringDocument)
                .SessionStore(SessionStore)
                .WorkerJobScheduler(WorkerJobScheduler)
                .BakeCoordinator(BakeCoordinator)
                .SpatialQueryService(SpatialQueryService)
                .TextureWorkspace(TextureWorkspace)
                .PreviewCommitCoordinator(PreviewCommitCoordinator)
                .RenderUploadQueue(RenderUploadQueue)
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

    const EWCAEditorMode ActiveMode = SessionStore.IsValid()
        ? SessionStore->GetState().ActiveMode
        : EWCAEditorMode::PartEdit;
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

bool SWCAEditorPanel::RequestBakeAllWrinkleMaps(
    TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
    FString* OutError)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !BakeCoordinator.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The asynchronous bake service is unavailable.");
        return false;
    }
    TArray<int32> MaterialSlots;
    FWetWrinkleBakeService::CollectBakeMaterialSlots(*Asset, MaterialSlots);
    return BakeCoordinator->RequestWrinkleBake(
        MoveTemp(MaterialSlots),
        true,
        MoveTemp(Completion),
        OutError);
}

bool SWCAEditorPanel::RequestBakeAllTransparencyMaps(
    TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
    FString* OutError)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !BakeCoordinator.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The asynchronous bake service is unavailable.");
        return false;
    }

    TArray<FGuid> LayerGuids;
    for (const FWetClothingTransparencyLayerData& Layer :
         Asset->Authored.TransparencyData.TransparencyLayers)
    {
        const int32 MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
        if (Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots &&
            MaterialSlotIndex != INDEX_NONE &&
            Asset->IsMaterialSlotWettable(MaterialSlotIndex))
        {
            LayerGuids.Add(Layer.LayerGuid);
        }
    }
    return BakeCoordinator->RequestTransparencyBake(
        MoveTemp(LayerGuids),
        true,
        MoveTemp(Completion),
        OutError);
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
    if (bHasActiveEditorMode && ActiveEditorMode != NewMode)
    {
        SuspendPreviewMode(ActiveEditorMode, EDWCEditorPreviewSuspendReason::ModeSwitch);
    }
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCActivateEditorModeAction{NewMode});
    }
    const bool bHadModeWidget =
        (NewMode == EWCAEditorMode::PartEdit && PartEditorPanel.IsValid()) ||
        (NewMode == EWCAEditorMode::WrinkleEdit && WrinkleEditorPanel.IsValid()) ||
        (NewMode == EWCAEditorMode::TransparencyBake && TransparencyBakePanel.IsValid());

    if (ModeContentBox.IsValid())
    {
        ModeContentBox->SetContent(EnsureModeWidget(NewMode));
    }

    ActiveEditorMode = NewMode;
    bHasActiveEditorMode = true;
    ResumePreviewModeIfNeeded(NewMode);

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

void SWCAEditorPanel::HandleAuthoringDocumentChanged(const FDWCEditorAuthoringChange& Change)
{
    if (!SessionStore.IsValid())
    {
        return;
    }

    FDWCReconcileAuthoringAction Action;
    Action.AuthoringRevision = Change.Revision;
    Action.Domain = Change.Domain;
    Action.Index = BuildAuthoringIndex(WetClothingAsset.Get());
    Action.Impact = Change.Impact;
    SessionStore->Dispatch(Action);
}

void SWCAEditorPanel::SuspendPreviewMode(
    const EWCAEditorMode Mode,
    const EDWCEditorPreviewSuspendReason Reason)
{
    switch (Mode)
    {
    case EWCAEditorMode::WrinkleEdit:
        if (WrinkleEditorPanel.IsValid())
        {
            WrinkleEditorPanel->SuspendPreview(Reason);
        }
        break;
    case EWCAEditorMode::TransparencyBake:
        if (TransparencyBakePanel.IsValid())
        {
            TransparencyBakePanel->SuspendPreview(Reason);
        }
        break;
    case EWCAEditorMode::PartEdit:
    default:
        break;
    }
}

void SWCAEditorPanel::ResumePreviewModeIfNeeded(const EWCAEditorMode Mode)
{
    switch (Mode)
    {
    case EWCAEditorMode::WrinkleEdit:
        if (WrinkleEditorPanel.IsValid())
        {
            WrinkleEditorPanel->ResumePreviewIfNeeded();
        }
        break;
    case EWCAEditorMode::TransparencyBake:
        if (TransparencyBakePanel.IsValid())
        {
            TransparencyBakePanel->ResumePreviewIfNeeded();
        }
        break;
    case EWCAEditorMode::PartEdit:
    default:
        break;
    }
}

void SWCAEditorPanel::SuspendAllPreviewModes(const EDWCEditorPreviewSuspendReason Reason)
{
    SuspendPreviewMode(EWCAEditorMode::WrinkleEdit, Reason);
    SuspendPreviewMode(EWCAEditorMode::TransparencyBake, Reason);
}

void SWCAEditorPanel::HandlePreBeginPIE(const bool)
{
    SuspendAllPreviewModes(EDWCEditorPreviewSuspendReason::BeginPIE);
}

void SWCAEditorPanel::HandleEndPIE(const bool)
{
    // Do not rebuild preview textures on PIE exit. The active editor mode
    // resumes lazily through its mode button or the next editing action.
}

#undef LOCTEXT_NAMESPACE
