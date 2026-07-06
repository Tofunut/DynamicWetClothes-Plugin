#include "WetWrinkleAssetFactory.h"

#include "DataAssets/WetWrinkleAsset.h"

UWetWrinkleAssetFactory::UWetWrinkleAssetFactory()
{
    SupportedClass = UWetWrinkleAsset::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}

UObject* UWetWrinkleAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context,
                                                   FFeedbackContext* Warn)
{
    return NewObject<UWetWrinkleAsset>(InParent, Class, Name, Flags | RF_Transactional);
}

bool UWetWrinkleAssetFactory::ShouldShowInNewMenu() const
{
    return true;
}
