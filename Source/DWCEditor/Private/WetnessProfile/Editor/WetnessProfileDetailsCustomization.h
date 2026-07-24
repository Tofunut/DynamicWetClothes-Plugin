#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class IDetailGroup;
class IPropertyHandle;
class IPropertyUtilities;
class UWetnessProfile;

class FWetnessProfileDetailsCustomization : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
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

    void AddDefaultProperty(
        IDetailGroup& Group,
        const TSharedPtr<IPropertyHandle>& Handle,
        const FText& DisplayName,
        const FText& Tooltip,
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

    TOptional<float> GetDisplayedFloatValue(
        TWeakPtr<IPropertyHandle> WeakHandle,
        float DisplayScale) const;
    void SetDisplayedFloatValue(
        float DisplayValue,
        TWeakPtr<IPropertyHandle> WeakHandle,
        float HardMin,
        float HardMax,
        float DisplayScale);

    FText GetValidationText() const;
    FReply HandleClampValuesClicked();
    void RefreshValidationIssues();

private:
    TWeakObjectPtr<UWetnessProfile> Profile;
    TWeakPtr<IPropertyUtilities> PropertyUtilities;
    TArray<FCollectedProperty> CollectedProperties;
    TArray<FString> ValidationIssues;
};
