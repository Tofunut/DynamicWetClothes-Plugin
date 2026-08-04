#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionReducer.h"

namespace
{
    bool AreBrushesEquivalent(const FWetWrinkleBrushSettings& A, const FWetWrinkleBrushSettings& B)
    {
        return A.ToolMode == B.ToolMode &&
            A.UVChannelIndex == B.UVChannelIndex &&
            A.MaterialSlotIndex == B.MaterialSlotIndex &&
            A.WrinkleNormalTexture == B.WrinkleNormalTexture &&
            FMath::IsNearlyEqual(A.BrushRadiusUV, B.BrushRadiusUV) &&
            FMath::IsNearlyEqual(A.Strength, B.Strength) &&
            FMath::IsNearlyEqual(A.Falloff, B.Falloff) &&
            FMath::IsNearlyEqual(A.RotationRadians, B.RotationRadians) &&
            FMath::IsNearlyEqual(A.PreviewWetness, B.PreviewWetness) &&
            A.RidgeShape == B.RidgeShape &&
            A.bFlipRidgeFoldSide == B.bFlipRidgeFoldSide &&
            FMath::IsNearlyEqual(A.RidgeStartTaper, B.RidgeStartTaper) &&
            FMath::IsNearlyEqual(A.RidgeEndTaper, B.RidgeEndTaper) &&
            FMath::IsNearlyEqual(A.RidgePointSpacingScale, B.RidgePointSpacingScale) &&
            FMath::IsNearlyEqual(A.RidgeFlareSettings.Length, B.RidgeFlareSettings.Length) &&
            FMath::IsNearlyEqual(A.RidgeFlareSettings.WidthScale, B.RidgeFlareSettings.WidthScale) &&
            FMath::IsNearlyEqual(A.RidgeFlareSettings.EndStrength, B.RidgeFlareSettings.EndStrength) &&
            FMath::IsNearlyEqual(A.RidgeFlareSettings.Softness, B.RidgeFlareSettings.Softness) &&
            A.RidgeNaturalVariation.bEnabled == B.RidgeNaturalVariation.bEnabled &&
            FMath::IsNearlyEqual(A.RidgeNaturalVariation.CenterlineAmount, B.RidgeNaturalVariation.CenterlineAmount) &&
            FMath::IsNearlyEqual(A.RidgeNaturalVariation.CenterlineFrequency, B.RidgeNaturalVariation.CenterlineFrequency) &&
            FMath::IsNearlyEqual(A.RidgeNaturalVariation.WidthVariation, B.RidgeNaturalVariation.WidthVariation) &&
            FMath::IsNearlyEqual(A.RidgeNaturalVariation.WidthFrequency, B.RidgeNaturalVariation.WidthFrequency) &&
            A.RidgeNaturalVariation.NoiseSeed == B.RidgeNaturalVariation.NoiseSeed &&
            A.RidgeEditMode == B.RidgeEditMode &&
            A.bRidgeJunctionModeEnabled == B.bRidgeJunctionModeEnabled &&
            A.bShowPreview == B.bShowPreview;
    }

    bool ArePaintSettingsEquivalent(
        const FDWCTransparencyPaintSettings& A,
        const FDWCTransparencyPaintSettings& B)
    {
        return A.Mode == B.Mode &&
            A.RevealColorMode == B.RevealColorMode &&
            FMath::IsNearlyEqual(A.RadiusUV, B.RadiusUV) &&
            FMath::IsNearlyEqual(A.Strength, B.Strength) &&
            FMath::IsNearlyEqual(A.Falloff, B.Falloff) &&
            FMath::IsNearlyEqual(A.Spacing, B.Spacing) &&
            FMath::IsNearlyEqual(A.TargetAlpha, B.TargetAlpha) &&
            A.bEnabled == B.bEnabled &&
            A.bRevealColorPaint == B.bRevealColorPaint &&
            A.RevealColor.Equals(B.RevealColor);
    }

