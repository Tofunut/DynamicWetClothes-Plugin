//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinkleAuthoringController.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinkleBrushConstants.h"

#include "DataAssets/WetClothingAsset.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionAction.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleViewport.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinklePatchDescriptor.h"

#define LOCTEXT_NAMESPACE "WetWrinkleAuthoringController"

namespace
{
    constexpr float DefaultBrushSizeCm = WetWrinkleBrushConstants::DefaultSizeCm;

    float WrapDelta(const float Delta)
    {
        return Delta - FMath::RoundToFloat(Delta);
    }

    float WrapUnit(float Value)
    {
        Value = FMath::Fmod(Value, 1.0f);
        return Value < 0.0f ? Value + 1.0f : Value;
    }

    EDWCEditorAuthoringImpact MakeCreateImpact()
    {
        return EDWCEditorAuthoringImpact::AssetDirty |
            EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::PreviewIncremental |
            EDWCEditorAuthoringImpact::WrinkleBake;
    }

    EDWCEditorAuthoringImpact MakeEditImpact()
    {
        return EDWCEditorAuthoringImpact::AssetDirty |
            EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::WrinkleBake;
    }
}

FWetWrinkleAuthoringController::FWetWrinkleAuthoringController(
    UWetClothingAsset* InAsset,
    TSharedPtr<FDWCEditorAuthoringDocument> InAuthoringDocument,
    TSharedPtr<FDWCEditorSessionStore> InSessionStore)
    : Asset(InAsset)
    , AuthoringDocument(MoveTemp(InAuthoringDocument))
    , SessionStore(MoveTemp(InSessionStore))
{
}

FWetWrinkleAuthoringController::~FWetWrinkleAuthoringController()
{
    CancelActiveInteraction(false);
    DetachViewport();
}

void FWetWrinkleAuthoringController::AttachViewport(const TSharedPtr<SWetWrinkleViewport>& InViewport)
{
    Viewport = InViewport;
}

void FWetWrinkleAuthoringController::DetachViewport()
{
    Viewport.Reset();
}

const FWetWrinkleBrushSettings& FWetWrinkleAuthoringController::GetBrushSettings() const
{
    static const FWetWrinkleBrushSettings EmptySettings;
    return SessionStore.IsValid() ? SessionStore->GetState().Wrinkle.Brush : EmptySettings;
}

bool FWetWrinkleAuthoringController::CanAuthorWithCurrentSettings() const
{
    const FWetWrinkleBrushSettings& Brush = GetBrushSettings();
    return Asset.IsValid() && AuthoringDocument.IsValid() &&
        Brush.MaterialSlotIndex != INDEX_NONE && Brush.UVChannelIndex != INDEX_NONE &&
        !IsUsingCustomWrinkleMap(Brush.MaterialSlotIndex);
}

bool FWetWrinkleAuthoringController::IsUsingCustomWrinkleMap(const int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* CurrentAsset = Asset.Get();
    return CurrentAsset != nullptr && MaterialSlotIndex != INDEX_NONE &&
        CurrentAsset->Authored.WrinkleData.IsUsingCustomWrinkleNormalMap(MaterialSlotIndex);
}

void FWetWrinkleAuthoringController::HandleSurfaceHitChanged(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (bCapturingRidgeStroke && !SurfaceHit.bHit)
    {
        bRidgeCaptureBlocked = true;
    }
}

void FWetWrinkleAuthoringController::BeginSurfaceInteraction(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (!CanAuthorWithCurrentSettings())
    {
        return;
    }

    const FWetWrinkleBrushSettings& Brush = GetBrushSettings();
    if (Brush.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke)
    {
        if (Brush.RidgeEditMode == EWetProceduralRidgeEditMode::Edit)
        {
            BeginRidgePointEdit(SurfaceHit);
        }
        else
        {
            BeginRidgeStroke(SurfaceHit);
        }
        return;
    }

    // Patch placement is WYSIWYG: only the immutable descriptor presented by
    // the viewport may be committed. Rebuilding from the click hit would use a
    // new seam tangent frame and can rotate the authored Patch.
}

void FWetWrinkleAuthoringController::UpdateSurfaceInteraction(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (!CanAuthorWithCurrentSettings())
    {
        return;
    }
    if (bEditingRidgePoint)
    {
        UpdateRidgePointEdit(SurfaceHit);
    }
    else if (bCapturingRidgeStroke)
    {
        AppendRidgeStrokePoint(SurfaceHit);
    }
}

void FWetWrinkleAuthoringController::EndSurfaceInteraction()
{
    if (bEditingRidgePoint)
    {
        EndRidgePointEdit(false, true);
    }
    else if (bCapturingRidgeStroke)
    {
        CommitRidgeStroke();
    }
}

