//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;

class DWC_API FWetVertexColorBuffer
{
  public:
    static void ApplyVertexColorOverride(
        USkeletalMeshComponent& TargetSkeletalMesh,
        int32                   LODIndex,
        const TArray<FColor>&   VertexColors);

  private:
    static bool ApplyVertexColorOverrideByDirectBufferSwap(
        USkeletalMeshComponent& TargetSkeletalMesh,
        int32                   LODIndex,
        const TArray<FColor>&   VertexColors);
};
