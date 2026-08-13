//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"

/**
 * Pure workflow policy shared by the Transparency panel and its regression
 * tests. Keeping this independent from Slate makes Stage 1/2/3 input routing
 * deterministic and prevents deferred widget refreshes from owning tool state.
 */
namespace DWCTransparencyWorkflow
{
    struct FDWCTransparencyTypeChangeImpact
    {
        int32 LayerCount = 0;
        int32 RevealStrokeCount = 0;
        int32 AlphaStrokeCount = 0;
        int32 GeneratedResultCount = 0;

        bool RequiresConfirmation() const
        {
            return LayerCount > 0 || GeneratedResultCount > 0;
        }
    };

    inline bool IsSourceTypeAvailable(const EDWCTransparencySourceType SourceType)
    {
        return SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots ||
            SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
            SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh ||
            SourceType == EDWCTransparencySourceType::ManualColorOrTexture;
    }

    inline bool CanContinueToGeneration(
        const bool bHasAsset,
        const bool bStructureTypeConfigured,
        const EDWCTransparencySourceType SourceType)
    {
        return bHasAsset && bStructureTypeConfigured && IsSourceTypeAvailable(SourceType);
    }

    inline bool IsCharacterTypeDraftDirty(
        const bool bSavedTypeConfigured,
        const EDWCTransparencySourceType SavedType,
        const bool bDraftTypeConfigured,
        const EDWCTransparencySourceType DraftType)
    {
        return bDraftTypeConfigured &&
            (!bSavedTypeConfigured || SavedType != DraftType);
    }

    inline void ApplyCharacterTypeCommit(
        FWetClothingTransparencyData& TransparencyData,
        const EDWCTransparencySourceType NewType)
    {
        TransparencyData.CharacterStructureType = NewType;
        TransparencyData.bCharacterStructureTypeConfigured = true;
        TransparencyData.SourceBlueprintClass.Reset();

        for (FWetClothingTransparencyLayerData& Layer : TransparencyData.TransparencyLayers)
        {
            Layer.SourceType = NewType;
            Layer.bSourceTypeConfigured = true;
            Layer.SameMeshSource = FWetClothingTransparencySameMeshSource{};
            Layer.BlueprintSource = FWetClothingTransparencyBlueprintSource{};
            Layer.ManualColorSource = FWetClothingTransparencyManualColorSource{};
            Layer.ExternalMeshSource = FWetClothingTransparencyExternalMeshSource{};
        }
    }

    struct FDWCTransparencyPreviewContext
    {
        EDWCTransparencyEditorStage Stage = EDWCTransparencyEditorStage::StructureSetup;
        EDWCTransparencyPaintTarget PaintTarget = EDWCTransparencyPaintTarget::None;
        EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
        EWetClothingTransparencyPreviewMode PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
        bool bUseRevealWorkingMap = false;
        bool bUseFinalWorkingMap = false;
        bool bEnableRevealColorPainting = false;
        bool bEnableFinalAlphaPainting = false;
    };

    inline EDWCTransparencyPaintTarget ResolvePaintTarget(
        const EDWCTransparencyEditorStage Stage,
        const EDWCTransparencySourceType)
    {
        // Every source type enters the same reveal-color authoring contract
        // after Stage 2 has produced a source working map.
        if (Stage == EDWCTransparencyEditorStage::RevealEditing)
        {
            return EDWCTransparencyPaintTarget::RevealColor;
        }

        return Stage == EDWCTransparencyEditorStage::FinalEditing
            ? EDWCTransparencyPaintTarget::FinalAlpha
            : EDWCTransparencyPaintTarget::None;
    }

    inline bool IsRaycastDiagnosticVisualization(
        const EDWCTransparencyVisualizationMode Mode)
    {
        return Mode == EDWCTransparencyVisualizationMode::ValidHit ||
            Mode == EDWCTransparencyVisualizationMode::HitDistance ||
            Mode == EDWCTransparencyVisualizationMode::SourcePriority ||
            Mode == EDWCTransparencyVisualizationMode::RaycastGaps;
    }

