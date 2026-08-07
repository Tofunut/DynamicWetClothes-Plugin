//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "IDetailCustomization.h"
#include "Input/Reply.h"
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
        NeedsApply,
        MissingAsset,
        UnsupportedAssetVersion,
        NoMatchingComponent,
        MissingSourceMesh,
        MissingDWCMesh,
        DuplicateAsset,
        ConflictingAsset
    };

    struct FBindingStatus
    {
        TWeakObjectPtr<UWetClothingAsset> Asset;
        TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
        TWeakObjectPtr<USkeletalMesh> CurrentMesh;
        TWeakObjectPtr<USkeletalMesh> SourceMesh;
        TWeakObjectPtr<USkeletalMesh> RequiredMesh;
        EBindingState State = EBindingState::NoMatchingComponent;
    };

    void RebuildBindingStatuses();
    void RequestRefresh();
    void HandleObjectPropertyChanged(UObject* ChangedObject, FPropertyChangedEvent& PropertyChangedEvent);
    FText GetBindingText(int32 BindingIndex) const;
    FText GetBindingStateText(int32 BindingIndex) const;
    EVisibility GetBindingWarningVisibility(int32 BindingIndex) const;
    EVisibility GetBindingApplyVisibility(int32 BindingIndex) const;
    bool CanApplyBinding(int32 BindingIndex) const;
    bool CanApplyAll() const;
    FReply HandleApplyBinding(int32 BindingIndex);
    FReply HandleApplyAll();
    bool ApplyBinding(int32 BindingIndex, bool bUseTransaction);

private:
    TWeakObjectPtr<UDynamicWetClothesComponent> Component;
    TWeakPtr<IPropertyUtilities> PropertyUtilities;
    TArray<FBindingStatus> CachedBindingStatuses;
    FDelegateHandle ObjectPropertyChangedHandle;
    bool bApplyingBinding = false;
};
