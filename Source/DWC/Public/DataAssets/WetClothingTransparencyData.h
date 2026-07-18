#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "WetClothingTransparencyData.generated.h"

class AActor;
class UMaterialInterface;
class USkeletalMesh;
class UTexture2D;

UENUM(BlueprintType)
enum class EDWCTransparencySourceType : uint8
{
    SameMeshMaterialSlots UMETA(DisplayName = "Same Skeletal Mesh / Material Slots"),
    OtherSkeletalMeshComponents UMETA(DisplayName = "Other Skeletal Mesh Components"),
    ManualColorOrTexture UMETA(DisplayName = "Manual Color or Texture")
};

UENUM(BlueprintType)
enum class EDWCTransparencyUVAddressMode : uint8
{
    Clamp,
    Wrap
};

UENUM(BlueprintType)
enum class EDWCTransparencyBrushMode : uint8
{
    Apply,
    Erase,
    SetValue UMETA(DisplayName = "Set Value"),
    Smooth,
    ResetToAuto UMETA(DisplayName = "Reset to Auto")
};

UENUM()
enum class EDWCTransparencyBakedMapMatch : uint8
{
    None,
    ExactSlotUVLOD,
    SlotUVFallback,
    SlotFallback
};

USTRUCT(BlueprintType)
struct DWC_API FDWCTransparencyBrushSample
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    FVector2D PositionUV = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush", meta = (ClampMin = "0.0"))
    float RadiusUV = 0.01f;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Strength = 1.0f;
};

USTRUCT(BlueprintType)
struct DWC_API FDWCTransparencyBrushStroke
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    FGuid StrokeGuid;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    FString DisplayName;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    int32 UVChannelIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    EDWCTransparencyUVAddressMode UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    EDWCTransparencyBrushMode BrushMode = EDWCTransparencyBrushMode::Apply;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Falloff = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TargetAlpha = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush", meta = (ClampMin = "0.01"))
    float Spacing = 0.25f;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    TArray<FDWCTransparencyBrushSample> Samples;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyInnerSlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slot")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slot")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Inner Slot")
    FName MaterialSlotName;

    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slot", meta = (ClampMin = "0"))
    int32 SourceUVChannel = 0;

    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slot", meta = (ClampMin = "0.0"))
    float MaxHitDistance = 10.0f;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyTargetSurface
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Outer Slot")
    int32 OuterMaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Outer Slot")
    FName OuterMaterialSlotName;

    UPROPERTY(EditAnywhere, Category = "Transparency Outer Slot", meta = (ClampMin = "0"))
    int32 OuterUVChannel = 0;

    UPROPERTY(EditAnywhere, Category = "Transparency Outer Slot")
    EDWCTransparencyUVAddressMode UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyRaySettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Ray", meta = (ClampMin = "-100.0", UIMin = "-10.0", UIMax = "10.0"))
    float RayStartOffset = 0.05f;

    UPROPERTY(EditAnywhere, Category = "Transparency Ray", meta = (ClampMin = "0.0"))
    float MinHitDistance = 0.01f;

    UPROPERTY(EditAnywhere, Category = "Transparency Ray", meta = (ClampMin = "0.0"))
    float FullTransparencyDistance = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Transparency Ray", meta = (ClampMin = "0.0"))
    float NoTransparencyDistance = 5.0f;

    UPROPERTY(EditAnywhere, Category = "Transparency Ray", meta = (ClampMin = "0.0"))
    float MaxRayDistance = 10.0f;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencySameMeshSource
{
    GENERATED_BODY()

    // Array order is the reveal-source priority. Distance only chooses among hits within the same slot.
    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slots")
    TArray<FWetClothingTransparencyInnerSlot> InnerSlotPriority;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyAutoBakeMetadata
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    FGuid AutoBakeGuid;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    int32 LODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    int32 Resolution = 1024;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    int32 PaddingPixels = 8;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    int32 ValidHitCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    int32 NoHitCount = 0;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingBakedTransparencyMap
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 UVChannelIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 LODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> TransparencyMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 Resolution = 1024;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 PaddingPixels = 8;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FGuid BakeGuid;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    bool bContainsColorRGB = true;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    bool bContainsTransparencyAlpha = true;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyLayerData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FGuid LayerGuid;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencyTargetSurface TargetSurface;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    EDWCTransparencySourceType SourceType = EDWCTransparencySourceType::SameMeshMaterialSlots;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencyRaySettings RaySettings;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencySameMeshSource SameMeshSource;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    TArray<FDWCTransparencyBrushStroke> EditableStrokes;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencyAutoBakeMetadata AutoBakeMetadata;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Layer")
    TArray<FWetClothingBakedTransparencyMap> BakedMaps;

    // Marks generated state stale while preserving the last usable texture for inspection.
    void MarkAutoBakeStale();
    void MarkFinalBakeStale();
};