void FWetWrinkleAuthoringController::CancelSurfaceInteraction()
{
    CancelActiveInteraction(true);
}

bool FWetWrinkleAuthoringController::CancelActiveInteraction(const bool bRefreshPreview)
{
    const bool bWasActive = IsInteracting();
    if (bEditingRidgePoint)
    {
        EndRidgePointEdit(true, bRefreshPreview);
    }
    if (bCapturingRidgeStroke)
    {
        CancelRidgeStroke(bRefreshPreview);
    }
    return bWasActive;
}

bool FWetWrinkleAuthoringController::IsInteracting() const
{
    return bCapturingRidgeStroke || bEditingRidgePoint;
}

bool FWetWrinkleAuthoringController::EditWrinkleData(
    const FText& TransactionText,
    const EDWCEditorAuthoringImpact Impact,
    const int32 MaterialSlotIndex,
    const FGuid& ElementGuid,
    TFunctionRef<bool(FWetClothingWrinkleData&)> Mutation) const
{
    if (!AuthoringDocument.IsValid())
    {
        return false;
    }
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    Change.Impact = Impact;
    Change.MaterialSlotIndex = MaterialSlotIndex;
    Change.ElementGuid = ElementGuid;
    return AuthoringDocument->Edit(
        TransactionText,
        Change,
        [&Mutation](UWetClothingAsset& MutableAsset)
        {
            return Mutation(MutableAsset.Authored.WrinkleData);
        }).bChanged;
}

void FWetWrinkleAuthoringController::SelectElement(
    const EWetWrinkleElementType ElementType,
    const FGuid& ElementGuid,
    const int32 RidgePointIndex) const
{
    if (!SessionStore.IsValid())
    {
        return;
    }
    FDWCSelectWrinkleElementAction Action;
    Action.ElementGuid = ElementGuid;
    Action.ElementType = ElementType;
    Action.RidgePointIndex = RidgePointIndex;
    SessionStore->Dispatch(Action);
}

FWetProceduralRidgeStrokePoint FWetWrinkleAuthoringController::MakeRidgePointFromHit(
    const FWetWrinkleSurfaceHit& SurfaceHit) const
{
    FWetProceduralRidgeStrokePoint Point;
    Point.PositionUV = SurfaceHit.UV;
    Point.AnchorTriangleID = SurfaceHit.TriangleID;
    Point.AnchorBarycentric = FVector3f(SurfaceHit.Barycentric);
    return Point;
}

FWetWrinklePatchCommitResult FWetWrinkleAuthoringController::CommitPresentedPatch(
    const FDWCEditorWrinklePatchDescriptor& Descriptor)
{
    FWetWrinklePatchCommitResult Result;
    const FWetWrinkleBrushSettings& Brush = GetBrushSettings();
    UWetClothingAsset* CurrentAsset = Asset.Get();
    if (CurrentAsset == nullptr)
    {
        Result.FailureReason = TEXT("The Wet Clothing Asset is no longer available.");
        return Result;
    }
    if (Brush.ToolMode != EWetWrinkleToolMode::Patch)
    {
        Result.FailureReason = TEXT("Patch placement is no longer the active wrinkle tool.");
        return Result;
    }
    if (Descriptor.MaterialSlotIndex != Brush.MaterialSlotIndex ||
        Descriptor.UVChannelIndex != Brush.UVChannelIndex)
    {
        Result.FailureReason = TEXT("The visible Patch belongs to a different material slot or Data UV.");
        return Result;
    }
    if (IsUsingCustomWrinkleMap(Descriptor.MaterialSlotIndex))
    {
        Result.FailureReason = TEXT("The selected slot uses a custom wrinkle normal map.");
        return Result;
    }

    FWetWrinklePatchPlacement NewPatch;
    FString DescriptorError;
    UTexture* SourceTexture = FWetClothingMaterialTextureResolver::ResolveOrSaveTextureSelection(
        CurrentAsset, Descriptor.MaterialSlotIndex);
    if (!FDWCEditorWrinklePatchDescriptorBuilder::BuildPlacement(
            Descriptor, SourceTexture, NewPatch, &DescriptorError))
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::FromString(DescriptorError.IsEmpty()
                ? TEXT("The visible wrinkle preview is no longer valid. Move the cursor and try again.")
                : DescriptorError));
        Result.FailureReason = DescriptorError.IsEmpty()
            ? TEXT("The visible wrinkle preview is no longer valid.")
            : DescriptorError;
        return Result;
    }
    NewPatch.DisplayName = FString::Printf(
        TEXT("Patch %03d"),
        CurrentAsset->Authored.WrinkleData.EditablePatches.Num() + 1);
    NewPatch.bEnabled = true;
    if (!EditWrinkleData(
            LOCTEXT("PlacePatchTransaction", "Place Wet Wrinkle Patch"),
            MakeCreateImpact(),
            NewPatch.MaterialSlotIndex,
            NewPatch.PatchGuid,
            [&NewPatch](FWetClothingWrinkleData& Data)
            {
                Data.EditablePatches.Add(NewPatch);
                return true;
            }))
    {
        Result.FailureReason = TEXT("The Patch could not be committed to the Wet Clothing Asset.");
        return Result;
    }

    SelectElement(EWetWrinkleElementType::Patch, NewPatch.PatchGuid);
    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->AppendAccumulatedPreviewStamp(NewPatch);
    }
    Result.bSucceeded = true;
    Result.PatchGuid = NewPatch.PatchGuid;
    return Result;
}

