//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionTypes.h"
#include "WetClothing/WCAEditor/WCAEditorMode.h"

class UTexture2D;

enum class EWetWrinkleToolMode : uint8
{
    Patch,
    ProceduralRidgeStroke
};

enum class EWetProceduralRidgeEditMode : uint8
{
    Draw,
    Edit
};

enum class EWetWrinkleElementType : uint8
{
    Patch,
    ProceduralRidgeStroke
};

enum class EWetClothingTransparencyPreviewMode : uint8
{
    TargetMeshOnly,
    FullBlueprint
};

enum class EDWCTransparencyVisualizationMode : uint8
{
    Final,
    // Stage 3's live corrected Reveal Color working result.
    InnerColor,
    AutoAlpha,
    WrinkleSeparation,
    ValidHit,
    HitDistance,
    SourcePriority,
    // Keep new values appended: preview materials consume the legacy values.
    BaseRevealColor,
    CorrectionDifference,
    RaycastGaps,
    RevealNormalOnly,
    RevealNormalTexture,
    SourceCoverage
};

enum class EDWCTransparencyRevealNormalPreviewSource : uint8
{
    Working,
    Baked
};

enum class EDWCTransparencyEditorStage : uint8
{
    StructureSetup,
    MapGeneration,
    RevealEditing,
    FinalEditing
};

enum class EDWCTransparencyPaintTarget : uint8
{
    None,
    FinalAlpha,
    RevealColor
};

struct FDWCTransparencyEditContext
{
    FGuid LayerGuid;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    EDWCTransparencyUVAddressMode AddressMode = EDWCTransparencyUVAddressMode::Clamp;
    EDWCTransparencyPaintTarget PaintTarget = EDWCTransparencyPaintTarget::None;
    // Derived from the active stage's working-map availability. This is not
    // an author-facing toggle: input becomes available as soon as its source
    // map is ready and is disabled again when that map is unavailable.
    bool bSurfacePaintingEnabled = false;
};

struct FWetWrinkleBrushSettings
{
    FWetWrinkleBrushSettings()
    {
        RidgeNaturalVariation.bEnabled = true;
    }

    EWetWrinkleToolMode ToolMode = EWetWrinkleToolMode::Patch;
    int32 UVChannelIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    TObjectPtr<UTexture2D> WrinkleNormalTexture = nullptr;
    FDWCEditorSurfacePatchProjectionSettings PatchProjection;
    // Full physical patch diameter in local mesh units (centimeters for the
    // standard Unreal asset scale). BrushRadiusUV remains ridge/UV UI state.
    float PatchDiameterLocal = 8.0f;
    float BrushRadiusUV = 0.025f;
    float Strength = 1.0f;
    float Falloff = 0.5f;
    float RotationRadians = 0.0f;
    float PreviewWetness = 1.0f;
    EWetProceduralRidgeShape RidgeShape = EWetProceduralRidgeShape::Convex;
    bool bFlipRidgeFoldSide = false;
    float RidgeStartTaper = 0.15f;
    float RidgeEndTaper = 0.15f;
    float RidgePointSpacingScale = 0.25f;
    FWetProceduralRidgeFlareSettings RidgeFlareSettings;
    FWetProceduralRidgeVariationSettings RidgeNaturalVariation;
    EWetProceduralRidgeEditMode RidgeEditMode = EWetProceduralRidgeEditMode::Draw;
    bool bRidgeJunctionModeEnabled = true;
    bool bShowPreview = true;

    bool IsEquivalent(const FWetWrinkleBrushSettings& Other) const;
};

struct FDWCTransparencyPaintSettings
{
    EDWCTransparencyBrushMode Mode = EDWCTransparencyBrushMode::Apply;
    EDWCTransparencyRevealColorBrushMode RevealColorMode = EDWCTransparencyRevealColorBrushMode::Paint;
    float RadiusUV = 0.0677f;
    float Strength = 0.5f;
    float Falloff = 0.5f;
    float Spacing = 0.25f;
    float TargetAlpha = 1.0f;
    bool bEnabled = true;
    bool bRevealColorPaint = false;
    FLinearColor RevealColor = FLinearColor::White;
};

enum class EDWCEditorSessionEffect : uint32
{
    None = 0,
    SyncControls = 1 << 0,
    SyncSelection = 1 << 1,
    RefreshElementList = 1 << 2,
    RefreshStageContent = 1 << 3,
    UpdatePreviewParameters = 1 << 4,
    RebuildPreviewContent = 1 << 5,
    RebuildPreviewMaterials = 1 << 6,
    RebuildHitTopology = 1 << 7,
    RefreshUVView = 1 << 8,
    RefreshDetails = 1 << 9,
    RefreshStatus = 1 << 10,
    RefreshPartSlotPresentation = 1 << 11
};
ENUM_CLASS_FLAGS(EDWCEditorSessionEffect);

