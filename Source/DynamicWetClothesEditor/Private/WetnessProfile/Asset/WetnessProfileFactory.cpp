#include "WetnessProfileFactory.h"

#include "WetnessProfile.h"

UWetnessProfileFactory::UWetnessProfileFactory()
{
    SupportedClass = UWetnessProfile::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}

UObject* UWetnessProfileFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context,
                                                  FFeedbackContext* Warn)
{
    return NewObject<UWetnessProfile>(InParent, Class, Name, Flags | RF_Transactional);
}

bool UWetnessProfileFactory::ShouldShowInNewMenu() const
{
    return true;
}
