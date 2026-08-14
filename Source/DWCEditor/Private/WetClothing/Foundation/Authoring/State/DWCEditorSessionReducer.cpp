//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionReducer.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinkleBrushConstants.h"

bool FWetWrinkleBrushSettings::IsEquivalent(const FWetWrinkleBrushSettings& Other) const
{
    return ToolMode == Other.ToolMode &&
        UVChannelIndex == Other.UVChannelIndex &&
        MaterialSlotIndex == Other.MaterialSlotIndex &&
        WrinkleNormalTexture == Other.WrinkleNormalTexture &&
        PatchProjection.IsEquivalent(Other.PatchProjection) &&
        FMath::IsNearlyEqual(PatchDiameterLocal, Other.PatchDiameterLocal) &&
        FMath::IsNearlyEqual(BrushRadiusUV, Other.BrushRadiusUV) &&
        FMath::IsNearlyEqual(Strength, Other.Strength) &&
        FMath::IsNearlyEqual(Falloff, Other.Falloff) &&
        FMath::IsNearlyEqual(RotationRadians, Other.RotationRadians) &&
        FMath::IsNearlyEqual(PreviewWetness, Other.PreviewWetness) &&
        RidgeShape == Other.RidgeShape &&
        bFlipRidgeFoldSide == Other.bFlipRidgeFoldSide &&
        FMath::IsNearlyEqual(RidgeStartTaper, Other.RidgeStartTaper) &&
        FMath::IsNearlyEqual(RidgeEndTaper, Other.RidgeEndTaper) &&
        FMath::IsNearlyEqual(RidgePointSpacingScale, Other.RidgePointSpacingScale) &&
        FMath::IsNearlyEqual(RidgeFlareSettings.Length, Other.RidgeFlareSettings.Length) &&
        FMath::IsNearlyEqual(RidgeFlareSettings.WidthScale, Other.RidgeFlareSettings.WidthScale) &&
        FMath::IsNearlyEqual(RidgeFlareSettings.EndStrength, Other.RidgeFlareSettings.EndStrength) &&
        FMath::IsNearlyEqual(RidgeFlareSettings.Softness, Other.RidgeFlareSettings.Softness) &&
        RidgeNaturalVariation.bEnabled == Other.RidgeNaturalVariation.bEnabled &&
        FMath::IsNearlyEqual(RidgeNaturalVariation.CenterlineAmount, Other.RidgeNaturalVariation.CenterlineAmount) &&
        FMath::IsNearlyEqual(RidgeNaturalVariation.CenterlineFrequency, Other.RidgeNaturalVariation.CenterlineFrequency) &&
        FMath::IsNearlyEqual(RidgeNaturalVariation.WidthVariation, Other.RidgeNaturalVariation.WidthVariation) &&
        FMath::IsNearlyEqual(RidgeNaturalVariation.WidthFrequency, Other.RidgeNaturalVariation.WidthFrequency) &&
        RidgeNaturalVariation.NoiseSeed == Other.RidgeNaturalVariation.NoiseSeed &&
        RidgeEditMode == Other.RidgeEditMode &&
        bRidgeJunctionModeEnabled == Other.bRidgeJunctionModeEnabled &&
        bShowPreview == Other.bShowPreview;
}

namespace
{
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
        Brush.PatchDiameterLocal = FMath::Clamp(
            Brush.PatchDiameterLocal,
            0.1f,
            WetWrinkleBrushConstants::MaxSizeCm);
        Brush.BrushRadiusUV = FMath::Clamp(
            Brush.BrushRadiusUV,
            0.001f,
            WetWrinkleBrushConstants::MaxRadiusUV);
        Brush.PatchProjection.Normalize();
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

    void NormalizeTransparencyPreviewSettings(FDWCTransparencyPreviewSettings& Settings)
    {
        Settings.TransparencyStrength = FMath::Max(0.0f, Settings.TransparencyStrength);
        Settings.WrinkleSuppressionStrength =
            FMath::Clamp(Settings.WrinkleSuppressionStrength, 0.0f, 5.0f);
        Settings.WrinkleMaskThreshold = FMath::Clamp(Settings.WrinkleMaskThreshold, 0.0f, 1.0f);
        Settings.WrinkleMaskSoftness = FMath::Clamp(Settings.WrinkleMaskSoftness, 0.0f, 1.0f);
        Settings.RevealNormalStrength = FMath::Clamp(Settings.RevealNormalStrength, 0.0f, 4.0f);
    }

