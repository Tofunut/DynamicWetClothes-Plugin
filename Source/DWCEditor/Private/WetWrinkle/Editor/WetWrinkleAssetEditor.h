#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class IDetailsView;
class SDockTab;
class SWetWrinkleAssetEditorPanel;
class UWetWrinkleAsset;
struct FPropertyChangedEvent;

class FWetWrinkleAssetEditor : public FAssetEditorToolkit
{
  public:
    virtual ~FWetWrinkleAssetEditor() override;

    void Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetWrinkleAsset* InWetWrinkleAsset);

    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

    virtual FName GetToolkitFName() const override;
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;

  private:
    void HandleFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);
    void HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);
    TSharedRef<SDockTab> SpawnMainTab(const FSpawnTabArgs& Args);

  private:
    static const FName EditorAppName;
    static const FName MainTabId;

    TWeakObjectPtr<UWetWrinkleAsset> WetWrinkleAsset;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SWetWrinkleAssetEditorPanel> EditorPanel;
    TSharedPtr<FWorkspaceItem> WorkspaceMenuCategory;
    FDelegateHandle ObjectPropertyChangedHandle;
};
