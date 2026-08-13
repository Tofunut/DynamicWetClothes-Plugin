// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/Authoring/DWCTransparencyAuthoringController.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Modes/Transparency/Viewport/SWetClothingTransparencyPreviewViewport.h"

#define LOCTEXT_NAMESPACE "DWCTransparencyAuthoringController"

namespace
{
    const TCHAR* GetBrushModeLabel(const EDWCTransparencyBrushMode Mode)
    {
        switch (Mode)
        {
        case EDWCTransparencyBrushMode::Erase:
            return TEXT("Erase");
        case EDWCTransparencyBrushMode::SetValue:
            return TEXT("Set Value");
        case EDWCTransparencyBrushMode::Smooth:
            return TEXT("Smooth");
        case EDWCTransparencyBrushMode::ResetToAuto:
            return TEXT("Reset To Auto");
        default:
            return TEXT("Apply");
        }
    }

    bool AreContextsEqual(
        const FDWCTransparencyEditContext& A,
        const FDWCTransparencyEditContext& B)
    {
        return A.LayerGuid == B.LayerGuid &&
               A.MaterialSlotIndex == B.MaterialSlotIndex &&
               A.UVChannelIndex == B.UVChannelIndex &&
               A.AddressMode == B.AddressMode &&
               A.PaintTarget == B.PaintTarget &&
               A.bSurfacePaintingEnabled == B.bSurfacePaintingEnabled;
    }

    EDWCEditorAuthoringImpact GetInteractiveImpact(const EDWCTransparencyPaintTarget Target)
    {
        return EDWCEditorAuthoringImpact::AssetDirty |
               EDWCEditorAuthoringImpact::PreviewIncremental |
               (Target == EDWCTransparencyPaintTarget::RevealColor
                    ? EDWCEditorAuthoringImpact::TransparencyAutoBake
                    : EDWCEditorAuthoringImpact::TransparencyFinalBake);
    }
} // namespace

FDWCTransparencyAuthoringController::FDWCTransparencyAuthoringController(
    UWetClothingAsset*                      InAsset,
    TSharedPtr<FDWCEditorAuthoringDocument> InAuthoringDocument,
    TSharedPtr<FDWCEditorSessionStore>      InSessionStore)
    : Asset(InAsset), AuthoringDocument(MoveTemp(InAuthoringDocument)), SessionStore(MoveTemp(InSessionStore))
{
}

FDWCTransparencyAuthoringController::~FDWCTransparencyAuthoringController()
{
    CancelActiveInteraction(false);
    DetachViewport();
}

void FDWCTransparencyAuthoringController::AttachViewport(
    const TSharedPtr<SWetClothingTransparencyPreviewViewport>& InViewport)
{
    if (Viewport.Pin() == InViewport)
    {
        return;
    }
    // An in-flight stroke must never be handed to a replacement viewport.
    // The caller can reattach repeatedly during a normal refresh without
    // touching the interaction because the identity check above is cheap.
    CancelActiveInteraction(false);
    Viewport = InViewport;
}

void FDWCTransparencyAuthoringController::DetachViewport()
{
    Viewport.Reset();
}

const FDWCEditorTransparencySessionState& FDWCTransparencyAuthoringController::GetTransparencyState() const
{
    static const FDWCEditorTransparencySessionState EmptyState;
    return SessionStore.IsValid() ? SessionStore->GetState().Transparency : EmptyState;
}

void FDWCTransparencyAuthoringController::HandleSessionStateChanged(
    const FDWCEditorSessionState& State)
{
    if (IsInteracting() && !AreContextsEqual(ActiveContext, State.Transparency.EditContext))
    {
        CancelActiveInteraction(true);
    }
}

bool FDWCTransparencyAuthoringController::CanBeginSurfaceInteraction(
    const FDWCEditorSurfaceHit& SurfaceHit) const
{
    const FDWCEditorTransparencySessionState& State = GetTransparencyState();
    const FDWCTransparencyEditContext&        Context = State.EditContext;
    const FDWCTransparencyPaintSettings&      Paint =
        Context.PaintTarget == EDWCTransparencyPaintTarget::RevealColor
                 ? State.RevealPaint
                 : State.Paint;
    const bool bPaintSettingsEnabled =
        Context.PaintTarget == EDWCTransparencyPaintTarget::RevealColor || Paint.bEnabled;
    return Asset.IsValid() && AuthoringDocument.IsValid() && SurfaceHit.bHit &&
           Context.PaintTarget != EDWCTransparencyPaintTarget::None &&
           Context.bSurfacePaintingEnabled && bPaintSettingsEnabled &&
           Context.LayerGuid.IsValid() && Context.MaterialSlotIndex != INDEX_NONE &&
           Context.UVChannelIndex != INDEX_NONE &&
           SurfaceHit.MaterialSlotIndex == Context.MaterialSlotIndex &&
           SurfaceHit.UVChannelIndex == Context.UVChannelIndex;
}

