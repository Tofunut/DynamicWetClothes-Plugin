// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UMaterialInterface;
class USkeletalMesh;

struct FDWCEditorPreviewMaterialBindingDecision
{
    bool bNeedsAssignment = false;
    bool bCacheMatched = false;
};

/**
 * Tracks the material interfaces last applied to a preview mesh. The live
 * component material is always supplied to Evaluate so an external override
 * cannot be hidden by the cache.
 */
class FDWCEditorPreviewMaterialBindingCache final
{
  public:
    FDWCEditorPreviewMaterialBindingDecision Evaluate(
        USkeletalMesh* CurrentMesh,
        int32 MaterialSlotCount,
        int32 MaterialSlotIndex,
        UMaterialInterface* DesiredMaterial,
        UMaterialInterface* ActualMaterial)
    {
        Synchronize(CurrentMesh, MaterialSlotCount);

        FDWCEditorPreviewMaterialBindingDecision Decision;
        if (!LastAppliedMaterials.IsValidIndex(MaterialSlotIndex))
        {
            return Decision;
        }

        Decision.bCacheMatched = LastAppliedMaterials[MaterialSlotIndex].Get() == DesiredMaterial;
        Decision.bNeedsAssignment = ActualMaterial != DesiredMaterial;
        if (!Decision.bNeedsAssignment)
        {
            LastAppliedMaterials[MaterialSlotIndex] = DesiredMaterial;
        }
        return Decision;
    }

    void RecordApplied(
        USkeletalMesh* CurrentMesh,
        int32 MaterialSlotCount,
        int32 MaterialSlotIndex,
        UMaterialInterface* Material)
    {
        Synchronize(CurrentMesh, MaterialSlotCount);
        if (LastAppliedMaterials.IsValidIndex(MaterialSlotIndex))
        {
            LastAppliedMaterials[MaterialSlotIndex] = Material;
        }
    }

    void Reset()
    {
        LastMesh.Reset();
        LastAppliedMaterials.Reset();
    }

  private:
    void Synchronize(USkeletalMesh* CurrentMesh, int32 MaterialSlotCount)
    {
        const int32 SanitizedSlotCount = FMath::Max(MaterialSlotCount, 0);
        if (LastMesh.Get() != CurrentMesh || LastAppliedMaterials.Num() != SanitizedSlotCount)
        {
            LastMesh = CurrentMesh;
            LastAppliedMaterials.Reset();
            LastAppliedMaterials.SetNum(SanitizedSlotCount);
        }
    }

    TWeakObjectPtr<USkeletalMesh> LastMesh;
    TArray<TWeakObjectPtr<UMaterialInterface>> LastAppliedMaterials;
};
