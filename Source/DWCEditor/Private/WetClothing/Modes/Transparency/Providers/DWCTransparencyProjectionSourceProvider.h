//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"

class UMaterialInterface;
class USkeletalMesh;
class UWetClothingAsset;
class AActor;
struct FWetClothingTransparencyLayerData;

struct FDWCTransparencyProjectionSource
{
    FDWCBakeResolvedLayer Layer;
    TObjectPtr<UMaterialInterface> EffectiveMaterial = nullptr;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 PriorityIndex = INDEX_NONE;
    FName MaterialSlotName;
};

/** Immutable, game-thread snapshot of Blueprint Skeletal Mesh Components. */
struct FDWCTransparencyBlueprintMeshComponent
{
    FName ComponentName;
    FName ParentComponentName;
    FString DisplayPath;
    int32 HierarchyDepth = 0;
    TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;
    TArray<TObjectPtr<UMaterialInterface>> Materials;
    FTransform BakeTransform = FTransform::Identity;
};

/** Shared by the Type 2 hierarchy UI and the Type 2 raycast source provider. */
struct FDWCTransparencyBlueprintHierarchy
{
    TArray<FDWCTransparencyBlueprintMeshComponent> MeshComponents;
    FString BuildSignature;
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
    static bool BuildBlueprintHierarchy(
        const TSubclassOf<AActor> BlueprintClass,
        FDWCTransparencyBlueprintHierarchy& OutHierarchy,
        FString& OutError);

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
