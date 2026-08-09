//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"

class UMaterialInterface;
class UWetClothingAsset;
struct FWetClothingTransparencyLayerData;

struct FDWCTransparencyProjectionSource
{
    FDWCBakeResolvedLayer Layer;
    TObjectPtr<UMaterialInterface> EffectiveMaterial = nullptr;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 PriorityIndex = INDEX_NONE;
    FName MaterialSlotName;
};

/** Game-thread provider output shared by every ray-projected transparency source type. */
struct FDWCTransparencyProjectionSourceSet
{
    FTransform OuterBakeTransform = FTransform::Identity;
    TArray<FDWCTransparencyProjectionSource> Sources;
    TArray<FString> Warnings;
    FString ProviderSignature;
};

class FDWCTransparencyProjectionSourceProvider
{
  public:
    static bool BuildBlueprintSources(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyProjectionSourceSet& OutSources,
        FString& OutError);

    static bool BuildExternalMeshSources(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyProjectionSourceSet& OutSources,
        FString& OutError);
};
