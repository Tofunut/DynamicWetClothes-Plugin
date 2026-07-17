#pragma once

#include "CoreMinimal.h"
#include "SurfaceWaterSimulationSettings.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class ESurfaceWaterDebugView : uint8
{
    None = 0,
    DropletAmount = 1,
    FlowAmount = 2,
    VisibleMask = 3,
    SurfaceAmountGate = 4,
    StaticDropletMask = 5,
    SurfaceWaterNormal = 6
};

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

    // Serialized only so existing precomputed assets keep their source signature.
    // These values no longer participate in water routing or rendering.
    UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Surface routing is controlled by FSurfaceWaterProfileParameters."))
    float InputFraction = 0.5f;
    UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Use DropletRadiusPixels."))
    float StampRadiusPixels = 16.0f;
    UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Use per-profile droplet and flow intensity multipliers."))
    float StampIntensityMultiplier = 1.0f;
    UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Use SurfaceWaterMaterialSlots"))
    FSurfaceWaterBakedFlowMapData BakedFlowMap;

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
