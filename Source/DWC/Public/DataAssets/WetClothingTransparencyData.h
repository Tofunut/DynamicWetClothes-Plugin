//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "WetClothingTransparencyData.generated.h"

class AActor;
class USkeletalMesh;
class UTexture2D;

UENUM(BlueprintType)
enum class EDWCTransparencySourceType : uint8
{
    SameMeshMaterialSlots UMETA(DisplayName = "Same Skeletal Mesh / Material Slots"),
    OtherSkeletalMeshComponents UMETA(DisplayName = "Other Skeletal Mesh Components"),
    ManualColorOrTexture UMETA(DisplayName = "Manual Color or Texture"),
    ExternalSkeletalMesh UMETA(DisplayName = "External Skeletal Mesh")
};

/** The selected Blueprint component contributes reveal color or only blocks rays. */
UENUM(BlueprintType)
enum class EDWCTransparencyBlueprintSourceRole : uint8
{
    RevealSource UMETA(DisplayName = "Reveal Source"),
    BlockerOnly UMETA(DisplayName = "Blocker Only")
};

UENUM(BlueprintType)
enum class EDWCTransparencyUVAddressMode : uint8
{
    Clamp,
    Wrap
};

/** Persisted authoring intent. Selection alone never creates or enables a layer. */
UENUM(BlueprintType)
enum class EDWCTransparencyLayerIntent : uint8
{
    Draft,
    Enabled,
    Disabled
};

/** Determines how a target slot chooses the square Transparency authoring resolution. */
UENUM(BlueprintType)
enum class EDWCTransparencyOutputResolutionMode : uint8
{
    /** Resolve from the original target material's effective Base Color texture chain. */
    Auto,
    /** Use the explicitly authored per-layer resolution. */
    Override
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

UENUM(BlueprintType)
enum class EDWCTransparencyRevealColorBrushMode : uint8
{
    Paint,
    EraseToBase UMETA(DisplayName = "Erase to Base"),
    Smooth UMETA(DisplayName = "Smooth")
};

/** Editor-generated, rebuildable transparency pipeline artifacts. */
UENUM()
enum class EDWCTransparencyTempArtifactKind : uint8
{
    /** Legacy enum value. Source material surfaces use MaterialColorCache entries. */
    SourceMaterialColor,
    BaseRevealColor,
    ValidHit,
    HitSource,
    HitDistance,
    CorrectedRevealColor,
    /** Target surface coverage required by Stage 4 feathering and padding. */
    OuterCoverage,
    /** Per-texel target UV island identity required by brush stroke replay. */
    OuterIslandID,
    /** Reoriented inner normal XY, inner metallic, and valid source-surface coverage. */
    BaseRevealSurface
};

USTRUCT()
struct DWC_API FDWCTransparencyTempArtifactReference
{
    GENERATED_BODY()

    UPROPERTY()
    EDWCTransparencyTempArtifactKind Kind = EDWCTransparencyTempArtifactKind::BaseRevealColor;

    UPROPERTY()
    TSoftObjectPtr<UTexture2D> Texture;

    UPROPERTY()
    FString BuildSignature;

    /** Schema version of the artifact payload and its dependency signature. */
    UPROPERTY()
    int32 ContractVersion = 0;

    /** Identifies the all-or-nothing Stage commit that published this reference. */
    UPROPERTY()
    FGuid CommitGeneration;

    /** Texture source identity captured after the payload was written. */
    UPROPERTY()
    FGuid TextureSourceId;

    /** Serialized resolved target-output extent for this editor artifact. */
    UPROPERTY()
    FIntPoint Resolution = FIntPoint::ZeroValue;

