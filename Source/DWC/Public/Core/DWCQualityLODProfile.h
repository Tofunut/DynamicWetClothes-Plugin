//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "DWCQualityLODProfile.generated.h"


/**
 * Internal data model for DWC quality LOD support. Not exposed through the public API.
 * The feature is intentionally disabled and not exposed through the shipping component UI or Blueprint API.
 */
USTRUCT()
struct DWC_API FDWCQualityLODPolicy
{
    GENERATED_BODY()

    UPROPERTY()
    bool bUpdateSurfaceWater = true;

    UPROPERTY()
    bool bUpdateWetRendering = true;

    UPROPERTY()
    bool bUpdateWrinkle = true;

    UPROPERTY()
    bool bUpdateTransparency = true;

    UPROPERTY()
    float RenderUpdateInterval = 0.0f;

};

USTRUCT()
struct DWC_API FDWCQualityLODPolicyEntry
{
    GENERATED_BODY()

    UPROPERTY()
    int32 LODLevel = 0;

    UPROPERTY()
    FDWCQualityLODPolicy Policy;
};

struct DWC_API FDWCQualityLODRuntimeState
{
    int32 CurrentQualityLOD = 0;
    FDWCQualityLODPolicy ResolvedPolicy;
    float RenderUpdateAccumulator = 0.0f;
};

USTRUCT()
struct DWC_API FDWCQualityLODScreenSizeThreshold
{
    GENERATED_BODY()

    UPROPERTY()
    int32 LODLevel = 0;

    /** This LOD becomes active when the merged receiver bounds screen size is at or above this value. */
    UPROPERTY()
    float ScreenSize = 0.0f;
};

struct DWC_API FDWCQualityLODScreenSizeRuntimeState
{
    FBoxSphereBounds MergedBounds;
    float ScreenSize = 0.0f;
    bool bHasValidScreenSize = false;
    int32 ActiveLODLevel = INDEX_NONE;
};

UCLASS(NotBlueprintable, HideDropdown)
class DWC_API UDWCQualityLODProfile : public UDataAsset
{
    GENERATED_BODY()

  public:
    UDWCQualityLODProfile();

    UPROPERTY()
    TArray<FDWCQualityLODPolicyEntry> Policies;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    static FDWCQualityLODPolicy MakeDefaultPolicyForLOD(int32 InQualityLOD);
    void NormalizeLODLevels();
};
