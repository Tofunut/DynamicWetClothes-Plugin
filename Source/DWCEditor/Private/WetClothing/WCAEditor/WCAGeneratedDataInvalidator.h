// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

class USkeletalMesh;
class UWetClothingAsset;

/** Central owner of editor-session derived-data invalidation policy. */
class FWCAGeneratedDataInvalidator
{
  public:
    /** Clears every DWC editor-session cache. Intended for editor shutdown/panel teardown. */
    static void InvalidateAll();

    /** Invalidates transient data that depends on one WCA and its source/prepared meshes. */
    static void InvalidateAsset(UWetClothingAsset& Asset);

    /**
     * Must be called when initial DWC UV Channel creation starts and after it finishes.
     * The optional touched mesh covers a newly created/replaced prepared mesh.
     */
    static void InvalidateDataUVInitialization(
        UWetClothingAsset&   Asset,
        const USkeletalMesh* TouchedMesh = nullptr);
};