    UPROPERTY()
    bool bObsolete = false;
};

UENUM(BlueprintType)
enum class EDWCTransparencyManualRevealSourceMode : uint8
{
    AuthoredColor UMETA(DisplayName = "Color Picker / Eyedropper"),
    UVIslandAverage UMETA(DisplayName = "UV Island Average")
};

/** Physical representation retained for an evaluated source material color. */
UENUM()
enum class EDWCTransparencyMaterialColorPayloadKind : uint8
{
    Texture,
    ConstantColor
};

/**
 * Shared editor cache entry for a source material evaluated in its original UV
 * space. Base Color remains required for ray projection; Normal and Metallic
 * are retained alongside it for the later Reveal Surface bake.
 */
USTRUCT()
struct DWC_API FDWCTransparencyMaterialColorCacheReference
{
    GENERATED_BODY()

    UPROPERTY()
    TSoftObjectPtr<USkeletalMesh> SourceMesh;

    UPROPERTY()
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY()
    int32 SourceUVChannel = 0;

    /** Requested source material evaluation resolution, independent of the target output. */
    UPROPERTY()
    int32 SourceBakeResolution = 0;

    /** Actual dimensions stored in Texture. Uniform material output may be represented as 1x1. */
    UPROPERTY()
    FIntPoint PayloadResolution = FIntPoint::ZeroValue;

    UPROPERTY()
    EDWCTransparencyMaterialColorPayloadKind PayloadKind =
        EDWCTransparencyMaterialColorPayloadKind::Texture;

    UPROPERTY()
    FString MaterialBakeSignature;

    /** Versioned digest of every input consumed by MaterialBaking. */
    UPROPERTY()
    FString CacheIdentity;

    UPROPERTY()
    int32 IdentityVersion = 0;

    /** Diagnostic components retained so stale cache entries can explain why they missed. */
    UPROPERTY()
    FString SourceMeshContentSignature;

    UPROPERTY()
    FString EffectiveMaterialSignature;

    UPROPERTY()
    FString PlacementSignature;

    UPROPERTY()
    TSoftObjectPtr<UTexture2D> Texture;

    /** Evaluated tangent-space material normal. Flat normal is persisted when the property is unavailable. */
    UPROPERTY()
    FIntPoint NormalPayloadResolution = FIntPoint::ZeroValue;

    UPROPERTY()
    EDWCTransparencyMaterialColorPayloadKind NormalPayloadKind =
        EDWCTransparencyMaterialColorPayloadKind::Texture;

    UPROPERTY()
    TSoftObjectPtr<UTexture2D> NormalTexture;

    /** Evaluated material metallic, packed as a linear G8 payload. */
    UPROPERTY()
    FIntPoint MetallicPayloadResolution = FIntPoint::ZeroValue;

    UPROPERTY()
    EDWCTransparencyMaterialColorPayloadKind MetallicPayloadKind =
        EDWCTransparencyMaterialColorPayloadKind::Texture;

    UPROPERTY()
    TSoftObjectPtr<UTexture2D> MetallicTexture;

    UPROPERTY()
    bool bHasBakedNormalProperty = false;

    UPROPERTY()
    bool bHasBakedMetallicProperty = false;

    UPROPERTY()
    bool bObsolete = false;
};

/** Editor-only checkpoints. Authoring data remains the source of truth. */
USTRUCT()
struct DWC_API FDWCTransparencyEditorStageCacheMetadata
{
    GENERATED_BODY()

    UPROPERTY()
    FString MaterialBakeSignature;

    UPROPERTY()
    FString SourceSignature;

    UPROPERTY()
    FString RevealSignature;

    UPROPERTY()
    bool bSourceGenerated = false;

    UPROPERTY()
    bool bRevealReviewed = false;

    UPROPERTY()
    TArray<FDWCTransparencyTempArtifactReference> Artifacts;

    void MarkSourceStale();
    void MarkRevealStale();
};

USTRUCT(BlueprintType)
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

/** Compact persisted form. Brush math decodes this to FDWCTransparencyBrushSample on demand. */
USTRUCT()
struct DWC_API FDWCTransparencyCompactBrushSample
{
    GENERATED_BODY()

    UPROPERTY()
    FVector2f PositionUV = FVector2f::ZeroVector;

    UPROPERTY()
    int32 UVIslandID = INDEX_NONE;

    UPROPERTY()
    float RadiusUV = 0.01f;

    UPROPERTY()
    float Strength = 1.0f;

