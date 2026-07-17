#pragma once

#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"

class UDynamicWetClothesComponent;

class FDynamicWetClothesComponentCustomization : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
    FText GetStatusText() const;
    FText GetRuntimeMeshWarningText() const;
    EVisibility GetRuntimeMeshWarningVisibility() const;
    FText GetApplyButtonText() const;
    bool HasRuntimeMeshWarning() const;
    bool HasRuntimeMeshMismatch() const;
    bool CanApplyRuntimeMesh() const;
    FReply HandleApplyRuntimeMesh();

private:
    TWeakObjectPtr<UDynamicWetClothesComponent> Component;
};