void FDWCTransparencyAuthoringController::HandleSurfaceHitChanged(const FDWCEditorSurfaceHit&)
{
}

void FDWCTransparencyAuthoringController::BeginSurfaceInteraction(
    const FDWCEditorSurfaceHit& SurfaceHit)
{
    if (!CanBeginSurfaceInteraction(SurfaceHit) || IsInteracting())
    {
        return;
    }

    const FDWCEditorTransparencySessionState& State = GetTransparencyState();
    ActiveContext = State.EditContext;
    ActivePaintSettings = ActiveContext.PaintTarget == EDWCTransparencyPaintTarget::RevealColor
                              ? State.RevealPaint
                              : State.Paint;
    ActiveStrokeGuid = FGuid::NewGuid();
    bCommitMutationApplied = false;

    if (ActiveContext.PaintTarget == EDWCTransparencyPaintTarget::RevealColor)
    {
        ActiveRevealColorStroke.Emplace();
        FDWCTransparencyRevealColorStroke& Stroke = ActiveRevealColorStroke.GetValue();
        Stroke.StrokeGuid = ActiveStrokeGuid;
        Stroke.MaterialSlotIndex = ActiveContext.MaterialSlotIndex;
        Stroke.UVAddressMode = ActiveContext.AddressMode;
        Stroke.PaintColor = ActivePaintSettings.RevealColor;
        Stroke.BrushMode = ActivePaintSettings.RevealColorMode;
        Stroke.Falloff = ActivePaintSettings.Falloff;
        Stroke.Spacing = ActivePaintSettings.Spacing;
    }
    else
    {
        ActiveBrushStroke.Emplace();
        FDWCTransparencyBrushStroke& Stroke = ActiveBrushStroke.GetValue();
        Stroke.StrokeGuid = ActiveStrokeGuid;
        Stroke.MaterialSlotIndex = ActiveContext.MaterialSlotIndex;
        Stroke.UVAddressMode = ActiveContext.AddressMode;
        Stroke.BrushMode = ActivePaintSettings.Mode;
        Stroke.Falloff = ActivePaintSettings.Falloff;
        Stroke.TargetAlpha = ActivePaintSettings.TargetAlpha;
        Stroke.Spacing = ActivePaintSettings.Spacing;

        if (const UWetClothingAsset* CurrentAsset = Asset.Get())
        {
            if (const FWetClothingTransparencyLayerData* Layer =
                    CurrentAsset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                        [LayerGuid = ActiveContext.LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                        {
                            return Candidate.LayerGuid == LayerGuid;
                        }))
            {
                int32 ModeStrokeNumber = 1;
                for (const FDWCTransparencyBrushStroke& Candidate : Layer->GetEditableStrokes())
                {
                    ModeStrokeNumber += Candidate.BrushMode == Stroke.BrushMode ? 1 : 0;
                }
                Stroke.DisplayName = FString::Printf(TEXT("%s %d"), GetBrushModeLabel(Stroke.BrushMode), ModeStrokeNumber);
            }
        }
    }

    LastPointerUV = SurfaceHit.UV;
    LastPointerUVIslandID = SurfaceHit.UVIslandID;
    DistanceToNextStamp = FMath::Max(ActivePaintSettings.RadiusUV * ActivePaintSettings.Spacing, 0.00001f);
    AppendPaintSample(SurfaceHit.UV, SurfaceHit.UVIslandID);
}

