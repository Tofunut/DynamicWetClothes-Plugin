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
    static void InvalidateAsset(const UWetClothingAsset& Asset);

    /**
     * Must be called when a Data UV rebuild starts and after it finishes.
     * The optional touched mesh covers a newly created/replaced prepared mesh.
     */
    static void InvalidateDataUVRebuild(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* TouchedMesh = nullptr);
};
