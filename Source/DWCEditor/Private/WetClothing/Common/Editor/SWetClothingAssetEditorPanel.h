#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "WetClothing/Common/Editor/WetClothingEditorMode.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class SBox;
class SWetClothingPartEditorPanel;
class SWetClothingTransparencyBakePanel;
class SWetWrinkleEditorPanel;
class UWetClothingAsset;

enum class EDWCEditorStatusSeverity : uint8
{
    Info,
    Warning,
    Error
};

struct FDWCEditorIssueStatus
{
    bool bGeneratedDataUVIssue = false;
    bool bRuntimeIssue = false;
    bool bMapIssue = false;
    bool bMaterialIssue = false;
    bool bFailure = false;
    EDWCEditorStatusSeverity Severity = EDWCEditorStatusSeverity::Info;
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
class SWetClothingAssetEditorPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWetClothingAssetEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    virtual ~SWetClothingAssetEditorPanel() override;
    void Construct(const FArguments& InArgs);

    void RefreshFromAsset();
    void RequestRefreshFromAsset();
    FDWCEditorIssueStatus CollectIssueStatus(bool bRefreshAssetState = true, bool bIncludeMapValidation = true) const;
    bool HasPendingVisualBakeTasks(FString* OutSummary = nullptr) const;
    bool BakeWetVisualAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool BakePendingVisualAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool BakeTransparencyRevealAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    FReply BakeSelectedWrinkleNormalMap();
    FReply BakeSelectedWrinkleMask();
    bool BakeAllWrinkleMaps(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool SaveBakedVisualAssets() const;
    bool SaveTransparencySetupAssets() const;
    void SetEditorMode(EWetClothingEditorMode NewMode);

private:
    TSharedRef<SWidget> EnsureModeWidget(EWetClothingEditorMode Mode);
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
    EWetClothingEditorMode ActiveMode = EWetClothingEditorMode::PartEdit;
    bool bRefreshPending = false;
    bool bStatusWarningVisible = false;
    FText CachedStatusText;
    EDWCEditorStatusSeverity CachedStatusSeverity = EDWCEditorStatusSeverity::Info;
};
