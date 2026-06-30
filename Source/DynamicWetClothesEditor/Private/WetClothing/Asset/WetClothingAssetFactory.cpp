#include "WetClothingAssetFactory.h"

#include "WetClothingAsset.h"

UWetClothingAssetFactory::UWetClothingAssetFactory()
{
    SupportedClass = UWetClothingAsset::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}

UObject* UWetClothingAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    return NewObject<UWetClothingAsset>(InParent, Class, Name, Flags | RF_Transactional);
}

bool UWetClothingAssetFactory::ShouldShowInNewMenu() const
{
    return true;
}
