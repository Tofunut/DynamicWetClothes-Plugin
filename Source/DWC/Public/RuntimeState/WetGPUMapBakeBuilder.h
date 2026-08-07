//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
struct FScopedSlowTask;

#if WITH_EDITOR
/** Builds Save-generated GPU runtime structures and explicit resolution-dependent GPU maps. */
class DWC_API FWetGPUMapBakeBuilder
{
public:
    /** Returns the progress work consumed by BuildLODMaps(). */
    static float GetLODMapBakeProgressWork(const UWetClothingAsset& Asset);

    /** Builds triangle/profile/incident data only. */
    static bool BuildRuntimeLOD(
        UWetClothingAsset& Asset,
        int32 LODIndex = 0,
        FString* OutErrorMessage = nullptr,
        FScopedSlowTask* SlowTask = nullptr);

    /** Builds resolution-dependent texel lookup and seam maps from existing GPU runtime data. */
    static bool BuildLODMaps(
        UWetClothingAsset& Asset,
        int32 LODIndex = 0,
        FString* OutErrorMessage = nullptr,
        FScopedSlowTask* SlowTask = nullptr);

    static bool BuildLODRuntimeSignature(const UWetClothingAsset& Asset, int32 LODIndex, FString& OutSignature, FString* OutErrorMessage = nullptr);
    static bool BuildLODMapSignature(const UWetClothingAsset& Asset, int32 LODIndex, FString& OutSignature, FString* OutErrorMessage = nullptr);
    static void ClearSignatureCache();
};
#endif