    static FDWCTransparencyCompactBrushSample Encode(
        const FDWCTransparencyBrushSample& Sample);
    FDWCTransparencyBrushSample Decode() const;
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

    UPROPERTY()
    TArray<FDWCTransparencyCompactBrushSample> CompactSamples;

    int32 GetSampleCount() const;
    bool HasSamples() const;
    uint64 GetSampleAllocatedSize() const;
    void AddSample(const FDWCTransparencyBrushSample& Sample);
    void DecodeSamples(TArray<FDWCTransparencyBrushSample>& OutSamples) const;
    bool CompactLegacySamples();

    template <typename VisitorType>
    void ForEachSample(VisitorType&& Visitor) const
    {
        if (!CompactSamples.IsEmpty())
        {
            for (const FDWCTransparencyCompactBrushSample& Sample : CompactSamples)
            {
                Visitor(Sample.Decode());
            }
        }
        for (const FDWCTransparencyBrushSample& Sample : Samples)
        {
            Visitor(Sample);
        }
    }
};

/** RGB paint stored for the Type 3 procedural inner-mesh workflow. */
USTRUCT(BlueprintType)
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

    UPROPERTY()
    TArray<FDWCTransparencyCompactBrushSample> CompactSamples;

    int32 GetSampleCount() const;
    bool HasSamples() const;
    uint64 GetSampleAllocatedSize() const;
    void AddSample(const FDWCTransparencyBrushSample& Sample);
    void DecodeSamples(TArray<FDWCTransparencyBrushSample>& OutSamples) const;
    bool CompactLegacySamples();

    template <typename VisitorType>
    void ForEachSample(VisitorType&& Visitor) const
    {
        if (!CompactSamples.IsEmpty())
        {
            for (const FDWCTransparencyCompactBrushSample& Sample : CompactSamples)
            {
                Visitor(Sample.Decode());
            }
        }
        for (const FDWCTransparencyBrushSample& Sample : Samples)
        {
            Visitor(Sample);
        }
    }
};

/** Editor-only, per-layer transaction boundary for high-volume authored strokes. */
UCLASS()
class DWC_API UDWCTransparencyLayerStrokeHistory : public UObject
{
    GENERATED_BODY()

  public:
    UPROPERTY()
    TArray<FDWCTransparencyBrushStroke> AlphaStrokes;

    UPROPERTY()
    TArray<FDWCTransparencyRevealColorStroke> RevealColorStrokes;

    bool CompactLegacySamples();
    uint64 GetAllocatedSize() const;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyInnerSlot
{
    GENERATED_BODY()

    // Legacy per-slot enable state. Type 1 now treats every listed slot as an active priority source.
    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Inner Source Parts are always active. Remove the entry to exclude it."))
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slot")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Inner Slot")
    FName MaterialSlotName;

    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slot", meta = (ClampMin = "0"))
    int32 SourceUVChannel = 0;

    // Legacy per-slot ray limit. Type 1 now uses RaySettings.MaxRayDistance for every source slot.
    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the global Maximum Ray Distance in Ray Settings."))
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

    // Array order is the reveal-source priority. All listed slots are active and share the global ray distance limit.
    UPROPERTY(EditAnywhere, Category = "Transparency Inner Slots")
    TArray<FWetClothingTransparencyInnerSlot> InnerSlotPriority;
};

/**
 * Stable Type 2 binding to one Skeletal Mesh Component in the selected
 * Blueprint. Component names are unique within an Actor; the expected mesh is
 * retained to detect Blueprint edits that would otherwise silently retarget a
 * transparency source.
 */
USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyBlueprintComponentBinding
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Blueprint Source")
    FName ComponentName;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Blueprint Source")
    TSoftObjectPtr<USkeletalMesh> ExpectedSkeletalMesh;

    UPROPERTY(EditAnywhere, Category = "Transparency Blueprint Source", meta = (ClampMin = "0"))
    int32 SourceUVChannel = 0;

    UPROPERTY(EditAnywhere, Category = "Transparency Blueprint Source")
    EDWCTransparencyBlueprintSourceRole Role = EDWCTransparencyBlueprintSourceRole::RevealSource;

    bool IsBound() const
    {
        return !ComponentName.IsNone();
    }
};