const FWetProceduralRidgeStroke* FWetWrinkleAuthoringController::FindProceduralRidgeStroke(
    const FGuid& StrokeGuid) const
{
    const UWetClothingAsset* CurrentAsset = Asset.Get();
    return CurrentAsset != nullptr && StrokeGuid.IsValid()
        ? CurrentAsset->Authored.WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
              [StrokeGuid](const FWetProceduralRidgeStroke& Stroke)
              {
                  return Stroke.StrokeGuid == StrokeGuid;
              })
        : nullptr;
}

void FWetWrinkleAuthoringController::BeginRidgeStroke(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    const FWetWrinkleBrushSettings InitialBrush = GetBrushSettings();
    if (!SurfaceHit.bHit || SurfaceHit.MaterialSlotIndex != InitialBrush.MaterialSlotIndex ||
        SurfaceHit.UVChannelIndex != InitialBrush.UVChannelIndex || SurfaceHit.UVIslandID == INDEX_NONE)
    {
        return;
    }

    if (InitialBrush.RidgeNaturalVariation.bEnabled && SessionStore.IsValid())
    {
        FDWCSetWrinkleBrushAction Action;
        Action.Brush = InitialBrush;
        Action.Brush.RidgeNaturalVariation.NoiseSeed =
            static_cast<int32>(FPlatformTime::Cycles64() & 0x7FFFFFFF);
        Action.BrushSizeCm = SessionStore->GetState().Wrinkle.BrushSizeCm;
        Action.BrushSizeUV = SessionStore->GetState().Wrinkle.BrushSizeUV;
        Action.Effects = EDWCEditorSessionEffect::UpdatePreviewParameters;
        SessionStore->Dispatch(Action);
    }

    FWetWrinkleSurfaceHit StartHit = SurfaceHit;
    PendingStartConnectionStrokeGuid.Invalidate();
    PendingStartConnectionSegmentIndex = INDEX_NONE;
    PendingStartConnectionSegmentT = 0.0f;
    FindRidgeJunctionSnap(
        SurfaceHit,
        FGuid(),
        StartHit,
        PendingStartConnectionStrokeGuid,
        PendingStartConnectionSegmentIndex,
        PendingStartConnectionSegmentT);

    CapturedRidgeHits.Reset();
    CapturedRidgeHits.Add(StartHit);
    SmoothedRidgeHits.Reset();
    SmoothedRidgeHits.Add(StartHit);
    LiveRidgeHit = StartHit;
    ActiveRidgeMaterialSlotIndex = StartHit.MaterialSlotIndex;
    ActiveRidgeUVChannelIndex = StartHit.UVChannelIndex;
    ActiveRidgeUVIslandID = StartHit.UVIslandID;
    bCapturingRidgeStroke = true;
    bRidgeCaptureBlocked = false;

    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->SetTransientProceduralStroke(
            BuildSmoothedRidgeHits(),
            PendingStartConnectionStrokeGuid.IsValid());
    }
}