void FDWCTransparencyAuthoringController::UpdateSurfaceInteraction(
    const FDWCEditorSurfaceHit& SurfaceHit)
{
    if (!IsInteracting() || !SurfaceHit.bHit ||
        SurfaceHit.MaterialSlotIndex != ActiveContext.MaterialSlotIndex ||
        SurfaceHit.UVChannelIndex != ActiveContext.UVChannelIndex)
    {
        return;
    }

    FVector2D Delta = SurfaceHit.UV - LastPointerUV;
    if (ActiveContext.AddressMode == EDWCTransparencyUVAddressMode::Wrap)
    {
        Delta.X -= FMath::RoundToDouble(Delta.X);
        Delta.Y -= FMath::RoundToDouble(Delta.Y);
    }
    const float MaximumContinuousUVDistance =
        FMath::Clamp(ActivePaintSettings.RadiusUV * 4.0f, 0.03f, 0.25f);
    if (SurfaceHit.UVIslandID != LastPointerUVIslandID || Delta.Size() > MaximumContinuousUVDistance)
    {
        LastPointerUV = SurfaceHit.UV;
        LastPointerUVIslandID = SurfaceHit.UVIslandID;
        DistanceToNextStamp = FMath::Max(ActivePaintSettings.RadiusUV * ActivePaintSettings.Spacing, 0.00001f);
        AppendPaintSample(SurfaceHit.UV, SurfaceHit.UVIslandID);
        return;
    }

    float           RemainingDistance = static_cast<float>(Delta.Size());
    FVector2D       SegmentStart = LastPointerUV;
    const FVector2D Direction = RemainingDistance > UE_SMALL_NUMBER
                                    ? Delta / RemainingDistance
                                    : FVector2D::ZeroVector;
    while (RemainingDistance + UE_SMALL_NUMBER >= DistanceToNextStamp)
    {
        SegmentStart += Direction * DistanceToNextStamp;
        if (ActiveContext.AddressMode == EDWCTransparencyUVAddressMode::Wrap)
        {
            SegmentStart.X -= FMath::FloorToDouble(SegmentStart.X);
            SegmentStart.Y -= FMath::FloorToDouble(SegmentStart.Y);
        }
        AppendPaintSample(SegmentStart, LastPointerUVIslandID);
        RemainingDistance -= DistanceToNextStamp;
        DistanceToNextStamp = FMath::Max(ActivePaintSettings.RadiusUV * ActivePaintSettings.Spacing, 0.00001f);
    }
    DistanceToNextStamp -= RemainingDistance;
    LastPointerUV = SurfaceHit.UV;
    LastPointerUVIslandID = SurfaceHit.UVIslandID;
}

bool FDWCTransparencyAuthoringController::AppendPaintSample(
    const FVector2D& PositionUV,
    const int32      UVIslandID)
{
    if (!IsInteracting() || !AuthoringDocument.IsValid())
    {
        return false;
    }

    FDWCTransparencyBrushSample Sample;
    Sample.PositionUV = PositionUV;
    Sample.RadiusUV = ActivePaintSettings.RadiusUV;
    Sample.Strength = ActivePaintSettings.Strength;
    Sample.UVIslandID = UVIslandID;

    if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> PinnedViewport = Viewport.Pin())
    {
        if (ActiveRevealColorStroke.IsSet())
        {
            ActiveRevealColorStroke->Samples.Add(Sample);
            PinnedViewport->ApplyAuthoringRevealColorSample(ActiveRevealColorStroke.GetValue(), Sample);
        }
        else if (ActiveBrushStroke.IsSet())
        {
            ActiveBrushStroke->Samples.Add(Sample);
            PinnedViewport->ApplyAuthoringBrushSample(ActiveBrushStroke.GetValue(), Sample);
        }
    }
    else if (ActiveRevealColorStroke.IsSet())
    {
        ActiveRevealColorStroke->Samples.Add(Sample);
    }
    else if (ActiveBrushStroke.IsSet())
    {
        ActiveBrushStroke->Samples.Add(Sample);
    }
    return ActiveRevealColorStroke.IsSet() || ActiveBrushStroke.IsSet();
}

