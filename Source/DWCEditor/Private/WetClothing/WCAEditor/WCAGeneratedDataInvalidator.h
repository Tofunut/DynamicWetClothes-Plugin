// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;

enum class EWCAGeneratedDataInvalidationScope : uint8
{
    All,
    Asset,
    Mesh
};

struct FWCAGeneratedDataInvalidation
{
    EWCAGeneratedDataInvalidationScope Scope = EWCAGeneratedDataInvalidationScope::All;
    const UWetClothingAsset* Asset = nullptr;
    const USkeletalMesh* Mesh = nullptr;
};

DECLARE_MULTICAST_DELEGATE_OneParam(
    FOnWCAGeneratedDataInvalidated,
    const FWCAGeneratedDataInvalidation&);

/** Central owner of editor-session derived-data invalidation policy. */
class FWCAGeneratedDataInvalidator
{
  public:
    /** Clears generated-data-dependent editor caches and transient readback state. */
    static void InvalidateAll();

    /** Invalidates transient data that depends on one WCA and its source/prepared meshes. */
    static void InvalidateAsset(UWetClothingAsset& Asset);

    /** Invalidates editor-session entries after an external asset property change. */
    static void NotifyAssetChanged(const UWetClothingAsset& Asset);

    /** Invalidates entries backed by one mesh without mutating persistent WCA state. */
    static void InvalidateMesh(const USkeletalMesh* Mesh);

    static FOnWCAGeneratedDataInvalidated& OnInvalidated();

    /**
     * Must be called when initial DWC UV Channel creation starts and after it finishes.
     * The optional touched mesh covers a newly created/replaced prepared mesh.
     */
    static void InvalidateDataUVInitialization(
        UWetClothingAsset&   Asset,
        const USkeletalMesh* TouchedMesh = nullptr);
};
