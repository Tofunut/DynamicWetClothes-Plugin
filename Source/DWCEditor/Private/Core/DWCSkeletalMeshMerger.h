//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct FDWCSkeletalMeshMergeResult
{
    bool bSucceeded = false;
    FString Message;
    FString OutputPackageName;
    USkeletalMesh* OutputMesh = nullptr;
};

class FDWCSkeletalMeshMerger
{
  public:
    static FDWCSkeletalMeshMergeResult MergeMeshes(
        const TArray<USkeletalMesh*>& SourceMeshes,
        const FString& OptionalOutputPackageName = FString());

    static void RegisterContentBrowserMenu(void* Owner);
    static void UnregisterContentBrowserMenu(void* Owner);
};