void FDWCTransparencyAuthoringController::EndSurfaceInteraction()
{
    if (!IsInteracting())
    {
        return;
    }
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::ElementList |
                    EDWCEditorAuthoringImpact::PreviewIncremental;
    Change.MaterialSlotIndex = ActiveContext.MaterialSlotIndex;
    Change.LayerGuid = ActiveContext.LayerGuid;
    Change.ElementGuid = ActiveStrokeGuid;
    Change.Impact |= GetInteractiveImpact(ActiveContext.PaintTarget);
    const FGuid                                  LayerGuid = ActiveContext.LayerGuid;
    TOptional<FDWCTransparencyBrushStroke>       BrushStroke = MoveTemp(ActiveBrushStroke);
    TOptional<FDWCTransparencyRevealColorStroke> RevealStroke = MoveTemp(ActiveRevealColorStroke);
    if ((!BrushStroke.IsSet() || !BrushStroke->HasSamples()) &&
        (!RevealStroke.IsSet() || !RevealStroke->HasSamples()))
    {
        CancelActiveInteraction(true);
        return;
    }

    if (BrushStroke.IsSet())
    {
        BrushStroke->CompactLegacySamples();
    }
    if (RevealStroke.IsSet())
    {
        RevealStroke->CompactLegacySamples();
    }

    UWetClothingAsset* MutableAsset = Asset.Get();
    UDWCTransparencyLayerStrokeHistory* StrokeHistory = MutableAsset != nullptr
        ? MutableAsset->EnsureTransparencyLayerStrokeHistory(LayerGuid)
        : nullptr;
    if (StrokeHistory == nullptr)
    {
        CancelActiveInteraction(true);
        return;
    }

    if (!AuthoringDocument->BeginInteractiveEdit(
            ActiveContext.PaintTarget == EDWCTransparencyPaintTarget::RevealColor
                ? LOCTEXT("PaintRevealColorStroke", "Paint Reveal Color")
                : LOCTEXT("PaintTransparencyStroke", "Paint Transparency Stroke"),
            Change,
            StrokeHistory))
    {
        CancelActiveInteraction(true);
        return;
    }

    const FDWCEditorAuthoringResult UpdateResult = AuthoringDocument->UpdateInteractiveEdit(
        Change,
        [LayerGuid, BrushStroke = MoveTemp(BrushStroke), RevealStroke = MoveTemp(RevealStroke)](
            UWetClothingAsset& MutableAsset) mutable
        {
            FWetClothingTransparencyLayerData* Layer =
                MutableAsset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            if (Layer == nullptr)
            {
                return false;
            }
            if (RevealStroke.IsSet())
            {
                Layer->GetMutableRevealColorPaintStrokes().Add(MoveTemp(RevealStroke.GetValue()));
                return true;
            }
            if (BrushStroke.IsSet())
            {
                Layer->GetMutableEditableStrokes().Add(MoveTemp(BrushStroke.GetValue()));
                return true;
            }
            return false;
        });
    if (!UpdateResult.bChanged)
    {
        CancelActiveInteraction(true);
        return;
    }
    bCommitMutationApplied = true;
    if (!AuthoringDocument->CommitInteractiveEdit(Change).bChanged)
    {
        CancelActiveInteraction(true);
        return;
    }

    const EDWCTransparencyPaintTarget CommittedPaintTarget = ActiveContext.PaintTarget;
    ResetInteractionState();
    if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->CommitAuthoringPreviewUpdate(CommittedPaintTarget);
    }
}

void FDWCTransparencyAuthoringController::CancelSurfaceInteraction()
{
    CancelActiveInteraction(true);
}

bool FDWCTransparencyAuthoringController::CancelActiveInteraction(const bool bRefreshPreview)
{
    if (!IsInteracting())
    {
        return false;
    }
    const FGuid               LayerGuid = ActiveContext.LayerGuid;
    const FGuid               StrokeGuid = ActiveStrokeGuid;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::ElementList | EDWCEditorAuthoringImpact::Preview;
    Change.MaterialSlotIndex = ActiveContext.MaterialSlotIndex;
    Change.LayerGuid = LayerGuid;
    Change.ElementGuid = StrokeGuid;
    if (AuthoringDocument.IsValid() && AuthoringDocument->HasInteractiveEdit())
    {
        const bool bRemoveCommittedStroke = bCommitMutationApplied;
        AuthoringDocument->CancelInteractiveEdit(
            Change,
            [LayerGuid, StrokeGuid, bRemoveCommittedStroke](UWetClothingAsset& MutableAsset)
            {
                if (!bRemoveCommittedStroke)
                {
                    return;
                }
                FWetClothingTransparencyLayerData* Layer =
                    MutableAsset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                        [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                        {
                            return Candidate.LayerGuid == LayerGuid;
                        });
                if (Layer == nullptr)
                {
                    return;
                }
                Layer->GetMutableRevealColorPaintStrokes().RemoveAll(
                    [StrokeGuid](const FDWCTransparencyRevealColorStroke& Stroke)
                    {
                        return Stroke.StrokeGuid == StrokeGuid;
                    });
                Layer->GetMutableEditableStrokes().RemoveAll(
                    [StrokeGuid](const FDWCTransparencyBrushStroke& Stroke)
                    {
                        return Stroke.StrokeGuid == StrokeGuid;
                    });
            });
    }
    ResetInteractionState();
    if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->CancelAuthoringLiveStroke();
    }
    if (bRefreshPreview)
    {
        if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> PinnedViewport = Viewport.Pin())
        {
            PinnedViewport->RefreshManualPreviewFromStrokes();
        }
    }
    return true;
}

void FDWCTransparencyAuthoringController::ResetInteractionState()
{
    ActiveStrokeGuid.Invalidate();
    ActiveBrushStroke.Reset();
    ActiveRevealColorStroke.Reset();
    bCommitMutationApplied = false;
    ActiveContext = FDWCTransparencyEditContext();
    ActivePaintSettings = FDWCTransparencyPaintSettings();
    LastPointerUV = FVector2D::ZeroVector;
    LastPointerUVIslandID = INDEX_NONE;
    DistanceToNextStamp = 0.0f;
}

#undef LOCTEXT_NAMESPACE
