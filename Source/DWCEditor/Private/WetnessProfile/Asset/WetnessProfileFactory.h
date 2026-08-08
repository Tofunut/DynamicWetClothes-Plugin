// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "WetnessProfileFactory.generated.h"

UCLASS()
class DWCEDITOR_API UWetnessProfileFactory : public UFactory
{
    GENERATED_BODY()

  public:
    UWetnessProfileFactory();

    virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
    virtual bool     ShouldShowInNewMenu() const override;
};