/** Per-target Type 2 Blueprint selection and ordered raycast sources. */
USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyBlueprintSource
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Blueprint Source")
    TSoftClassPtr<AActor> BlueprintClass;

    UPROPERTY(EditAnywhere, Category = "Transparency Blueprint Source")
    FWetClothingTransparencyBlueprintComponentBinding TargetComponent;

    /** Array order is the explicit raycast source priority. */
    UPROPERTY(EditAnywhere, Category = "Transparency Blueprint Source")
    TArray<FWetClothingTransparencyBlueprintComponentBinding> SourcePriority;
};

/**
 * One independently placed Skeletal Mesh used by the Type 3 raycast source.
 * Array order is the raycast priority; every material slot on the mesh shares
 * that entry's priority and transform.
 */
USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyExternalMeshEntry
{
    GENERATED_BODY()

    /** Stable identity permits multiple instances of the same Skeletal Mesh. */
    UPROPERTY(EditAnywhere, Category = "Transparency External Mesh")
    FGuid SourceGuid;

    UPROPERTY(EditAnywhere, Category = "Transparency External Mesh")
    TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

    /** Places this reference-pose mesh in the target mesh bake space. */
    UPROPERTY(EditAnywhere, Category = "Transparency External Mesh")
    FTransform BakeTransform = FTransform::Identity;

    UPROPERTY(EditAnywhere, Category = "Transparency External Mesh", meta = (ClampMin = "0"))
    int32 SourceUVChannel = 0;

    UPROPERTY(EditAnywhere, Category = "Transparency External Mesh")
    EDWCTransparencyBlueprintSourceRole Role = EDWCTransparencyBlueprintSourceRole::RevealSource;

    bool IsConfigured() const
    {
        return SkeletalMesh != nullptr;
    }
};

/** Source geometry supplied independently from the WCA target mesh. */
USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyExternalMeshSource
{
    GENERATED_BODY()

    /** Array order is the explicit raycast source priority. */
    UPROPERTY(EditAnywhere, Category = "Transparency External Mesh")
    TArray<FWetClothingTransparencyExternalMeshEntry> SourcePriority;

    /** Legacy single-source data. Kept only so already-authored Type 3 data remains readable. */
    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use SourcePriority."))
    TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use SourcePriority."))
    FTransform BakeTransform = FTransform::Identity;

    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use SourcePriority."))
    TArray<FWetClothingTransparencyInnerSlot> SourceSlotPriority;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyManualColorSource
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Manual Color")
    EDWCTransparencyManualRevealSourceMode SourceMode =
        EDWCTransparencyManualRevealSourceMode::AuthoredColor;

    UPROPERTY(EditAnywhere, Category = "Transparency Manual Color")
    FLinearColor BaseRevealColor = FLinearColor::White;

    /** Informational source metadata used to explain and invalidate an island-derived color. */
    UPROPERTY(VisibleAnywhere, Category = "Transparency Manual Color")
    TSoftObjectPtr<UTexture2D> SampledColorTexture;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Manual Color")
    int32 SampledMaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Manual Color")
    int32 SampledUVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Manual Color")
    int32 SampledUVIslandID = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Transparency Manual Color", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float InitialTransparencyAlpha = 0.0f;

};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyAutoBakeMetadata
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    FGuid AutoBakeGuid;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Auto Bake")
    FString BuildSignature;

    /** Resolved per-slot target-output resolution used by the Stage 2 payload. */
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
    TObjectPtr<UTexture2D> TransparencyMap = nullptr;

    /**
     * Runtime-only reveal normal. RG stores the inner normal reoriented into
     * the outer tangent frame with source coverage already folded into its XY
     * magnitude. Editor Metallic and source coverage are not runtime channels.
     */
    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> RevealNormalMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FString RevealNormalBuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    bool bSourceCoverageBakedIntoRevealNormal = false;

