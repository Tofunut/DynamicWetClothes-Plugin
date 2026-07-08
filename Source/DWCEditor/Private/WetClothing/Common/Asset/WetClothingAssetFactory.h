#pragma once

#include "Factories/Factory.h"
#include "WetClothingAssetFactory.generated.h"

UCLASS()
class DWCEDITOR_API UWetClothingAssetFactory : public UFactory
{
    GENERATED_BODY()

  public:
    UWetClothingAssetFactory();

    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    virtual bool     ShouldShowInNewMenu() const override;
};
