#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class IDetailsView;
class SDockTab;
class SWetnessProfileEditorPanel;
class UWetnessProfile;
struct FPropertyChangedEvent;

class FWetnessProfileEditor : public FAssetEditorToolkit
{
  public:
    virtual ~FWetnessProfileEditor() override;

    void Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetnessProfile* InProfile);

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

    TWeakObjectPtr<UWetnessProfile>        WetnessProfile;
    TSharedPtr<IDetailsView>               DetailsView;
    TSharedPtr<SWetnessProfileEditorPanel> EditorPanel;
    TSharedPtr<FWorkspaceItem>             WorkspaceMenuCategory;
    FDelegateHandle                        ObjectPropertyChangedHandle;
};