#if WITH_EDITORONLY_DATA
    /**
     * Deprecated packed runtime payload retained only so existing WCA assets
     * deserialize safely in the editor. It is stripped from cooked packages.
     */
    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Re-bake Transparency Textures to create RevealNormalMap."))
    TObjectPtr<UTexture2D> RevealSurfaceMap = nullptr;

    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Re-bake Transparency Textures to create RevealNormalBuildSignature."))
    FString RevealSurfaceBuildSignature;

    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Legacy packed Reveal Surface metadata."))
    bool bContainsRevealNormalRG = false;

    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Metallic darkening is now baked into corrected reveal color."))
    bool bContainsInnerMetallicB = false;

    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Source coverage is now baked into RevealNormalMap RG."))
    bool bContainsRevealSurfaceCoverageAlpha = false;
#endif

    /** Resolved per-slot runtime output resolution shared by both runtime textures. */
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

    /** Stage 4 alpha-domain signature, independent from corrected reveal RGB. */
    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FString FinalAlphaBuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    bool bContainsColorRGB = true;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    bool bContainsTransparencyAlpha = true;

    // New final maps store the Stage 2 inner-metallic response directly in
    // the Stage 3 corrected reveal RGB. Runtime materials must not apply it again.
    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    bool bMetallicDarkeningBakedIntoColor = false;

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

#if WITH_EDITORONLY_DATA
    /** True when any deprecated packed Reveal Surface state remains serialized in an editor asset. */
    bool HasAnyLegacyRevealSurfaceData() const
    {
        return RevealSurfaceMap != nullptr ||
               !RevealSurfaceBuildSignature.IsEmpty() ||
               bContainsRevealNormalRG ||
               bContainsInnerMetallicB ||
               bContainsRevealSurfaceCoverageAlpha;
    }

    /** True only when the complete deprecated packed payload is present. */
    bool HasLegacyRevealSurfacePayload() const
    {
        return RevealSurfaceMap != nullptr &&
               !RevealSurfaceBuildSignature.IsEmpty() &&
               bContainsRevealNormalRG &&
               bContainsInnerMetallicB &&
               bContainsRevealSurfaceCoverageAlpha;
    }
#endif

    bool HasRuntimeRevealNormalPayload() const
    {
        return RevealNormalMap != nullptr &&
               !RevealNormalBuildSignature.IsEmpty() &&
               bSourceCoverageBakedIntoRevealNormal;
    }

    bool IsRuntimeUsableForLayer(const bool bRequiresRevealNormal) const
    {
        return IsRuntimeUsable() &&
               (!bRequiresRevealNormal ||
                   (HasRuntimeRevealNormalPayload() && bMetallicDarkeningBakedIntoColor));
    }
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyLayerData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FGuid LayerGuid;

    /**
     * Draft layers are explicit editor work but do not require runtime output.
     * Enabled layers participate in validation, build and runtime lookup.
     * Disabled layers preserve authored data while opting out of runtime output.
     * The Enabled default preserves the behavior of assets serialized before this field existed.
     */
    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    EDWCTransparencyLayerIntent Intent = EDWCTransparencyLayerIntent::Enabled;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencyTargetSurface TargetSurface;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    EDWCTransparencySourceType SourceType = EDWCTransparencySourceType::SameMeshMaterialSlots;

    // Distinguishes a newly-added target part from an existing layer that uses
    // the default source type. The editor uses this to choose Stage 1 or Stage 2.
    UPROPERTY()
    bool bSourceTypeConfigured = false;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencyRaySettings RaySettings;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer|Resolution")
    EDWCTransparencyOutputResolutionMode OutputResolutionMode =
        EDWCTransparencyOutputResolutionMode::Auto;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer|Resolution",
        meta = (ClampMin = "256", ClampMax = "4096", UIMin = "256", UIMax = "4096"))
    int32 OutputResolutionOverride = 2048;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencySameMeshSource SameMeshSource;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencyBlueprintSource BlueprintSource;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencyManualColorSource ManualColorSource;

    UPROPERTY(EditAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencyExternalMeshSource ExternalMeshSource;

#if WITH_EDITORONLY_DATA
    /** Canonical editor history. A separate UObject keeps Undo snapshots scoped to this layer. */
    UPROPERTY(Instanced)
    TObjectPtr<UDWCTransparencyLayerStrokeHistory> EditorStrokeHistory;

    /** Legacy inline storage retained for loading pre-compaction WCA assets. */
    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Migrated to EditorStrokeHistory."))
    TArray<FDWCTransparencyBrushStroke> EditableStrokes;

    /** Legacy inline storage retained for loading pre-compaction WCA assets. */
    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Migrated to EditorStrokeHistory."))
    TArray<FDWCTransparencyRevealColorStroke> RevealColorPaintStrokes;
#endif

    UPROPERTY(VisibleAnywhere, Category = "Transparency Layer")
    FWetClothingTransparencyAutoBakeMetadata AutoBakeMetadata;

    UPROPERTY(VisibleAnywhere, Category = "Transparency Layer")
    TArray<FWetClothingBakedTransparencyMap> BakedMaps;

    /** Runtime application policy. The raycast source remains bakeable even while presentation is disabled. */
    UPROPERTY(EditAnywhere, Category = "Transparency Layer|Reveal Normal")
    bool bEnableRevealNormal = true;

    /** Scales coverage-weighted Reveal Normal detail in the material; it does not change the baked texture. */
    UPROPERTY(EditAnywhere, Category = "Transparency Layer|Reveal Normal",
        meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "4.0"))
    float RevealNormalStrength = 1.0f;