    void NormalizeWrinkleBrush(FWetWrinkleBrushSettings& Brush)
    {
        Brush.BrushRadiusUV = FMath::Clamp(Brush.BrushRadiusUV, 0.001f, 0.5f);
        Brush.Strength = FMath::Clamp(Brush.Strength, 0.0f, 4.0f);
        Brush.Falloff = FMath::Clamp(Brush.Falloff, 0.0f, 1.0f);
        Brush.PreviewWetness = FMath::Clamp(Brush.PreviewWetness, 0.0f, 1.0f);
        Brush.RidgeStartTaper = FMath::Clamp(Brush.RidgeStartTaper, 0.0f, 0.5f);
        Brush.RidgeEndTaper = FMath::Clamp(Brush.RidgeEndTaper, 0.0f, 0.5f);
        Brush.RidgePointSpacingScale = FMath::Clamp(Brush.RidgePointSpacingScale, 0.05f, 2.0f);
    }

    void NormalizeTransparencyPaint(FDWCTransparencyPaintSettings& Paint)
    {
        Paint.RadiusUV = FMath::Clamp(Paint.RadiusUV, 0.001f, 0.5f);
        Paint.Strength = FMath::Clamp(Paint.Strength, 0.0f, 1.0f);
        Paint.Falloff = FMath::Clamp(Paint.Falloff, 0.0f, 1.0f);
        Paint.Spacing = FMath::Clamp(Paint.Spacing, 0.01f, 2.0f);
        Paint.TargetAlpha = FMath::Clamp(Paint.TargetAlpha, 0.0f, 1.0f);
    }

