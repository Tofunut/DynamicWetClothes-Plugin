#pragma once

#include "CoreMinimal.h"
#include "SurfaceWaterSimulationSettings.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct DWC_API FSurfaceWaterBakedFlowMapData
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Surface Water|Flow Map")
    bool bEnabled = true;
    UPROPERTY(VisibleAnywhere, Category="Surface Water|Flow Map")
    bool bIsValid = false;
    UPROPERTY(VisibleAnywhere, Category="Surface Water|Flow Map")
    int32 SourceLODIndex = 0;
    UPROPERTY(VisibleAnywhere, Category="Surface Water|Flow Map")
    int32 Resolution = 512;
    UPROPERTY(EditAnywhere, Category="Surface Water|Flow Map", meta=(ClampMin="0", ClampMax="64"))
    int32 PaddingPixels = 4;
    UPROPERTY(VisibleAnywhere, Category="Surface Water|Flow Map")
    TObjectPtr<UTexture2D> FlowMap = nullptr;
    UPROPERTY(VisibleAnywhere, Category="Surface Water|Flow Map")
    FString BuildSignature;
    UPROPERTY(VisibleAnywhere, Category="Surface Water|Flow Map")
    FGuid BakeGuid;
};

USTRUCT(BlueprintType)
struct DWC_API FSurfaceWaterMaterialSlotData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category="Surface Water|Material Slot")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category="Surface Water|Material Slot")
    bool bEnabled = true;

    UPROPERTY(VisibleAnywhere, Category="Surface Water|Material Slot", meta=(ShowOnlyInnerProperties))
    FSurfaceWaterBakedFlowMapData BakedFlowMap;
};

USTRUCT(BlueprintType)
struct DWC_API FSurfaceWaterSimulationSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Surface Water")
    bool bEnabled = true;
    UPROPERTY(EditAnywhere, Category="Surface Water", meta=(ClampMin="16", ClampMax="4096"))
    int32 RenderTargetResolution = 1024;

    UPROPERTY(VisibleAnywhere, Category="Surface Water|Material Slots", meta=(TitleProperty="Material Slot {MaterialSlotIndex}"))
    TArray<FSurfaceWaterMaterialSlotData> SurfaceWaterMaterialSlots;

    const FSurfaceWaterMaterialSlotData* FindMaterialSlot(int32 MaterialSlotIndex) const
    {
        return SurfaceWaterMaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FSurfaceWaterMaterialSlotData& Data)
            {
                return Data.MaterialSlotIndex == MaterialSlotIndex;
            });
    }
};