void FWetWrinkleAuthoringController::AppendRidgeStrokePoint(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (!bCapturingRidgeStroke || bRidgeCaptureBlocked || !SurfaceHit.bHit)
    {
        return;
    }
    if (SurfaceHit.MaterialSlotIndex != ActiveRidgeMaterialSlotIndex ||
        SurfaceHit.UVChannelIndex != ActiveRidgeUVChannelIndex ||
        SurfaceHit.UVIslandID != ActiveRidgeUVIslandID)
    {
        bRidgeCaptureBlocked = true;
        return;
    }

    LiveRidgeHit = SurfaceHit;
    if (ShouldAddRidgePoint(SurfaceHit))
    {
        CapturedRidgeHits.Add(SurfaceHit);
        SmoothedRidgeHits.Add(SurfaceHit);
        const int32 InteriorIndex = CapturedRidgeHits.Num() - 2;
        if (InteriorIndex > 0)
        {
            FWetWrinkleSurfaceHit SmoothedHit;
            if (TrySmoothRidgeInteriorHit(
                    CapturedRidgeHits[InteriorIndex - 1],
                    CapturedRidgeHits[InteriorIndex],
                    CapturedRidgeHits[InteriorIndex + 1],
                    SmoothedHit))
            {
                SmoothedRidgeHits[InteriorIndex] = SmoothedHit;
            }
        }
    }

    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        TArray<FWetWrinkleSurfaceHit> PreviewHits = BuildSmoothedRidgeHits();
        bool bEndJunction = false;
        if (PreviewHits.Num() >= 2)
        {
            FWetWrinkleSurfaceHit SnappedEndHit;
            FGuid ConnectedStrokeGuid;
            int32 SegmentIndex = INDEX_NONE;
            float SegmentT = 0.0f;
            bEndJunction = FindRidgeJunctionSnap(
                PreviewHits.Last(), FGuid(), SnappedEndHit, ConnectedStrokeGuid, SegmentIndex, SegmentT);
            if (bEndJunction)
            {
                PreviewHits.Last() = SnappedEndHit;
            }
        }
        PinnedViewport->SetTransientProceduralStroke(
            PreviewHits,
            PendingStartConnectionStrokeGuid.IsValid(),
            bEndJunction);
    }
}

void FWetWrinkleAuthoringController::CommitRidgeStroke()
{
    UWetClothingAsset* CurrentAsset = Asset.Get();
    TArray<FWetWrinkleSurfaceHit> FinalHits = BuildSmoothedRidgeHits();
    if (CurrentAsset == nullptr || FinalHits.Num() < 2)
    {
        CancelRidgeStroke(true);
        return;
    }

    FGuid EndConnectionGuid;
    int32 EndConnectionSegment = INDEX_NONE;
    float EndConnectionT = 0.0f;
    FWetWrinkleSurfaceHit SnappedEndHit;
    if (FindRidgeJunctionSnap(
            FinalHits.Last(), FGuid(), SnappedEndHit,
            EndConnectionGuid, EndConnectionSegment, EndConnectionT))
    {
        FinalHits.Last() = SnappedEndHit;
    }

    const FWetWrinkleBrushSettings Brush = GetBrushSettings();
    FWetProceduralRidgeStroke NewStroke;
    NewStroke.StrokeGuid = FGuid::NewGuid();
    NewStroke.DisplayName = FString::Printf(
        TEXT("Ridge %03d"),
        CurrentAsset->Authored.WrinkleData.EditableProceduralRidgeStrokes.Num() + 1);
    NewStroke.MaterialSlotIndex = ActiveRidgeMaterialSlotIndex;
    NewStroke.Shape = Brush.RidgeShape;
    NewStroke.bFlipFoldSide = Brush.bFlipRidgeFoldSide;
    NewStroke.WidthUV = Brush.BrushRadiusUV;
    NewStroke.Strength = Brush.Strength;
    NewStroke.Falloff = Brush.Falloff;
    NewStroke.StartTaper = Brush.RidgeStartTaper;
    NewStroke.EndTaper = Brush.RidgeEndTaper;
    NewStroke.FlareSettings = Brush.RidgeFlareSettings;
    NewStroke.NaturalVariation = Brush.RidgeNaturalVariation;
    if (PendingStartConnectionStrokeGuid.IsValid())
    {
        NewStroke.StartEndpoint.Mode = EWetProceduralRidgeEndpointMode::Junction;
        NewStroke.StartEndpoint.ConnectedStrokeGuid = PendingStartConnectionStrokeGuid;
        NewStroke.StartEndpoint.ConnectedSegmentIndex = PendingStartConnectionSegmentIndex;
        NewStroke.StartEndpoint.ConnectedSegmentT = PendingStartConnectionSegmentT;
    }
    if (EndConnectionGuid.IsValid())
    {
        NewStroke.EndEndpoint.Mode = EWetProceduralRidgeEndpointMode::Junction;
        NewStroke.EndEndpoint.ConnectedStrokeGuid = EndConnectionGuid;
        NewStroke.EndEndpoint.ConnectedSegmentIndex = EndConnectionSegment;
        NewStroke.EndEndpoint.ConnectedSegmentT = EndConnectionT;
    }
    NewStroke.Points.Reserve(FinalHits.Num());
    for (const FWetWrinkleSurfaceHit& Hit : FinalHits)
    {
        NewStroke.Points.Add(MakeRidgePointFromHit(Hit));
    }

    if (!EditWrinkleData(
            LOCTEXT("CreateRidgeTransaction", "Create Procedural Ridge Stroke"),
            MakeCreateImpact(),
            NewStroke.MaterialSlotIndex,
            NewStroke.StrokeGuid,
            [&NewStroke](FWetClothingWrinkleData& Data)
            {
                Data.EditableProceduralRidgeStrokes.Add(NewStroke);
                return true;
            }))
    {
        CancelRidgeStroke(true);
        return;
    }

    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->AppendAccumulatedPreviewProceduralStroke(NewStroke);
    }
    SelectElement(EWetWrinkleElementType::ProceduralRidgeStroke, NewStroke.StrokeGuid);
    // The accumulated layer now contains the committed stroke. Remove the
    // transient layer immediately so both normals are not composed together.
    CancelRidgeStroke(true);
}

