#pragma once

#include "CoreMinimal.h"
#include "WetClothing/WCAEditor/WCAEditorMode.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class FDWCEditorAuthoringDocument;
class FDWCEditorBakeCoordinator;
class FDWCEditorCacheStore;
class FDWCEditorRenderUploadQueue;
class FDWCEditorPreviewCommitCoordinator;
class FDWCEditorResourceGovernor;
class FDWCEditorSessionStore;
class FDWCEditorSpatialQueryService;
class FDWCEditorTextureWorkspace;
class FDWCEditorWorkerJobScheduler;
class SBox;
class SWetClothingPartEditorPanel;
class SWetClothingTransparencyBakePanel;
class SWetWrinkleEditorPanel;
class UWetClothingAsset;
struct FDWCEditorAuthoringChange;
struct FDWCEditorBakeBatchResult;
enum class EDWCEditorPreviewSuspendReason : uint8;

enum class EWCAEditorStatusSeverity : uint8
{
    Info,
    Warning,
    Error
};

/** Lightweight validation summary used by toolbar refresh and resolve/close flows. */
struct FWCAEditorIssueStatus
{
    bool bGeneratedDataUVIssue = false;
    bool bRuntimeIssue = false;
    bool bGeneratedMaterialsIssue = false;
    bool bGPUMapsIssue = false;
    bool bRenderProfileIssue = false;
    bool bWrinkleMapsIssue = false;
    bool bTransparencyMapsIssue = false;
    bool bFailure = false;
    int32 IssueCount = 0;
    EWCAEditorStatusSeverity Severity = EWCAEditorStatusSeverity::Info;

    TArray<FString> GeneratedDataUVMessages;
    TArray<FString> RuntimeMessages;
    TArray<FString> GeneratedMaterialMessages;
    TArray<FString> GPUMapMessages;
    TArray<FString> RenderProfileMessages;
    TArray<FString> WrinkleMapMessages;
    TArray<FString> TransparencyMapMessages;
    TArray<FString> FailureMessages;

    bool HasIssues() const { return IssueCount > 0; }
    FString BuildSummary() const;
};

/** Main editor panel. Heavy mode panels are created lazily and only the active mode is refreshed. */
class SWCAEditorPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWCAEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_EVENT(FSimpleDelegate, OnStatusChanged)
    SLATE_END_ARGS()

    virtual ~SWCAEditorPanel() override;
    void Construct(const FArguments& InArgs);

    void RefreshFromAsset(bool bRebuildActiveModePreview = true);
    void RefreshStatusFromAsset();
    void RequestRefreshFromAsset(bool bRebuildActiveModePreview = true);
    FWCAEditorIssueStatus CollectIssueStatus(bool bRefreshAssetState = true, bool bRunDeepValidation = false) const;
    bool HasPendingVisualBakeTasks(FString* OutSummary = nullptr) const;
    bool BakeWetVisualAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool BakePendingVisualAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    FReply BakeSelectedWrinkleNormalMap();
    bool BakeAllWrinkleMaps(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool RequestBakeAllWrinkleMaps(
        TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
        FString* OutError = nullptr);
    bool RequestBakeAllTransparencyMaps(
        TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
        FString* OutError = nullptr);
    bool SaveBakedVisualAssets() const;
    bool SaveTransparencySetupAssets() const;
    void SetEditorMode(EWCAEditorMode NewMode);

private:
    TSharedRef<SWidget> EnsureModeWidget(EWCAEditorMode Mode);
    EActiveTimerReturnType HandleDeferredRefresh(double CurrentTime, float DeltaTime);
    EActiveTimerReturnType HandleStatusRefreshTimer(double CurrentTime, float DeltaTime);
    EActiveTimerReturnType HandleTextureUploadTimer(double CurrentTime, float DeltaTime);
    void UpdateCachedStatus(bool bRefreshAssetState = true);
    void HandleAuthoringDocumentChanged(const FDWCEditorAuthoringChange& Change);
    void SuspendPreviewMode(EWCAEditorMode Mode, EDWCEditorPreviewSuspendReason Reason);
    void ResumePreviewModeIfNeeded(EWCAEditorMode Mode);
    void SuspendAllPreviewModes(EDWCEditorPreviewSuspendReason Reason);
    void HandlePreBeginPIE(bool bIsSimulating);
    void HandleEndPIE(bool bIsSimulating);

private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<FDWCEditorAuthoringDocument> AuthoringDocument;
    TSharedPtr<FDWCEditorCacheStore> CacheStore;
    TSharedPtr<FDWCEditorSpatialQueryService> SpatialQueryService;
    TSharedPtr<FDWCEditorRenderUploadQueue> RenderUploadQueue;
    TSharedPtr<FDWCEditorTextureWorkspace> TextureWorkspace;
    TSharedPtr<FDWCEditorPreviewCommitCoordinator> PreviewCommitCoordinator;
    TSharedPtr<FDWCEditorSessionStore> SessionStore;
    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
    TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> WorkerJobScheduler;
    TSharedPtr<FDWCEditorBakeCoordinator> BakeCoordinator;
    TSharedPtr<IDetailsView> DetailsView;
    FSimpleDelegate OnStatusChanged;
    TSharedPtr<SBox> ModeContentBox;
    TSharedPtr<SWetClothingPartEditorPanel> PartEditorPanel;
    TSharedPtr<SWetWrinkleEditorPanel> WrinkleEditorPanel;
    TSharedPtr<SWetClothingTransparencyBakePanel> TransparencyBakePanel;
    bool bRefreshPending = false;
    bool bPendingFullModeRefresh = false;
    bool bSuppressStatusChangedNotification = false;
    bool bHasActiveEditorMode = false;
    EWCAEditorMode ActiveEditorMode = EWCAEditorMode::PartEdit;
    int32 CachedIssueCount = 0;
    EWCAEditorStatusSeverity CachedStatusSeverity = EWCAEditorStatusSeverity::Info;
    FDelegateHandle PreBeginPIEHandle;
    FDelegateHandle EndPIEHandle;
};
