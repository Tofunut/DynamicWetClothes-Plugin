#pragma once

#include "Factories/Factory.h"
#include "WetWrinkleAssetFactory.generated.h"

UCLASS()
class DWCEDITOR_API UWetWrinkleAssetFactory : public UFactory
{
    GENERATED_BODY()

  public:
    UWetWrinkleAssetFactory();

    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    virtual bool     ShouldShowInNewMenu() const override;
};
