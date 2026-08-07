//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"

struct FDWCActivateEditorModeAction
{
    EWCAEditorMode Mode = EWCAEditorMode::PartEdit;
};

struct FDWCReconcileAuthoringAction
{
    uint64 AuthoringRevision = 0;
    EDWCEditorAuthoringDomain Domain = EDWCEditorAuthoringDomain::None;
    FDWCEditorAuthoringIndex Index;
    EDWCEditorAuthoringImpact Impact = EDWCEditorAuthoringImpact::None;
};

struct FDWCSetWrinkleBrushAction
{
    FWetWrinkleBrushSettings Brush;
    float BrushSizeCm = 8.0f;
    float BrushSizeUV = 0.0677f;
    EDWCEditorSessionEffect Effects = EDWCEditorSessionEffect::UpdatePreviewParameters;
};

struct FDWCSelectWrinkleElementAction
{
    FGuid ElementGuid;
    EWetWrinkleElementType ElementType = EWetWrinkleElementType::Patch;
    int32 RidgePointIndex = INDEX_NONE;
};

struct FDWCSetWrinkleCrossPreviewAction
{
    bool bShowBakedTransparency = true;
};

struct FDWCSelectTransparencyLayerAction
{
    FGuid LayerGuid;
};

struct FDWCSetTransparencyStageAction
{
    FGuid LayerGuid;
    EDWCTransparencyEditorStage Stage = EDWCTransparencyEditorStage::StructureSetup;
};

struct FDWCSetTransparencyPreviewAction
{
    EWetClothingTransparencyPreviewMode PreviewMode =
        EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    EDWCTransparencyVisualizationMode VisualizationMode =
        EDWCTransparencyVisualizationMode::Final;
    float WetnessPreviewPercent = 100.0f;
    FDWCTransparencyPreviewSettings Settings;
    bool bShowSavedWrinkle = true;
};

struct FDWCInitializeTransparencyPreviewSettingsAction
{
    FDWCTransparencyPreviewSettings Settings;
    bool bForce = false;
};

struct FDWCSetTransparencyPaintAction
{
    FDWCTransparencyPaintSettings Paint;
    bool bRevealPaint = false;
    EDWCEditorSessionEffect Effects = EDWCEditorSessionEffect::UpdatePreviewParameters;
};

struct FDWCSetTransparencyEditContextAction
{
    FDWCTransparencyEditContext Context;
};
