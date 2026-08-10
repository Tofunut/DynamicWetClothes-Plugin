//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"

/**
 * Lightweight, metadata-only description of a Transparency Target Part's
 * persisted workflow state. This intentionally never loads Temp textures;
 * Stage 3/4 restore their working payload only after the author enters them.
 */
namespace DWCTransparencyWorkflow
{
    struct FDWCTransparencyLayerWorkflowState
    {
        EDWCTransparencyEditorStage DefaultStage = EDWCTransparencyEditorStage::StructureSetup;
        bool bStructureConfigured = false;
        bool bHasCanonicalSource = false;
        bool bHasReviewedReveal = false;
        bool bHasBakedBaseline = false;
        bool bRequiresSourceRegeneration = false;

        bool CanEnterMapGeneration() const
        {
            return bStructureConfigured;
        }

        bool CanEnterRevealEditing() const
        {
            return bHasCanonicalSource;
        }

        bool CanEnterFinalEditing() const
        {
            return bHasReviewedReveal || bHasBakedBaseline;
        }
    };

    inline bool HasCurrentArtifactReference(
        const FWetClothingTransparencyLayerData& Layer,
        const EDWCTransparencyTempArtifactKind Kind,
        const FString& ExpectedSignature)
    {
#if WITH_EDITORONLY_DATA
        if (ExpectedSignature.IsEmpty())
        {
            return false;
        }

        const FDWCTransparencyTempArtifactReference* Reference =
            Layer.EditorStageCache.Artifacts.FindByPredicate(
                [Kind](const FDWCTransparencyTempArtifactReference& Candidate)
                {
                    return Candidate.Kind == Kind;
                });
        return Reference != nullptr && !Reference->bObsolete &&
            Reference->BuildSignature == ExpectedSignature &&
            Reference->Resolution.X > 0 && Reference->Resolution.Y > 0 &&
            !Reference->Texture.IsNull();
#else
        return false;
#endif
    }

    inline FDWCTransparencyLayerWorkflowState ResolveLayerWorkflowState(
        const bool bCharacterStructureTypeConfigured,
        const FWetClothingTransparencyLayerData* Layer,
        const bool bHasBakedBaseline)
    {
        FDWCTransparencyLayerWorkflowState State;
        State.bStructureConfigured = bCharacterStructureTypeConfigured;
        State.bHasBakedBaseline = bHasBakedBaseline;
        if (!State.bStructureConfigured)
        {
            return State;
        }

        // A configured WCA without a selected target part should begin at
        // Stage 2, where the author chooses the target material slot.
        if (Layer == nullptr)
        {
            State.DefaultStage = EDWCTransparencyEditorStage::MapGeneration;
            return State;
        }

#if WITH_EDITORONLY_DATA
        const FDWCTransparencyEditorStageCacheMetadata& Cache = Layer->EditorStageCache;
        const bool bHasSourceMetadata = Cache.bSourceGenerated || !Cache.SourceSignature.IsEmpty();
        const FString& SourceSignature = Cache.SourceSignature;
        State.bHasCanonicalSource = Cache.bSourceGenerated &&
            HasCurrentArtifactReference(*Layer, EDWCTransparencyTempArtifactKind::BaseRevealColor, SourceSignature) &&
            HasCurrentArtifactReference(*Layer, EDWCTransparencyTempArtifactKind::ValidHit, SourceSignature) &&
            HasCurrentArtifactReference(*Layer, EDWCTransparencyTempArtifactKind::OuterCoverage, SourceSignature) &&
            HasCurrentArtifactReference(*Layer, EDWCTransparencyTempArtifactKind::OuterIslandID, SourceSignature) &&
            HasCurrentArtifactReference(*Layer, EDWCTransparencyTempArtifactKind::HitSource, SourceSignature) &&
            HasCurrentArtifactReference(*Layer, EDWCTransparencyTempArtifactKind::HitDistance, SourceSignature);
        State.bHasReviewedReveal = State.bHasCanonicalSource && Cache.bRevealReviewed &&
            HasCurrentArtifactReference(
                *Layer,
                EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
                Cache.RevealSignature);
        State.bRequiresSourceRegeneration = bHasSourceMetadata && !State.bHasCanonicalSource;
#endif

        if (State.bRequiresSourceRegeneration)
        {
            State.DefaultStage = EDWCTransparencyEditorStage::MapGeneration;
        }
        else if (State.bHasReviewedReveal)
        {
            State.DefaultStage = EDWCTransparencyEditorStage::FinalEditing;
        }
        else if (State.bHasCanonicalSource)
        {
            State.DefaultStage = EDWCTransparencyEditorStage::RevealEditing;
        }
        else if (State.bHasBakedBaseline)
        {
            // Older assets can contain a final map without persistent Stage 2
            // artifacts. Keep that result inspectable from Stage 4.
            State.DefaultStage = EDWCTransparencyEditorStage::FinalEditing;
        }
        else
        {
            State.DefaultStage = EDWCTransparencyEditorStage::MapGeneration;
        }
        return State;
    }

    inline EDWCTransparencyEditorStage NormalizeRequestedStage(
        const EDWCTransparencyEditorStage RequestedStage,
        const FDWCTransparencyLayerWorkflowState& State)
    {
        if (RequestedStage == EDWCTransparencyEditorStage::StructureSetup ||
            !State.bStructureConfigured)
        {
            return EDWCTransparencyEditorStage::StructureSetup;
        }
        if (RequestedStage == EDWCTransparencyEditorStage::MapGeneration)
        {
            return EDWCTransparencyEditorStage::MapGeneration;
        }
        if (RequestedStage == EDWCTransparencyEditorStage::RevealEditing &&
            State.CanEnterRevealEditing())
        {
            return RequestedStage;
        }
        if (RequestedStage == EDWCTransparencyEditorStage::FinalEditing &&
            State.CanEnterFinalEditing())
        {
            return RequestedStage;
        }
        return State.DefaultStage;
    }
}