void FWetWrinkleAuthoringController::CancelRidgeStroke(const bool bRefreshPreview)
{
    bCapturingRidgeStroke = false;
    bRidgeCaptureBlocked = false;
    ActiveRidgeMaterialSlotIndex = INDEX_NONE;
    ActiveRidgeUVChannelIndex = INDEX_NONE;
    ActiveRidgeUVIslandID = INDEX_NONE;
    PendingStartConnectionStrokeGuid.Invalidate();
    PendingStartConnectionSegmentIndex = INDEX_NONE;
    PendingStartConnectionSegmentT = 0.0f;
    CapturedRidgeHits.Reset();
    SmoothedRidgeHits.Reset();
    LiveRidgeHit = FWetWrinkleSurfaceHit();
    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->ClearTransientProceduralStroke(bRefreshPreview);
    }
}

bool FWetWrinkleAuthoringController::ShouldAddRidgePoint(const FWetWrinkleSurfaceHit& SurfaceHit) const
{
    if (CapturedRidgeHits.IsEmpty())
    {
        return true;
    }
    const FWetWrinkleBrushSettings& Brush = GetBrushSettings();
    const float SpacingScale = FMath::Clamp(Brush.RidgePointSpacingScale, 0.05f, 1.0f);
    const double SizeCm = SessionStore.IsValid()
        ? SessionStore->GetState().Wrinkle.BrushSizeCm
        : DefaultBrushSizeCm;
    const double MinSurfaceSpacing = FMath::Max(SizeCm * SpacingScale, 0.1);
    const double MinUVSpacing = FMath::Max(static_cast<double>(Brush.BrushRadiusUV * SpacingScale), 0.00025);
    const FWetWrinkleSurfaceHit& LastHit = CapturedRidgeHits.Last();
    return FVector::Distance(LastHit.WorldPosition, SurfaceHit.WorldPosition) >= MinSurfaceSpacing ||
        FVector2D::Distance(LastHit.UV, SurfaceHit.UV) >= MinUVSpacing;
}

TArray<FWetWrinkleSurfaceHit> FWetWrinkleAuthoringController::BuildSmoothedRidgeHits() const
{
    TArray<FWetWrinkleSurfaceHit> Result = SmoothedRidgeHits;
    if (bCapturingRidgeStroke && LiveRidgeHit.bHit)
    {
        constexpr double PositionToleranceSq = 1.0e-8;
        constexpr double UVToleranceSq = 1.0e-12;
        if (Result.IsEmpty() ||
            FVector::DistSquared(Result.Last().WorldPosition, LiveRidgeHit.WorldPosition) > PositionToleranceSq ||
            FVector2D::DistSquared(Result.Last().UV, LiveRidgeHit.UV) > UVToleranceSq)
        {
            Result.Add(LiveRidgeHit);
            const int32 InteriorIndex = Result.Num() - 2;
            if (InteriorIndex > 0 && CapturedRidgeHits.IsValidIndex(InteriorIndex))
            {
                FWetWrinkleSurfaceHit SmoothedHit;
                if (TrySmoothRidgeInteriorHit(
                        CapturedRidgeHits[InteriorIndex - 1],
                        CapturedRidgeHits[InteriorIndex],
                        LiveRidgeHit,
                        SmoothedHit))
                {
                    Result[InteriorIndex] = SmoothedHit;
                }
            }
        }
    }
    return Result;
}

bool FWetWrinkleAuthoringController::TrySmoothRidgeInteriorHit(
    const FWetWrinkleSurfaceHit& Previous,
    const FWetWrinkleSurfaceHit& Current,
    const FWetWrinkleSurfaceHit& Next,
    FWetWrinkleSurfaceHit& OutSmoothedHit) const
{
    const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin();
    if (!PinnedViewport.IsValid())
    {
        return false;
    }
    constexpr double SmoothingAlpha = 0.25;
    const double SizeCm = SessionStore.IsValid()
        ? SessionStore->GetState().Wrinkle.BrushSizeCm
        : DefaultBrushSizeCm;
    const double MaxProjectionDistance = FMath::Max(SizeCm * 0.5, 0.5);
    const FVector2D SmoothedUV = FMath::Lerp(Current.UV, (Previous.UV + Next.UV) * 0.5, SmoothingAlpha);
    return PinnedViewport->TryBuildSurfaceHitAtUVNearWorldPosition(
               ActiveRidgeMaterialSlotIndex,
               ActiveRidgeUVChannelIndex,
               SmoothedUV,
               Current.WorldPosition,
               OutSmoothedHit) &&
        OutSmoothedHit.UVIslandID == ActiveRidgeUVIslandID &&
        FVector::Distance(OutSmoothedHit.WorldPosition, Current.WorldPosition) <= MaxProjectionDistance;
}

