#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct FDWCSkeletalMeshMaterialSlotExtractionResult
{
    bool bSucceeded = false;
    int32 RemovedTriangleCount = 0;
    FString OutputPackageName;
    FString Message;
};

class FDWCSkeletalMeshMaterialSlotExtractor
{
  public:
    static FDWCSkeletalMeshMaterialSlotExtractionResult ExtractMaterialSlot(
        USkeletalMesh* SourceMesh,
        int32 MaterialSlotIndex,
        const FString& OptionalOutputPackageName = FString());

    static void RegisterContentBrowserMenu(void* Owner);
    static void UnregisterContentBrowserMenu(void* Owner);
};
