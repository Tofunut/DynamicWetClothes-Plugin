//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"
#include "UObject/GCObject.h"

class UMaterialInterface;
class USkeletalMesh;
class UWetClothingAsset;
class AActor;
struct FWetClothingTransparencyLayerData;

/**
 * Keeps transient Blueprint material snapshots alive until every consumer of a
 * hierarchy/projection snapshot has finished using their render resources.
 */
class FDWCTransparencyProjectionObjectLease final : public FGCObject
{
  public:
    void Retain(UObject* Object);

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("FDWCTransparencyProjectionObjectLease");
    }

  private:
    TArray<TObjectPtr<UObject>> Objects;
};

struct FDWCTransparencyProjectionSource
{
    FDWCBakeResolvedLayer Layer;
    TObjectPtr<UMaterialInterface> EffectiveMaterial = nullptr;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 PriorityIndex = INDEX_NONE;
    FName MaterialSlotName;
};

/**
 * Lightweight Blueprint component description used by Type 2 UI and preview.
 * It deliberately stores soft asset identities only; effective materials and
 * render resources belong to the explicit projection build snapshot.
 */
struct FDWCTransparencyBlueprintMeshComponentMetadata
{
    FName ComponentName;
    FName ParentComponentName;
    FString DisplayPath;
    int32 HierarchyDepth = 0;
    FSoftObjectPath SkeletalMeshPath;
    TArray<FSoftObjectPath> MaterialPaths;
    FTransform BakeTransform = FTransform::Identity;
};

/** Immutable metadata-only hierarchy consumed by ordinary editor refreshes. */
struct FDWCTransparencyBlueprintHierarchyMetadata
{
    TArray<FDWCTransparencyBlueprintMeshComponentMetadata> MeshComponents;
    FString BuildSignature;
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
    /** Captured before transient component materials are converted to durable bake snapshots. */
    TArray<FGuid> MaterialLightingGuids;
    FTransform BakeTransform = FTransform::Identity;
};

/** Shared by the Type 2 hierarchy UI and the Type 2 raycast source provider. */
struct FDWCTransparencyBlueprintHierarchy
{
    TArray<FDWCTransparencyBlueprintMeshComponent> MeshComponents;
    FString BuildSignature;
    TSharedPtr<FDWCTransparencyProjectionObjectLease> ObjectLease;
};

/** Game-thread provider output shared by every ray-projected transparency source type. */
struct FDWCTransparencyProjectionSourceSet
{
    FTransform OuterBakeTransform = FTransform::Identity;
    TArray<FDWCTransparencyProjectionSource> Sources;
    TArray<FString> Warnings;
    FString ProviderSignature;
    TSharedPtr<FDWCTransparencyProjectionObjectLease> ObjectLease;
};

class FDWCTransparencyProjectionSourceProvider
{
  public:
    static bool BuildBlueprintHierarchyMetadata(
        const TSubclassOf<AActor> BlueprintClass,
        FDWCTransparencyBlueprintHierarchyMetadata& OutHierarchy,
        FString& OutError);

    static bool BuildBlueprintHierarchy(
        const TSubclassOf<AActor> BlueprintClass,
        FDWCTransparencyBlueprintHierarchy& OutHierarchy,
        FString& OutError);

    static bool BuildBlueprintSources(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyProjectionSourceSet& OutSources,
        FString& OutError);
    static bool BuildBlueprintSources(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyBlueprintHierarchy& Hierarchy,
        FDWCTransparencyProjectionSourceSet& OutSources,
        FString& OutError);

    static bool BuildExternalMeshSources(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyProjectionSourceSet& OutSources,
        FString& OutError);
};
