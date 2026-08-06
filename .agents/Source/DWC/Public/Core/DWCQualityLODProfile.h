#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "DWCQualityLODProfile.generated.h"

USTRUCT(BlueprintType)
struct DWC_API FDWCQualityLODPolicy
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD|Feature", meta = (DisplayName = "Update GPU Surface Water"))
    bool bUpdateSurfaceWater = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD|Feature", meta = (DisplayName = "Update CPU Wetness Rendering"))
    bool bUpdateWetRendering = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD|Feature")
    bool bUpdateWrinkle = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD|Feature")
    bool bUpdateTransparency = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD|Performance", meta = (ClampMin = "0.0", Units = "s", DisplayName = "CPU Wetness Render Update Interval"))
    float RenderUpdateInterval = 0.0f;

};

USTRUCT(BlueprintType)
struct DWC_API FDWCQualityLODPolicyEntry
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LOD")
    int32 LODLevel = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ShowOnlyInnerProperties))
    FDWCQualityLODPolicy Policy;
};

struct DWC_API FDWCQualityLODRuntimeState
{
    int32 CurrentQualityLOD = 0;
    FDWCQualityLODPolicy ResolvedPolicy;
    float RenderUpdateAccumulator = 0.0f;
};

USTRUCT(BlueprintType)
struct DWC_API FDWCQualityLODScreenSizeThreshold
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wetness|LOD")
    int32 LODLevel = 0;

    /** This LOD becomes active when the merged receiver bounds screen size is at or above this value. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|LOD", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Screen Size"))
    float ScreenSize = 0.0f;
};

struct DWC_API FDWCQualityLODScreenSizeRuntimeState
{
    FBoxSphereBounds MergedBounds;
    float ScreenSize = 0.0f;
    bool bHasValidScreenSize = false;
    int32 ActiveLODLevel = INDEX_NONE;
};

UCLASS(BlueprintType)
class DWC_API UDWCQualityLODProfile : public UDataAsset
{
    GENERATED_BODY()

  public:
    UDWCQualityLODProfile();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LOD", meta = (TitleProperty = "LODLevel"))
    TArray<FDWCQualityLODPolicyEntry> Policies;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    static FDWCQualityLODPolicy MakeDefaultPolicyForLOD(int32 InQualityLOD);
    void NormalizeLODLevels();
};
