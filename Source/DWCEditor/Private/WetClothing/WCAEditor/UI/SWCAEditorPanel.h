#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "WetClothing/WCAEditor/WCAEditorMode.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class SBox;
class SWetClothingPartEditorPanel;
class SWetClothingTransparencyBakePanel;
class SWetWrinkleEditorPanel;
class UWetClothingAsset;

enum class EWCAEditorStatusSeverity : uint8
{
    Info,
    Warning,
    Error
};

struct FWCAEditorIssueStatus
{
    bool bGeneratedDataUVIssue = false;
    bool bRuntimeIssue = false;
    bool bMapIssue = false;
    bool bMaterialIssue = false;
    bool bFailure = false;
    EWCAEditorStatusSeverity Severity = EWCAEditorStatusSeverity::Info;
    TArray<FString> GeneratedDataUVMessages;
    TArray<FString> RuntimeMessages;
    TArray<FString> MapMessages;
    TArray<FString> MaterialMessages;
    TArray<FString> FailureMessages;

    bool HasIssues() const
    {
        return bGeneratedDataUVIssue || bRuntimeIssue || bMapIssue || bMaterialIssue || bFailure;
    }

    FString BuildSummary() const;
};

/** Main editor panel. Heavy mode panels are created lazily and only the active mode is refreshed. */
class SWCAEditorPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWCAEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    virtual ~SWCAEditorPanel() override;
    void Construct(const FArguments& InArgs);

    void RefreshFromAsset();
    void RequestRefreshFromAsset();
    FWCAEditorIssueStatus CollectIssueStatus(bool bRefreshAssetState = true, bool bRunDeepValidation = false) const;
    bool HasPendingVisualBakeTasks(FString* OutSummary = nullptr) const;
    bool BakeWetVisualAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool BakePendingVisualAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool BakeTransparencyRevealAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    FReply BakeSelectedWrinkleNormalMap();
    FReply BakeSelectedWrinkleMask();
    bool BakeAllWrinkleMaps(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool SaveBakedVisualAssets() const;
    bool SaveTransparencySetupAssets() const;
    void SetEditorMode(EWCAEditorMode NewMode);

private:
    TSharedRef<SWidget> EnsureModeWidget(EWCAEditorMode Mode);
    EActiveTimerReturnType HandleDeferredRefresh(double CurrentTime, float DeltaTime);
    void UpdateCachedStatus();
    EVisibility GetRuntimeReadyWarningVisibility() const;
    FText GetRuntimeReadyWarningText() const;
    FSlateColor GetRuntimeReadyStatusTextColor() const;
    FSlateColor GetRuntimeReadyStatusBackgroundColor() const;

private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SBox> ModeContentBox;
    TSharedPtr<SWetClothingPartEditorPanel> PartEditorPanel;
    TSharedPtr<SWetWrinkleEditorPanel> WrinkleEditorPanel;
    TSharedPtr<SWetClothingTransparencyBakePanel> TransparencyBakePanel;
    EWCAEditorMode ActiveMode = EWCAEditorMode::PartEdit;
    bool bRefreshPending = false;
    bool bStatusWarningVisible = false;
    FText CachedStatusText;
    EWCAEditorStatusSeverity CachedStatusSeverity = EWCAEditorStatusSeverity::Info;
};
