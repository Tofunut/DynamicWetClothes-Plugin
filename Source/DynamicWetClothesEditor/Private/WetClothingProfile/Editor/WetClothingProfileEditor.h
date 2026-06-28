#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

struct FPropertyChangedEvent;
class IDetailsView;
class SDockTab;
class SWetClothingProfileEditorPanel;
class UWetClothingProfile;

class FWetClothingProfileEditor : public FAssetEditorToolkit
{
  public:
    virtual ~FWetClothingProfileEditor() override;

    void Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetClothingProfile* InWetClothingProfile);

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

    TWeakObjectPtr<UWetClothingProfile>        WetClothingProfile;
    TSharedPtr<IDetailsView>                   DetailsView;
    TSharedPtr<SWetClothingProfileEditorPanel> EditorPanel;
    TSharedPtr<FWorkspaceItem>                 WorkspaceMenuCategory;
    FDelegateHandle                            ObjectPropertyChangedHandle;
};
