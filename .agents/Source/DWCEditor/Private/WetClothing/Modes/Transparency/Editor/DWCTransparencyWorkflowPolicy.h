#pragma once

#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"

/**
 * Pure workflow policy shared by the Transparency panel and its regression
 * tests. Keeping this independent from Slate makes Stage 1/2/3 input routing
 * deterministic and prevents deferred widget refreshes from owning tool state.
 */
namespace DWCTransparencyWorkflow
{
    inline bool IsSourceTypeAvailable(const EDWCTransparencySourceType SourceType)
    {
        // The multi-component source path is present in the data model, but
        // its generation path is not implemented by the current editor.
        return SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents;
    }

    inline bool CanContinueToGeneration(
        const bool bHasAsset,
        const bool bStructureTypeConfigured,
        const EDWCTransparencySourceType SourceType)
    {
        return bHasAsset && bStructureTypeConfigured && IsSourceTypeAvailable(SourceType);
    }

    struct FDWCTransparencyPreviewContext
    {
        EDWCTransparencyEditorStage Stage = EDWCTransparencyEditorStage::StructureSetup;
        EDWCTransparencyPaintTarget PaintTarget = EDWCTransparencyPaintTarget::None;
        EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
        EWetClothingTransparencyPreviewMode PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
        bool bUseManualRevealWorkingMap = false;
        bool bUseFinalWorkingMap = false;
        bool bEnableRevealColorPainting = false;
        bool bEnableFinalAlphaPainting = false;
    };

    inline EDWCTransparencyPaintTarget ResolvePaintTarget(
        const EDWCTransparencyEditorStage Stage,
        const EDWCTransparencySourceType SourceType)
    {
        // Stage 2 manual-color authoring always owns a reveal-color target so
        // its surface hit/cursor context is available immediately. The
        // The separate bEnabled setting gates whether a left click writes a
        // stroke; it must not make hover and target preview disappear.
        if (Stage == EDWCTransparencyEditorStage::MapGeneration &&
            SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
        {
            return EDWCTransparencyPaintTarget::RevealColor;
        }

        return Stage == EDWCTransparencyEditorStage::FinalEditing
            ? EDWCTransparencyPaintTarget::FinalAlpha
            : EDWCTransparencyPaintTarget::None;
    }

    inline FDWCTransparencyPreviewContext ResolvePreviewContext(
        const EDWCTransparencyEditorStage Stage,
        const EDWCTransparencySourceType SourceType,
        const EDWCTransparencyVisualizationMode RequestedVisualizationMode,
        const EWetClothingTransparencyPreviewMode RequestedPreviewMode,
        const bool bRevealPaintEnabled,
        const bool bCanUseFullBlueprintPreview,
        const bool bHasManualRevealWorkingMap,
        const bool bHasFinalWorkingMap)
    {
        FDWCTransparencyPreviewContext Context;
        Context.Stage = Stage;
        Context.PaintTarget = ResolvePaintTarget(Stage, SourceType);
        Context.VisualizationMode = RequestedVisualizationMode;
        Context.PreviewMode = RequestedPreviewMode;
        Context.bUseManualRevealWorkingMap =
            Context.PaintTarget == EDWCTransparencyPaintTarget::RevealColor &&
            bHasManualRevealWorkingMap;
        Context.bUseFinalWorkingMap =
            Context.PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha &&
            bHasFinalWorkingMap;
        Context.bEnableRevealColorPainting =
            Context.PaintTarget == EDWCTransparencyPaintTarget::RevealColor &&
            bRevealPaintEnabled;
        Context.bEnableFinalAlphaPainting =
            Context.PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha &&
            bHasFinalWorkingMap;

        if (Context.PaintTarget == EDWCTransparencyPaintTarget::RevealColor)
        {
            Context.VisualizationMode = EDWCTransparencyVisualizationMode::InnerColor;
            Context.PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
        }
        else if (Context.PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha)
        {
            if (!bHasFinalWorkingMap)
            {
                Context.VisualizationMode = EDWCTransparencyVisualizationMode::Final;
            }
            Context.PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
        }
        else if (!bCanUseFullBlueprintPreview)
        {
            Context.PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
        }

        return Context;
    }
}
