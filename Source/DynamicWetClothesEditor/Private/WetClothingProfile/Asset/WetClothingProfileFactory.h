#pragma once

#include "Factories/Factory.h"
#include "WetClothingProfileFactory.generated.h"

UCLASS()
class DYNAMICWETCLOTHESEDITOR_API UWetClothingProfileFactory : public UFactory
{
	GENERATED_BODY()

public:
	UWetClothingProfileFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
};
