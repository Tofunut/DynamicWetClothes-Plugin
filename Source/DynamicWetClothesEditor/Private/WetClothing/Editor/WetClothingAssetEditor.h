#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

struct FPropertyChangedEvent;
class IDetailsView;
class SDockTab;
class SWetClothingAssetEditorPanel;
class UWetClothingAsset;

class FWetClothingAssetEditor : public FAssetEditorToolkit
{
  public:
    virtual ~FWetClothingAssetEditor() override;

    void Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetClothingAsset* InWetClothingAsset);

    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

    virtual FName        GetToolkitFName() const override;
    virtual FText        GetBaseToolkitName() const override;
    virtual FString      GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;

  private:
    void                 HandleFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);
    void                 HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);
    TSharedRef<SDockTab> SpawnMainTab(const FSpawnTabArgs& Args);

  private:
    static const FName EditorAppName;
    static const FName MainTabId;

    TWeakObjectPtr<UWetClothingAsset>        WetClothingAsset;
    TSharedPtr<IDetailsView>                   DetailsView;
    TSharedPtr<SWetClothingAssetEditorPanel> EditorPanel;
    TSharedPtr<FWorkspaceItem>                 WorkspaceMenuCategory;
    FDelegateHandle                            ObjectPropertyChangedHandle;
};
