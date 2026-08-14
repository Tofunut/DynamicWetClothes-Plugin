//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Assets/DWCEditorAssetResidency.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorHostLifecycle.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorPreviewModeLifetime.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"
#include "WetClothing/WCAEditor/WCAEditorMode.h"
#include "WetClothing/WCAEditor/UI/WCAEditorRefreshState.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class FDWCEditorAuthoringDocument;
class FDWCEditorBakeCoordinator;
class FDWCEditorBuildOperationManager;
class FDWCEditorExclusiveBuildCoordinator;
class FDWCEditorCacheStore;
class FDWCWrinkleSuppressionCoverageService;
class FDWCEditorRenderUploadQueue;
class FDWCEditorPreviewCommitCoordinator;
class FDWCEditorPreviewResourceContext;
class FDWCEditorPreviewGPUResidencyManager;
class FDWCEditorResourceGovernor;
class FDWCEditorSessionStore;
class FDWCEditorSpatialQueryService;
class FDWCEditorSurfacePatchProjectionCacheService;
class FDWCEditorTextureWorkspace;
class FDWCEditorWorkerJobScheduler;
class SBox;
class SWetClothingPartEditorPanel;
class SWetClothingTransparencyBakePanel;
class SWetWrinkleEditorPanel;
class UWetClothingAsset;
class FActiveTimerHandle;
struct FDWCEditorAuthoringChange;
struct FWCAGeneratedDataInvalidation;
struct FDWCEditorBakeBatchResult;
struct FDWCEditorWorkerJobDescriptor;
enum class EDWCEditorPreviewSuspendReason : uint8;
enum class EDWCEditorPreviewResourceReleasePolicy : uint8;
enum class EDWCEditorTransparencyBakeKind : uint8;

enum class EWCAEditorStatusSeverity : uint8
{
    Info,
    Warning,
    Error
};

enum class EWCAEditorPanelShutdownState : uint8
{
    Running,
    Quiescing,
    Closed
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
    void Shutdown();
    bool IsShuttingDown() const;
    bool IsShutdownComplete() const;