    bool AreTransparencyPreviewSettingsEquivalent(
        const FDWCTransparencyPreviewSettings& A,
        const FDWCTransparencyPreviewSettings& B)
    {
        return FMath::IsNearlyEqual(A.TransparencyStrength, B.TransparencyStrength) &&
            FMath::IsNearlyEqual(A.WrinkleSuppressionStrength, B.WrinkleSuppressionStrength) &&
            FMath::IsNearlyEqual(A.WrinkleMaskThreshold, B.WrinkleMaskThreshold) &&
            FMath::IsNearlyEqual(A.WrinkleMaskSoftness, B.WrinkleMaskSoftness) &&
            FMath::IsNearlyEqual(A.RevealNormalStrength, B.RevealNormalStrength) &&
            A.bShowRevealNormal == B.bShowRevealNormal &&
            A.RevealNormalSource == B.RevealNormalSource;
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
        if (EnumHasAnyFlags(Impact, EDWCEditorAuthoringImpact::PartSlotPresentation))
        {
            Effects |= EDWCEditorSessionEffect::RefreshPartSlotPresentation;
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
    const FGuid PreviousSelectedLayerGuid =
        State.AuthoringIndex.TransparencyLayerByMaterialSlot.FindRef(
            State.Transparency.SelectedMaterialSlotIndex);
    State.AuthoringRevision = Action.AuthoringRevision;
    switch (Action.Domain)
    {
    case EDWCEditorAuthoringDomain::Part:
        State.PartAuthoringRevision = Action.AuthoringRevision;
        State.LastPartImpactMaterialSlotIndex = Action.MaterialSlotIndex;
        State.LastPartImpactWetPartID = Action.WetPartID;
        break;
    case EDWCEditorAuthoringDomain::Wrinkle:
        State.WrinkleAuthoringRevision = Action.AuthoringRevision;
        break;
    case EDWCEditorAuthoringDomain::Transparency:
        State.TransparencyAuthoringRevision = Action.AuthoringRevision;
        break;
    default:
        State.PartAuthoringRevision = Action.AuthoringRevision;
        State.WrinkleAuthoringRevision = Action.AuthoringRevision;
        State.TransparencyAuthoringRevision = Action.AuthoringRevision;
        State.LastPartImpactMaterialSlotIndex = Action.MaterialSlotIndex;
        State.LastPartImpactWetPartID = Action.WetPartID;
        break;
    }
    State.AuthoringIndex = Action.Index;
    const FGuid ReconciledSelectedLayerGuid =
        State.AuthoringIndex.TransparencyLayerByMaterialSlot.FindRef(
            State.Transparency.SelectedMaterialSlotIndex);
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
    if (PreviousSelectedLayerGuid != ReconciledSelectedLayerGuid)
    {
        Effects |= EDWCEditorSessionEffect::SyncSelection |
            EDWCEditorSessionEffect::RefreshStageContent |
            EDWCEditorSessionEffect::RebuildPreviewContent;
    }
    return Effects;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSetWrinkleBrushAction& Action)
{
    FWetWrinkleBrushSettings Brush = Action.Brush;
    NormalizeWrinkleBrush(Brush);
    const float SizeCm = FMath::Clamp(
        Action.BrushSizeCm, 0.1f, WetWrinkleBrushConstants::MaxSizeCm);
    const float SizeUV = FMath::Clamp(
        Action.BrushSizeUV, 0.001f, WetWrinkleBrushConstants::MaxRadiusUV);
    if (State.Wrinkle.Brush.IsEquivalent(Brush) &&
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
    const FDWCSetWrinkleEditContextAction& Action)
{
    const int32 MaterialSlotIndex = Action.MaterialSlotIndex >= 0
        ? Action.MaterialSlotIndex
        : INDEX_NONE;
    const int32 UVChannelIndex = Action.UVChannelIndex >= 0
        ? Action.UVChannelIndex
        : INDEX_NONE;
    const bool bTopologyChanged =
        State.Wrinkle.Brush.MaterialSlotIndex != MaterialSlotIndex ||
        State.Wrinkle.Brush.UVChannelIndex != UVChannelIndex;
    const bool bSelectionChanged = Action.bClearElementSelection &&
        (State.Wrinkle.SelectedElementGuid.IsValid() ||
         State.Wrinkle.SelectedRidgePointIndex != INDEX_NONE);

    if (!bTopologyChanged && !bSelectionChanged)
    {
        return EDWCEditorSessionEffect::None;
    }

    EDWCEditorSessionEffect Effects = EDWCEditorSessionEffect::None;
    if (bTopologyChanged)
    {
        State.Wrinkle.Brush.MaterialSlotIndex = MaterialSlotIndex;
        State.Wrinkle.Brush.UVChannelIndex = UVChannelIndex;
        Effects |= EDWCEditorSessionEffect::SyncControls |
            EDWCEditorSessionEffect::RefreshElementList |
            EDWCEditorSessionEffect::RebuildHitTopology |
            EDWCEditorSessionEffect::RefreshUVView;
    }
    if (bSelectionChanged)
    {
        State.Wrinkle.SelectedElementGuid.Invalidate();
        State.Wrinkle.SelectedElementType = EWetWrinkleElementType::Patch;
        State.Wrinkle.SelectedRidgePointIndex = INDEX_NONE;
        Effects |= EDWCEditorSessionEffect::SyncSelection |
            EDWCEditorSessionEffect::RefreshDetails;
    }
    return Effects;
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
    const FDWCSelectTransparencyTargetSlotAction& Action)
{
    if (State.Transparency.SelectedMaterialSlotIndex == Action.MaterialSlotIndex)
    {
        return EDWCEditorSessionEffect::None;
    }
    State.Transparency.SelectedMaterialSlotIndex = Action.MaterialSlotIndex;
    return EDWCEditorSessionEffect::SyncSelection |
        EDWCEditorSessionEffect::RefreshStageContent |
        EDWCEditorSessionEffect::RebuildPreviewContent;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSelectTransparencyTargetSlotAndStageAction& Action)
{
    FDWCEditorTransparencySessionState& Transparency = State.Transparency;
    const FGuid LayerGuid = State.AuthoringIndex.TransparencyLayerByMaterialSlot.FindRef(
        Action.MaterialSlotIndex);
    const EDWCTransparencyEditorStage* ExistingStage =
        LayerGuid.IsValid() ? Transparency.StageByLayer.Find(LayerGuid) : nullptr;
    if (Transparency.SelectedMaterialSlotIndex == Action.MaterialSlotIndex &&
        (!LayerGuid.IsValid() || (ExistingStage != nullptr && *ExistingStage == Action.Stage)))
    {
        return EDWCEditorSessionEffect::None;
    }

    Transparency.SelectedMaterialSlotIndex = Action.MaterialSlotIndex;
    if (LayerGuid.IsValid())
    {
        Transparency.StageByLayer.FindOrAdd(LayerGuid) = Action.Stage;
    }
    return EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::SyncSelection |
        EDWCEditorSessionEffect::RefreshStageContent |
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
    const FDWCInitializeTransparencyCharacterTypeAction& Action)
{
    FDWCEditorTransparencySessionState& Transparency = State.Transparency;
    if (Transparency.bCharacterTypeStateInitialized)
    {
        return EDWCEditorSessionEffect::None;
    }

    Transparency.SavedCharacterType = Action.SavedType;
    Transparency.DraftCharacterType = Action.SavedType;
    Transparency.bSavedCharacterTypeConfigured = Action.bSavedTypeConfigured;
    Transparency.bDraftCharacterTypeConfigured = Action.bSavedTypeConfigured;
    Transparency.bCharacterTypeDraftDirty = false;
    Transparency.bCharacterTypeStateInitialized = true;
    return EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::RefreshStatus;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSelectTransparencyCharacterTypeDraftAction& Action)
{
    FDWCEditorTransparencySessionState& Transparency = State.Transparency;
    const bool bDirty = !Transparency.bSavedCharacterTypeConfigured ||
        Transparency.SavedCharacterType != Action.DraftType;
    if (Transparency.bDraftCharacterTypeConfigured &&
        Transparency.DraftCharacterType == Action.DraftType &&
        Transparency.bCharacterTypeDraftDirty == bDirty)
    {
        return EDWCEditorSessionEffect::None;
    }

    Transparency.DraftCharacterType = Action.DraftType;
    Transparency.bDraftCharacterTypeConfigured = true;
    Transparency.bCharacterTypeDraftDirty = bDirty;
    Transparency.bCharacterTypeStateInitialized = true;
    return EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::RefreshStatus;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCCancelTransparencyCharacterTypeDraftAction&)
{
    FDWCEditorTransparencySessionState& Transparency = State.Transparency;
    if (Transparency.DraftCharacterType == Transparency.SavedCharacterType &&
        Transparency.bDraftCharacterTypeConfigured == Transparency.bSavedCharacterTypeConfigured &&
        !Transparency.bCharacterTypeDraftDirty)
    {
        return EDWCEditorSessionEffect::None;
    }

    Transparency.DraftCharacterType = Transparency.SavedCharacterType;
    Transparency.bDraftCharacterTypeConfigured = Transparency.bSavedCharacterTypeConfigured;
    Transparency.bCharacterTypeDraftDirty = false;
    return EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::RefreshStatus;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCCommitTransparencyCharacterTypeSucceededAction& Action)
{
    FDWCEditorTransparencySessionState& Transparency = State.Transparency;
    Transparency.SavedCharacterType = Action.CommittedType;
    Transparency.DraftCharacterType = Action.CommittedType;
    Transparency.bSavedCharacterTypeConfigured = true;
    Transparency.bDraftCharacterTypeConfigured = true;
    Transparency.bCharacterTypeDraftDirty = false;
    Transparency.bCharacterTypeStateInitialized = true;
    return EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::RefreshStageContent |
        EDWCEditorSessionEffect::RefreshStatus;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCReconcileTransparencyCharacterTypeAction& Action)
{
    FDWCEditorTransparencySessionState& Transparency = State.Transparency;
    const bool bSavedChanged = !Transparency.bCharacterTypeStateInitialized ||
        Transparency.SavedCharacterType != Action.SavedType ||
        Transparency.bSavedCharacterTypeConfigured != Action.bSavedTypeConfigured;
    if (!bSavedChanged)
    {
        return EDWCEditorSessionEffect::None;
    }

    const bool bPreserveDraft = Transparency.bCharacterTypeStateInitialized &&
        Transparency.bCharacterTypeDraftDirty;
    Transparency.SavedCharacterType = Action.SavedType;
    Transparency.bSavedCharacterTypeConfigured = Action.bSavedTypeConfigured;
    Transparency.bCharacterTypeStateInitialized = true;
    if (!bPreserveDraft)
    {
        Transparency.DraftCharacterType = Action.SavedType;
        Transparency.bDraftCharacterTypeConfigured = Action.bSavedTypeConfigured;
        Transparency.bCharacterTypeDraftDirty = false;
    }
    else
    {
        Transparency.bCharacterTypeDraftDirty =
            !Action.bSavedTypeConfigured || Transparency.DraftCharacterType != Action.SavedType;
    }
    return EDWCEditorSessionEffect::SyncControls |
        EDWCEditorSessionEffect::RefreshStageContent |
        EDWCEditorSessionEffect::RefreshStatus;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCInitializeTransparencyPreviewSettingsAction& Action)
{
    FDWCEditorTransparencySessionState& Transparency = State.Transparency;
    if (Transparency.bPreviewSettingsInitialized && !Action.bForce)
    {
        return EDWCEditorSessionEffect::None;
    }

    FDWCTransparencyPreviewSettings Settings = Action.Settings;
    NormalizeTransparencyPreviewSettings(Settings);
    const bool bChanged = !Transparency.bPreviewSettingsInitialized ||
        !AreTransparencyPreviewSettingsEquivalent(Transparency.PreviewSettings, Settings);
    Transparency.PreviewSettings = Settings;
    Transparency.bPreviewSettingsInitialized = true;
    return bChanged
        ? EDWCEditorSessionEffect::SyncControls |
            EDWCEditorSessionEffect::UpdatePreviewParameters
        : EDWCEditorSessionEffect::None;
}

EDWCEditorSessionEffect FDWCEditorSessionReducer::Reduce(
    FDWCEditorSessionState& State,
    const FDWCSetTransparencyPreviewAction& Action)
{
    FDWCEditorTransparencySessionState& Preview = State.Transparency;
    const float Wetness = FMath::Clamp(Action.WetnessPreviewPercent, 0.0f, 100.0f);
    FDWCTransparencyPreviewSettings Settings = Action.Settings;
    NormalizeTransparencyPreviewSettings(Settings);
    const bool bChanged = Preview.PreviewMode != Action.PreviewMode ||
        Preview.VisualizationMode != Action.VisualizationMode ||
        (Action.Stage == EDWCTransparencyEditorStage::RevealEditing &&
            Preview.RevealVisualizationMode != Action.VisualizationMode) ||
        (Action.Stage == EDWCTransparencyEditorStage::FinalEditing &&
            Preview.FinalVisualizationMode != Action.VisualizationMode) ||
        !FMath::IsNearlyEqual(Preview.WetnessPreviewPercent, Wetness) ||
        !AreTransparencyPreviewSettingsEquivalent(Preview.PreviewSettings, Settings) ||
        Preview.bShowSavedWrinkle != Action.bShowSavedWrinkle;
    if (!bChanged)
    {
        return EDWCEditorSessionEffect::None;
    }
    Preview.PreviewMode = Action.PreviewMode;
    Preview.VisualizationMode = Action.VisualizationMode;
    if (Action.Stage == EDWCTransparencyEditorStage::RevealEditing)
    {
        Preview.RevealVisualizationMode = Action.VisualizationMode;
    }
    else if (Action.Stage == EDWCTransparencyEditorStage::FinalEditing)
    {
        Preview.FinalVisualizationMode = Action.VisualizationMode;
    }
    Preview.WetnessPreviewPercent = Wetness;
    Preview.PreviewSettings = Settings;
    Preview.bPreviewSettingsInitialized = true;
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
        Paint.bEnabled = true;
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
        Current.PaintTarget == Next.PaintTarget &&
        Current.bSurfacePaintingEnabled == Next.bSurfacePaintingEnabled)
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
