// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "WetClothingTransparencyData.generated.h"

class AActor;
class USkeletalMesh;
class UTexture2D;

UENUM()
enum class EDWCTransparencySourceType : uint8
{
    SameMeshMaterialSlots = 0 UMETA(DisplayName = "Same Skeletal Mesh / Material Slots"),
    OtherSkeletalMeshComponents = 1 UMETA(DisplayName = "Other Skeletal Mesh Components"),
    ManualColorOrTexture = 2 UMETA(DisplayName = "Manual Color or Texture")
};

UENUM()
enum class EDWCTransparencyUVAddressMode : uint8
{
    Clamp,
    Wrap
};

UENUM()
enum class EDWCTransparencyBrushMode : uint8
{
    Apply,
    Erase,
    SetValue UMETA(DisplayName = "Set Value"),
    Smooth,
    ResetToAuto UMETA(DisplayName = "Reset to Auto")
};

UENUM()
enum class EDWCTransparencyRevealColorBrushMode : uint8
{
    Paint,
    EraseToBase UMETA(DisplayName = "Erase to Base"),
    Smooth      UMETA(DisplayName = "Smooth")
};

USTRUCT()
struct DWC_API FDWCTransparencyBrushSample
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    FVector2D PositionUV = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush")
    int32 UVIslandID = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush", meta = (ClampMin = "0.0"))
    float RadiusUV = 0.01f;

    UPROPERTY(EditAnywhere, Category = "Transparency Brush", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Strength = 1.0f;
};

USTRUCT()
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

/** RGB paint stored for the Type 3 procedural inner-mesh workflow. */
USTRUCT()
struct DWC_API FDWCTransparencyRevealColorStroke
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Reveal Color Paint")
    FGuid StrokeGuid;

    UPROPERTY(EditAnywhere, Category = "Transparency Reveal Color Paint")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, Category = "Transparency Reveal Color Paint")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Transparency Reveal Color Paint")
    EDWCTransparencyUVAddressMode UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;

    UPROPERTY(EditAnywhere, Category = "Transparency Reveal Color Paint")
    FLinearColor PaintColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, Category = "Transparency Reveal Color Paint")
    EDWCTransparencyRevealColorBrushMode BrushMode = EDWCTransparencyRevealColorBrushMode::Paint;

    UPROPERTY(EditAnywhere, Category = "Transparency Reveal Color Paint", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Falloff = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Transparency Reveal Color Paint", meta = (ClampMin = "0.01"))
    float Spacing = 0.25f;

    UPROPERTY(EditAnywhere, Category = "Transparency Reveal Color Paint")
    TArray<FDWCTransparencyBrushSample> Samples;
};

USTRUCT()
struct DWC_API FWetClothingTransparencyInnerSlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slot")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Inner Slot")
    FName MaterialSlotName;

    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slot", meta = (ClampMin = "0"))
    int32 SourceUVChannel = 0;
};

USTRUCT()
struct DWC_API FWetClothingTransparencyTargetSurface
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Outer Slot")
    int32 OuterMaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Outer Slot")
    FName OuterMaterialSlotName;

    UPROPERTY(EditAnywhere, Category = "Transparency Outer Slot")
    EDWCTransparencyUVAddressMode UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
};

USTRUCT()
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

USTRUCT()
struct DWC_API FWetClothingTransparencySameMeshSource
{
    GENERATED_BODY()

    // Array order is the reveal-source priority. All listed slots are active and share the global ray distance limit.
    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slots")
    TArray<FWetClothingTransparencyInnerSlot> InnerSlotPriority;
};

USTRUCT()
struct DWC_API FWetClothingTransparencyManualColorSource
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Manual Color")
    FLinearColor BaseRevealColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, Category = "Transparency Manual Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float InitialTransparencyAlpha = 0.0f;
};

USTRUCT()
struct DWC_API FWetClothingTransparencyAutoBakeMetadata
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    FGuid AutoBakeGuid;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    int32 Resolution = 1024;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    int32 PaddingPixels = 8;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    int32 ValidHitCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    int32 NoHitCount = 0;
};