int32 FWetWrinkleAuthoringController::FindNearestRidgeSegment(
    const FWetProceduralRidgeStroke& Stroke,
    const FVector2D& UV,
    float& OutSegmentT) const
{
    int32 NearestIndex = INDEX_NONE;
    double NearestDistanceSq = TNumericLimits<double>::Max();
    OutSegmentT = 0.0f;
    for (int32 Index = 0; Index + 1 < Stroke.Points.Num(); ++Index)
    {
        const FVector2D Start = Stroke.Points[Index].PositionUV;
        const FVector2D Delta(
            WrapDelta(Stroke.Points[Index + 1].PositionUV.X - Start.X),
            WrapDelta(Stroke.Points[Index + 1].PositionUV.Y - Start.Y));
        const FVector2D WrappedUV(
            Start.X + WrapDelta(UV.X - Start.X),
            Start.Y + WrapDelta(UV.Y - Start.Y));
        const double LengthSq = Delta.SizeSquared();
        const float SegmentT = LengthSq > UE_SMALL_NUMBER
            ? FMath::Clamp(static_cast<float>(FVector2D::DotProduct(WrappedUV - Start, Delta) / LengthSq), 0.0f, 1.0f)
            : 0.0f;
        const double DistanceSq = FVector2D::DistSquared(WrappedUV, Start + Delta * SegmentT);
        if (DistanceSq < NearestDistanceSq)
        {
            NearestDistanceSq = DistanceSq;
            NearestIndex = Index;
            OutSegmentT = SegmentT;
        }
    }
    return NearestIndex;
}

