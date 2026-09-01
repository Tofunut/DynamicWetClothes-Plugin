// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "DWCPersistentWetnessProvider.generated.h"

class UDynamicWetClothesComponent;

/**
 * A persistent wetness source sampled by DWC at the receiver's simulation cadence.
 * Providers own only their current spatial/source state; DWC owns when input is
 * evaluated and when the resulting surface water is absorbed.
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDWCPersistentWetnessProvider : public UInterface
{
    GENERATED_BODY()
};

class DWC_API IDWCPersistentWetnessProvider
{
    GENERATED_BODY()

  public:
    virtual void ApplyPersistentWetness(
        UDynamicWetClothesComponent& Receiver,
        float                        DeltaSeconds) = 0;
};