// Legacy multi-component reveal output. Retained until the existing DWCBakeComponent path writes packed maps.
USTRUCT(BlueprintType)
struct DWC_API FWetClothingBakedTransparencyRevealLayer
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FName LayerId;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> LookupMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> ColorMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> MaskMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> ConfidenceMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UMaterialInterface> RevealMaterial = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FGuid BakeGuid;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyData
{
    GENERATED_BODY()

    static constexpr int32 CurrentDataVersion = 2;

    UPROPERTY(VisibleAnywhere, Category = "Transparency")
    int32 DataVersion = CurrentDataVersion;

    UPROPERTY(EditAnywhere, Category = "Transparency")
    TArray<FWetClothingTransparencyLayerData> TransparencyLayers;

    UPROPERTY(EditAnywhere, Category = "Transparency", meta = (ClampMin = "16", UIMin = "128", UIMax = "4096"))
    int32 TransparencyBakeResolution = 1024;

    UPROPERTY(EditAnywhere, Category = "Transparency", meta = (ClampMin = "0", ClampMax = "64"))
    int32 TransparencyPaddingPixels = 8;

    UPROPERTY(EditAnywhere, Category = "Transparency", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0"))
    float TransparencyEdgeFeatherPixels = 4.0f;

    UPROPERTY(EditAnywhere, Category = "Transparency Preview", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
    float TransparencyPreviewStrength = 0.4f;

    UPROPERTY()
    float WrinkleSuppressionRadiusPixels = 4.0f;

    UPROPERTY()
    float WrinkleSuppressionFeatherPixels = 16.0f;

    UPROPERTY()
    float WrinkleSuppressionCoverageThreshold = 0.15f;

    UPROPERTY(EditAnywhere, Category = "Transparency Preview", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "5.0"))
    float WrinkleSuppressionStrength = 0.6f;

    // Existing multi-component bake configuration. Kept operational during the staged migration.
    UPROPERTY(EditAnywhere, Category = "Legacy Bake")
    TSoftClassPtr<AActor> SourceBlueprintClass;

    UPROPERTY(EditAnywhere, Category = "Legacy Bake", meta = (ClampMin = "16", UIMin = "128", UIMax = "4096"))
    int32 RevealBakeResolution = 1024;

    UPROPERTY(EditAnywhere, Category = "Legacy Bake", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0"))
    float RevealMaskFeatherRadiusPixels = 4.0f;

    UPROPERTY(VisibleAnywhere, Category = "Legacy Baked")
    TArray<FWetClothingBakedTransparencyRevealLayer> BakedRevealLayers;

    FWetClothingTransparencyLayerData* FindTransparencyLayer(int32 MaterialSlotIndex, int32 UVChannelIndex);
    const FWetClothingTransparencyLayerData* FindTransparencyLayer(int32 MaterialSlotIndex, int32 UVChannelIndex) const;

    const FWetClothingBakedTransparencyMap* FindBakedTransparencyMap(
        int32 MaterialSlotIndex,
        int32 PreferredUVChannelIndex = INDEX_NONE,
        int32 PreferredLODIndex = INDEX_NONE,
        EDWCTransparencyBakedMapMatch* OutMatch = nullptr) const;

    UTexture2D* ResolveBakedTransparencyMap(
        int32 MaterialSlotIndex,
        int32 PreferredUVChannelIndex = INDEX_NONE,
        int32 PreferredLODIndex = INDEX_NONE) const;
};

class DWC_API FWetClothingTransparencyDataHelpers
{
  public:
    static bool ValidateTransparencyLayer(
        const USkeletalMesh* TargetMesh,
        const FWetClothingTransparencyLayerData& Layer,
        TArray<FString>& OutErrors,
        int32 LODIndex = 0);
};