USTRUCT()
struct DWC_API FWetClothingBakedTransparencyMap
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> TransparencyMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 Resolution = 1024;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 PaddingPixels = 8;

    // Number of editable strokes already flattened into this texture. Strokes
    // after this index can be replayed when the baked texture is the baseline.
    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 BakedStrokeCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FGuid BakeGuid;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    bool bContainsColorRGB = true;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    bool bContainsTransparencyAlpha = true;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency|Wrinkle Suppression")
    bool bWrinkleSuppressionBakedIntoAlpha = false;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency|Wrinkle Suppression")
    FGuid SourceWrinkleMaskBakeGuid;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency|Wrinkle Suppression")
    FString SourceWrinkleMaskBuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency|Wrinkle Suppression")
    FString WrinkleSuppressionSettingsSignature;

    bool IsRuntimeUsable() const
    {
        return TransparencyMap != nullptr &&
               BakeGuid.IsValid() &&
               !BuildSignature.IsEmpty() &&
               bContainsColorRGB &&
               bContainsTransparencyAlpha;
    }
};

USTRUCT()
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
    FWetClothingTransparencyManualColorSource ManualColorSource;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    TArray<FDWCTransparencyBrushStroke> EditableStrokes;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    TArray<FDWCTransparencyRevealColorStroke> RevealColorPaintStrokes;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencyAutoBakeMetadata AutoBakeMetadata;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Layer")
    TArray<FWetClothingBakedTransparencyMap> BakedMaps;

    // Marks generated state stale while preserving the last usable texture for inspection.
    void MarkAutoBakeStale();
    void MarkFinalBakeStale();
};

USTRUCT()
struct DWC_API FWetClothingTransparencyData
{
    GENERATED_BODY()

    static constexpr int32 CurrentDataVersion = 12;

    UPROPERTY(VisibleAnywhere, Category = "Transparency")
    int32 DataVersion = CurrentDataVersion;

    UPROPERTY(EditAnywhere, Category = "Transparency")
    TArray<FWetClothingTransparencyLayerData> TransparencyLayers;

    // Character-level structure selection made in Transparency Editor Stage 1.
    // Target Parts created in Stage 2 inherit this source type.
    UPROPERTY(EditAnywhere, Category = "Transparency")
    EDWCTransparencySourceType CharacterStructureType = EDWCTransparencySourceType::SameMeshMaterialSlots;

    UPROPERTY()
    bool bCharacterStructureTypeConfigured = false;

    UPROPERTY(EditAnywhere, Category = "Transparency", meta = (ClampMin = "16", UIMin = "128", UIMax = "4096"))
    int32 TransparencyBakeResolution = 1024;

    UPROPERTY(EditAnywhere, Category = "Transparency", meta = (ClampMin = "0", ClampMax = "64"))
    int32 TransparencyPaddingPixels = 8;

    UPROPERTY(EditAnywhere, Category = "Transparency", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0"))
    float TransparencyEdgeFeatherPixels = 4.0f;

    // Authoring strength baked into the final Transparency Map alpha.
    UPROPERTY(EditAnywhere, Category = "Transparency Bake", meta = (DisplayName = "Transparency Strength", ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
    float TransparencyPreviewStrength = 0.4f;

    UPROPERTY(EditAnywhere, Category = "Transparency Bake|Wrinkle Suppression", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float WrinkleSuppressionCoverageThreshold = 0.15f;

    // Softens only the value transition inside the baked wrinkle mask. This is
    // not a spatial radius and never expands suppression into neighboring texels.
    UPROPERTY(EditAnywhere, Category = "Transparency Bake|Wrinkle Suppression", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.25"))
    float WrinkleSuppressionMaskSoftness = 0.05f;

    UPROPERTY(EditAnywhere, Category = "Transparency Bake|Wrinkle Suppression", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "5.0"))
    float WrinkleSuppressionStrength = 0.6f;

    // Internal configuration for the multi-component Transparency source path.
    UPROPERTY(EditAnywhere, Category = "Multi-Mesh Source")
    TSoftClassPtr<AActor> SourceBlueprintClass;

    FWetClothingTransparencyLayerData*       FindTransparencyLayer(int32 MaterialSlotIndex);
    const FWetClothingTransparencyLayerData* FindTransparencyLayer(int32 MaterialSlotIndex) const;

    const FWetClothingBakedTransparencyMap* FindBakedTransparencyMap(int32 MaterialSlotIndex) const;

    /** Runtime lookup rejects stale baked output for the requested material slot. */
    const FWetClothingBakedTransparencyMap* FindRuntimeBakedTransparencyMap(int32 MaterialSlotIndex) const;

    UTexture2D* ResolveBakedTransparencyMap(int32 MaterialSlotIndex) const;
};

class DWC_API FWetClothingTransparencyDataHelpers
{
  public:
    static bool ValidateTransparencyLayer(
        const USkeletalMesh*                     TargetMesh,
        const FWetClothingTransparencyLayerData& Layer,
        TArray<FString>&                         OutErrors,
        int32                                    DWCDataUVChannelIndex);
};
