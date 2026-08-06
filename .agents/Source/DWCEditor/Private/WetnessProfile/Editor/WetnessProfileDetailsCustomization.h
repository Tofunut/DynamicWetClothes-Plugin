#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "Templates/Function.h"

class IDetailCategoryBuilder;
class IDetailGroup;
class IPropertyHandle;
class IPropertyUtilities;
class FProperty;
class SWidget;
class UWetnessProfile;

enum class EWetnessProfileDetailsMode : uint8
{
    Combined,
    AbsorbedWater,
    SurfaceWater
};

class FWetnessProfileDetailsCustomization : public IDetailCustomization
{
public:
    explicit FWetnessProfileDetailsCustomization(
        EWetnessProfileDetailsMode InMode = EWetnessProfileDetailsMode::Combined);

    static TSharedRef<IDetailCustomization> MakeInstance();
    static TSharedRef<IDetailCustomization> MakeInstance(EWetnessProfileDetailsMode InMode);
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
    struct FCollectedProperty
    {
        TSharedPtr<IPropertyHandle> Handle;
        FString Path;
    };

    void CollectPropertiesRecursive(
        const TSharedPtr<IPropertyHandle>& Parent,
        const FString& ParentPath);

    TSharedPtr<IPropertyHandle> FindPropertyByPath(const TCHAR* PropertyPath) const;
    bool ResolveSavedPropertyValue(
        TWeakPtr<IPropertyHandle> WeakHandle,
        FProperty*& OutProperty,
        void*& OutCurrentValue,
        const void*& OutSavedValue) const;
    bool IsPropertyDifferentFromSaved(TWeakPtr<IPropertyHandle> WeakHandle) const;
    bool AreStampSizePropertiesDifferentFromSaved(
        TWeakPtr<IPropertyHandle> WeakWidth,
        TWeakPtr<IPropertyHandle> WeakHeight) const;
    FReply RevertPropertyToSaved(TWeakPtr<IPropertyHandle> WeakHandle);
    FReply RevertStampSizeToSaved(
        TWeakPtr<IPropertyHandle> WeakWidth,
        TWeakPtr<IPropertyHandle> WeakHeight);
    TSharedRef<SWidget> BuildRevertButton(const TSharedPtr<IPropertyHandle>& Handle);
    TSharedRef<SWidget> BuildStampSizeRevertButton(
        const TSharedPtr<IPropertyHandle>& WidthHandle,
        const TSharedPtr<IPropertyHandle>& HeightHandle);

    void AddDefaultProperty(
        IDetailCategoryBuilder& Category,
        const TSharedPtr<IPropertyHandle>& Handle,
        const FText& DisplayName,
        const FText& Tooltip,
        TAttribute<bool> IsEnabled = TAttribute<bool>(true),
        float NameIndent = 0.0f);

    void AddDefaultProperty(
        IDetailGroup& Group,
        const TSharedPtr<IPropertyHandle>& Handle,
        const FText& DisplayName,
        const FText& Tooltip,
        TAttribute<bool> IsEnabled = TAttribute<bool>(true));

    void AddFloatProperty(
        IDetailCategoryBuilder& Category,
        const TSharedPtr<IPropertyHandle>& Handle,
        const FText& DisplayName,
        const FText& Tooltip,
        float HardMin,
        float HardMax,
        float SliderMin,
        float SliderMax,
        float Delta,
        float DisplayScale,
        int32 MaxFractionalDigits,
        const FText& Suffix,
        TAttribute<bool> IsEnabled = TAttribute<bool>(true));

    void AddFloatProperty(
        IDetailGroup& Group,
        const TSharedPtr<IPropertyHandle>& Handle,
        const FText& DisplayName,
        const FText& Tooltip,
        float HardMin,
        float HardMax,
        float SliderMin,
        float SliderMax,
        float Delta,
        float DisplayScale,
        int32 MaxFractionalDigits,
        const FText& Suffix,
        TAttribute<bool> IsEnabled = TAttribute<bool>(true));

    void AddMappedFloatProperty(
        IDetailCategoryBuilder& Category,
        const TSharedPtr<IPropertyHandle>& Handle,
        const FText& DisplayName,
        const FText& Tooltip,
        float HardDisplayMin,
        float HardDisplayMax,
        float SliderDisplayMin,
        float SliderDisplayMax,
        float DisplayDelta,
        int32 MaxFractionalDigits,
        const FText& Suffix,
        TFunction<float(float)> RawToDisplay,
        TFunction<float(float)> DisplayToRaw,
        float RawHardMin,
        float RawHardMax,
        TAttribute<bool> IsEnabled = TAttribute<bool>(true));

    void AddMappedFloatProperty(
        IDetailGroup& Group,
        const TSharedPtr<IPropertyHandle>& Handle,
        const FText& DisplayName,
        const FText& Tooltip,
        float HardDisplayMin,
        float HardDisplayMax,
        float SliderDisplayMin,
        float SliderDisplayMax,
        float DisplayDelta,
        int32 MaxFractionalDigits,
        const FText& Suffix,
        TFunction<float(float)> RawToDisplay,
        TFunction<float(float)> DisplayToRaw,
        float RawHardMin,
        float RawHardMax,
        TAttribute<bool> IsEnabled = TAttribute<bool>(true));

    void AddStampSizeProperty(
        IDetailCategoryBuilder& Category,
        const TSharedPtr<IPropertyHandle>& WidthHandle,
        const TSharedPtr<IPropertyHandle>& HeightHandle,
        const FText& Tooltip,
        TAttribute<bool> IsEnabled = TAttribute<bool>(true));

    TOptional<float> GetDisplayedFloatValue(
        TWeakPtr<IPropertyHandle> WeakHandle,
        float DisplayScale) const;
    void SetDisplayedFloatValue(
        float DisplayValue,
        TWeakPtr<IPropertyHandle> WeakHandle,
        float HardMin,
        float HardMax,
        float DisplayScale);
    TOptional<float> GetMappedDisplayedFloatValue(
        TWeakPtr<IPropertyHandle> WeakHandle,
        const TFunction<float(float)>& RawToDisplay) const;
    void SetMappedDisplayedFloatValue(
        float DisplayValue,
        TWeakPtr<IPropertyHandle> WeakHandle,
        const TFunction<float(float)>& DisplayToRaw,
        float RawHardMin,
        float RawHardMax);

    FText GetValidationText() const;
    FReply HandleClampValuesClicked();
    void RefreshValidationIssues();

private:
    EWetnessProfileDetailsMode Mode = EWetnessProfileDetailsMode::Combined;
    TWeakObjectPtr<UWetnessProfile> Profile;
    TWeakPtr<IPropertyUtilities> PropertyUtilities;
    TArray<FCollectedProperty> CollectedProperties;
    TArray<FString> ValidationIssues;
    bool bLockStampAspectRatio = true;
};