struct FDWCEditorAuthoringIndex
{
    TSet<int32> WrinkleMaterialSlots;
    TSet<FGuid> WrinkleElementGuids;
    TSet<FGuid> TransparencyLayerGuids;
    TMap<int32, FGuid> TransparencyLayerByMaterialSlot;
};

struct FDWCEditorWrinkleSessionState
{
    FWetWrinkleBrushSettings Brush;
    float BrushSizeCm = 8.0f;
    float BrushSizeUV = 0.0677f;
    bool bShowBakedTransparency = true;
    FGuid SelectedElementGuid;
    EWetWrinkleElementType SelectedElementType = EWetWrinkleElementType::Patch;
    int32 SelectedRidgePointIndex = INDEX_NONE;
};

/**
 * Transient editor values used to preview the authored transparency result.
 * The WCA stores committed defaults; this snapshot owns the values currently
 * being edited so an authoring notification cannot restore stale asset data.
 */
struct FDWCTransparencyPreviewSettings
{
    float TransparencyStrength = 0.4f;
    float WrinkleSuppressionStrength = 0.6f;
    float WrinkleMaskThreshold = 0.15f;
    float WrinkleMaskSoftness = 0.05f;
    float RevealNormalStrength = 1.0f;
    bool bShowRevealNormal = true;
    EDWCTransparencyRevealNormalPreviewSource RevealNormalSource =
        EDWCTransparencyRevealNormalPreviewSource::Working;
};

struct FDWCEditorTransparencySessionState
{
    FDWCEditorTransparencySessionState()
    {
        RevealPaint.bEnabled = true;
        RevealPaint.bRevealColorPaint = true;
        RevealPaint.Strength = 1.0f;
    }

    /** Selected target slot exists independently from an authored transparency layer. */
    int32 SelectedMaterialSlotIndex = INDEX_NONE;
    FDWCTransparencyEditContext EditContext;
    TMap<FGuid, EDWCTransparencyEditorStage> StageByLayer;
    EDWCTransparencySourceType SavedCharacterType =
        EDWCTransparencySourceType::SameMeshMaterialSlots;
    EDWCTransparencySourceType DraftCharacterType =
        EDWCTransparencySourceType::SameMeshMaterialSlots;
    bool bSavedCharacterTypeConfigured = false;
    bool bDraftCharacterTypeConfigured = false;
    bool bCharacterTypeDraftDirty = false;
    bool bCharacterTypeStateInitialized = false;
    EWetClothingTransparencyPreviewMode PreviewMode =
        EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    EDWCTransparencyVisualizationMode VisualizationMode =
        EDWCTransparencyVisualizationMode::Final;
    // Stage-local selections prevent Stage 3 diagnostics from replacing the
    // Stage 4 final-alpha view when the author moves between the stages.
    EDWCTransparencyVisualizationMode RevealVisualizationMode =
        EDWCTransparencyVisualizationMode::InnerColor;
    EDWCTransparencyVisualizationMode FinalVisualizationMode =
        EDWCTransparencyVisualizationMode::Final;
    float WetnessPreviewPercent = 100.0f;
    FDWCTransparencyPreviewSettings PreviewSettings;
    bool bPreviewSettingsInitialized = false;
    bool bShowSavedWrinkle = true;
    FDWCTransparencyPaintSettings Paint;
    FDWCTransparencyPaintSettings RevealPaint;
};

struct FDWCEditorSessionState
{
    EWCAEditorMode ActiveMode = EWCAEditorMode::PartEdit;
    uint64 SessionRevision = 0;
    uint64 AuthoringRevision = 0;
    uint64 PartAuthoringRevision = 0;
    uint64 WrinkleAuthoringRevision = 0;
    uint64 TransparencyAuthoringRevision = 0;
    int32 LastPartImpactMaterialSlotIndex = INDEX_NONE;
    int32 LastPartImpactWetPartID = INDEX_NONE;
    FDWCEditorAuthoringIndex AuthoringIndex;
    FDWCEditorWrinkleSessionState Wrinkle;
    FDWCEditorTransparencySessionState Transparency;
};

inline uint64 GetDWCEditorDomainRevision(
    const FDWCEditorSessionState& State,
    const EDWCEditorAuthoringDomain Domain)
{
    switch (Domain)
    {
    case EDWCEditorAuthoringDomain::Part:
        return State.PartAuthoringRevision;
    case EDWCEditorAuthoringDomain::Wrinkle:
        return State.WrinkleAuthoringRevision;
    case EDWCEditorAuthoringDomain::Transparency:
        return State.TransparencyAuthoringRevision;
    default:
        return State.AuthoringRevision;
    }
}