    EDWCEditorSessionEffect EffectsForAuthoringImpact(const EDWCEditorAuthoringImpact Impact)
    {
        EDWCEditorSessionEffect Effects = EDWCEditorSessionEffect::RefreshStatus;
        if (EnumHasAnyFlags(Impact, EDWCEditorAuthoringImpact::ElementList))
        {
            Effects |= EDWCEditorSessionEffect::RefreshElementList |
                EDWCEditorSessionEffect::RefreshUVView;
        }
        if (EnumHasAnyFlags(Impact, EDWCEditorAuthoringImpact::Preview))
        {
            Effects |= EDWCEditorSessionEffect::RebuildPreviewContent;
        }
        if (EnumHasAnyFlags(Impact, EDWCEditorAuthoringImpact::HitTopology))
        {
            Effects |= EDWCEditorSessionEffect::RebuildHitTopology;
        }
        if (EnumHasAnyFlags(Impact, EDWCEditorAuthoringImpact::Details))
        {
            Effects |= EDWCEditorSessionEffect::RefreshDetails;
        }
        return Effects;
    }
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCActivateEditorModeAction& Action)
{
    if (State.ActiveMode == Action.Mode)
    {
        return EDWCEditorSessionEffect::None;
    }
    State.ActiveMode = Action.Mode;
    return EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::SyncSelection |
        EDWCEditorSessionEffect::UpdatePreviewParameters;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCReconcileAuthoringAction& Action)
{
    State.AuthoringRevision = Action.AuthoringRevision;
    switch (Action.Domain)
    {
    case EDWCEditorAuthoringDomain::Wrinkle:
        State.WrinkleAuthoringRevision = Action.AuthoringRevision;
        break;
    case EDWCEditorAuthoringDomain::Transparency:
        State.TransparencyAuthoringRevision = Action.AuthoringRevision;
        break;
    default:
        State.WrinkleAuthoringRevision = Action.AuthoringRevision;
        State.TransparencyAuthoringRevision = Action.AuthoringRevision;
        break;
    }
    State.AuthoringIndex = Action.Index;
    for (auto It = State.Transparency.StageByLayer.CreateIterator(); It; ++It)
    {
        if (It.Key().IsValid() && !State.AuthoringIndex.TransparencyLayerGuids.Contains(It.Key()))
        {
            It.RemoveCurrent();
        }
    }

    EDWCEditorSessionEffect Effects = EffectsForAuthoringImpact(Action.Impact);
    if (State.Wrinkle.Brush.MaterialSlotIndex != INDEX_NONE &&
        !State.AuthoringIndex.WrinkleMaterialSlots.Contains(State.Wrinkle.Brush.MaterialSlotIndex))
    {
        State.Wrinkle.SelectedElementGuid.Invalidate();
        State.Wrinkle.SelectedRidgePointIndex = INDEX_NONE;
        Effects |= EDWCEditorSessionEffect::SyncSelection;
    }
    if (State.Wrinkle.SelectedElementGuid.IsValid() &&
        !State.AuthoringIndex.WrinkleElementGuids.Contains(State.Wrinkle.SelectedElementGuid))
    {
        State.Wrinkle.SelectedElementGuid.Invalidate();
        State.Wrinkle.SelectedRidgePointIndex = INDEX_NONE;
        Effects |= EDWCEditorSessionEffect::SyncSelection;
    }
    if (State.Transparency.SelectedLayerGuid.IsValid() &&
        !State.AuthoringIndex.TransparencyLayerGuids.Contains(State.Transparency.SelectedLayerGuid))
    {
        State.Transparency.SelectedLayerGuid.Invalidate();
        Effects |= EDWCEditorSessionEffect::SyncSelection |
            EDWCEditorSessionEffect::RefreshStageContent;
    }
    return Effects;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSetWrinkleBrushAction& Action)
{
    FWetWrinkleBrushSettings Brush = Action.Brush;
    NormalizeWrinkleBrush(Brush);
    const float SizeCm = FMath::Clamp(Action.BrushSizeCm, 0.1f, 100.0f);
    const float SizeUV = FMath::Clamp(Action.BrushSizeUV, 0.001f, 0.5f);
    if (AreBrushesEquivalent(State.Wrinkle.Brush, Brush) &&
        FMath::IsNearlyEqual(State.Wrinkle.BrushSizeCm, SizeCm) &&
        FMath::IsNearlyEqual(State.Wrinkle.BrushSizeUV, SizeUV))
    {
        return EDWCEditorSessionEffect::None;
    }
    State.Wrinkle.Brush = MoveTemp(Brush);
    State.Wrinkle.BrushSizeCm = SizeCm;
    State.Wrinkle.BrushSizeUV = SizeUV;
    return Action.Effects | EDWCEditorSessionEffect::SyncControls;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSelectWrinkleElementAction& Action)
{
    if (State.Wrinkle.SelectedElementGuid == Action.ElementGuid &&
        State.Wrinkle.SelectedElementType == Action.ElementType &&
        State.Wrinkle.SelectedRidgePointIndex == Action.RidgePointIndex)
    {
        return EDWCEditorSessionEffect::None;
    }
    State.Wrinkle.SelectedElementGuid = Action.ElementGuid;
    State.Wrinkle.SelectedElementType = Action.ElementType;
    State.Wrinkle.SelectedRidgePointIndex = Action.RidgePointIndex;
    return EDWCEditorSessionEffect::SyncSelection |
        EDWCEditorSessionEffect::UpdatePreviewParameters |
        EDWCEditorSessionEffect::RefreshDetails;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSetWrinkleCrossPreviewAction& Action)
{
    if (State.Wrinkle.bShowBakedTransparency == Action.bShowBakedTransparency)
    {
        return EDWCEditorSessionEffect::None;
    }
    State.Wrinkle.bShowBakedTransparency = Action.bShowBakedTransparency;
    return EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::UpdatePreviewParameters;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSelectTransparencyLayerAction& Action)
{
    if (State.Transparency.SelectedLayerGuid == Action.LayerGuid)
    {
        return EDWCEditorSessionEffect::None;
    }
    State.Transparency.SelectedLayerGuid = Action.LayerGuid;
    return EDWCEditorSessionEffect::SyncSelection |
        EDWCEditorSessionEffect::RebuildPreviewContent;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSetTransparencyStageAction& Action)
{
    EDWCTransparencyEditorStage& Stage = State.Transparency.StageByLayer.FindOrAdd(Action.LayerGuid);
    if (Stage == Action.Stage)
    {
        return EDWCEditorSessionEffect::None;
    }
    Stage = Action.Stage;
    return EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::RefreshStageContent |
        EDWCEditorSessionEffect::RebuildPreviewContent;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSetTransparencyPreviewAction& Action)
{
    FDWCEditorTransparencySessionState& Preview = State.Transparency;
    const float Wetness = FMath::Clamp(Action.WetnessPreviewPercent, 0.0f, 100.0f);
    const float Strength = FMath::Max(0.0f, Action.TransparencyPreviewStrength);
    const float Suppression = FMath::Clamp(Action.WrinkleSuppressionStrength, 0.0f, 5.0f);
    const bool bChanged = Preview.PreviewMode != Action.PreviewMode ||
        Preview.VisualizationMode != Action.VisualizationMode ||
        !FMath::IsNearlyEqual(Preview.WetnessPreviewPercent, Wetness) ||
        !FMath::IsNearlyEqual(Preview.TransparencyPreviewStrength, Strength) ||
        !FMath::IsNearlyEqual(Preview.WrinkleSuppressionStrength, Suppression) ||
        Preview.bShowSavedWrinkle != Action.bShowSavedWrinkle;
    if (!bChanged)
    {
        return EDWCEditorSessionEffect::None;
    }
    Preview.PreviewMode = Action.PreviewMode;
    Preview.VisualizationMode = Action.VisualizationMode;
    Preview.WetnessPreviewPercent = Wetness;
    Preview.TransparencyPreviewStrength = Strength;
    Preview.WrinkleSuppressionStrength = Suppression;
    Preview.bShowSavedWrinkle = Action.bShowSavedWrinkle;
    return EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::UpdatePreviewParameters;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSetTransparencyPaintAction& Action)
{
    FDWCTransparencyPaintSettings Paint = Action.Paint;
    NormalizeTransparencyPaint(Paint);
    if (Action.bRevealPaint)
    {
        // Reveal Color is a distinct authoring layer. Keep its invariants in
        // the reducer so panel, input, and viewport code cannot diverge.
        Paint.bRevealColorPaint = true;
        Paint.Spacing = 0.25f;
        Paint.TargetAlpha = 1.0f;
        Paint.RevealColor.A = 1.0f;
    }
    else
    {
        Paint.bRevealColorPaint = false;
    }
    FDWCTransparencyPaintSettings& Target = Action.bRevealPaint
        ? State.Transparency.RevealPaint
        : State.Transparency.Paint;
    if (ArePaintSettingsEquivalent(Target, Paint))
    {
        return EDWCEditorSessionEffect::None;
    }
    Target = MoveTemp(Paint);
    // Paint settings are consumed by the viewport.  Returning the preview
    // effect here keeps UI -> session -> viewport as the only update path.
    return Action.Effects |
        EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::UpdatePreviewParameters;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSetTransparencyEditContextAction& Action)
{
    const FDWCTransparencyEditContext& Current = State.Transparency.EditContext;
    const FDWCTransparencyEditContext& Next = Action.Context;
    if (Current.LayerGuid == Next.LayerGuid &&
        Current.MaterialSlotIndex == Next.MaterialSlotIndex &&
        Current.UVChannelIndex == Next.UVChannelIndex &&
        Current.AddressMode == Next.AddressMode &&
        Current.PaintTarget == Next.PaintTarget)
    {
        return EDWCEditorSessionEffect::None;
    }

    const bool bTopologyChanged =
        Current.MaterialSlotIndex != Next.MaterialSlotIndex ||
        Current.UVChannelIndex != Next.UVChannelIndex;
    State.Transparency.EditContext = Next;
    EDWCEditorSessionEffect Effects =
        EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::UpdatePreviewParameters;
    if (bTopologyChanged)
    {
        Effects |= EDWCEditorSessionEffect::RebuildHitTopology;
    }
    return Effects;
}