#if WITH_EDITORONLY_DATA
    UPROPERTY()
    FDWCTransparencyEditorStageCacheMetadata EditorStageCache;
#endif

    // Marks generated state stale while preserving the last usable texture for inspection.
    void MarkAutoBakeStale();
    void MarkFinalBakeStale();

    /** Manual color has no inner surface to encode; all raycast source types do. */
    bool RequiresRevealSurface() const
    {
        return SourceType != EDWCTransparencySourceType::ManualColorOrTexture;
    }

#if WITH_EDITOR
    const TArray<FDWCTransparencyBrushStroke>& GetEditableStrokes() const;
    TArray<FDWCTransparencyBrushStroke>& GetMutableEditableStrokes();
    const TArray<FDWCTransparencyRevealColorStroke>& GetRevealColorPaintStrokes() const;
    TArray<FDWCTransparencyRevealColorStroke>& GetMutableRevealColorPaintStrokes();
    UDWCTransparencyLayerStrokeHistory* GetEditorStrokeHistory() const;
#endif

    bool RequiresRuntimeRevealNormal() const
    {
        return RequiresRevealSurface() && bEnableRevealNormal;
    }

    bool IsRuntimeEnabled() const
    {
        return Intent == EDWCTransparencyLayerIntent::Enabled;
    }
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyData
{
    GENERATED_BODY()

    static constexpr int32 PerLayerResolutionDataVersion = 13;
    static constexpr int32 LayerIntentDataVersion = 14;
    static constexpr int32 CurrentDataVersion = LayerIntentDataVersion;

    UPROPERTY(VisibleAnywhere, Category = "Transparency")
    int32 DataVersion = CurrentDataVersion;

    UPROPERTY(EditAnywhere, Category = "Transparency")
    TArray<FWetClothingTransparencyLayerData> TransparencyLayers;

#if WITH_EDITORONLY_DATA
    /** Rebuildable Stage 2 source-color cache shared by every transparency layer. */
    UPROPERTY()
    TArray<FDWCTransparencyMaterialColorCacheReference> MaterialColorCache;
#endif

    // Character-level structure selection made in Transparency Editor Stage 1.
    // Target Parts created in Stage 2 inherit this source type.
    UPROPERTY(EditAnywhere, Category = "Transparency")
    EDWCTransparencySourceType CharacterStructureType = EDWCTransparencySourceType::SameMeshMaterialSlots;

    UPROPERTY()
    bool bCharacterStructureTypeConfigured = false;

    /** Legacy WCA-wide value retained only to migrate pre-v13 layers without changing their output. */
    UPROPERTY(meta = (DeprecatedProperty,
        DeprecationMessage = "Transparency resolution is authored per target layer."))
    int32 TransparencyBakeResolution = 1024;

    UPROPERTY(EditAnywhere, Category = "Transparency", meta = (ClampMin = "0", ClampMax = "64"))
    int32 TransparencyPaddingPixels = 8;

    UPROPERTY(EditAnywhere, Category = "Transparency", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0"))
    float TransparencyEdgeFeatherPixels = 4.0f;

    // Authoring strength baked into the final Transparency Map alpha. The legacy
    // property name is retained so existing WCA assets keep their authored value.
    UPROPERTY(EditAnywhere, Category = "Transparency Bake", meta = (DisplayName = "Transparency Strength", ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
    float TransparencyPreviewStrength = 0.4f;

    // Stage 3 correction strength baked into Corrected Reveal Color RGB.
    // Stage 2 source data remains immutable and runtime materials do not
    // evaluate metallic darkening dynamically.
    UPROPERTY(EditAnywhere, Category = "Transparency Reveal Correction", meta = (DisplayName = "Metallic Darkening", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float RevealMetallicDarkeningStrength = 0.25f;

    // Legacy spatial expansion settings. Suppression now follows the baked
    // wrinkle mask exactly and does not create coverage outside that mask.
    UPROPERTY(meta = (DeprecatedProperty))
    float WrinkleSuppressionRadiusPixels = 4.0f;

    UPROPERTY(meta = (DeprecatedProperty))
    float WrinkleSuppressionFeatherPixels = 16.0f;

    UPROPERTY(EditAnywhere, Category = "Transparency Bake|Wrinkle Suppression", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float WrinkleSuppressionCoverageThreshold = 0.15f;

    // Softens only the value transition inside the baked wrinkle mask. This is
    // not a spatial radius and never expands suppression into neighboring texels.
    UPROPERTY(EditAnywhere, Category = "Transparency Bake|Wrinkle Suppression", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.25"))
    float WrinkleSuppressionMaskSoftness = 0.05f;

    UPROPERTY(EditAnywhere, Category = "Transparency Bake|Wrinkle Suppression", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "5.0"))
    float WrinkleSuppressionStrength = 0.6f;

    // Legacy WCA-wide Type 2 source. Type 2 now stores its Blueprint binding
    // per target layer so separate garment slots can use distinct source sets.
    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the selected Transparency Layer's Blueprint Source settings."))
    TSoftClassPtr<AActor> SourceBlueprintClass;

    FWetClothingTransparencyLayerData* FindTransparencyLayer(int32 MaterialSlotIndex);
    const FWetClothingTransparencyLayerData* FindTransparencyLayer(int32 MaterialSlotIndex) const;

    const FWetClothingBakedTransparencyMap* FindBakedTransparencyMap(int32 MaterialSlotIndex) const;

    /** Runtime lookup rejects stale output and any required Reveal Surface payload that is incomplete. */
    const FWetClothingBakedTransparencyMap* FindRuntimeBakedTransparencyMap(int32 MaterialSlotIndex) const;

    UTexture2D* ResolveBakedTransparencyMap(int32 MaterialSlotIndex) const;

#if WITH_EDITORONLY_DATA
    /** Upgrades pre-intent layers without loading or deleting referenced assets. */
    bool NormalizeLegacyLayerIntents(
        const FString& AssetIdentity,
        int32& OutDraftCount,
        int32& OutRepairedIdentityCount);
#endif
};

class DWC_API FWetClothingTransparencyDataHelpers
{
  public:
    static bool ValidateTransparencyLayer(
        const USkeletalMesh* TargetMesh,
        const FWetClothingTransparencyLayerData& Layer,
        TArray<FString>& OutErrors,
        int32 DWCDataUVChannelIndex,
        bool bValidateRenderPayload = true);
};