    inline bool IsVisualizationModeAllowed(
        const EDWCTransparencyEditorStage Stage,
        const EDWCTransparencySourceType SourceType,
        const EDWCTransparencyVisualizationMode Mode)
    {
        if (Stage == EDWCTransparencyEditorStage::RevealEditing)
        {
            if (Mode == EDWCTransparencyVisualizationMode::InnerColor ||
                Mode == EDWCTransparencyVisualizationMode::BaseRevealColor ||
                Mode == EDWCTransparencyVisualizationMode::CorrectionDifference)
            {
                return true;
            }
            return SourceType != EDWCTransparencySourceType::ManualColorOrTexture &&
                IsRaycastDiagnosticVisualization(Mode);
        }

        return Stage == EDWCTransparencyEditorStage::FinalEditing &&
            (Mode == EDWCTransparencyVisualizationMode::Final ||
             Mode == EDWCTransparencyVisualizationMode::AutoAlpha ||
             Mode == EDWCTransparencyVisualizationMode::WrinkleSeparation ||
             (SourceType != EDWCTransparencySourceType::ManualColorOrTexture &&
              (Mode == EDWCTransparencyVisualizationMode::RevealNormalOnly ||
               Mode == EDWCTransparencyVisualizationMode::RevealNormalTexture ||
               Mode == EDWCTransparencyVisualizationMode::SourceCoverage)));
    }

    inline EDWCTransparencyVisualizationMode ResolveVisualizationMode(
        const EDWCTransparencyEditorStage Stage,
        const EDWCTransparencySourceType SourceType,
        const EDWCTransparencyVisualizationMode RequestedMode)
    {
        if (IsVisualizationModeAllowed(Stage, SourceType, RequestedMode))
        {
            return RequestedMode;
        }
        return Stage == EDWCTransparencyEditorStage::RevealEditing
            ? EDWCTransparencyVisualizationMode::InnerColor
            : EDWCTransparencyVisualizationMode::Final;
    }

    inline FDWCTransparencyPreviewContext ResolvePreviewContext(
        const EDWCTransparencyEditorStage Stage,
        const EDWCTransparencySourceType SourceType,
        const EDWCTransparencyVisualizationMode RequestedVisualizationMode,
        const EWetClothingTransparencyPreviewMode RequestedPreviewMode,
        const bool bCanUseFullBlueprintPreview,
        const bool bHasRevealWorkingMap,
        const bool bHasFinalWorkingMap)
    {
        FDWCTransparencyPreviewContext Context;
        Context.Stage = Stage;
        Context.PaintTarget = ResolvePaintTarget(Stage, SourceType);
        Context.VisualizationMode = ResolveVisualizationMode(
            Stage,
            SourceType,
            RequestedVisualizationMode);
        Context.PreviewMode = RequestedPreviewMode;
        Context.bUseRevealWorkingMap =
            Context.PaintTarget == EDWCTransparencyPaintTarget::RevealColor &&
            bHasRevealWorkingMap;
        Context.bUseFinalWorkingMap =
            Context.PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha &&
            bHasFinalWorkingMap;
        Context.bEnableRevealColorPainting =
            Context.PaintTarget == EDWCTransparencyPaintTarget::RevealColor &&
            bHasRevealWorkingMap;
        Context.bEnableFinalAlphaPainting =
            Context.PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha &&
            bHasFinalWorkingMap;

        if (Context.PaintTarget == EDWCTransparencyPaintTarget::RevealColor)
        {
            Context.PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
        }
        else if (Context.PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha)
        {
            Context.PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
        }
        else if (!bCanUseFullBlueprintPreview)
        {
            Context.PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
        }
        else if (Stage == EDWCTransparencyEditorStage::MapGeneration &&
            (SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
                SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh))
        {
            Context.PreviewMode = EWetClothingTransparencyPreviewMode::FullBlueprint;
        }

        return Context;
    }
}