bool FWetWrinkleAuthoringController::FindRidgeJunctionSnap(
    const FWetWrinkleSurfaceHit& SurfaceHit,
    const FGuid& ExcludedStrokeGuid,
    FWetWrinkleSurfaceHit& OutSnappedHit,
    FGuid& OutConnectedStrokeGuid,
    int32& OutConnectedSegmentIndex,
    float& OutConnectedSegmentT) const
{
    OutConnectedStrokeGuid.Invalidate();
    OutConnectedSegmentIndex = INDEX_NONE;
    OutConnectedSegmentT = 0.0f;
    const FWetWrinkleBrushSettings& Brush = GetBrushSettings();
    const UWetClothingAsset* CurrentAsset = Asset.Get();
    const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin();
    if (!Brush.bRidgeJunctionModeEnabled || CurrentAsset == nullptr || !PinnedViewport.IsValid() || !SurfaceHit.bHit)
    {
        return false;
    }

    const double SizeCm = SessionStore.IsValid()
        ? SessionStore->GetState().Wrinkle.BrushSizeCm
        : DefaultBrushSizeCm;
    double BestDistanceSq = FMath::Square(FMath::Max(SizeCm * 0.75, 1.0));
    for (const FWetProceduralRidgeStroke& Candidate : CurrentAsset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
    {
        if (!Candidate.bEnabled || Candidate.StrokeGuid == ExcludedStrokeGuid || Candidate.Points.Num() < 2 ||
            Candidate.MaterialSlotIndex != SurfaceHit.MaterialSlotIndex)
        {
            continue;
        }

        for (int32 PointIndex = 0; PointIndex < Candidate.Points.Num(); ++PointIndex)
        {
            FVector PointWorld = FVector::ZeroVector;
            FVector PointNormal = FVector::UpVector;
            if (!PinnedViewport->ResolveProceduralStrokePointWorld(
                    Candidate.Points[PointIndex], Candidate.MaterialSlotIndex, PointWorld, PointNormal))
            {
                continue;
            }
            const double DistanceSq = FVector::DistSquared(PointWorld, SurfaceHit.WorldPosition);
            if (DistanceSq > BestDistanceSq)
            {
                continue;
            }
            FWetWrinkleSurfaceHit PointHit;
            if (!PinnedViewport->TryBuildSurfaceHitFromProceduralStrokePoint(
                    Candidate.Points[PointIndex], Candidate.MaterialSlotIndex,
                    SurfaceHit.UVChannelIndex, PointHit))
            {
                continue;
            }
            BestDistanceSq = DistanceSq;
            OutSnappedHit = PointHit;
            OutConnectedStrokeGuid = Candidate.StrokeGuid;
            OutConnectedSegmentIndex = PointIndex + 1 < Candidate.Points.Num() ? PointIndex : PointIndex - 1;
            OutConnectedSegmentT = PointIndex + 1 < Candidate.Points.Num() ? 0.0f : 1.0f;
        }

        for (int32 SegmentIndex = 0; SegmentIndex + 1 < Candidate.Points.Num(); ++SegmentIndex)
        {
            FVector StartWorld = FVector::ZeroVector;
            FVector EndWorld = FVector::ZeroVector;
            FVector StartNormal = FVector::UpVector;
            FVector EndNormal = FVector::UpVector;
            if (!PinnedViewport->ResolveProceduralStrokePointWorld(
                    Candidate.Points[SegmentIndex], Candidate.MaterialSlotIndex, StartWorld, StartNormal) ||
                !PinnedViewport->ResolveProceduralStrokePointWorld(
                    Candidate.Points[SegmentIndex + 1], Candidate.MaterialSlotIndex, EndWorld, EndNormal))
            {
                continue;
            }
            const FVector Delta = EndWorld - StartWorld;
            const double LengthSq = Delta.SizeSquared();
            const float SegmentT = LengthSq > UE_SMALL_NUMBER
                ? FMath::Clamp(static_cast<float>(FVector::DotProduct(SurfaceHit.WorldPosition - StartWorld, Delta) / LengthSq), 0.0f, 1.0f)
                : 0.0f;
            const FVector ClosestWorld = StartWorld + Delta * SegmentT;
            const double DistanceSq = FVector::DistSquared(ClosestWorld, SurfaceHit.WorldPosition);
            if (DistanceSq > BestDistanceSq)
            {
                continue;
            }
            const FVector2D StartUV = Candidate.Points[SegmentIndex].PositionUV;
            const FVector2D UVDelta(
                WrapDelta(Candidate.Points[SegmentIndex + 1].PositionUV.X - StartUV.X),
                WrapDelta(Candidate.Points[SegmentIndex + 1].PositionUV.Y - StartUV.Y));
            FWetWrinkleSurfaceHit CandidateHit;
            if (!PinnedViewport->TryBuildSurfaceHitAtUVNearWorldPosition(
                    SurfaceHit.MaterialSlotIndex,
                    SurfaceHit.UVChannelIndex,
                    FVector2D(WrapUnit(StartUV.X + UVDelta.X * SegmentT), WrapUnit(StartUV.Y + UVDelta.Y * SegmentT)),
                    ClosestWorld,
                    CandidateHit))
            {
                continue;
            }
            BestDistanceSq = DistanceSq;
            OutSnappedHit = CandidateHit;
            OutConnectedStrokeGuid = Candidate.StrokeGuid;
            OutConnectedSegmentIndex = SegmentIndex;
            OutConnectedSegmentT = SegmentT;
        }
    }
    return OutConnectedStrokeGuid.IsValid();
}

void FWetWrinkleAuthoringController::BeginRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (!SessionStore.IsValid())
    {
        return;
    }
    const FDWCEditorWrinkleSessionState& State = SessionStore->GetState().Wrinkle;
    const FWetProceduralRidgeStroke* AuthoredStroke =
        State.SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke
        ? FindProceduralRidgeStroke(State.SelectedElementGuid)
        : nullptr;
    const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin();
    if (AuthoredStroke == nullptr || !PinnedViewport.IsValid() || !SurfaceHit.bHit ||
        SurfaceHit.MaterialSlotIndex != AuthoredStroke->MaterialSlotIndex)
    {
        return;
    }

    const bool bInsertPoint = FSlateApplication::Get().GetModifierKeys().IsShiftDown();
    int32 PointIndex = INDEX_NONE;
    if (bInsertPoint)
    {
        float SegmentT = 0.0f;
        const int32 SegmentIndex = FindNearestRidgeSegment(*AuthoredStroke, SurfaceHit.UV, SegmentT);
        PointIndex = SegmentIndex != INDEX_NONE ? SegmentIndex + 1 : INDEX_NONE;
    }
    else
    {
        PointIndex = PinnedViewport->FindNearestProceduralStrokePoint(
            *AuthoredStroke,
            SurfaceHit.WorldPosition,
            FMath::Max(State.BrushSizeCm * 0.75f, 1.0f));
    }
    if (PointIndex == INDEX_NONE)
    {
        return;
    }

    EditedRidgeStroke = *AuthoredStroke;
    if (bInsertPoint)
    {
        EditedRidgeStroke->Points.Insert(MakeRidgePointFromHit(SurfaceHit), PointIndex);
    }
    EditingRidgePointIndex = PointIndex;
    EditingRidgeUVIslandID = SurfaceHit.UVIslandID;
    bEditingRidgePoint = true;
    SelectElement(EWetWrinkleElementType::ProceduralRidgeStroke, AuthoredStroke->StrokeGuid, PointIndex);
    PinnedViewport->SetEditingProceduralStrokeGuid(AuthoredStroke->StrokeGuid, false);
    PinnedViewport->PreviewEditedProceduralStroke(EditedRidgeStroke.GetValue());
}