    void RefreshFromAsset(bool bRebuildActiveModePreview = true);
    void RefreshStatusFromAsset();
    void RequestRefreshFromAsset(bool bRebuildActiveModePreview = true);
    FWCAEditorIssueStatus CollectIssueStatus(bool bRunDeepValidation = false) const;
    bool HasPendingVisualBakeTasks(FString* OutSummary = nullptr) const;
    bool BakeWetVisualAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool BakePendingVisualAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool BakeAllWrinkleMaps(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool RequestBakeAllWrinkleMaps(
        TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
        FString* OutError = nullptr);
    bool RequestBakeAllTransparencyMaps(
        TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
        FString* OutError = nullptr);
    bool RequestRebakeAffectedTransparencyMaps(
        TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
        FString* OutError = nullptr);
    bool IsWrinkleBakeActive() const;
    EDWCEditorTransparencyBakeKind GetActiveTransparencyBakeKind() const;
    TSet<EDWCEditorBuildAction> GetRunningBuildActions() const;
    bool RequestExclusiveBuild(
        const FString& DebugName,
        TFunction<void()> Work,
        FString* OutError = nullptr);
    bool CanStartBuildAction(FString* OutReason = nullptr) const;
    bool IsExclusiveBuildActive() const;
    void SetEditorMode(EWCAEditorMode NewMode);
    void SetHostVisibilitySnapshot(const FDWCEditorHostVisibilitySnapshot& Visibility);

private:
    TSharedRef<SWidget> EnsureModeWidget(EWCAEditorMode Mode);
    EActiveTimerReturnType HandleDeferredRefresh(double CurrentTime, float DeltaTime);
    EActiveTimerReturnType HandleDeferredStatusRefresh(double CurrentTime, float DeltaTime);
    EActiveTimerReturnType HandleTextureUploadTimer(double CurrentTime, float DeltaTime);
    void EnsureTextureUploadTimer();
    void RequestStatusRefresh();
    void UpdateCachedStatus();
    void HandleAuthoringDocumentChanged(const FDWCEditorAuthoringChange& Change);
    void HandleGeneratedDataInvalidated(const FWCAGeneratedDataInvalidation& Invalidation);
    void SetHostLifecycleBlocker(EDWCEditorHostLifecycleBlocker Blocker, bool bEnabled);
    void ApplyHostLifecycleTransition(const FDWCEditorHostLifecycleTransition& Transition);
    bool CanRunInteractivePreview() const;
    EDWCEditorPreviewSuspendReason ResolveHostSuspendReason() const;
    EDWCEditorPreviewResourceReleasePolicy ResolveResourceReleasePolicy(
        EDWCEditorPreviewSuspendReason Reason) const;
    TSharedPtr<FDWCEditorPreviewModeLifetime> FindPreviewModeLifetime(EWCAEditorMode Mode) const;
    FDWCEditorPreviewRunToken CapturePreviewRunToken(
        const FDWCEditorWorkerJobDescriptor& Descriptor) const;
    void SuspendPreviewMode(
        EWCAEditorMode Mode,
        EDWCEditorPreviewSuspendReason Reason,
        bool bManageResidency = true);
    void ResumePreviewModeIfNeeded(EWCAEditorMode Mode);
    void SuspendAllPreviewModes(EDWCEditorPreviewSuspendReason Reason);
    void HandlePreBeginPIE(bool bIsSimulating);
    void HandleEndPIE(bool bIsSimulating);
    void RegisterResourceParticipants();
    void UnregisterResourceParticipants();
    void UnregisterActiveTimers();
    void ShutdownInternal(bool bNotifyPreviewModes);
    void BeginPreviewResourceShutdown();
    void CompletePreviewResourceShutdown();
    void HandleExclusiveBuildBarrierChanged(bool bActive);

private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<FDWCEditorAuthoringDocument> AuthoringDocument;
    TSharedPtr<FDWCEditorCacheStore> CacheStore;
    FDelegateHandle GeneratedDataInvalidationHandle;
    TSharedPtr<FDWCWrinkleSuppressionCoverageService> WrinkleSuppressionCoverageService;
    TSharedPtr<FDWCEditorSpatialQueryService> SpatialQueryService;
    TSharedPtr<FDWCEditorSurfacePatchProjectionCacheService> SurfacePatchProjectionCache;
    TSharedPtr<FDWCEditorRenderUploadQueue> RenderUploadQueue;
    TSharedPtr<FDWCEditorTextureWorkspace> TextureWorkspace;
    TSharedPtr<FDWCEditorPreviewGPUResidencyManager> PreviewGPUResidencyManager;
    TSharedPtr<FDWCEditorPreviewCommitCoordinator> PreviewCommitCoordinator;
    TSharedPtr<FDWCEditorPreviewResourceContext> PreviewResources;
    TSharedPtr<FDWCEditorAssetResidencyRegistry> AssetResidency;
    FDWCEditorAssetResidencyLease EditorAssetResidencyLease;
    TSharedPtr<FDWCEditorSessionStore> SessionStore;
    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
    TSharedPtr<FDWCEditorResourceBroker> ResourceBroker;
    FGuid ResourceBrokerSessionId;
    TArray<uint64> ResourceParticipantIds;
    TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> WorkerJobScheduler;
    TSharedPtr<FDWCEditorBuildOperationManager> BuildOperationManager;
    TSharedPtr<FDWCEditorBakeCoordinator> BakeCoordinator;
    TSharedPtr<FDWCEditorExclusiveBuildCoordinator> ExclusiveBuildCoordinator;
    FDWCEditorHostLifecycleReducer HostLifecycle { EDWCEditorHostLifecycleBlocker::HostUnavailable };
    TSharedPtr<FDWCEditorPreviewModeLifetime> PartPreviewLifetime;
    TSharedPtr<FDWCEditorPreviewModeLifetime> WrinklePreviewLifetime;
    TSharedPtr<FDWCEditorPreviewModeLifetime> TransparencyPreviewLifetime;
    TSharedPtr<IDetailsView> DetailsView;
    FSimpleDelegate OnStatusChanged;
    TSharedPtr<SBox> ModeContentBox;
    TSharedPtr<SWetClothingPartEditorPanel> PartEditorPanel;
    TSharedPtr<SWetWrinkleEditorPanel> WrinkleEditorPanel;
    TSharedPtr<SWetClothingTransparencyBakePanel> TransparencyBakePanel;
    FWCAEditorRefreshState RefreshState;
    bool bSuppressStatusChangedNotification = false;
    bool bHasActiveEditorMode = false;
    EWCAEditorMode ActiveEditorMode = EWCAEditorMode::PartEdit;
    int32 CachedIssueCount = 0;
    EWCAEditorStatusSeverity CachedStatusSeverity = EWCAEditorStatusSeverity::Info;
    FDelegateHandle PreBeginPIEHandle;
    FDelegateHandle EndPIEHandle;
    TWeakPtr<FActiveTimerHandle> DeferredRefreshTimerHandle;
    TWeakPtr<FActiveTimerHandle> DeferredStatusRefreshTimerHandle;
    TWeakPtr<FActiveTimerHandle> TextureUploadTimerHandle;
    EWCAEditorPanelShutdownState ShutdownState = EWCAEditorPanelShutdownState::Running;
};
