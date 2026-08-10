//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
struct FDWCTransparencyMaterialColorBakeResult;
struct FDWCRevealBakeSurface;
struct FWetClothingTransparencyInnerSlot;

/** Immutable material-surface dependencies prepared on the game thread for a Type 1 projection job. */
struct FDWCTransparencyType1SourceBindings
{
    TMap<FName, TSharedPtr<const FDWCTransparencyMaterialColorBakeResult>> SurfacesBySourceLayerId;
    TArray<FString> Warnings;

    uint64 GetAllocatedBytes() const;
};

/**
 * Same-mesh/material-slot Stage 2 provider. It resolves original source
 * materials and shares exact GPU material-surface bakes across target layers.
 */
class FDWCTransparencyType1SourceProvider
{
  public:
    static bool AddValidatedBinding(
        UWetClothingAsset& Asset,
        const FWetClothingTransparencyInnerSlot& InnerSlot,
        int32 PriorityIndex,
        const FDWCRevealBakeSurface& SourceSurface,
        FDWCTransparencyType1SourceBindings& OutBindings,
        FString& OutError);
};
