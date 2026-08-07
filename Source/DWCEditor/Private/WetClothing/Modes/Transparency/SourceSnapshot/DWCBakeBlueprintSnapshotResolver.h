//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"
#include "Templates/SubclassOf.h"

class AActor;
class UDWCBakeComponent;

class FDWCBakeBlueprintSnapshotResolver
{
  public:
    static bool BuildSnapshot(TSubclassOf<AActor> BlueprintClass, FDWCBakeSnapshot& OutSnapshot, FString& OutErrorMessage);

  private:
    static UDWCBakeComponent* FindBakeComponent(AActor& Actor, FString& OutErrorMessage);
};
