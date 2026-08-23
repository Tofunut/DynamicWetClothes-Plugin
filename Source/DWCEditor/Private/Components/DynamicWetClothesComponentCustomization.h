// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Layout/Visibility.h"

class IPropertyUtilities;
struct FPropertyChangedEvent;
class UDynamicWetClothesComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UWetClothingAsset;

class FDynamicWetClothesComponentCustomization : public IDetailCustomization
{
  public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual ~FDynamicWetClothesComponentCustomization() override;
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

  private:
    enum class EBindingState : uint8
    {
        Ready,
        SourceMeshInUse,
        UnsupportedAssetVersion,
        NoMatchingComponent,
        MissingSourceMesh,
        MissingDWCMesh
    };

    struct FBindingStatus
    {
        TWeakObjectPtr<UWetClothingAsset>      Asset;
        TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
        TWeakObjectPtr<USkeletalMesh>          CurrentMesh;
        TWeakObjectPtr<USkeletalMesh>          SourceMesh;
        TWeakObjectPtr<USkeletalMesh>          RequiredMesh;
        bool                                   bCPURuntimeDataValid = false;
        bool                                   bGPURuntimeDataValid = false;
        bool                                   bGPUMapDataValid = false;
        EBindingState                          State = EBindingState::NoMatchingComponent;
    };

    void        RebuildBindingStatus();
    void        RequestRefresh();
    void        HandleObjectPropertyChanged(UObject* ChangedObject, FPropertyChangedEvent& PropertyChangedEvent);
    FText       GetBindingWarningText() const;
    EVisibility GetBindingWarningVisibility() const;
    FText       GetRuntimeDataWarningText() const;
    EVisibility GetRuntimeDataWarningVisibility() const;

  private:
    TWeakObjectPtr<UDynamicWetClothesComponent> Component;
    TWeakPtr<IPropertyUtilities>                PropertyUtilities;
    FBindingStatus                              CachedBindingStatus;
    bool                                        bHasBindingStatus = false;
    FDelegateHandle                             ObjectPropertyChangedHandle;
};