void FWetWrinkleAuthoringController::UpdateRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin();
    if (!bEditingRidgePoint || !EditedRidgeStroke.IsSet() || !PinnedViewport.IsValid() || !SurfaceHit.bHit ||
        !EditedRidgeStroke->Points.IsValidIndex(EditingRidgePointIndex) ||
        SurfaceHit.MaterialSlotIndex != EditedRidgeStroke->MaterialSlotIndex ||
        SurfaceHit.UVIslandID != EditingRidgeUVIslandID)
    {
        return;
    }

    FWetWrinkleSurfaceHit FinalHit = SurfaceHit;
    const bool bEndpoint = EditingRidgePointIndex == 0 || EditingRidgePointIndex == EditedRidgeStroke->Points.Num() - 1;
    FGuid ConnectedGuid;
    int32 ConnectedSegment = INDEX_NONE;
    float ConnectedT = 0.0f;
    if (bEndpoint)
    {
        FindRidgeJunctionSnap(
            SurfaceHit, EditedRidgeStroke->StrokeGuid, FinalHit,
            ConnectedGuid, ConnectedSegment, ConnectedT);
    }
    EditedRidgeStroke->Points[EditingRidgePointIndex] = MakeRidgePointFromHit(FinalHit);
    if (EditingRidgePointIndex == 0)
    {
        EditedRidgeStroke->StartEndpoint.Mode = ConnectedGuid.IsValid()
            ? EWetProceduralRidgeEndpointMode::Junction
            : EWetProceduralRidgeEndpointMode::Pointed;
        EditedRidgeStroke->StartEndpoint.ConnectedStrokeGuid = ConnectedGuid;
        EditedRidgeStroke->StartEndpoint.ConnectedSegmentIndex = ConnectedSegment;
        EditedRidgeStroke->StartEndpoint.ConnectedSegmentT = ConnectedT;
    }
    else if (EditingRidgePointIndex == EditedRidgeStroke->Points.Num() - 1)
    {
        EditedRidgeStroke->EndEndpoint.Mode = ConnectedGuid.IsValid()
            ? EWetProceduralRidgeEndpointMode::Junction
            : EWetProceduralRidgeEndpointMode::Pointed;
        EditedRidgeStroke->EndEndpoint.ConnectedStrokeGuid = ConnectedGuid;
        EditedRidgeStroke->EndEndpoint.ConnectedSegmentIndex = ConnectedSegment;
        EditedRidgeStroke->EndEndpoint.ConnectedSegmentT = ConnectedT;
    }
    PinnedViewport->PreviewEditedProceduralStroke(EditedRidgeStroke.GetValue());
}

void FWetWrinkleAuthoringController::EndRidgePointEdit(const bool bCancel, const bool bRefreshPreview)
{
    if (!bEditingRidgePoint)
    {
        return;
    }

    const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin();
    if (!bCancel && EditedRidgeStroke.IsSet())
    {
        FWetProceduralRidgeStroke EditedStroke = MoveTemp(EditedRidgeStroke.GetValue());
        const FGuid StrokeGuid = EditedStroke.StrokeGuid;
        const int32 MaterialSlotIndex = EditedStroke.MaterialSlotIndex;
        EditWrinkleData(
            LOCTEXT("EditRidgePointTransaction", "Edit Procedural Ridge Point"),
            MakeEditImpact(),
            MaterialSlotIndex,
            StrokeGuid,
            [StrokeGuid, EditedStroke = MoveTemp(EditedStroke)](FWetClothingWrinkleData& Data) mutable
            {
                FWetProceduralRidgeStroke* MutableStroke =
                    Data.EditableProceduralRidgeStrokes.FindByPredicate(
                        [StrokeGuid](const FWetProceduralRidgeStroke& Candidate)
                        {
                            return Candidate.StrokeGuid == StrokeGuid;
                        });
                if (MutableStroke == nullptr)
                {
                    return false;
                }
                *MutableStroke = MoveTemp(EditedStroke);
                return true;
            });
    }

    EditedRidgeStroke.Reset();
    bEditingRidgePoint = false;
    EditingRidgePointIndex = INDEX_NONE;
    EditingRidgeUVIslandID = INDEX_NONE;
    if (PinnedViewport.IsValid())
    {
        PinnedViewport->ClearTransientProceduralStroke(bRefreshPreview);
        PinnedViewport->SetEditingProceduralStrokeGuid(FGuid(), false);
    }
}

#undef LOCTEXT_NAMESPACE
