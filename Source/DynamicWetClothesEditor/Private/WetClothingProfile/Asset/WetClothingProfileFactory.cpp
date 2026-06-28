#include "WetClothingProfileFactory.h"

#include "WetClothingProfile.h"

UWetClothingProfileFactory::UWetClothingProfileFactory()
{
	SupportedClass = UWetClothingProfile::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UWetClothingProfileFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UWetClothingProfile>(InParent, Class, Name, Flags | RF_Transactional);
}

bool UWetClothingProfileFactory::ShouldShowInNewMenu() const
{
	return true;
}
