//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Editor/SWetClothingTransparencyBakePanel.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyBakePanelUtilities.h"

#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"
#include "WetClothing/Foundation/Preview/DWCEditorPreviewResourceContext.h"

#include "AssetThumbnail.h"
#include "DataAssets/WetClothingAsset.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "PropertyCustomizationHelpers.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SAdvancedTransformInputBox.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "WetClothing/WCAEditor/WCAEditorTypes.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyProjectionSourceProvider.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyResolutionResolver.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyRevealCommitWorker.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAffectedStage4Rebake.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyWorkflowPolicy.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyWorkflowStateResolver.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyBlueprintHierarchySession.h"
#include "WetClothing/Modes/Transparency/Authoring/DWCTransparencyAuthoringController.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"
#include "WetClothing/Modes/Transparency/Viewport/SWetClothingTransparencyPreviewViewport.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorAuthoringPayloadDiagnostics.h"
#include "WetClothing/Modes/Transparency/Diagnostics/DWCTransparencyBaselineDiagnostics.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyBakedBaselineMemoryPolicy.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "WetClothingTransparencyBakePanel"

namespace
{
FDWCTransparencyPreviewSettings MakeTransparencyPreviewSettings(
    const FWetClothingTransparencyData& Data)
{
    FDWCTransparencyPreviewSettings Settings;
    Settings.TransparencyStrength = Data.TransparencyPreviewStrength;
    Settings.WrinkleSuppressionStrength = Data.WrinkleSuppressionStrength;
    Settings.WrinkleMaskThreshold = Data.WrinkleSuppressionCoverageThreshold;
    Settings.WrinkleMaskSoftness = Data.WrinkleSuppressionMaskSoftness;
    return Settings;
}
}

void SWetClothingTransparencyBakePanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    AuthoringDocument = InArgs._AuthoringDocument;
    if (!AuthoringDocument.IsValid())
    {
        AuthoringDocument = MakeShared<FDWCEditorAuthoringDocument>(WetClothingAsset.Get());
    }
    SessionStore = InArgs._SessionStore;
    if (!SessionStore.IsValid())
    {
        SessionStore = MakeShared<FDWCEditorSessionStore>();
    }
    PlacementSession = MakeShared<FDWCTransparencyPlacementSession>();
    InitializeCharacterTypeSessionState();
    AuthoringController = MakeShared<FDWCTransparencyAuthoringController>(
        WetClothingAsset.Get(), AuthoringDocument, SessionStore);
    WorkerJobScheduler = InArgs._WorkerJobScheduler;
    BakeCoordinator = InArgs._BakeCoordinator;
    WrinkleSuppressionCoverageService = InArgs._WrinkleSuppressionCoverageService;
    SpatialQueryService = InArgs._SpatialQueryService;
    const TSharedPtr<FDWCEditorPreviewResourceContext> PreviewResources =
        InArgs._PreviewResources;
    checkf(PreviewResources.IsValid(), TEXT("Transparency editor requires WCA-owned preview resources."));
    TextureWorkspace = PreviewResources->GetTextureWorkspace();
    PreviewCommitCoordinator = PreviewResources->GetCommitCoordinator();
    PreviewModeLifetime = InArgs._PreviewModeLifetime;
    RenderUploadQueue = PreviewResources->GetUploadQueue();
    ResourceGovernor = InArgs._ResourceGovernor;
    CacheStore = InArgs._CacheStore;
    DetailsView = InArgs._DetailsView;
    FDWCInitializeTransparencyPreviewSettingsAction InitializePreviewSettings;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        InitializePreviewSettings.Settings =
            MakeTransparencyPreviewSettings(Asset->Authored.TransparencyData);
    }
    SessionStore->Dispatch(InitializePreviewSettings);
    SessionStore->OnChanged().AddSP(this, &SWetClothingTransparencyBakePanel::HandleSessionStateChanged);
    BlueprintHierarchySession = MakeShared<FDWCTransparencyBlueprintHierarchySession>();
    BlueprintHierarchySession->OnChanged().AddSP(
        this,
        &SWetClothingTransparencyBakePanel::HandleBlueprintHierarchySessionChanged);
    ThumbnailPool = MakeShared<FAssetThumbnailPool>(32);
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::BaseRevealColor));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::InnerColor));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::CorrectionDifference));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::RaycastGaps));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::ValidHit));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::HitDistance));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::SourcePriority));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::Final));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::AutoAlpha));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::WrinkleSeparation));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::RevealNormalOnly));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::RevealNormalTexture));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::SourceCoverage));
    BlueprintSourceRoleItems.Add(MakeShared<EDWCTransparencyBlueprintSourceRole>(EDWCTransparencyBlueprintSourceRole::RevealSource));
    BlueprintSourceRoleItems.Add(MakeShared<EDWCTransparencyBlueprintSourceRole>(EDWCTransparencyBlueprintSourceRole::BlockerOnly));
    SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
    DispatchTransparencyPreviewState();
    RefreshModelState();
    RebuildEditorLayout();
    RefreshViewportContext();
}

SWetClothingTransparencyBakePanel::~SWetClothingTransparencyBakePanel()
{
    if (BlueprintHierarchySession.IsValid())
    {
        BlueprintHierarchySession->OnChanged().RemoveAll(this);
        BlueprintHierarchySession->CancelPendingRequest();
        BlueprintHierarchySession.Reset();
    }
    if (PendingRevealCommitTicket.IsValid() && WorkerJobScheduler.IsValid())
    {
        WorkerJobScheduler->Cancel(PendingRevealCommitTicket.Key);
        PendingRevealCommitTicket = {};
        ++RevealCommitEpoch;
    }
    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction(false);
        AuthoringController->DetachViewport();
        AuthoringController.Reset();
    }
    if (SessionStore.IsValid())
    {
        SessionStore->OnChanged().RemoveAll(this);
    }
}

void SWetClothingTransparencyBakePanel::DispatchTransparencyPreviewState()
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    FDWCSetTransparencyPreviewAction Action;
    Action.Stage = GetCurrentStage();
    Action.PreviewMode = PreviewViewport.IsValid()
        ? PreviewViewport->GetPreviewMode()
        : EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    Action.VisualizationMode = SelectedVisualizationMode;
    Action.WetnessPreviewPercent = WetnessPreviewPercent;
    Action.Settings = GetTransparencyPreviewSettings();
    Action.bShowSavedWrinkle = bShowSavedWrinkle;
    SessionStore->Dispatch(Action);
}

FDWCTransparencyPreviewSettings SWetClothingTransparencyBakePanel::GetTransparencyPreviewSettings() const
{
    if (SessionStore.IsValid())
    {
        return SessionStore->GetState().Transparency.PreviewSettings;
    }

    FDWCTransparencyPreviewSettings Settings;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Settings = MakeTransparencyPreviewSettings(Asset->Authored.TransparencyData);
    }
    return Settings;
}

void SWetClothingTransparencyBakePanel::DispatchTransparencyPreviewSettings(
    FDWCTransparencyPreviewSettings Settings)
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    FDWCSetTransparencyPreviewAction Action;
    const FDWCEditorTransparencySessionState& Current = SessionStore->GetState().Transparency;
    Action.Stage = GetCurrentStage();
    Action.PreviewMode = Current.PreviewMode;
    Action.VisualizationMode = Current.VisualizationMode;
    Action.WetnessPreviewPercent = Current.WetnessPreviewPercent;
    Action.Settings = MoveTemp(Settings);
    Action.bShowSavedWrinkle = Current.bShowSavedWrinkle;
    SessionStore->Dispatch(Action);
}

void SWetClothingTransparencyBakePanel::DispatchTransparencyPaintState(
    const EDWCEditorSessionEffect Effects)
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    FDWCTransparencyPaintSettings Paint;
    Paint.Mode = BrushMode;
    Paint.RadiusUV = BrushRadiusUV;
    Paint.Strength = BrushStrength;
    Paint.Falloff = BrushFalloff;
    Paint.Spacing = BrushSpacing;
    Paint.TargetAlpha = BrushTargetAlpha;
    Paint.bEnabled = true;
    Paint.bRevealColorPaint = false;

    FDWCSetTransparencyPaintAction Action;
    Action.Paint = Paint;
    Action.bRevealPaint = false;
    Action.Effects = Effects;
    SessionStore->Dispatch(Action);
}

FDWCTransparencyPaintSettings SWetClothingTransparencyBakePanel::GetRevealPaintSettingsFromSession() const
{
    if (SessionStore.IsValid())
    {
        return SessionStore->GetState().Transparency.RevealPaint;
    }

    // Construction can briefly precede session attachment. This fallback is
    // display-only; all interactive changes still require a session action.
    FDWCTransparencyPaintSettings Settings;
    Settings.bRevealColorPaint = true;
    Settings.bEnabled = true;
    Settings.RevealColorMode = RevealPaintMode;
    Settings.RadiusUV = RevealPaintRadiusUV;
    Settings.Strength = RevealPaintStrength;
    Settings.Falloff = RevealPaintFalloff;
    Settings.Spacing = 0.25f;
    Settings.TargetAlpha = 1.0f;
    Settings.RevealColor = RevealPaintColor;
    return Settings;
}

void SWetClothingTransparencyBakePanel::DispatchRevealPaintState(
    FDWCTransparencyPaintSettings Settings,
    const EDWCEditorSessionEffect Effects)
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    Settings.bRevealColorPaint = true;
    Settings.bEnabled = true;
    FDWCSetTransparencyPaintAction Action;
    Action.Paint = MoveTemp(Settings);
    Action.bRevealPaint = true;
    Action.Effects = Effects;
    SessionStore->Dispatch(Action);
}

void SWetClothingTransparencyBakePanel::DispatchTransparencyEditContext()
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const EDWCTransparencyEditorStage Stage = GetCurrentStage();
    FDWCSetTransparencyEditContextAction Action;
    Action.Context.LayerGuid = Layer != nullptr ? Layer->LayerGuid : FGuid();
    Action.Context.MaterialSlotIndex =
        Layer != nullptr ? Layer->TargetSurface.OuterMaterialSlotIndex : SelectedMaterialSlotIndex;
    Action.Context.UVChannelIndex = GetTransparencyDataUVChannel();
    Action.Context.AddressMode = Layer != nullptr
        ? Layer->TargetSurface.UVAddressMode
        : EDWCTransparencyUVAddressMode::Clamp;
    Action.Context.PaintTarget = DWCTransparencyWorkflow::ResolvePaintTarget(
        Stage,
        Layer != nullptr ? Layer->SourceType : EDWCTransparencySourceType::SameMeshMaterialSlots);
    const TSharedPtr<FDWCTransparencySourcePayload>* WorkingMap = Layer != nullptr
        ? AutoBakeResults.Find(Layer->LayerGuid)
        : nullptr;
    Action.Context.bSurfacePaintingEnabled =
        Action.Context.PaintTarget != EDWCTransparencyPaintTarget::None &&
        WorkingMap != nullptr && WorkingMap->IsValid();
    SessionStore->Dispatch(Action);
}

void SWetClothingTransparencyBakePanel::HandleSessionStateChanged(
    const FDWCEditorSessionState& State,
    const EDWCEditorSessionEffect Effects,
    uint64)
{
    TGuardValue<bool> Guard(bApplyingSessionState, true);
    const FDWCEditorTransparencySessionState& TransparencyState = State.Transparency;
    if (AuthoringController.IsValid())
    {
        AuthoringController->HandleSessionStateChanged(State);
    }
    SelectedMaterialSlotIndex = TransparencyState.SelectedMaterialSlotIndex;
    StageByLayer = TransparencyState.StageByLayer;
    SelectedVisualizationMode = TransparencyState.VisualizationMode;
    WetnessPreviewPercent = TransparencyState.WetnessPreviewPercent;
    bShowSavedWrinkle = TransparencyState.bShowSavedWrinkle;

    const FDWCTransparencyPaintSettings& Paint = TransparencyState.Paint;
    BrushMode = Paint.Mode;
    BrushRadiusUV = Paint.RadiusUV;
    BrushStrength = Paint.Strength;
    BrushFalloff = Paint.Falloff;
    BrushSpacing = Paint.Spacing;
    BrushTargetAlpha = Paint.TargetAlpha;
    const FDWCTransparencyPaintSettings& RevealPaint = TransparencyState.RevealPaint;
    RevealPaintMode = RevealPaint.RevealColorMode;
    RevealPaintRadiusUV = RevealPaint.RadiusUV;
    RevealPaintStrength = RevealPaint.Strength;
    RevealPaintFalloff = RevealPaint.Falloff;
    RevealPaintColor = RevealPaint.RevealColor;

    if (State.ActiveMode != EWCAEditorMode::TransparencyBake)
    {
        return;
    }

    if (!bPreviewSuspended && PreviewViewport.IsValid() &&
        EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::UpdatePreviewParameters))
    {
        // Derive the complete viewport state from the session in one place.
        // This also prepares the Stage 2 transient reveal-color map before
        // input is rebound to that paint target.
        RefreshViewportContext();
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RefreshStageContent))
    {
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RefreshElementList))
    {
        RefreshTransparencyStrokeList();
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RebuildPreviewContent))
    {
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::SyncSelection) && LayerListView.IsValid())
    {
        const FLayerItemPtr* Item = LayerItems.FindByPredicate(
            [this](const FLayerItemPtr& Candidate)
            {
                return Candidate.IsValid() &&
                       Candidate->MaterialSlotIndex == SelectedMaterialSlotIndex;
            });
        TGuardValue<bool> SelectionGuard(bRefreshingLayerSelection, true);
        Item != nullptr
            ? LayerListView->SetSelection(*Item, ESelectInfo::Direct)
            : LayerListView->ClearSelection();
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RefreshDetails) && DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
}

void SWetClothingTransparencyBakePanel::RebuildEditorLayout()
{
    FDWCTransparencyBaselineDiagnostics::RecordFullLayoutRebuild();
    const float PreviousScrollOffset = ControlPanelScrollBox.IsValid() ? ControlPanelScrollBox->GetScrollOffset() : 0.0f;
    if (ControlPanelScrollBox.IsValid())
    {
        ControlPanelScrollBox->SetScrollOffset(PreviousScrollOffset);
    }
    else
    {
        ChildSlot
            [SNew(SSplitter)
             + SSplitter::Slot().Value(0.34f)
                 [SAssignNew(ControlPanelContainer, SBox)
                     [BuildControlPanel()]]
             + SSplitter::Slot().Value(0.66f)
                 [BuildTransparencyPreviewSection()]];
    }

    // The switcher owns all stage roots for this panel lifetime. Select the
    // current stage only after it exists; do not rebuild a stage tree here.
    RefreshStageContent();

    if (LayerListView.IsValid())
    {
        const FLayerItemPtr* SelectedItem = LayerItems.FindByPredicate(
            [this](const FLayerItemPtr& Item)
            {
                return Item.IsValid() &&
                       Item->MaterialSlotIndex == SelectedMaterialSlotIndex;
            });
        bRefreshingLayerSelection = true;
        SelectedItem != nullptr ? LayerListView->SetSelection(*SelectedItem, ESelectInfo::Direct) : LayerListView->ClearSelection();
        bRefreshingLayerSelection = false;
    }
}

bool SWetClothingTransparencyBakePanel::RefreshOptionItems()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* Mesh = Asset != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
    int32 NumUVChannels = 0;
    if (Mesh != nullptr)
    {
        if (const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
            RenderData != nullptr && RenderData->LODRenderData.IsValidIndex(0))
        {
            NumUVChannels = RenderData->LODRenderData[0].StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
        }
    }

    const int32 MaterialSlotCount = Mesh != nullptr ? Mesh->GetMaterials().Num() : 0;
    const bool bBaseOptionsChanged = OptionItemsTargetMesh.Get() != Mesh ||
        OptionItemsMaterialSlotCount != MaterialSlotCount || OptionItemsUVChannelCount != NumUVChannels;
    if (!bBaseOptionsChanged && !bPreviewSlotStateRefreshRequested)
    {
        return false;
    }

    FDWCEditorPreviewSlotCollection ResolvedPreviewSlotStates =
        FDWCEditorPreviewSlotResolver::Resolve(Asset);
    const uint32 PreviewStateSignature = ResolvedPreviewSlotStates.StateSignature;
    const bool bMeshOptionsChanged = bBaseOptionsChanged ||
        OptionItemsPreviewStateSignature != PreviewStateSignature;
    bPreviewSlotStateRefreshRequested = false;
    if (!bMeshOptionsChanged)
    {
        return false;
    }

    OptionItemsTargetMesh = const_cast<USkeletalMesh*>(Mesh);
    OptionItemsMaterialSlotCount = MaterialSlotCount;
    OptionItemsUVChannelCount = NumUVChannels;
    OptionItemsPreviewStateSignature = PreviewStateSignature;
    PreviewSlotStates = MoveTemp(ResolvedPreviewSlotStates);
    MaterialSlotItems.Reset();
    TargetMaterialSlotItems.Reset();
    MaterialSlotThumbnails.Reset();
    UVChannelItems.Reset();
    if (Mesh != nullptr)
    {
        const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
        for (int32 SlotIndex = 0; SlotIndex < Materials.Num(); ++SlotIndex)
        {
            FMaterialSlotItemPtr Item = MakeShared<FDWCTransparencyMaterialSlotItem>();
            Item->SlotIndex = SlotIndex;
            Item->SlotName = Materials[SlotIndex].MaterialSlotName;
            MaterialSlotItems.Add(Item);
            const FDWCEditorPreviewSlotState* PreviewState = PreviewSlotStates.Find(SlotIndex);
            if (PreviewState != nullptr && PreviewState->bWettable)
            {
                TargetMaterialSlotItems.Add(Item);
            }
        }
    }
    for (int32 UVChannelIndex = 0; UVChannelIndex < NumUVChannels; ++UVChannelIndex)
    {
        UVChannelItems.Add(MakeShared<int32>(UVChannelIndex));
    }
    return true;
}

void SWetClothingTransparencyBakePanel::RefreshLayerItems()
{
    FDWCTransparencyBaselineDiagnostics::RecordLayerListRefresh();
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    bool bLayerItemsChanged = LayerItems.Num() != TargetMaterialSlotItems.Num();
    if (!bLayerItemsChanged)
    {
        for (int32 ItemIndex = 0; ItemIndex < LayerItems.Num(); ++ItemIndex)
        {
            const FLayerItemPtr& ExistingItem = LayerItems[ItemIndex];
            const FMaterialSlotItemPtr& TargetSlotItem = TargetMaterialSlotItems[ItemIndex];
            const FWetClothingTransparencyLayerData* ExistingLayer =
                Asset != nullptr && TargetSlotItem.IsValid()
                    ? Asset->Authored.TransparencyData.FindTransparencyLayer(TargetSlotItem->SlotIndex)
                    : nullptr;
            const FGuid ExpectedGuid = ExistingLayer != nullptr
                ? ExistingLayer->LayerGuid
                : FGuid();
            if (!ExistingItem.IsValid() || !TargetSlotItem.IsValid() ||
                ExistingItem->MaterialSlotIndex != TargetSlotItem->SlotIndex ||
                ExistingItem->MaterialSlotName != TargetSlotItem->SlotName ||
                ExistingItem->LayerGuid != ExpectedGuid)
            {
                bLayerItemsChanged = true;
                break;
            }
        }
    }
    if (bLayerItemsChanged)
    {
        LayerItems.Reset();
        for (const FMaterialSlotItemPtr& TargetSlotItem : TargetMaterialSlotItems)
        {
            if (!TargetSlotItem.IsValid())
            {
                continue;
            }

            FLayerItemPtr Item = MakeShared<FDWCTransparencyLayerListItem>();
            Item->MaterialSlotIndex = TargetSlotItem->SlotIndex;
            Item->MaterialSlotName = TargetSlotItem->SlotName;
            if (Asset != nullptr)
            {
                if (const FWetClothingTransparencyLayerData* ExistingLayer =
                        Asset->Authored.TransparencyData.FindTransparencyLayer(Item->MaterialSlotIndex))
                {
                    Item->LayerGuid = ExistingLayer->LayerGuid;
                }
            }
            LayerItems.Add(Item);
        }
    }

    const bool bSelectedSlotStillExists = LayerItems.ContainsByPredicate(
        [this](const FLayerItemPtr& Item)
        {
            return Item.IsValid() && Item->MaterialSlotIndex == SelectedMaterialSlotIndex;
        });
    if (!bSelectedSlotStillExists && SelectedMaterialSlotIndex != INDEX_NONE)
    {
        SelectedMaterialSlotIndex = INDEX_NONE;
        if (SessionStore.IsValid())
        {
            SessionStore->Dispatch(FDWCSelectTransparencyTargetSlotAction{INDEX_NONE});
        }
    }
    EnsureStageForSelectedLayer();
    if (LayerListView.IsValid())
    {
        LayerListView->RequestListRefresh();
        const FLayerItemPtr* SelectedItem = LayerItems.FindByPredicate(
            [this](const FLayerItemPtr& Item)
            {
                return Item.IsValid() &&
                       Item->MaterialSlotIndex == SelectedMaterialSlotIndex;
            });
        bRefreshingLayerSelection = true;
        SelectedItem != nullptr ? LayerListView->SetSelection(*SelectedItem, ESelectInfo::Direct) : LayerListView->ClearSelection();
        bRefreshingLayerSelection = false;
    }
}

void SWetClothingTransparencyBakePanel::RefreshFromAsset()
{
    // Asset-wide refreshes arrive after saves, external edits and editor mode
    // changes. They update model/viewport state, but must not reconstruct the
    // active Stage 2/3 subtree. Stage content is refreshed explicitly only for
    // a stage or source-type change, or after an operation that changed its
    // generated-output data.
    bPreviewSlotStateRefreshRequested = true;
    if (SessionStore.IsValid())
    {
        if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
        {
            ReconcileCharacterTypeSessionState();
            FDWCInitializeTransparencyPreviewSettingsAction SyncPreviewSettings;
            SyncPreviewSettings.Settings =
                MakeTransparencyPreviewSettings(Asset->Authored.TransparencyData);
            SyncPreviewSettings.bForce = true;
            SessionStore->Dispatch(SyncPreviewSettings);
        }
    }
    EDWCTransparencyPanelRefreshFlags Flags =
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::SourceModel;
    if (!bPreviewSuspended)
    {
        Flags |= EDWCTransparencyPanelRefreshFlags::Viewport;
    }
    RequestRefresh(Flags);
}

void SWetClothingTransparencyBakePanel::SuspendPreview(const EDWCEditorPreviewSuspendReason Reason)
{
    if (bPreviewSuspended)
    {
        return;
    }

    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction(false);
    }
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SuspendPreview(Reason);
    }
    bPreviewSuspended = true;
}

void SWetClothingTransparencyBakePanel::ResumePreviewIfNeeded()
{
    if (!bPreviewSuspended)
    {
        return;
    }

    bPreviewSuspended = false;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ResumePreviewIfNeeded();
    }
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
}

bool SWetClothingTransparencyBakePanel::RefreshModelState()
{
    FDWCTransparencyBaselineDiagnostics::RecordModelRefresh();
    const bool bOptionItemsChanged = RefreshOptionItems();
    RefreshLayerItems();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr)
    {
        StatusMessage = TEXT("No Wet Clothing Asset.");
        PanelStatus = EDWCTransparencyPanelStatus::Error;
    }
    else if (Asset->GetDWCSkeletalMesh() == nullptr)
    {
        StatusMessage = TEXT("Assign a Target Skeletal Mesh before configuring Transparency.");
        PanelStatus = EDWCTransparencyPanelStatus::Error;
    }
    else if (Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion)
    {
        StatusMessage = FString::Printf(
            TEXT("Unsupported Transparency data version %d (current: %d). Recreate the Transparency setup for this WCA."),
            Asset->Authored.TransparencyData.DataVersion,
            FWetClothingTransparencyData::CurrentDataVersion);
        PanelStatus = EDWCTransparencyPanelStatus::Error;
    }
    else if (Layer == nullptr)
    {
        StatusMessage = TEXT("Select a ready Wettable material slot as a Transparency Target Part.");
        PanelStatus = EDWCTransparencyPanelStatus::Info;
    }
    else if (!HasUsableTransparencyDataUV())
    {
        StatusMessage = TEXT("Generate the DWC UV Channel before configuring Transparency Target Parts.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
    }
    else if (!PreviewSlotStates.IsReady(Layer->TargetSurface.OuterMaterialSlotIndex))
    {
        const FDWCEditorPreviewSlotState* State =
            FindPreviewSlotState(Layer->TargetSurface.OuterMaterialSlotIndex);
        StatusMessage = State != nullptr
            ? FDWCEditorPreviewSlotResolver::GetIssueText(State->Issue).ToString()
            : TEXT("The selected Transparency Target Part is unavailable for preview.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
    }
    else
    {
        TArray<FString> Errors;
        const TSharedPtr<FDWCTransparencySourcePayload>* Existing =
            AutoBakeResults.Find(Layer->LayerGuid);
        if (GetCurrentStage() == EDWCTransparencyEditorStage::FinalEditing &&
            Existing != nullptr && Existing->IsValid())
        {
            const FDWCTransparencySourcePayload& Result = **Existing;
            if (Result.bIsFinalBakedBaseline)
            {
                StatusMessage = FString::Printf(
                    TEXT("Baked map loaded as an editable baseline. New brush edits: %d."),
                    FMath::Max(Layer->GetEditableStrokes().Num() - Result.BaselineStrokeCount, 0));
                PanelStatus = EDWCTransparencyPanelStatus::Ready;
            }
            else
            {
                StatusMessage = Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture
                    ? FString::Printf(
                        TEXT("Base-color preview map ready. Target texels: %d, UV Overlaps: %d"),
                        Result.OuterSampleCount,
                        Result.OverlappedUVPixelCount)
                    : FString::Printf(
                        TEXT("Preview map ready. Samples: %d, Valid Hits: %d, No Hits: %d, UV Overlaps: %d"),
                        Result.OuterSampleCount,
                        Result.ValidHitCount,
                        Result.NoHitCount,
                        Result.OverlappedUVPixelCount);
                PanelStatus = Result.OverlappedUVPixelCount > 0 ? EDWCTransparencyPanelStatus::Warning : EDWCTransparencyPanelStatus::Ready;
            }
        }
        else if (!FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
                     Asset->GetDWCSkeletalMesh(), *Layer, Errors, UE::DWCEditor::TransparencyPanel::ResolveDataUVChannel(Asset)))
        {
            StatusMessage = FString::Join(Errors, TEXT("\n"));
            PanelStatus = EDWCTransparencyPanelStatus::Error;
        }
        else if (const FWetClothingBakedTransparencyMap* BakedMap =
                     UE::DWCEditor::TransparencyPanel::FindExactBakedMap(Asset, Layer))
        {
            const bool bFinalBakeCurrent =
                BakedMap->BakeGuid.IsValid() && !BakedMap->BuildSignature.IsEmpty();
            StatusMessage = bFinalBakeCurrent
                ? FString::Printf(
                    TEXT("Baked map is available but its editable source data could not be loaded: %s."),
                    *GetNameSafe(BakedMap->TransparencyMap))
                : FString::Printf(
                    TEXT("A stale baked map is available for inspection: %s. Generate Transparency Map and rebake to update it."),
                    *GetNameSafe(BakedMap->TransparencyMap));
            PanelStatus = bFinalBakeCurrent
                ? EDWCTransparencyPanelStatus::Ready
                : EDWCTransparencyPanelStatus::Warning;
        }
        else
        {
            StatusMessage = TEXT("Ready for automatic transparency generation.");
            PanelStatus = EDWCTransparencyPanelStatus::Ready;
        }
    }
    UpdateInnerSourceStatus();
    // Edit context follows authoring state reconciliation. The viewport only
    // consumes it, preventing a viewport refresh from becoming a state write.
    DispatchTransparencyEditContext();
    return bOptionItemsChanged;
}

void SWetClothingTransparencyBakePanel::RefreshSourceModelState()
{
    FDWCTransparencyBaselineDiagnostics::RecordSourceModelRefresh();
    RefreshInnerSourceSlotItems();
    RefreshBlueprintHierarchy(false);
    RefreshBlueprintSourcePriorityItems();
    RefreshExternalSourcePriorityItems();
    UpdateInnerSourceStatus();
}

EDWCTransparencyEditorStage SWetClothingTransparencyBakePanel::GetCurrentStage() const
{
    if (const EDWCTransparencyEditorStage* Stage = StageByLayer.Find(GetSelectedLayerGuid()))
    {
        return *Stage;
    }
    return ResolveSelectedLayerWorkflowState().DefaultStage;
}

void SWetClothingTransparencyBakePanel::EnsureStageForSelectedLayer()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const FGuid LayerGuid = Layer != nullptr ? Layer->LayerGuid : FGuid();
    const EDWCTransparencyEditorStage ResolvedStage = ResolveStageForLayer(Layer);
    const EDWCTransparencyEditorStage* ExistingStage = StageByLayer.Find(LayerGuid);
    if (ExistingStage != nullptr && *ExistingStage == ResolvedStage)
    {
        return;
    }

    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCSetTransparencyStageAction{LayerGuid, ResolvedStage});
    }
    else
    {
        StageByLayer.FindOrAdd(LayerGuid) = ResolvedStage;
    }
}

DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState
SWetClothingTransparencyBakePanel::ResolveSelectedLayerWorkflowState() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const bool bHasBakedBaseline = UE::DWCEditor::TransparencyPanel::FindExactBakedMap(Asset, Layer) != nullptr;
    return DWCTransparencyWorkflow::ResolveLayerWorkflowState(
        Asset != nullptr && Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured,
        Layer,
        bHasBakedBaseline);
}

EDWCTransparencyEditorStage SWetClothingTransparencyBakePanel::ResolveStageForLayer(
    const FWetClothingTransparencyLayerData* Layer) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const bool bHasBakedBaseline = UE::DWCEditor::TransparencyPanel::FindExactBakedMap(Asset, Layer) != nullptr;
    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState WorkflowState =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(
            Asset != nullptr && Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured,
            Layer,
            bHasBakedBaseline);
    const FGuid LayerGuid = Layer != nullptr ? Layer->LayerGuid : FGuid();
    if (const EDWCTransparencyEditorStage* SessionStage = StageByLayer.Find(LayerGuid))
    {
        return DWCTransparencyWorkflow::NormalizeRequestedStage(*SessionStage, WorkflowState);
    }
    return WorkflowState.DefaultStage;
}

void SWetClothingTransparencyBakePanel::SelectTransparencyTargetSlotWithResolvedStage(
    const int32 MaterialSlotIndex,
    const EDWCTransparencyEditorStage Stage)
{
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(
            FDWCSelectTransparencyTargetSlotAndStageAction{MaterialSlotIndex, Stage});
        return;
    }

    SelectedMaterialSlotIndex = MaterialSlotIndex;
    const FGuid LayerGuid = GetSelectedLayerGuid();
    if (LayerGuid.IsValid())
    {
        StageByLayer.FindOrAdd(LayerGuid) = Stage;
    }
}

bool SWetClothingTransparencyBakePanel::CanEnterFinalEditingStage() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Layer == nullptr || Asset == nullptr)
    {
        return false;
    }

    if (const TSharedPtr<FDWCTransparencySourcePayload>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
        Existing != nullptr && Existing->IsValid())
    {
        const EDWCTransparencyEditorStage CurrentStage = GetCurrentStage();
        return (*Existing)->bIsFinalBakedBaseline ||
            CurrentStage == EDWCTransparencyEditorStage::RevealEditing ||
            CurrentStage == EDWCTransparencyEditorStage::FinalEditing;
    }

    return ResolveSelectedLayerWorkflowState().CanEnterFinalEditing();
}

bool SWetClothingTransparencyBakePanel::CanEnterRevealEditingStage() const
{
    if (bRevealCommitInFlight)
    {
        return false;
    }
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return false;
    }
    const TSharedPtr<FDWCTransparencySourcePayload>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
    return (Existing != nullptr && Existing->IsValid() && !(*Existing)->bIsFinalBakedBaseline) ||
        ResolveSelectedLayerWorkflowState().CanEnterRevealEditing();
}

void SWetClothingTransparencyBakePanel::SetCurrentStage(const EDWCTransparencyEditorStage Stage)
{
    if (Stage != EDWCTransparencyEditorStage::StructureSetup && HasDirtySourceTypeDraft())
    {
        StatusMessage = TEXT("Apply or cancel the pending Character Structure change before leaving Stage 1.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
        return;
    }

    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Stage == EDWCTransparencyEditorStage::RevealEditing && !CanEnterRevealEditingStage())
    {
        StatusMessage = TEXT("Generate the Stage 2 source map before entering Reveal Color editing.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return;
    }
    if (Stage == EDWCTransparencyEditorStage::RevealEditing && !EnsureRevealEditingWorkingMap())
    {
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return;
    }
    if (Stage == EDWCTransparencyEditorStage::FinalEditing && !CanEnterFinalEditingStage())
    {
        StatusMessage = TEXT("Complete source generation or load an existing Transparency Map before entering Stage 4.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return;
    }
    if (Stage == EDWCTransparencyEditorStage::FinalEditing && !EnsureFinalEditingWorkingMap())
    {
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return;
    }
    const FGuid StageLayerGuid = Layer != nullptr ? Layer->LayerGuid : FGuid();
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCSetTransparencyStageAction{StageLayerGuid, Stage});
    }
    else
    {
        StageByLayer.FindOrAdd(StageLayerGuid) = Stage;
    }
    if (Stage == EDWCTransparencyEditorStage::RevealEditing)
    {
        SelectedVisualizationMode = GetVisualizationModeForStage(Stage);
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
        }
        EnsureRevealEditingWorkingMap();
    }
    else if (Stage == EDWCTransparencyEditorStage::FinalEditing)
    {
        SelectedVisualizationMode = GetVisualizationModeForStage(Stage);
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
        }
        EnsureFinalEditingWorkingMap();
    }
    DispatchTransparencyPreviewState();
    DispatchTransparencyEditContext();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
}

FReply SWetClothingTransparencyBakePanel::HandleStageClicked(const EDWCTransparencyEditorStage Stage)
{
    SetCurrentStage(Stage);
    return FReply::Handled();
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsStageChecked(const EDWCTransparencyEditorStage Stage) const
{
    return GetCurrentStage() == Stage ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsSourceTypeCardChecked(
    const EDWCTransparencySourceType SourceType) const
{
    const FDWCEditorTransparencySessionState* Transparency = SessionStore.IsValid()
        ? &SessionStore->GetState().Transparency
        : nullptr;
    return Transparency != nullptr &&
        Transparency->bDraftCharacterTypeConfigured &&
        Transparency->DraftCharacterType == SourceType
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

FText SWetClothingTransparencyBakePanel::GetSourceTypeCardStatusText(
    const EDWCTransparencySourceType SourceType,
    const FText Availability) const
{
    if (!IsSourceTypeAvailable(SourceType) || !SessionStore.IsValid())
    {
        return Availability;
    }

    const FDWCEditorTransparencySessionState& Transparency =
        SessionStore->GetState().Transparency;
    if (Transparency.bDraftCharacterTypeConfigured &&
        Transparency.DraftCharacterType == SourceType &&
        Transparency.bCharacterTypeDraftDirty)
    {
        return LOCTEXT("TransparencyTypeSelectedNotApplied", "Selected - Not Applied");
    }
    if (Transparency.bSavedCharacterTypeConfigured &&
        Transparency.SavedCharacterType == SourceType)
    {
        return LOCTEXT("TransparencyTypeSaved", "Saved");
    }
    return Availability;
}

bool SWetClothingTransparencyBakePanel::HasDirtySourceTypeDraft() const
{
    return SessionStore.IsValid() &&
        SessionStore->GetState().Transparency.bCharacterTypeDraftDirty;
}

EVisibility SWetClothingTransparencyBakePanel::GetSourceTypeDraftStatusVisibility() const
{
    return HasDirtySourceTypeDraft() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SWetClothingTransparencyBakePanel::GetSourceTypeDraftStatusText() const
{
    if (!SessionStore.IsValid())
    {
        return FText::GetEmpty();
    }

    return FText::Format(
        LOCTEXT(
            "TransparencySourceTypeDraftStatus",
            "Selected: {0} (not applied). Continue to Stage 2 to apply this change."),
        UE::DWCEditor::TransparencyPanel::GetSourceTypeLabel(
            SessionStore->GetState().Transparency.DraftCharacterType));
}

bool SWetClothingTransparencyBakePanel::IsSourceTypeAvailable(
    const EDWCTransparencySourceType SourceType) const
{
    return DWCTransparencyWorkflow::IsSourceTypeAvailable(SourceType);
}

bool SWetClothingTransparencyBakePanel::CanContinueToGeneration() const
{
    const FDWCEditorTransparencySessionState* Transparency = SessionStore.IsValid()
        ? &SessionStore->GetState().Transparency
        : nullptr;
    return DWCTransparencyWorkflow::CanContinueToGeneration(
        WetClothingAsset.IsValid(),
        Transparency != nullptr && Transparency->bDraftCharacterTypeConfigured,
        Transparency != nullptr
            ? Transparency->DraftCharacterType
            : EDWCTransparencySourceType::SameMeshMaterialSlots);
}

void SWetClothingTransparencyBakePanel::InitializeCharacterTypeSessionState()
{
    if (!SessionStore.IsValid())
    {
        return;
    }

    FDWCInitializeTransparencyCharacterTypeAction Action;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Action.SavedType = Asset->Authored.TransparencyData.CharacterStructureType;
        Action.bSavedTypeConfigured =
            Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured;
    }
    SessionStore->Dispatch(Action);
}

void SWetClothingTransparencyBakePanel::ReconcileCharacterTypeSessionState()
{
    if (!SessionStore.IsValid())
    {
        return;
    }

    FDWCReconcileTransparencyCharacterTypeAction Action;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Action.SavedType = Asset->Authored.TransparencyData.CharacterStructureType;
        Action.bSavedTypeConfigured =
            Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured;
    }
    SessionStore->Dispatch(Action);
}

DWCTransparencyWorkflow::FDWCTransparencyTypeChangeImpact
SWetClothingTransparencyBakePanel::EvaluateCharacterTypeChangeImpact() const
{
    DWCTransparencyWorkflow::FDWCTransparencyTypeChangeImpact Impact;
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return Impact;
    }

    const TArray<FWetClothingTransparencyLayerData>& Layers =
        Asset->Authored.TransparencyData.TransparencyLayers;
    Impact.LayerCount = Layers.Num();
    for (const FWetClothingTransparencyLayerData& Layer : Layers)
    {
        Impact.RevealStrokeCount += Layer.GetRevealColorPaintStrokes().Num();
        Impact.AlphaStrokeCount += Layer.GetEditableStrokes().Num();
        if (Layer.AutoBakeMetadata.AutoBakeGuid.IsValid() ||
            !Layer.AutoBakeMetadata.BuildSignature.IsEmpty())
        {
            ++Impact.GeneratedResultCount;
        }
        Impact.GeneratedResultCount += Layer.BakedMaps.Num();
    }
    return Impact;
}

FReply SWetClothingTransparencyBakePanel::HandleSourceTypeCardClicked(
    const EDWCTransparencySourceType SourceType)
{
    if (!WetClothingAsset.IsValid() || !SessionStore.IsValid())
    {
        return FReply::Handled();
    }

    if (!IsSourceTypeAvailable(SourceType))
    {
        StatusMessage = TEXT("This Transparency character structure is not implemented yet.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
        return FReply::Handled();
    }

    SessionStore->Dispatch(FDWCSelectTransparencyCharacterTypeDraftAction{SourceType});
    if (GetCurrentStage() != EDWCTransparencyEditorStage::StructureSetup)
    {
        SetCurrentStage(EDWCTransparencyEditorStage::StructureSetup);
    }
    else
    {
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleCancelSourceTypeDraftClicked()
{
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCCancelTransparencyCharacterTypeDraftAction{});
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleContinueToGenerationClicked()
{
    if (!CanContinueToGeneration() || !SessionStore.IsValid())
    {
        StatusMessage = TEXT("Choose an available character structure type before continuing to Stage 2.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
        return FReply::Handled();
    }


    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return FReply::Handled();
    }

    const FDWCEditorTransparencySessionState& TransparencySession =
        SessionStore->GetState().Transparency;
    const EDWCTransparencySourceType DraftType = TransparencySession.DraftCharacterType;
    if (!TransparencySession.bCharacterTypeDraftDirty)
    {
        SetCurrentStage(EDWCTransparencyEditorStage::MapGeneration);
        return FReply::Handled();
    }

    const DWCTransparencyWorkflow::FDWCTransparencyTypeChangeImpact Impact =
        EvaluateCharacterTypeChangeImpact();
    if (Impact.RequiresConfirmation())
    {
        const FText Message = FText::Format(
            LOCTEXT(
                "ConfirmTransparencyCharacterTypeChange",
                "Change Transparency Character Structure?\n\n"
                "Current: {0}\nNew: {1}\n\n"
                "{2} target part(s) will use the new source type. Previous source settings will be cleared, and generated Transparency results will become out of date.\n\n"
                "Reveal Color strokes ({3}) and Transparency Alpha strokes ({4}) will be preserved."),
            UE::DWCEditor::TransparencyPanel::GetSourceTypeLabel(TransparencySession.SavedCharacterType),
            UE::DWCEditor::TransparencyPanel::GetSourceTypeLabel(DraftType),
            FText::AsNumber(Impact.LayerCount),
            FText::AsNumber(Impact.RevealStrokeCount),
            FText::AsNumber(Impact.AlphaStrokeCount));
        if (FMessageDialog::Open(EAppMsgType::YesNo, Message) != EAppReturnType::Yes)
        {
            return FReply::Handled();
        }
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake |
        EDWCEditorAuthoringImpact::Details;
    const FDWCEditorAuthoringResult Result = AuthoringDocument.IsValid()
        ? AuthoringDocument->Edit(
            LOCTEXT("ChangeTransparencyStructureType", "Change Transparency Character Structure"),
            Change,
            [DraftType](UWetClothingAsset& MutableAsset)
            {
                DWCTransparencyWorkflow::ApplyCharacterTypeCommit(
                    MutableAsset.Authored.TransparencyData,
                    DraftType);
                return true;
            })
        : FDWCEditorAuthoringResult{};
    if (!Result.bChanged)
    {
        StatusMessage = Result.Error.IsEmpty()
            ? TEXT("Could not apply the Transparency character structure change.")
            : Result.Error;
        PanelStatus = EDWCTransparencyPanelStatus::Error;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
        return FReply::Handled();
    }

    AutoBakeResults.Reset();
    SessionStore->Dispatch(FDWCCommitTransparencyCharacterTypeSucceededAction{DraftType});
    StatusMessage = FString::Printf(
        TEXT("Transparency character structure changed to %s. Generated results are out of date."),
        *UE::DWCEditor::TransparencyPanel::GetSourceTypeLabel(DraftType).ToString());
    PanelStatus = EDWCTransparencyPanelStatus::Warning;
    SetCurrentStage(EDWCTransparencyEditorStage::MapGeneration);
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::RefreshStageContent()
{
    FDWCTransparencyBaselineDiagnostics::RecordStageContentRefresh();
    if (!StageContentSwitcher.IsValid())
    {
        return;
    }

    switch (GetCurrentStage())
    {
    case EDWCTransparencyEditorStage::MapGeneration:
        StageContentSwitcher->SetActiveWidgetIndex(1);
        RefreshMapGenerationSettings();
        break;
    case EDWCTransparencyEditorStage::FinalEditing:
        StageContentSwitcher->SetActiveWidgetIndex(3);
        RefreshFinalEditingContent();
        break;
    case EDWCTransparencyEditorStage::RevealEditing:
        StageContentSwitcher->SetActiveWidgetIndex(2);
        RefreshRevealEditingContent();
        break;
    default:
        StageContentSwitcher->SetActiveWidgetIndex(0);
        break;
    }
}

void SWetClothingTransparencyBakePanel::RefreshMapGenerationSettings()
{
    if (!MapGenerationSettingsSwitcher.IsValid())
    {
        return;
    }

    int32 SettingsIndex = 0;
    if (const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer())
    {
        switch (Layer->SourceType)
        {
        case EDWCTransparencySourceType::SameMeshMaterialSlots:
            SettingsIndex = 1;
            break;
        case EDWCTransparencySourceType::OtherSkeletalMeshComponents:
            SettingsIndex = 2;
            break;
        case EDWCTransparencySourceType::ManualColorOrTexture:
            SettingsIndex = 3;
            break;
        case EDWCTransparencySourceType::ExternalSkeletalMesh:
            SettingsIndex = 4;
            break;
        default:
            break;
        }
    }

    MapGenerationSettingsSwitcher->SetActiveWidgetIndex(SettingsIndex);
    RefreshInnerSourceSlotItems();
    RefreshBlueprintHierarchy(false);
    RefreshBlueprintSourcePriorityItems();
    RefreshExternalSourcePriorityItems();
    RefreshRevealColorStrokeList();
}

void SWetClothingTransparencyBakePanel::RefreshInnerSourceSlotItems()
{
    InnerSourceSlotItems.Reset();
    if (const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer())
    {
        for (int32 PriorityIndex = 0; PriorityIndex < Layer->SameMeshSource.InnerSlotPriority.Num(); ++PriorityIndex)
        {
            InnerSourceSlotItems.Add(MakeShared<int32>(PriorityIndex));
        }
    }

    if (InnerSourceListView.IsValid())
    {
        InnerSourceListView->RequestListRefresh();
    }
}

bool SWetClothingTransparencyBakePanel::IsBlueprintHierarchyCurrent() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return Layer != nullptr && BlueprintHierarchySession.IsValid() &&
        Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents &&
        BlueprintHierarchySession->GetSnapshot().IsReadyFor(
            Layer->LayerGuid,
            Layer->BlueprintSource.BlueprintClass.ToSoftObjectPath());
}

bool SWetClothingTransparencyBakePanel::IsBlueprintTargetCandidate(
    const FDWCTransparencyBlueprintMeshComponentMetadata& Component) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr && !Component.SkeletalMeshPath.IsNull() &&
        (Component.SkeletalMeshPath == FSoftObjectPath(Asset->GetRuntimeSkeletalMesh()) ||
            Component.SkeletalMeshPath == FSoftObjectPath(Asset->GetSourceSkeletalMesh()));
}

bool SWetClothingTransparencyBakePanel::HasBlueprintTargetCandidate() const
{
    if (!IsBlueprintHierarchyCurrent())
    {
        return false;
    }

    const FDWCTransparencyBlueprintHierarchySnapshot& Snapshot =
        BlueprintHierarchySession->GetSnapshot();
    return Snapshot.Hierarchy.MeshComponents.ContainsByPredicate(
        [this](const FDWCTransparencyBlueprintMeshComponentMetadata& Component)
        {
            return IsBlueprintTargetCandidate(Component);
        });
}

int32 SWetClothingTransparencyBakePanel::GetBlueprintHierarchyDepth(
    const FDWCTransparencyBlueprintMeshComponentMetadata& Component) const
{
    return Component.HierarchyDepth;
}

void SWetClothingTransparencyBakePanel::RefreshBlueprintHierarchy(const bool bForceRefresh)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr ||
        Layer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
        Layer->BlueprintSource.BlueprintClass.IsNull())
    {
        if (BlueprintHierarchySession.IsValid())
        {
            BlueprintHierarchySession->Reset();
        }
        SyncBlueprintHierarchyItemsFromSession();
        return;
    }

    if (!BlueprintHierarchySession.IsValid())
    {
        return;
    }

    BlueprintHierarchySession->Request(
        Layer->LayerGuid,
        Layer->BlueprintSource.BlueprintClass,
        bForceRefresh);
    SyncBlueprintHierarchyItemsFromSession();
}

void SWetClothingTransparencyBakePanel::HandleBlueprintHierarchySessionChanged()
{
    SyncBlueprintHierarchyItemsFromSession();
    PushBlueprintHierarchySnapshotToPreview();

    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer != nullptr && IsBlueprintHierarchyCurrent())
    {
        FWetClothingTransparencyBlueprintSource ReconciledSource = Layer->BlueprintSource;
        const FDWCTransparencyType2BindingReconcileResult ReconcileResult =
            FDWCTransparencyBlueprintHierarchySession::ReconcileBindings(
                WetClothingAsset->GetRuntimeSkeletalMesh(),
                WetClothingAsset->GetSourceSkeletalMesh(),
                *Layer,
                BlueprintHierarchySession->GetSnapshot(),
                ReconciledSource);
        if (ReconcileResult.bChanged)
        {
            EditSelectedLayer(
                LOCTEXT("ReconcileTransparencyBlueprintBindings", "Refresh Transparency Blueprint Bindings"),
                [ReconciledSource = MoveTemp(ReconciledSource)](
                    FWetClothingTransparencyLayerData& MutableLayer) mutable
                {
                    MutableLayer.BlueprintSource = MoveTemp(ReconciledSource);
                    return true;
                },
                EDWCTransparencyPanelRefreshFlags::Model |
                    EDWCTransparencyPanelRefreshFlags::SourceModel |
                    EDWCTransparencyPanelRefreshFlags::Viewport);
        }
    }

    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::SourceModel);
    RefreshType2PreviewAfterStructureChange();
}

void SWetClothingTransparencyBakePanel::PushBlueprintHierarchySnapshotToPreview()
{
    if (PreviewViewport.IsValid() && BlueprintHierarchySession.IsValid())
    {
        PreviewViewport->SetType2BlueprintHierarchySnapshot(
            BlueprintHierarchySession->GetSnapshot());
    }
}

void SWetClothingTransparencyBakePanel::SyncBlueprintHierarchyItemsFromSession()
{
    const uint64 Revision = BlueprintHierarchySession.IsValid()
        ? BlueprintHierarchySession->GetSnapshot().Revision
        : 0;
    if (BlueprintHierarchyItemsRevision == Revision)
    {
        return;
    }
    BlueprintHierarchyItemsRevision = Revision;
    BlueprintHierarchyItems.Reset();
    if (BlueprintHierarchySession.IsValid())
    {
        const FDWCTransparencyBlueprintHierarchySnapshot& Snapshot =
            BlueprintHierarchySession->GetSnapshot();
        if (Snapshot.State == EDWCTransparencyBlueprintHierarchyState::Ready)
        {
            BlueprintHierarchyItems.Reserve(Snapshot.Hierarchy.MeshComponents.Num());
            for (const FDWCTransparencyBlueprintMeshComponentMetadata& Component :
                 Snapshot.Hierarchy.MeshComponents)
            {
                BlueprintHierarchyItems.Add(
                    MakeShared<FDWCTransparencyBlueprintMeshComponentMetadata>(Component));
            }
        }
    }

    if (BlueprintHierarchyListView.IsValid())
    {
        BlueprintHierarchyListView->RequestListRefresh();
    }
}

FString SWetClothingTransparencyBakePanel::GetBlueprintHierarchyError() const
{
    if (!BlueprintHierarchySession.IsValid())
    {
        return TEXT("The Source Blueprint hierarchy session is unavailable.");
    }
    const FDWCTransparencyBlueprintHierarchySnapshot& Snapshot =
        BlueprintHierarchySession->GetSnapshot();
    return Snapshot.State == EDWCTransparencyBlueprintHierarchyState::Error
        ? Snapshot.Error
        : FString();
}

void SWetClothingTransparencyBakePanel::RefreshType2PreviewAfterStructureChange()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!PreviewViewport.IsValid() || Layer == nullptr ||
        Layer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
        GetCurrentStage() != EDWCTransparencyEditorStage::MapGeneration)
    {
        return;
    }

    PushBlueprintHierarchySnapshotToPreview();
    const bool bWasFullSourcePreview =
        PreviewViewport->GetPreviewMode() == EWetClothingTransparencyPreviewMode::FullBlueprint;
    PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::FullBlueprint);
    if (bWasFullSourcePreview)
    {
        PreviewViewport->InvalidateFullSourceLayout();
    }
}

void SWetClothingTransparencyBakePanel::SyncType2PreviewSourcesAfterSelectionChange()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!PreviewViewport.IsValid() || Layer == nullptr ||
        Layer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
        GetCurrentStage() != EDWCTransparencyEditorStage::MapGeneration)
    {
        return;
    }

    PushBlueprintHierarchySnapshotToPreview();
    const bool bWasFullSourcePreview =
        PreviewViewport->GetPreviewMode() == EWetClothingTransparencyPreviewMode::FullBlueprint;
    PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::FullBlueprint);
    if (bWasFullSourcePreview)
    {
        PreviewViewport->SyncType2SelectedSourceComponents();
    }
}

void SWetClothingTransparencyBakePanel::RefreshBlueprintSourcePriorityItems()
{
    BlueprintSourcePriorityItems.Reset();
    if (const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer())
    {
        for (int32 PriorityIndex = 0;
             PriorityIndex < Layer->BlueprintSource.SourcePriority.Num();
             ++PriorityIndex)
        {
            BlueprintSourcePriorityItems.Add(MakeShared<int32>(PriorityIndex));
        }
    }
    if (BlueprintSourcePriorityListView.IsValid())
    {
        BlueprintSourcePriorityListView->RequestListRefresh();
    }
}

void SWetClothingTransparencyBakePanel::RefreshExternalSourcePriorityItems()
{
    ExternalSourcePriorityItems.Reset();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer != nullptr)
    {
        for (int32 PriorityIndex = 0;
             PriorityIndex < Layer->ExternalMeshSource.SourcePriority.Num();
             ++PriorityIndex)
        {
            ExternalSourcePriorityItems.Add(MakeShared<int32>(PriorityIndex));
        }
    }
    if (ExternalSourcePriorityListView.IsValid())
    {
        ExternalSourcePriorityListView->RequestListRefresh();
        const FDWCTransparencyPlacementSelection& Selection = PlacementSession->GetSelection();
        const TSharedPtr<int32>* SelectedItem = ExternalSourcePriorityItems.FindByPredicate(
            [Layer, &Selection](const TSharedPtr<int32>& Item)
            {
                return Selection.IsSource() && Item.IsValid() && Layer != nullptr &&
                    Layer->ExternalMeshSource.SourcePriority.IsValidIndex(*Item) &&
                    Layer->ExternalMeshSource.SourcePriority[*Item].SourceGuid == Selection.SourceGuid;
            });
        if (SelectedItem != nullptr)
        {
            ExternalSourcePriorityListView->SetSelection(*SelectedItem, ESelectInfo::Direct);
        }
        else
        {
            ExternalSourcePriorityListView->ClearSelection();
        }
    }
}

FReply SWetClothingTransparencyBakePanel::HandleRefreshBlueprintHierarchyClicked()
{
    RefreshBlueprintHierarchy(true);
    UpdateInnerSourceStatus();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::SourceModel);
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleBlueprintTargetComponentChanged(
    const ECheckBoxState NewState,
    const FName ComponentName)
{
    const TSharedPtr<FDWCTransparencyBlueprintMeshComponentMetadata>* Component =
        BlueprintHierarchyItems.FindByPredicate(
            [ComponentName](
                const TSharedPtr<FDWCTransparencyBlueprintMeshComponentMetadata>& Candidate)
            {
                return Candidate.IsValid() && Candidate->ComponentName == ComponentName;
            });
    if (Component == nullptr || !Component->IsValid() || !IsBlueprintTargetCandidate(**Component))
    {
        return;
    }

    if (!EditSelectedLayer(
        LOCTEXT("SetTransparencyBlueprintTarget", "Set Transparency Blueprint Target"),
        [NewState, ComponentName, Component = *Component](FWetClothingTransparencyLayerData& MutableLayer)
        {
            FWetClothingTransparencyBlueprintSource& BlueprintSource = MutableLayer.BlueprintSource;
            if (NewState == ECheckBoxState::Checked)
            {
                const bool bAlreadyTarget =
                    BlueprintSource.TargetComponent.ComponentName == ComponentName &&
                    BlueprintSource.TargetComponent.ExpectedSkeletalMesh.ToSoftObjectPath() ==
                        Component->SkeletalMeshPath;
                const int32 RemovedSourceCount = BlueprintSource.SourcePriority.RemoveAll(
                    [ComponentName](const FWetClothingTransparencyBlueprintComponentBinding& Source)
                    {
                        return Source.ComponentName == ComponentName;
                    });
                if (bAlreadyTarget && RemovedSourceCount == 0)
                {
                    return false;
                }
                BlueprintSource.TargetComponent.ComponentName = ComponentName;
                BlueprintSource.TargetComponent.ExpectedSkeletalMesh =
                    TSoftObjectPtr<USkeletalMesh>(Component->SkeletalMeshPath);
            }
            else if (BlueprintSource.TargetComponent.ComponentName == ComponentName)
            {
                BlueprintSource.TargetComponent = FWetClothingTransparencyBlueprintComponentBinding{};
            }
            else
            {
                return false;
            }
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport))
    {
        return;
    }
    RefreshType2PreviewAfterStructureChange();
}

void SWetClothingTransparencyBakePanel::HandleBlueprintSourceComponentChanged(
    const ECheckBoxState NewState,
    const FName ComponentName)
{
    const TSharedPtr<FDWCTransparencyBlueprintMeshComponentMetadata>* Component =
        BlueprintHierarchyItems.FindByPredicate(
            [ComponentName](
                const TSharedPtr<FDWCTransparencyBlueprintMeshComponentMetadata>& Candidate)
            {
                return Candidate.IsValid() && Candidate->ComponentName == ComponentName;
            });
    if (Component == nullptr || !Component->IsValid())
    {
        return;
    }

    if (!EditSelectedLayer(
        LOCTEXT("SetTransparencyBlueprintSource", "Set Transparency Blueprint Source"),
        [NewState, ComponentName, Component = *Component](FWetClothingTransparencyLayerData& MutableLayer)
        {
            FWetClothingTransparencyBlueprintSource& BlueprintSource = MutableLayer.BlueprintSource;
            if (BlueprintSource.TargetComponent.ComponentName == ComponentName)
            {
                return false;
            }
            if (NewState == ECheckBoxState::Checked)
            {
                if (BlueprintSource.SourcePriority.ContainsByPredicate(
                        [ComponentName](const FWetClothingTransparencyBlueprintComponentBinding& Source)
                        {
                            return Source.ComponentName == ComponentName;
                        }))
                {
                    return false;
                }
                FWetClothingTransparencyBlueprintComponentBinding& Source =
                    BlueprintSource.SourcePriority.AddDefaulted_GetRef();
                Source.ComponentName = ComponentName;
                Source.ExpectedSkeletalMesh =
                    TSoftObjectPtr<USkeletalMesh>(Component->SkeletalMeshPath);
            }
            else
            {
                if (BlueprintSource.SourcePriority.RemoveAll(
                    [ComponentName](const FWetClothingTransparencyBlueprintComponentBinding& Source)
                    {
                        return Source.ComponentName == ComponentName;
                    }) == 0)
                {
                    return false;
                }
            }
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport))
    {
        return;
    }
    SyncType2PreviewSourcesAfterSelectionChange();
}

FReply SWetClothingTransparencyBakePanel::HandleRemoveBlueprintSourceClicked(const int32 PriorityIndex)
{
    if (EditSelectedLayer(
        LOCTEXT("RemoveTransparencyBlueprintSource", "Remove Transparency Blueprint Source"),
        [PriorityIndex](FWetClothingTransparencyLayerData& MutableLayer)
        {
            if (!MutableLayer.BlueprintSource.SourcePriority.IsValidIndex(PriorityIndex))
            {
                return false;
            }
            MutableLayer.BlueprintSource.SourcePriority.RemoveAt(PriorityIndex);
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport))
    {
        SyncType2PreviewSourcesAfterSelectionChange();
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleMoveBlueprintSourceClicked(
    const int32 PriorityIndex,
    const int32 Direction)
{
    if (EditSelectedLayer(
        LOCTEXT("ReorderTransparencyBlueprintSource", "Reorder Transparency Blueprint Source"),
        [PriorityIndex, Direction](FWetClothingTransparencyLayerData& MutableLayer)
        {
            TArray<FWetClothingTransparencyBlueprintComponentBinding>& Sources =
                MutableLayer.BlueprintSource.SourcePriority;
            const int32 Destination = PriorityIndex + Direction;
            if (!Sources.IsValidIndex(PriorityIndex) || !Sources.IsValidIndex(Destination))
            {
                return false;
            }
            Sources.Swap(PriorityIndex, Destination);
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport))
    {
        SyncType2PreviewSourcesAfterSelectionChange();
    }
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleBlueprintSourceUVChannelChanged(
    TSharedPtr<int32> Item,
    ESelectInfo::Type,
    const int32 PriorityIndex)
{
    if (!Item.IsValid())
    {
        return;
    }
    EditSelectedLayer(
        LOCTEXT("SetTransparencyBlueprintSourceUV", "Set Transparency Blueprint Source UV"),
        [PriorityIndex, UVChannel = *Item](FWetClothingTransparencyLayerData& MutableLayer)
        {
            if (!MutableLayer.BlueprintSource.SourcePriority.IsValidIndex(PriorityIndex) ||
                MutableLayer.BlueprintSource.SourcePriority[PriorityIndex].SourceUVChannel == UVChannel)
            {
                return false;
            }
            MutableLayer.BlueprintSource.SourcePriority[PriorityIndex].SourceUVChannel = UVChannel;
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport);
}

void SWetClothingTransparencyBakePanel::HandleBlueprintSourceRoleChanged(
    TSharedPtr<EDWCTransparencyBlueprintSourceRole> Item,
    ESelectInfo::Type,
    const int32 PriorityIndex)
{
    if (!Item.IsValid())
    {
        return;
    }
    EditSelectedLayer(
        LOCTEXT("SetTransparencyBlueprintSourceRole", "Set Transparency Blueprint Source Role"),
        [PriorityIndex, Role = *Item](FWetClothingTransparencyLayerData& MutableLayer)
        {
            if (!MutableLayer.BlueprintSource.SourcePriority.IsValidIndex(PriorityIndex) ||
                MutableLayer.BlueprintSource.SourcePriority[PriorityIndex].Role == Role)
            {
                return false;
            }
            MutableLayer.BlueprintSource.SourcePriority[PriorityIndex].Role = Role;
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport);
}

void SWetClothingTransparencyBakePanel::RefreshFinalEditingContent()
{
    if (FinalEditingNoticeContainer.IsValid())
    {
        FinalEditingNoticeContainer->SetContent(BuildFinalEditingNotice());
    }
    if (FinalEditingPreviewSettingsContainer.IsValid())
    {
        FinalEditingPreviewSettingsContainer->SetContent(BuildPreviewSettingsSection());
    }
    if (FinalEditingGeneratedOutputsContainer.IsValid())
    {
        GeneratedOutputThumbnails.Reset();
        FinalEditingGeneratedOutputsContainer->SetContent(BuildGeneratedOutputsSection());
    }
    RefreshTransparencyStrokeList();
}

void SWetClothingTransparencyBakePanel::RequestRefresh(
    const EDWCTransparencyPanelRefreshFlags Flags)
{
    PendingRefreshFlags |= Flags;
    if (bRefreshTimerRegistered)
    {
        return;
    }

    bRefreshTimerRegistered = true;
    RegisterActiveTimer(
        0.0f,
        FWidgetActiveTimerDelegate::CreateSP(
            this,
            &SWetClothingTransparencyBakePanel::HandleDeferredRefresh));
}

EActiveTimerReturnType SWetClothingTransparencyBakePanel::HandleDeferredRefresh(
    double,
    float)
{
    EDWCTransparencyPanelRefreshFlags RefreshFlags = PendingRefreshFlags;
    PendingRefreshFlags = EDWCTransparencyPanelRefreshFlags::None;

    if (EnumHasAnyFlags(RefreshFlags, EDWCTransparencyPanelRefreshFlags::Model) &&
        RefreshModelState())
    {
        RefreshFlags |= EDWCTransparencyPanelRefreshFlags::StageContent;
    }
    if (EnumHasAnyFlags(RefreshFlags, EDWCTransparencyPanelRefreshFlags::SourceModel))
    {
        RefreshSourceModelState();
    }
    if (EnumHasAnyFlags(RefreshFlags, EDWCTransparencyPanelRefreshFlags::StageContent))
    {
        // Stage changes only replace the active stage subtree. Rebuilding the
        // whole control panel needlessly recreates slot rows and thumbnails.
        if (StageContentSwitcher.IsValid())
        {
            RefreshStageContent();
        }
        else
        {
            RebuildEditorLayout();
        }
    }
    if (EnumHasAnyFlags(RefreshFlags, EDWCTransparencyPanelRefreshFlags::Viewport))
    {
        if (!bPreviewSuspended)
        {
            RefreshViewportContext();
        }
    }
    if (EnumHasAnyFlags(RefreshFlags, EDWCTransparencyPanelRefreshFlags::Details) &&
        DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    if (PendingRefreshFlags != EDWCTransparencyPanelRefreshFlags::None)
    {
        return EActiveTimerReturnType::Continue;
    }

    bRefreshTimerRegistered = false;
    return EActiveTimerReturnType::Stop;
}

bool SWetClothingTransparencyBakePanel::LoadBakedMapAsWorkingResult(
    const FWetClothingBakedTransparencyMap& BakedMap,
    const FWetClothingTransparencyLayerData& Layer,
    TSharedPtr<FDWCTransparencySourcePayload>& OutResult,
    FString& OutError) const
{
    OutResult.Reset();
    OutError.Reset();
    UTexture2D* Texture = BakedMap.TransparencyMap;
    if (Texture == nullptr || !Texture->Source.IsValid())
    {
        OutError = TEXT("The baked Transparency Map has no readable source data.");
        return false;
    }
    if (Texture->Source.GetFormat() != TSF_BGRA8)
    {
        OutError = FString::Printf(
            TEXT("The baked Transparency Map source format is not BGRA8 (%s)."),
            *GetNameSafe(Texture));
        return false;
    }

    const int32 Width = Texture->Source.GetSizeX();
    const int32 Height = Texture->Source.GetSizeY();
    FDWCTransparencyBakedBaselineMemoryPlan MemoryPlan;
    if (!FDWCTransparencyBakedBaselineMemoryPolicy::TryBuildPlan(
            FIntPoint(Width, Height),
            BakedMap.BuildSignature.Len(),
            MemoryPlan,
            OutError))
    {
        return false;
    }

    FDWCEditorMemoryLease ScratchLease;
    FDWCEditorMemoryLease RetainedLease;
    if (ResourceGovernor.IsValid())
    {
        FDWCEditorAsyncOperationIdentity Owner;
        Owner.Key.Namespace = TEXT("DWC.Transparency.BakedBaselineRestore");
        Owner.Key.MaterialSlotIndex = BakedMap.MaterialSlotIndex;
        Owner.Key.ResourceGuid = Layer.LayerGuid;
        Owner.SessionEpoch = WorkerJobScheduler.IsValid()
            ? WorkerJobScheduler->GetSessionEpoch()
            : FGuid::NewGuid();
        Owner.OperationId = ++BakedBaselineRestoreSerial;
        Owner.Generation = 1;
        Owner.Domain = EDWCEditorAuthoringDomain::Transparency;

        TArray<FDWCEditorResourceReservationRequest> Requests;
        FDWCEditorResourceReservationRequest& ScratchRequest = Requests.AddDefaulted_GetRef();
        ScratchRequest.Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
        ScratchRequest.Bytes = MemoryPlan.WorkerPeakBytes;
        ScratchRequest.Owner = Owner;
        ScratchRequest.DebugName = TEXT("Transparency baked baseline restore scratch");
        FDWCEditorResourceReservationRequest& RetainedRequest = Requests.AddDefaulted_GetRef();
        RetainedRequest.Pool = EDWCEditorResourcePool::PreviewWorkspaceCPU;
        RetainedRequest.Bytes = MemoryPlan.RetainedPayloadBytes;
        RetainedRequest.Owner = Owner;
        RetainedRequest.DebugName = TEXT("Transparency baked baseline retained payload");

        EDWCEditorResourceAdmissionResult Admission =
            EDWCEditorResourceAdmissionResult::InvalidRequest;
        FDWCEditorMemoryLeaseSet Leases =
            ResourceGovernor->TryAcquireBundleForAdmission(Requests, Admission, &OutError);
        if (!Leases.IsValid())
        {
            OutError = FString::Printf(
                TEXT("The baked Transparency Map baseline cannot be restored within the editor memory budget (scratch %.2f MiB, retained %.2f MiB). %s"),
                static_cast<double>(MemoryPlan.WorkerPeakBytes) /
                    FDWCEditorResourceBudgetConfig::MiB,
                static_cast<double>(MemoryPlan.RetainedPayloadBytes) /
                    FDWCEditorResourceBudgetConfig::MiB,
                *OutError);
            return false;
        }
        ScratchLease = Leases.TakeLease(EDWCEditorResourcePool::WorkerPrivateCPU);
        RetainedLease = Leases.TakeLease(EDWCEditorResourcePool::PreviewWorkspaceCPU);
        if (!ScratchLease.IsValid() || !RetainedLease.IsValid())
        {
            OutError = TEXT("The baked baseline memory admission did not return both required leases.");
            return false;
        }
    }

    const int32 PixelCount = Width * Height;
    TArray64<uint8> RawMipData;
    if (Width <= 0 || Height <= 0 || !Texture->Source.GetMipData(RawMipData, 0) ||
        RawMipData.Num() < static_cast<int64>(PixelCount) * static_cast<int64>(sizeof(FColor)))
    {
        OutError = FString::Printf(TEXT("Could not read baked Transparency Map pixels from '%s'."), *GetNameSafe(Texture));
        return false;
    }

    TSharedPtr<FDWCTransparencySourcePayload> Result = MakeShared<FDWCTransparencySourcePayload>();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    Result->LayerGuid = Layer.LayerGuid;
    Result->MaterialSlotIndex = BakedMap.MaterialSlotIndex;
    Result->UVChannelIndex = Asset != nullptr ? Asset->GetDWCDataUVChannelIndex() : INDEX_NONE;
    Result->LODIndex = 0;
    Result->Resolution = FIntPoint(Width, Height);
    Result->BuildSignature = BakedMap.BuildSignature;
    Result->InnerColorBuffer.SetNumUninitialized(PixelCount);
    Result->AutoAlphaBuffer.SetNumUninitialized(PixelCount);
    Result->ValidHitBuffer.Init(true, PixelCount);
    Result->HitDistanceBuffer.Init(0.0f, PixelCount);
    Result->SourcePriorityBuffer.Init(INDEX_NONE, PixelCount);
    const FColor* SourcePixels = reinterpret_cast<const FColor*>(RawMipData.GetData());
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        Result->InnerColorBuffer[PixelIndex] = SourcePixels[PixelIndex];
        Result->InnerColorBuffer[PixelIndex].A = 255;
        Result->AutoAlphaBuffer[PixelIndex] = SourcePixels[PixelIndex].A;
    }
    const uint64 ReadbackBytes = static_cast<uint64>(RawMipData.GetAllocatedSize());
    RawMipData.Empty();

    if (Asset == nullptr ||
        !FDWCTransparencyAutoMapGenerator::BuildTargetSurfaceBuffers(
            *Asset,
            Layer.TargetSurface,
            0,
            Result->Resolution,
            Result->OuterCoverageBuffer,
            Result->OuterIslandIDBuffer,
            &Result->OuterSampleCount,
            &Result->OverlappedUVPixelCount,
            OutError,
            CacheStore))
    {
        OutError = FString::Printf(
            TEXT("Could not rebuild target UV island clip data for baked Transparency Map editing. %s"),
            *OutError);
        return false;
    }
    Result->bIsFinalBakedBaseline = true;
    Result->BaselineStrokeCount = FMath::Clamp(
        BakedMap.BakedStrokeCount,
        0,
        Layer.GetEditableStrokes().Num());
    Result->BaselineBakeGuid = BakedMap.BakeGuid;
    if (ResourceGovernor.IsValid())
    {
        TSharedPtr<FDWCEditorAccountedMemory, ESPMode::ThreadSafe> Account =
            MakeShared<FDWCEditorAccountedMemory, ESPMode::ThreadSafe>();
        FDWCEditorAsyncOperationIdentity Owner;
        Owner.Key.Namespace = TEXT("DWC.Transparency.SourcePayload");
        Owner.Key.MaterialSlotIndex = Result->MaterialSlotIndex;
        Owner.Key.ResourceGuid = Result->LayerGuid;
        Owner.SessionEpoch = WorkerJobScheduler.IsValid()
            ? WorkerJobScheduler->GetSessionEpoch()
            : FGuid::NewGuid();
        Owner.OperationId = BakedBaselineRestoreSerial;
        Owner.Generation = 1;
        Owner.Domain = EDWCEditorAuthoringDomain::Transparency;
        Account->Configure(
            ResourceGovernor,
            EDWCEditorResourcePool::PreviewWorkspaceCPU,
            Owner,
            TEXT("Transparency baked baseline payload"));
        if (!Account->AdoptExistingLease(
                MoveTemp(RetainedLease),
                FMath::Max<uint64>(Result->GetAllocatedBytes(), 1),
                &OutError))
        {
            OutError = FString::Printf(
                TEXT("The baked Transparency Map baseline exceeded its retained memory plan. %s"),
                *OutError);
            return false;
        }
        Result->PersistentMemoryAccount = MoveTemp(Account);
    }
    ScratchLease.Reset();
    FDWCTransparencyBaselineDiagnostics::RecordBakedBaselineRestore(ReadbackBytes);
    OutResult = MoveTemp(Result);
    return true;
}

bool SWetClothingTransparencyBakePanel::EnsureSourcePayloadAccounted(
    const TSharedPtr<FDWCTransparencySourcePayload>& Payload,
    FString& OutError) const
{
    OutError.Reset();
    if (!Payload.IsValid())
    {
        OutError = TEXT("The Transparency working payload is invalid.");
        return false;
    }
    if (!ResourceGovernor.IsValid())
    {
        return true;
    }

    const uint64 ActualBytes = FMath::Max<uint64>(Payload->GetAllocatedBytes(), 1);
    if (Payload->PersistentMemoryAccount.IsValid())
    {
        if (Payload->PersistentMemoryAccount->GetActualBytes() == ActualBytes &&
            Payload->PersistentMemoryAccount->IsAccounted())
        {
            return true;
        }
        if (!Payload->PersistentMemoryAccount->TryAdoptActualBytes(ActualBytes, &OutError))
        {
            OutError = FString::Printf(
                TEXT("The Transparency working payload could not update its memory reservation. %s"),
                *OutError);
            return false;
        }
        return true;
    }

    FDWCEditorAsyncOperationIdentity Owner;
    Owner.Key.Namespace = TEXT("DWC.Transparency.SourcePayload");
    Owner.Key.MaterialSlotIndex = Payload->MaterialSlotIndex;
    Owner.Key.ResourceGuid = Payload->LayerGuid;
    Owner.SessionEpoch = WorkerJobScheduler.IsValid()
        ? WorkerJobScheduler->GetSessionEpoch()
        : FGuid::NewGuid();
    Owner.OperationId = 1;
    Owner.Generation = 1;
    Owner.Domain = EDWCEditorAuthoringDomain::Transparency;

    TSharedPtr<FDWCEditorAccountedMemory, ESPMode::ThreadSafe> Account =
        MakeShared<FDWCEditorAccountedMemory, ESPMode::ThreadSafe>();
    Account->Configure(
        ResourceGovernor,
        EDWCEditorResourcePool::PreviewWorkspaceCPU,
        Owner,
        TEXT("Transparency canonical source payload"));
    if (!Account->TryAdoptActualBytes(ActualBytes, &OutError))
    {
        OutError = FString::Printf(
            TEXT("The Transparency working payload requires %.2f MiB, but the preview workspace cannot retain it. %s"),
            static_cast<double>(ActualBytes) / (1024.0 * 1024.0),
            *OutError);
        return false;
    }

    Payload->PersistentMemoryAccount = MoveTemp(Account);
    return true;
}

bool SWetClothingTransparencyBakePanel::EnsureFinalEditingWorkingMap()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Layer == nullptr || Asset == nullptr)
    {
        return false;
    }
    if (const TSharedPtr<FDWCTransparencySourcePayload>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
        Existing != nullptr && Existing->IsValid() && !(*Existing)->bIsFinalBakedBaseline)
    {
        return true;
    }

    FString RestoreError;
    if (RestoreCanonicalWorkingMap(RestoreError))
    {
        return true;
    }

    // Old assets can have a baked output but no canonical Stage 2 Temp
    // artifacts. Keep that baseline path as a read-only compatibility fallback.
    if (const TSharedPtr<FDWCTransparencySourcePayload>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
        Existing != nullptr && Existing->IsValid())
    {
        return true;
    }

    const FWetClothingBakedTransparencyMap* BakedMap = UE::DWCEditor::TransparencyPanel::FindExactBakedMap(Asset, Layer);
    if (BakedMap == nullptr)
    {
        return false;
    }
    TSharedPtr<FDWCTransparencySourcePayload> LoadedResult;
    FString LoadError;
    if (!LoadBakedMapAsWorkingResult(*BakedMap, *Layer, LoadedResult, LoadError))
    {
        StatusMessage = LoadError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return false;
    }

    FString AccountingError;
    if (!EnsureSourcePayloadAccounted(LoadedResult, AccountingError))
    {
        StatusMessage = AccountingError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return false;
    }

    AutoBakeResults.Reset();
    AutoBakeResults.Add(Layer->LayerGuid, MoveTemp(LoadedResult));
    if (!IsVisualizationModeAvailable(SelectedVisualizationMode, GetCurrentStage()))
    {
        SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
    }
    return true;
}

bool SWetClothingTransparencyBakePanel::EnsureRevealEditingWorkingMap()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        return false;
    }

    if (const TSharedPtr<FDWCTransparencySourcePayload>* Existing =
            AutoBakeResults.Find(Layer->LayerGuid);
        Existing != nullptr && Existing->IsValid() &&
        !(*Existing)->bIsFinalBakedBaseline)
    {
        return true;
    }
    FString RestoreError;
    if (RestoreCanonicalWorkingMap(RestoreError))
    {
        return true;
    }
    StatusMessage = RestoreError.IsEmpty()
        ? TEXT("The Stage 2 source working map is unavailable. Return to Stage 2 and generate it again.")
        : RestoreError;
    PanelStatus = EDWCTransparencyPanelStatus::Warning;
    return false;
}

bool SWetClothingTransparencyBakePanel::RestoreCanonicalWorkingMap(FString& OutError)
{
    OutError.Reset();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        OutError = TEXT("Select a Transparency Target Part before restoring its Stage 2 source.");
        return false;
    }

    FDWCTransparencySourcePayload RestoredPayload;
    if (!FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalSource(
            *Asset,
            Layer->LayerGuid,
            RestoredPayload,
            OutError))
    {
        return false;
    }

    RestoredPayload.bIsFinalBakedBaseline = false;
    RestoredPayload.BaselineStrokeCount = 0;
    RestoredPayload.BaselineBakeGuid.Invalidate();
    TSharedPtr<FDWCTransparencySourcePayload> RestoredResult =
        MakeShared<FDWCTransparencySourcePayload>(MoveTemp(RestoredPayload));
    if (!EnsureSourcePayloadAccounted(RestoredResult, OutError))
    {
        return false;
    }

    AutoBakeResults.Reset();
    AutoBakeResults.Add(Layer->LayerGuid, MoveTemp(RestoredResult));
    return true;
}

int32 SWetClothingTransparencyBakePanel::GetCurrentBaselineStrokeCount() const
{
    const TSharedPtr<FDWCTransparencySourcePayload>* Result = AutoBakeResults.Find(GetSelectedLayerGuid());
    return Result != nullptr && Result->IsValid()
        ? FMath::Max((*Result)->BaselineStrokeCount, 0)
        : 0;
}

bool SWetClothingTransparencyBakePanel::SaveTransparencySetupAssets() const
{
    return FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(WetClothingAsset.Get());
}

const UClass* SWetClothingTransparencyBakePanel::GetSelectedSourceClass() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return nullptr;
    }
    if (BlueprintHierarchySession.IsValid())
    {
        const FDWCTransparencyBlueprintHierarchySnapshot& Snapshot =
            BlueprintHierarchySession->GetSnapshot();
        if (Snapshot.Matches(
                Layer->LayerGuid,
                Layer->BlueprintSource.BlueprintClass.ToSoftObjectPath()))
        {
            if (const UClass* LoadedClass = Snapshot.LoadedClass)
            {
                return LoadedClass;
            }
        }
    }
    return Layer->BlueprintSource.BlueprintClass.Get();
}

void SWetClothingTransparencyBakePanel::HandleSourceClassChanged(const UClass* NewClass)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }
    const TSoftClassPtr<AActor> NewSourceClass =
        NewClass != nullptr && NewClass->IsChildOf(AActor::StaticClass())
            ? const_cast<UClass*>(NewClass)
            : nullptr;
    const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
    if (SelectedLayer == nullptr ||
        SelectedLayer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        return;
    }
    const FGuid LayerGuid = SelectedLayer->LayerGuid;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    if (!AuthoringDocument.IsValid() ||
        !AuthoringDocument->Edit(
            LOCTEXT("SetTransparencySourceBlueprint", "Set Transparency Source Blueprint"),
            Change,
            [LayerGuid, NewSourceClass](UWetClothingAsset& MutableAsset)
            {
                FWetClothingTransparencyLayerData* MutableLayer =
                    MutableAsset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                        [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                        {
                            return Candidate.LayerGuid == LayerGuid;
                        });
                if (MutableLayer == nullptr || MutableLayer->BlueprintSource.BlueprintClass == NewSourceClass)
                {
                    return false;
                }
                MutableLayer->BlueprintSource = FWetClothingTransparencyBlueprintSource{};
                MutableLayer->BlueprintSource.BlueprintClass = NewSourceClass;
                MutableLayer->MarkAutoBakeStale();
                MutableLayer->MarkFinalBakeStale();
                return true;
            }).bChanged)
    {
        return;
    }
    AutoBakeResults.Remove(LayerGuid);
    RefreshBlueprintHierarchy(true);
    UpdateInnerSourceStatus();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent);
}

void SWetClothingTransparencyBakePanel::RefreshRevealEditingContent()
{
    RefreshRevealColorStrokeList();
    if (RevealEditingContentContainer.IsValid())
    {
        RevealEditingContentContainer->SetContent(BuildRevealColorEditingSection());
    }
}

FString SWetClothingTransparencyBakePanel::GetPendingExternalSourceMeshPath() const
{
    return PendingExternalSourceMesh.ToSoftObjectPath().ToString();
}

void SWetClothingTransparencyBakePanel::HandleExternalSourceMeshChanged(
    const FAssetData& AssetData)
{
    PendingExternalSourceMesh = Cast<USkeletalMesh>(AssetData.GetAsset());
}

FReply SWetClothingTransparencyBakePanel::HandleAddExternalSourceClicked()
{
    USkeletalMesh* NewMesh = PendingExternalSourceMesh.LoadSynchronous();
    if (NewMesh == nullptr)
    {
        StatusMessage = TEXT("Choose a Skeletal Mesh before adding an External Raycast Source.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return FReply::Handled();
    }

    const FGuid NewSourceGuid = FGuid::NewGuid();
    if (!EditSelectedLayer(
        LOCTEXT("AddTransparencyExternalMesh", "Add Transparency External Mesh Source"),
        [NewMesh, NewSourceGuid](FWetClothingTransparencyLayerData& Layer)
        {
            FWetClothingTransparencyExternalMeshEntry& Entry =
                Layer.ExternalMeshSource.SourcePriority.AddDefaulted_GetRef();
            Entry.SourceGuid = NewSourceGuid;
            Entry.SkeletalMesh = NewMesh;
            Entry.BakeTransform = FTransform::Identity;
            Entry.SourceUVChannel = 0;
            Entry.Role = EDWCTransparencyBlueprintSourceRole::RevealSource;
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport |
            EDWCTransparencyPanelRefreshFlags::Details))
    {
        return FReply::Handled();
    }
    PlacementSession->SetSourceTransform(NewSourceGuid, FTransform::Identity);
    PlacementSession->SetSelection(FDWCTransparencyPlacementSelection::Source(NewSourceGuid));
    PendingExternalSourceMesh.Reset();
    if (PreviewViewport.IsValid())
    {
        const bool bWasFullSourcePreview =
            PreviewViewport->GetPreviewMode() == EWetClothingTransparencyPreviewMode::FullBlueprint;
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::FullBlueprint);
        if (bWasFullSourcePreview)
        {
            PreviewViewport->InvalidateFullSourceLayout();
        }
        PreviewViewport->SetPlacementSelection(PlacementSession->GetSelection());
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleRemoveExternalSourceClicked(const int32 PriorityIndex)
{
    const FWetClothingTransparencyLayerData* ExistingLayer = GetSelectedLayer();
    const FGuid RemovedGuid = ExistingLayer != nullptr &&
            ExistingLayer->ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex)
        ? ExistingLayer->ExternalMeshSource.SourcePriority[PriorityIndex].SourceGuid
        : FGuid();
    const bool bRemoved = EditSelectedLayer(
        LOCTEXT("RemoveTransparencyExternalMesh", "Remove Transparency External Mesh Source"),
        [PriorityIndex](FWetClothingTransparencyLayerData& Layer)
        {
            if (!Layer.ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex))
            {
                return false;
            }
            Layer.ExternalMeshSource.SourcePriority.RemoveAt(PriorityIndex);
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport |
            EDWCTransparencyPanelRefreshFlags::Details);
    if (bRemoved && RemovedGuid.IsValid())
    {
        PlacementSession->RemoveSource(RemovedGuid);
    }
    if (bRemoved && PreviewViewport.IsValid())
    {
        PreviewViewport->InvalidateFullSourceLayout();
        PreviewViewport->SetPlacementSelection(PlacementSession->GetSelection());
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleMoveExternalSourceClicked(
    const int32 PriorityIndex,
    const int32 Direction)
{
    EditSelectedLayer(
        LOCTEXT("ReorderTransparencyExternalMesh", "Reorder Transparency External Mesh Source"),
        [PriorityIndex, Direction](FWetClothingTransparencyLayerData& Layer)
        {
            TArray<FWetClothingTransparencyExternalMeshEntry>& Sources =
                Layer.ExternalMeshSource.SourcePriority;
            const int32 Destination = PriorityIndex + Direction;
            if (!Sources.IsValidIndex(PriorityIndex) || !Sources.IsValidIndex(Destination))
            {
                return false;
            }
            Sources.Swap(PriorityIndex, Destination);
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleExternalSourceListSelectionChanged(
    const TSharedPtr<int32> Item,
    const ESelectInfo::Type)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!Item.IsValid() || Layer == nullptr ||
        !Layer->ExternalMeshSource.SourcePriority.IsValidIndex(*Item))
    {
        return;
    }
    const FGuid SourceGuid = Layer->ExternalMeshSource.SourcePriority[*Item].SourceGuid;
    PlacementSession->SetSelection(FDWCTransparencyPlacementSelection::Source(SourceGuid));
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::FullBlueprint);
        PreviewViewport->SetPlacementSelection(PlacementSession->GetSelection());
    }
}

void SWetClothingTransparencyBakePanel::HandlePlacementSelectionChanged(
    const FDWCTransparencyPlacementSelection& Selection)
{
    PlacementSession->SetSelection(Selection);
    RefreshExternalSourcePriorityItems();
}

FReply SWetClothingTransparencyBakePanel::HandleToggleExternalSourceVisibilityClicked(
    const FGuid SourceGuid)
{
    if (SourceGuid.IsValid())
    {
        PlacementSession->SetSourceHidden(
            SourceGuid,
            !PlacementSession->IsSourceHidden(SourceGuid));
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->RefreshType3PlacementPresentation();
        }
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleToggleExternalSourceSoloClicked(
    const FGuid SourceGuid)
{
    if (SourceGuid.IsValid())
    {
        PlacementSession->ToggleSourceSolo(SourceGuid);
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->RefreshType3PlacementPresentation();
        }
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleToggleExternalSourceLockClicked(
    const FGuid SourceGuid)
{
    if (SourceGuid.IsValid())
    {
        PlacementSession->SetSourceLocked(
            SourceGuid,
            !PlacementSession->IsSourceLocked(SourceGuid));
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->RefreshType3PlacementPresentation();
        }
    }
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleExternalSourceUVChannelChanged(
    TSharedPtr<int32> Item,
    ESelectInfo::Type,
    const int32 PriorityIndex)
{
    if (!Item.IsValid()) return;
    EditSelectedLayer(
        LOCTEXT("SetTransparencyExternalMeshUV", "Set Transparency External Mesh Source UV"),
        [PriorityIndex, UVChannel = *Item](FWetClothingTransparencyLayerData& Layer)
        {
            if (!Layer.ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex) ||
                Layer.ExternalMeshSource.SourcePriority[PriorityIndex].SourceUVChannel == UVChannel)
            {
                return false;
            }
            Layer.ExternalMeshSource.SourcePriority[PriorityIndex].SourceUVChannel = UVChannel;
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport);
}

void SWetClothingTransparencyBakePanel::HandleExternalSourceRoleChanged(
    TSharedPtr<EDWCTransparencyBlueprintSourceRole> Item,
    ESelectInfo::Type,
    const int32 PriorityIndex)
{
    if (!Item.IsValid()) return;
    EditSelectedLayer(
        LOCTEXT("SetTransparencyExternalMeshRole", "Set Transparency External Mesh Source Role"),
        [PriorityIndex, Role = *Item](FWetClothingTransparencyLayerData& Layer)
        {
            if (!Layer.ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex) ||
                Layer.ExternalMeshSource.SourcePriority[PriorityIndex].Role == Role)
            {
                return false;
            }
            Layer.ExternalMeshSource.SourcePriority[PriorityIndex].Role = Role;
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::SourceModel |
            EDWCTransparencyPanelRefreshFlags::Viewport);
}

void SWetClothingTransparencyBakePanel::HandleExternalSourceTransformCommitted(
    const FGuid& SourceGuid,
    const FTransform& Transform)
{
    if (!SourceGuid.IsValid()) return;
    const FWetClothingTransparencyLayerData* ExistingLayer = GetSelectedLayer();
    const FWetClothingTransparencyExternalMeshEntry* ExistingEntry = ExistingLayer != nullptr
        ? ExistingLayer->ExternalMeshSource.SourcePriority.FindByPredicate(
            [SourceGuid](const FWetClothingTransparencyExternalMeshEntry& Candidate)
            {
                return Candidate.SourceGuid == SourceGuid;
            })
        : nullptr;
    if (ExistingEntry == nullptr || ExistingEntry->BakeTransform.Equals(Transform))
    {
        return;
    }

    const FGuid LayerGuid = ExistingLayer->LayerGuid;
    const int32 MaterialSlotIndex = ExistingLayer->TargetSurface.OuterMaterialSlotIndex;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    Change.MaterialSlotIndex = MaterialSlotIndex;
    Change.LayerGuid = LayerGuid;
    const FDWCEditorAuthoringResult Result = AuthoringDocument->Edit(
        LOCTEXT("PlaceTransparencyExternalMesh", "Place Transparency External Mesh Source"),
        Change,
        [LayerGuid, SourceGuid, Transform](UWetClothingAsset& Asset)
        {
            FWetClothingTransparencyLayerData* Layer =
                Asset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                {
                    return Candidate.LayerGuid == LayerGuid;
                });
            if (Layer == nullptr)
            {
                return false;
            }
            FWetClothingTransparencyExternalMeshEntry* Entry =
                Layer->ExternalMeshSource.SourcePriority.FindByPredicate(
                    [SourceGuid](const FWetClothingTransparencyExternalMeshEntry& Candidate)
                    {
                        return Candidate.SourceGuid == SourceGuid;
                    });
            if (Entry == nullptr || Entry->BakeTransform.Equals(Transform))
            {
                return false;
            }
            Entry->BakeTransform = Transform;
            Layer->MarkAutoBakeStale();
            Layer->MarkFinalBakeStale();
            return true;
        });
    if (Result.bChanged)
    {
        PlacementSession->SetSourceTransform(SourceGuid, Transform);
        AutoBakeResults.Remove(LayerGuid);
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Model);
    }
}

FReply SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset != nullptr && Layer != nullptr &&
        IsSourceTypeAvailable(Layer->SourceType))
    {
        const FString DisabledReason = GetGenerateDisabledReason();
        if (!DisabledReason.IsEmpty())
        {
            StatusMessage = DisabledReason;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(DisabledReason));
            return FReply::Handled();
        }

        FDWCTransparencyStage2GenerationDiagnosticScope GenerationDiagnosticScope;

        TSharedPtr<FDWCTransparencySourcePayload> Result = MakeShared<FDWCTransparencySourcePayload>();
        FString Summary;
        TArray<FString> Warnings;
        bool bGenerated = false;
        if (Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
        {
            bGenerated = FDWCTransparencyAutoMapGenerator::GenerateBaseRevealColorMap(
                *Asset,
                *Layer,
                *Result,
                Summary,
                Warnings,
                CacheStore);
        }
        else
        {
            FScopedSlowTask RaycastTask(
                100.0f,
                LOCTEXT(
                    "GenerateTransparencyRaycastProgress",
                    "Generating preview transparency data from the selected source surfaces..."));
            RaycastTask.MakeDialog(true);

            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> CancellationToken =
                MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
            double PresentedFraction = 0.0;
            double LastPresentationSeconds = 0.0;
            TOptional<EDWCTransparencyGenerationPhase> LastPresentedPhase;
            FName LastPresentedSource = NAME_None;
            int32 LastPresentedMaterialSlot = INDEX_NONE;

            const FDWCTransparencyGenerationProgressCallback ProgressCallback =
                [&RaycastTask,
                 &PresentedFraction,
                 &LastPresentationSeconds,
                 &LastPresentedPhase,
                 &LastPresentedSource,
                 &LastPresentedMaterialSlot,
                 CancellationToken](const FDWCTransparencyGenerationProgress& Progress)
            {
                if (RaycastTask.ShouldCancel())
                {
                    CancellationToken->Cancel();
                }

                const double Fraction = FMath::Max(
                    PresentedFraction,
                    FMath::Clamp(Progress.OverallFraction, 0.0, 1.0));
                const double NowSeconds = FPlatformTime::Seconds();
                const bool bPhaseChanged =
                    !LastPresentedPhase.IsSet() ||
                    LastPresentedPhase.GetValue() != Progress.Phase ||
                    LastPresentedSource != Progress.SourceName ||
                    LastPresentedMaterialSlot != Progress.MaterialSlotIndex;
                const bool bFractionChanged = Fraction - PresentedFraction >= 0.005;
                const bool bPresentationDue =
                    NowSeconds - LastPresentationSeconds >= 0.05;
                if (!bPhaseChanged && !bFractionChanged && !bPresentationDue && Fraction < 1.0)
                {
                    return;
                }

                FText DetailText;
                switch (Progress.Phase)
                {
                case EDWCTransparencyGenerationPhase::PreparingTarget:
                    DetailText = LOCTEXT(
                        "TransparencyProgressPreparingTarget",
                        "Preparing the target surface.");
                    break;
                case EDWCTransparencyGenerationPhase::RasterizingTarget:
                    DetailText = LOCTEXT(
                        "TransparencyProgressRasterizingTarget",
                        "Rasterizing the target Wet Part into DWC Data UV texels.");
                    break;
                case EDWCTransparencyGenerationPhase::PreparingSources:
                    DetailText = LOCTEXT(
                        "TransparencyProgressPreparingSources",
                        "Preparing the selected raycast source surfaces.");
                    break;
                case EDWCTransparencyGenerationPhase::BakingSourceMaterial:
                    DetailText = FText::Format(
                        LOCTEXT(
                            "TransparencyProgressBakingSourceMaterial",
                            "Baking source material {0} / {1}: {2}"),
                        FText::AsNumber(FMath::Min(
                            Progress.CompletedItems + 1,
                            FMath::Max(1, Progress.TotalItems))),
                        FText::AsNumber(FMath::Max(1, Progress.TotalItems)),
                        FText::FromName(Progress.SourceName));
                    break;
                case EDWCTransparencyGenerationPhase::ProjectingSamples:
                    DetailText = FText::Format(
                        LOCTEXT(
                            "TransparencyProgressProjectingSamples",
                            "Raycasting {0}: {1} / {2} texel samples"),
                        FText::FromName(Progress.SourceName),
                        FText::AsNumber(Progress.CompletedItems),
                        FText::AsNumber(Progress.TotalItems));
                    break;
                case EDWCTransparencyGenerationPhase::ComposingResult:
                    DetailText = LOCTEXT(
                        "TransparencyProgressComposingResult",
                        "Composing reveal color, normal, coverage, and alpha data.");
                    break;
                case EDWCTransparencyGenerationPhase::CommittingResult:
                    DetailText = LOCTEXT(
                        "TransparencyProgressCommittingResult",
                        "Finalizing the preview transparency data.");
                    break;
                default:
                    DetailText = LOCTEXT(
                        "TransparencyProgressWorking",
                        "Generating preview transparency data.");
                    break;
                }

                RaycastTask.EnterProgressFrame(
                    static_cast<float>((Fraction - PresentedFraction) * 100.0),
                    DetailText);
                PresentedFraction = Fraction;
                LastPresentationSeconds = NowSeconds;
                LastPresentedPhase = Progress.Phase;
                LastPresentedSource = Progress.SourceName;
                LastPresentedMaterialSlot = Progress.MaterialSlotIndex;
            };
            FDWCTransparencyStage2ExecutionOptions GenerationOptions;
            GenerationOptions.CancellationToken = &CancellationToken.Get();
            GenerationOptions.ProgressCallback = &ProgressCallback;
            GenerationOptions.ResourceGovernor = ResourceGovernor;
            GenerationOptions.CacheStore = CacheStore;
            bGenerated = FDWCTransparencyAutoMapGenerator::GenerateSameMesh(
                *Asset,
                *Layer,
                *Result,
                Summary,
                Warnings,
                GenerationOptions);

            if (!bGenerated && CancellationToken->IsCanceled())
            {
                GenerationDiagnosticScope.MarkCanceled();
                StatusMessage = TEXT("Transparency preview generation was canceled.");
                PanelStatus = EDWCTransparencyPanelStatus::Info;
                return FReply::Handled();
            }
        }
        if (!bGenerated)
        {
            GenerationDiagnosticScope.MarkFailed();
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
            return FReply::Handled();
        }

        FString AccountingError;
        if (!EnsureSourcePayloadAccounted(Result, AccountingError))
        {
            GenerationDiagnosticScope.MarkFailed();
            StatusMessage = AccountingError;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(AccountingError));
            return FReply::Handled();
        }

        Layer = GetSelectedLayer();
        if (Layer == nullptr)
        {
            GenerationDiagnosticScope.MarkFailed();
            return FReply::Handled();
        }
        const FGuid GeneratedLayerGuid = Layer->LayerGuid;
        FWetClothingTransparencyLayerData CommittedLayer = *Layer;
        // Generation is the explicit authoring checkpoint. From this point the
        // layer requires a current runtime output until the user disables or removes it.
        CommittedLayer.Intent = EDWCTransparencyLayerIntent::Enabled;
        CommittedLayer.AutoBakeMetadata.AutoBakeGuid = FGuid::NewGuid();
        CommittedLayer.AutoBakeMetadata.BuildSignature = Result->BuildSignature;
        CommittedLayer.AutoBakeMetadata.Resolution = Result->Resolution.X;
        CommittedLayer.AutoBakeMetadata.PaddingPixels =
            Asset->Authored.TransparencyData.TransparencyPaddingPixels;
        CommittedLayer.AutoBakeMetadata.ValidHitCount = Result->ValidHitCount;
        CommittedLayer.AutoBakeMetadata.NoHitCount = Result->NoHitCount;

        if (Result->BuildSignature.IsEmpty())
        {
            Warnings.Add(TEXT("The generated Transparency source did not provide a canonical build signature."));
        }
        else
        {
            CommittedLayer.AutoBakeMetadata.BuildSignature = Result->BuildSignature;
            FString TempStoreError;
            if (!FDWCTransparencyTempAssetStore::CommitSourceArtifacts(
                    *Asset,
                    CommittedLayer,
                    *Result,
                    Result->MaterialBakeSignature,
                    TempStoreError))
            {
                Warnings.Add(FString::Printf(
                    TEXT("The generated map is usable, but its editor Temp artifacts could not be updated: %s"),
                    *TempStoreError));
            }
        }

        FDWCEditorAuthoringChange CommitChange;
        CommitChange.Domain = EDWCEditorAuthoringDomain::Transparency;
        CommitChange.Impact = EDWCEditorAuthoringImpact::AssetDirty |
            EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::TransparencyFinalBake;
        CommitChange.MaterialSlotIndex =
            CommittedLayer.TargetSurface.OuterMaterialSlotIndex;
        CommitChange.LayerGuid = GeneratedLayerGuid;
        const FDWCEditorAuthoringResult CommitResult = AuthoringDocument.IsValid()
            ? AuthoringDocument->Edit(
                LOCTEXT("GenerateTransparencyAutoMap", "Generate Transparency Map"),
                CommitChange,
                [GeneratedLayerGuid, CommittedLayer = MoveTemp(CommittedLayer)](
                    UWetClothingAsset& MutableAsset) mutable
                {
                    FWetClothingTransparencyLayerData* MutableLayer =
                        MutableAsset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                            [GeneratedLayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                            {
                                return Candidate.LayerGuid == GeneratedLayerGuid;
                            });
                    if (MutableLayer == nullptr)
                    {
                        return false;
                    }
                    *MutableLayer = MoveTemp(CommittedLayer);
                    return true;
                })
            : FDWCEditorAuthoringResult{};
        if (!CommitResult.bChanged)
        {
            GenerationDiagnosticScope.MarkFailed();
            StatusMessage = CommitResult.Error.IsEmpty()
                ? TEXT("The generated Transparency result could not be committed to the WCA.")
                : CommitResult.Error;
            PanelStatus = EDWCTransparencyPanelStatus::Error;
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(StatusMessage));
            return FReply::Handled();
        }

        Layer = GetSelectedLayer();
        if (Layer == nullptr)
        {
            GenerationDiagnosticScope.MarkFailed();
            return FReply::Handled();
        }
        // Keep only the active layer's large CPU intermediate buffers in memory.
        AutoBakeResults.Reset();
        AutoBakeResults.Add(Layer->LayerGuid, Result);
        const EDWCTransparencyEditorStage ResultStage =
            EDWCTransparencyEditorStage::RevealEditing;
        if (SessionStore.IsValid())
        {
            SessionStore->Dispatch(FDWCSetTransparencyStageAction{Layer->LayerGuid, ResultStage});
        }
        else
        {
            StageByLayer.FindOrAdd(Layer->LayerGuid) = ResultStage;
        }
        SelectedVisualizationMode = EDWCTransparencyVisualizationMode::InnerColor;
        // Generation can advance Stage 2 directly to Stage 3 without passing
        // through SetCurrentStage. Keep the controller's paint target in the
        // session authoritative for that direct transition as well.
        DispatchTransparencyEditContext();

        // The working map is ready now. Push it directly so the viewport does
        // not depend on the deferred Stage 2 -> Stage 3 layout rebuild before
        // it can show an authored manual reveal color (or a ray-generated map).
        if (PreviewViewport.IsValid())
        {
            // Reveal-color painting is surface editing. It must always
            // use the single target mesh so the hit BVH and the displayed MID
            // describe the same selected material slot.
            PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
            PreviewViewport->SetAutoBakePreviewResult(Result);
        }
        DispatchTransparencyPreviewState();

        for (const FDWCTransparencySourceHitStats& Stats : Result->SourceStats)
        {
            Summary += FString::Printf(
                TEXT("\n- Priority %d: %s (Slot %d, source bake %d) -> %d hit(s)"),
                Stats.PriorityIndex,
                *Stats.MaterialSlotName.ToString(),
                Stats.MaterialSlotIndex,
                Stats.SourceBakeResolution,
                Stats.HitCount);
        }
        if (!Warnings.IsEmpty()) Summary += TEXT("\n\nWarnings:\n- ") + FString::Join(Warnings, TEXT("\n- "));
        StatusMessage = Summary;
        PanelStatus = Warnings.IsEmpty() ? EDWCTransparencyPanelStatus::Ready : EDWCTransparencyPanelStatus::Warning;
        EDWCTransparencyPanelRefreshFlags RefreshFlags = EDWCTransparencyPanelRefreshFlags::Viewport;
        RefreshFlags |=
            EDWCTransparencyPanelRefreshFlags::StageContent |
            EDWCTransparencyPanelRefreshFlags::Details;
        RequestRefresh(RefreshFlags);
        FMessageDialog::Open(Warnings.IsEmpty() ? EAppMsgCategory::Success : EAppMsgCategory::Warning,
            EAppMsgType::Ok, FText::FromString(Summary));
        GenerationDiagnosticScope.MarkCompleted();
        return FReply::Handled();
    }

    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleContinueToFinalEditingClicked()
{
    if (!CanEnterRevealEditingStage())
    {
        StatusMessage = TEXT("The Reveal Color working map is unavailable. Return to Stage 2 and generate it again.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return FReply::Handled();
    }

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const TSharedPtr<FDWCTransparencySourcePayload>* StoredResult =
        Layer != nullptr ? AutoBakeResults.Find(Layer->LayerGuid) : nullptr;
    if (Asset == nullptr || Layer == nullptr || StoredResult == nullptr || !StoredResult->IsValid())
    {
        StatusMessage = TEXT("The Reveal Color stage could not commit because its source result is unavailable.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return FReply::Handled();
    }

    if (!WorkerJobScheduler.IsValid() || !PreviewViewport.IsValid())
    {
        StatusMessage = TEXT("The asynchronous reveal-color commit service is unavailable.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return FReply::Handled();
    }
    if (bRevealCommitInFlight)
    {
        return FReply::Handled();
    }

    const FGuid ExpectedLayerGuid = Layer->LayerGuid;
    const int32 ExpectedSlot = Layer->TargetSurface.OuterMaterialSlotIndex;
    const FString ExpectedSourceSignature = (*StoredResult)->BuildSignature;
    const float ExpectedMetallicDarkeningStrength =
        Asset->Authored.TransparencyData.RevealMetallicDarkeningStrength;
    const FString ExpectedRevealSignature = FDWCTransparencySignatureService::BuildRevealSignature(
        (*StoredResult)->BuildSignature,
        *Layer,
        ExpectedMetallicDarkeningStrength);
    const uint64 ExpectedEpoch = ++RevealCommitEpoch;

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::TransparencyRevealColorCommit;
    Descriptor.Key.MaterialSlotIndex = ExpectedSlot;
    Descriptor.Key.LayerGuid = ExpectedLayerGuid;
    Descriptor.Priority = EDWCEditorWorkerJobPriority::UserInitiated;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    Descriptor.WorkClass = EDWCEditorWorkClass::UserBuild;
    const uint64 PixelBytes = static_cast<uint64>((*StoredResult)->Resolution.X) *
        (*StoredResult)->Resolution.Y * sizeof(FColor);
    Descriptor.MemoryEstimate.ResidentSharedBytes = (*StoredResult)->InnerColorBuffer.GetAllocatedSize();
    Descriptor.MemoryEstimate.OutputBytes = PixelBytes;
    Descriptor.DebugName = FString::Printf(
        TEXT("Transparency reveal commit slot %d"), ExpectedSlot);

    bRevealCommitInFlight = true;
    StatusMessage = TEXT("Committing the Stage 3 reveal-color corrections...");
    PanelStatus = EDWCTransparencyPanelStatus::Info;

    TWeakPtr<SWetClothingTransparencyBakePanel> WeakPanel = SharedThis(this);
    FString SubmitError;
    PendingRevealCommitTicket = WorkerJobScheduler->SubmitPrepared(
        Descriptor,
        [WeakPanel, ExpectedLayerGuid, ExpectedSlot, ExpectedEpoch](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& OutPrepareError)
        {
            const TSharedPtr<SWetClothingTransparencyBakePanel> Panel = WeakPanel.Pin();
            if (!Panel.IsValid() || CancellationToken->IsCanceled() ||
                Panel->RevealCommitEpoch != ExpectedEpoch ||
                !Panel->PreviewViewport.IsValid())
            {
                OutPrepareError = TEXT("The reveal-color commit context changed before admission.");
                return false;
            }
            const FWetClothingTransparencyLayerData* CurrentLayer = Panel->GetSelectedLayer();
            if (CurrentLayer == nullptr || CurrentLayer->LayerGuid != ExpectedLayerGuid ||
                CurrentLayer->TargetSurface.OuterMaterialSlotIndex != ExpectedSlot)
            {
                OutPrepareError = TEXT("The selected transparency layer changed before reveal commit.");
                return false;
            }

            FDWCTransparencyRevealCommitJobInput Input;
            if (!Panel->PreviewViewport->BuildRevealColorCommitInput(Input, OutPrepareError))
            {
                return false;
            }
            OutPrepared.ActualMemoryEstimate =
                FDWCTransparencyRevealCommitWorker::EstimateMemory(Input);
            OutPrepared.Work = [Input = MoveTemp(Input)](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerToken) mutable
            {
                return FDWCTransparencyRevealCommitWorker::Build(MoveTemp(Input), WorkerToken);
            };
            return true;
        },
        [WeakPanel, ExpectedLayerGuid, ExpectedSlot, ExpectedSourceSignature,
         ExpectedRevealSignature, ExpectedEpoch](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetClothingTransparencyBakePanel> Panel = WeakPanel.Pin();
            const TSharedPtr<FDWCTransparencyRevealCommitJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FDWCTransparencyRevealCommitJobResult>(BaseResult);
            if (!Panel.IsValid() || Panel->RevealCommitEpoch != ExpectedEpoch ||
                Panel->PendingRevealCommitTicket.JobId != CompletedTicket.JobId ||
                Panel->PendingRevealCommitTicket.Generation != CompletedTicket.Generation)
            {
                return;
            }
            Panel->PendingRevealCommitTicket = {};
            Panel->bRevealCommitInFlight = false;

            UWetClothingAsset* CurrentAsset = Panel->WetClothingAsset.Get();
            FWetClothingTransparencyLayerData* CurrentLayer = Panel->GetSelectedLayer();
            const FString CurrentRevealSignature = CurrentLayer != nullptr
                ? FDWCTransparencySignatureService::BuildRevealSignature(
                    ExpectedSourceSignature,
                    *CurrentLayer,
                    CurrentAsset != nullptr
                        ? CurrentAsset->Authored.TransparencyData.RevealMetallicDarkeningStrength
                        : 0.0f)
                : FString();
            if (!Result.IsValid() || !Result->bSucceeded || CurrentAsset == nullptr ||
                CurrentLayer == nullptr || CurrentLayer->LayerGuid != ExpectedLayerGuid ||
                CurrentLayer->TargetSurface.OuterMaterialSlotIndex != ExpectedSlot ||
                Result->SourceSignature != ExpectedSourceSignature ||
                CurrentRevealSignature != ExpectedRevealSignature)
            {
                Panel->StatusMessage = Result.IsValid() && !Result->Error.IsEmpty()
                    ? Result->Error
                    : TEXT("The reveal-color commit became stale before it could be applied.");
                Panel->PanelStatus = EDWCTransparencyPanelStatus::Warning;
                return;
            }

            FString CommitError;
            if (!FDWCTransparencyTempAssetStore::CommitRevealArtifact(
                    *CurrentAsset,
                    *CurrentLayer,
                    Result->CorrectedRevealPixels,
                    Result->Resolution,
                    ExpectedSourceSignature,
                    ExpectedRevealSignature,
                    CommitError))
            {
                Panel->StatusMessage = CommitError;
                Panel->PanelStatus = EDWCTransparencyPanelStatus::Warning;
                return;
            }

            CurrentAsset->MarkPackageDirty();
            Panel->SetCurrentStage(EDWCTransparencyEditorStage::FinalEditing);
        },
        &SubmitError,
        [WeakPanel, ExpectedEpoch](
            const FDWCEditorWorkerJobTicket& FinishedTicket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString& Detail)
        {
            if (Completion == EDWCEditorWorkerJobCompletion::Applied)
            {
                return;
            }
            const TSharedPtr<SWetClothingTransparencyBakePanel> Panel = WeakPanel.Pin();
            if (!Panel.IsValid() || Panel->RevealCommitEpoch != ExpectedEpoch ||
                Panel->PendingRevealCommitTicket.JobId != FinishedTicket.JobId ||
                Panel->PendingRevealCommitTicket.Generation != FinishedTicket.Generation)
            {
                return;
            }
            Panel->PendingRevealCommitTicket = {};
            Panel->bRevealCommitInFlight = false;
            Panel->StatusMessage = Detail.IsEmpty()
                ? TEXT("The reveal-color commit did not complete.")
                : Detail;
            Panel->PanelStatus = EDWCTransparencyPanelStatus::Warning;
        });

    if (!PendingRevealCommitTicket.IsValid())
    {
        bRevealCommitInFlight = false;
        StatusMessage = SubmitError.IsEmpty()
            ? TEXT("The reveal-color commit could not be scheduled.")
            : SubmitError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleBakeEditedTransparencyMapClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("BakeEditedNoTarget", "Select a Transparency Target Part before baking."));
        return FReply::Handled();
    }

    const TSharedPtr<FDWCTransparencySourcePayload>* StoredResult = AutoBakeResults.Find(Layer->LayerGuid);
    if (StoredResult == nullptr || !StoredResult->IsValid())
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("BakeEditedGenerateFirst", "Generate a Preview Transparency Map or load an existing baked map before baking."));
        return FReply::Handled();
    }

    FString CompatibilityReason;
    if (!FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(*Layer, *StoredResult->Get(), CompatibilityReason))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(CompatibilityReason));
        return FReply::Handled();
    }
    if (!BakeCoordinator.IsValid())
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BakeCoordinatorUnavailable", "The asynchronous bake service is unavailable."));
        return FReply::Handled();
    }

    // The preview controls are session-owned while the bake/signature contract
    // is asset-owned. Persist the current session values once, then freeze the
    // exact resulting settings for the job so a pending numeric commit cannot
    // bake a different strength than the viewport just displayed.
    const FDWCTransparencyPreviewSettings BakePreviewSettings = GetTransparencyPreviewSettings();
    CommitTransparencyPreviewSettings(
        LOCTEXT("CommitTransparencyBakeSettings", "Commit Transparency Bake Settings"),
        BakePreviewSettings);
    const FDWCTransparencyFinalSettingsSnapshot FinalSettings =
        FDWCTransparencyFinalSettingsSnapshot::FromAuthoredData(Asset->Authored.TransparencyData);
    FString FinalSettingsError;
    if (!FinalSettings.IsValid(&FinalSettingsError))
    {
        StatusMessage = FinalSettingsError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, FText::FromString(FinalSettingsError));
        return FReply::Handled();
    }
    const TSharedRef<const FDWCTransparencyFinalSettingsSnapshot> FinalSettingsSnapshot =
        MakeShared<FDWCTransparencyFinalSettingsSnapshot>(FinalSettings);

    StatusMessage = TEXT("Baking the edited transparency map...");
    PanelStatus = EDWCTransparencyPanelStatus::Info;
    const FGuid LayerGuid = Layer->LayerGuid;
    TWeakPtr<SWetClothingTransparencyBakePanel> WeakPanel = SharedThis(this);
    FString RequestError;
    const bool bRequiresCanonicalRebuild = (*StoredResult)->bIsFinalBakedBaseline;
    const auto Completion = [WeakPanel, LayerGuid](const FDWCEditorBakeBatchResult& Result)
    {
        const TSharedPtr<SWetClothingTransparencyBakePanel> Panel = WeakPanel.Pin();
        if (!Panel.IsValid())
        {
            return;
        }
        if (Result.bSucceeded)
        {
            Panel->AutoBakeResults.Remove(LayerGuid);
        }
        Panel->StatusMessage = Result.Summary;
        Panel->PanelStatus = Result.bSucceeded
            ? EDWCTransparencyPanelStatus::Ready
            : EDWCTransparencyPanelStatus::Warning;
        Panel->RequestRefresh(
            EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::StageContent |
            EDWCTransparencyPanelRefreshFlags::Viewport |
            EDWCTransparencyPanelRefreshFlags::Details);
        FMessageDialog::Open(
            Result.bSucceeded ? EAppMsgCategory::Success : EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            FText::FromString(Result.Summary));
    };
    TSharedPtr<const FDWCTransparencyAlphaWorkingSnapshot> AlphaSnapshot;
    if (!bRequiresCanonicalRebuild && PreviewViewport.IsValid())
    {
        FDWCTransparencyAlphaWorkingSnapshot CapturedAlpha;
        FString SnapshotError;
        if (!PreviewViewport->BuildAlphaWorkingSnapshot(CapturedAlpha, SnapshotError))
        {
            StatusMessage = SnapshotError;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, FText::FromString(SnapshotError));
            return FReply::Handled();
        }
        AlphaSnapshot = MakeShared<const FDWCTransparencyAlphaWorkingSnapshot>(MoveTemp(CapturedAlpha));
    }
    const bool bRequested = bRequiresCanonicalRebuild
        ? BakeCoordinator->RequestTransparencyBake(
            TArray<FGuid>{LayerGuid},
            true,
            Completion,
            &RequestError)
        : BakeCoordinator->RequestTransparencyFinalBake(
            LayerGuid,
            StoredResult->ToSharedRef(),
            MoveTemp(AlphaSnapshot),
            FinalSettingsSnapshot,
            true,
            Completion,
            &RequestError);
    if (!bRequested)
    {
        StatusMessage = RequestError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, FText::FromString(RequestError));
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleFocusPreviewClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusOnPreviewMesh();
    }
    return FReply::Handled();
}

FText SWetClothingTransparencyBakePanel::GetStatusText() const { return FText::FromString(StatusMessage); }
FSlateColor SWetClothingTransparencyBakePanel::GetStatusColor() const
{
    switch (PanelStatus)
    {
    case EDWCTransparencyPanelStatus::Ready: return FSlateColor(FLinearColor(0.32f, 0.80f, 0.42f));
    case EDWCTransparencyPanelStatus::Warning: return FSlateColor(FLinearColor(0.95f, 0.68f, 0.20f));
    case EDWCTransparencyPanelStatus::Error: return FSlateColor(FLinearColor(0.95f, 0.30f, 0.25f));
    default: return FSlateColor::UseSubduedForeground();
    }
}
FText SWetClothingTransparencyBakePanel::GetGenerateTooltipText() const
{
    const FString DisabledReason = GetGenerateDisabledReason();
    if (DisabledReason.IsEmpty())
    {
        return LOCTEXT("GenerateTransparencyReadyTooltip", "Generate an editable preview Transparency Map for the selected Target Part.");
    }
    return FText::FromString(DisabledReason);
}
FText SWetClothingTransparencyBakePanel::GetBakeEditedTooltipText() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return LOCTEXT("BakeEditedSelectTargetTooltip", "Select a Transparency Target Part first.");
    }
    const TSharedPtr<FDWCTransparencySourcePayload>* Result = AutoBakeResults.Find(Layer->LayerGuid);
    if (Result == nullptr || !Result->IsValid())
    {
        return LOCTEXT("BakeEditedGenerateTooltip", "Generate a preview map or load an existing baked map before baking.");
    }
    FString Reason;
    if (!FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(*Layer, *Result->Get(), Reason))
    {
        return FText::FromString(Reason);
    }
    return LOCTEXT("BakeEditedReadyTooltip", "Bake the current working map and new brush edits into one packed RGBA Transparency Map.");
}
float SWetClothingTransparencyBakePanel::GetWetnessPreviewPercent() const { return WetnessPreviewPercent; }
void SWetClothingTransparencyBakePanel::HandleWetnessPreviewChanged(float InValue)
{
    const float NewPercent = FMath::Clamp(InValue, 0.0f, 100.0f);
    if (FMath::IsNearlyEqual(WetnessPreviewPercent, NewPercent))
    {
        return;
    }
    WetnessPreviewPercent = NewPercent;
    DispatchTransparencyPreviewState();
}

TOptional<float> SWetClothingTransparencyBakePanel::GetTransparencyPreviewStrength() const
{
    return GetTransparencyPreviewSettings().TransparencyStrength;
}

void SWetClothingTransparencyBakePanel::HandleTransparencyPreviewStrengthChanged(const float InValue)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.TransparencyStrength = FMath::Max(0.0f, InValue);
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

void SWetClothingTransparencyBakePanel::HandleTransparencyPreviewStrengthCommitted(
    const float InValue,
    ETextCommit::Type)
{
    HandleTransparencyPreviewStrengthChanged(InValue);
    CommitTransparencyPreviewSettings(
        LOCTEXT("SetTransparencyPreviewStrength", "Set Transparency Preview Strength"),
        GetTransparencyPreviewSettings());
}

ECheckBoxState SWetClothingTransparencyBakePanel::GetRevealNormalEnabledState() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return Layer != nullptr && Layer->bEnableRevealNormal
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleRevealNormalEnabledChanged(
    const ECheckBoxState NewState)
{
    const bool bEnable = NewState == ECheckBoxState::Checked;
    CommitRevealNormalRuntimeSettings(
        LOCTEXT("SetRevealNormalEnabled", "Set Reveal Normal Enabled"),
        bEnable,
        GetTransparencyPreviewSettings().RevealNormalStrength);

    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.bShowRevealNormal = bEnable;
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

TOptional<float> SWetClothingTransparencyBakePanel::GetRevealNormalStrength() const
{
    return GetTransparencyPreviewSettings().RevealNormalStrength;
}

void SWetClothingTransparencyBakePanel::HandleRevealNormalStrengthChanged(const float InValue)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.RevealNormalStrength = FMath::Clamp(InValue, 0.0f, 4.0f);
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

void SWetClothingTransparencyBakePanel::HandleRevealNormalStrengthCommitted(
    const float InValue,
    ETextCommit::Type)
{
    HandleRevealNormalStrengthChanged(InValue);
    CommitRevealNormalRuntimeSettings(
        LOCTEXT("SetRevealNormalStrength", "Set Reveal Normal Strength"),
        GetRevealNormalEnabledState() == ECheckBoxState::Checked,
        GetTransparencyPreviewSettings().RevealNormalStrength);
}

ECheckBoxState SWetClothingTransparencyBakePanel::GetShowRevealNormalState() const
{
    return GetTransparencyPreviewSettings().bShowRevealNormal
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleShowRevealNormalChanged(
    const ECheckBoxState NewState)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.bShowRevealNormal = NewState == ECheckBoxState::Checked;
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

ECheckBoxState SWetClothingTransparencyBakePanel::GetRevealNormalSourceState(
    const EDWCTransparencyRevealNormalPreviewSource Source) const
{
    return GetTransparencyPreviewSettings().RevealNormalSource == Source
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleRevealNormalSourceChanged(
    const ECheckBoxState NewState,
    const EDWCTransparencyRevealNormalPreviewSource Source)
{
    if (NewState != ECheckBoxState::Checked)
    {
        return;
    }
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.RevealNormalSource = Source;
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));

    if (Source == EDWCTransparencyRevealNormalPreviewSource::Baked &&
        (SelectedVisualizationMode == EDWCTransparencyVisualizationMode::RevealNormalOnly ||
         SelectedVisualizationMode == EDWCTransparencyVisualizationMode::RevealNormalTexture ||
         SelectedVisualizationMode == EDWCTransparencyVisualizationMode::SourceCoverage))
    {
        SetVisualizationModeForStage(
            EDWCTransparencyVisualizationMode::Final,
            EDWCTransparencyEditorStage::FinalEditing);
    }
}

FText SWetClothingTransparencyBakePanel::GetRevealNormalPreviewSourceStatusText() const
{
    return PreviewViewport.IsValid()
        ? PreviewViewport->GetRevealNormalPreviewSourceStatusText()
        : LOCTEXT("RevealNormalPreviewSourceUnavailable", "Preview Source: unavailable");
}

ECheckBoxState SWetClothingTransparencyBakePanel::GetShowSavedWrinkleState() const
{
    return bShowSavedWrinkle ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleShowSavedWrinkleChanged(
    const ECheckBoxState NewState)
{
    bShowSavedWrinkle = NewState == ECheckBoxState::Checked;
    DispatchTransparencyPreviewState();
}


TOptional<float> SWetClothingTransparencyBakePanel::GetWrinkleSuppressionStrength() const
{
    return GetTransparencyPreviewSettings().WrinkleSuppressionStrength;
}

void SWetClothingTransparencyBakePanel::HandleWrinkleSuppressionStrengthChanged(const float InValue)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.WrinkleSuppressionStrength = FMath::Clamp(InValue, 0.0f, 5.0f);
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

void SWetClothingTransparencyBakePanel::HandleWrinkleSuppressionStrengthCommitted(
    const float InValue,
    ETextCommit::Type)
{
    HandleWrinkleSuppressionStrengthChanged(InValue);
    CommitTransparencyPreviewSettings(
        LOCTEXT("SetWrinkleSuppressionStrength", "Set Wrinkle Suppression Strength"),
        GetTransparencyPreviewSettings());
}

TOptional<float> SWetClothingTransparencyBakePanel::GetWrinkleMaskThreshold() const
{
    return GetTransparencyPreviewSettings().WrinkleMaskThreshold;
}

void SWetClothingTransparencyBakePanel::HandleWrinkleMaskThresholdChanged(const float InValue)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.WrinkleMaskThreshold = FMath::Clamp(InValue, 0.0f, 1.0f);
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

void SWetClothingTransparencyBakePanel::HandleWrinkleMaskThresholdCommitted(
    const float InValue,
    ETextCommit::Type)
{
    HandleWrinkleMaskThresholdChanged(InValue);
    CommitTransparencyPreviewSettings(
        LOCTEXT("SetWrinkleSuppressionThreshold", "Set Wrinkle Coverage Threshold"),
        GetTransparencyPreviewSettings());
}

TOptional<float> SWetClothingTransparencyBakePanel::GetWrinkleMaskSoftness() const
{
    return GetTransparencyPreviewSettings().WrinkleMaskSoftness;
}

void SWetClothingTransparencyBakePanel::HandleWrinkleMaskSoftnessChanged(const float InValue)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.WrinkleMaskSoftness = FMath::Clamp(InValue, 0.0f, 1.0f);
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

void SWetClothingTransparencyBakePanel::HandleWrinkleMaskSoftnessCommitted(
    const float InValue,
    ETextCommit::Type)
{
    HandleWrinkleMaskSoftnessChanged(InValue);
    CommitTransparencyPreviewSettings(
        LOCTEXT("SetWrinkleSuppressionSoftness", "Set Wrinkle Mask Softness"),
        GetTransparencyPreviewSettings());
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsBrushModeChecked(const EDWCTransparencyBrushMode Mode) const
{
    return BrushMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleBrushModeChanged(
    const ECheckBoxState NewState,
    const EDWCTransparencyBrushMode Mode)
{
    if (NewState == ECheckBoxState::Checked)
    {
        BrushMode = Mode;
        PushPaintSettingsToViewport();
        // Target Alpha is a persistent child with a live visibility binding.
        // Changing modes must not reconstruct the stage or preview viewport.
    }
}

float SWetClothingTransparencyBakePanel::GetBrushSizeCm() const
{
    return UE::DWCEditor::TransparencyPanel::RadiusUVToSizeCm(BrushRadiusUV);
}

FText SWetClothingTransparencyBakePanel::GetBrushSizeDisplayText() const
{
    return UE::DWCEditor::TransparencyPanel::FormatBrushSizeCm(GetBrushSizeCm());
}

TOptional<float> SWetClothingTransparencyBakePanel::GetBrushStrength() const { return BrushStrength; }
TOptional<float> SWetClothingTransparencyBakePanel::GetBrushFalloff() const { return BrushFalloff; }
TOptional<float> SWetClothingTransparencyBakePanel::GetBrushSpacing() const { return BrushSpacing; }
TOptional<float> SWetClothingTransparencyBakePanel::GetBrushTargetAlpha() const { return BrushTargetAlpha; }

void SWetClothingTransparencyBakePanel::HandleBrushSizeChanged(
    const float Value,
    const EDWCTransparencyBrushSizeTarget Target)
{
    const float NewRadiusUV = UE::DWCEditor::TransparencyPanel::SizeCmToRadiusUV(Value);
    if (Target == EDWCTransparencyBrushSizeTarget::RevealColorPaint)
    {
        FDWCTransparencyPaintSettings Settings = GetRevealPaintSettingsFromSession();
        if (FMath::IsNearlyEqual(Settings.RadiusUV, NewRadiusUV))
        {
            return;
        }
        Settings.RadiusUV = NewRadiusUV;
        DispatchRevealPaintState(MoveTemp(Settings));
        return;
    }

    if (FMath::IsNearlyEqual(BrushRadiusUV, NewRadiusUV))
    {
        return;
    }
    BrushRadiusUV = NewRadiusUV;
    PushPaintSettingsToViewport();
}

void SWetClothingTransparencyBakePanel::HandleBrushSizeCommitted(
    const float Value,
    ETextCommit::Type,
    const EDWCTransparencyBrushSizeTarget Target)
{
    HandleBrushSizeChanged(Value, Target);
}

FReply SWetClothingTransparencyBakePanel::HandleBrushSizePresetClicked(
    const float Value,
    const EDWCTransparencyBrushSizeTarget Target)
{
    HandleBrushSizeChanged(Value, Target);
    TSharedPtr<SComboButton> ComboButton = Target == EDWCTransparencyBrushSizeTarget::RevealColorPaint
        ? RevealPaintSizeComboButton
        : TransparencyBrushSizeComboButton;
    if (ComboButton.IsValid())
    {
        ComboButton->SetIsOpen(false);
    }
    return FReply::Handled();
}
void SWetClothingTransparencyBakePanel::HandleBrushStrengthCommitted(float Value, ETextCommit::Type)
{
    BrushStrength = FMath::Clamp(Value, 0.0f, 1.0f);
    PushPaintSettingsToViewport();
}
void SWetClothingTransparencyBakePanel::HandleBrushFalloffCommitted(float Value, ETextCommit::Type)
{
    BrushFalloff = FMath::Clamp(Value, 0.0f, 1.0f);
    PushPaintSettingsToViewport();
}
void SWetClothingTransparencyBakePanel::HandleBrushSpacingCommitted(float Value, ETextCommit::Type)
{
    BrushSpacing = FMath::Clamp(Value, 0.01f, 2.0f);
    PushPaintSettingsToViewport();
}
void SWetClothingTransparencyBakePanel::HandleBrushTargetAlphaCommitted(float Value, ETextCommit::Type)
{
    BrushTargetAlpha = FMath::Clamp(Value, 0.0f, 1.0f);
    PushPaintSettingsToViewport();
}

void SWetClothingTransparencyBakePanel::PushPaintSettingsToViewport()
{
    DispatchTransparencyPaintState(EDWCEditorSessionEffect::None);
    if (!PreviewViewport.IsValid())
    {
        return;
    }
    FDWCTransparencyPaintSettings Settings;
    Settings.Mode = BrushMode;
    Settings.RadiusUV = BrushRadiusUV;
    Settings.Strength = BrushStrength;
    Settings.Falloff = BrushFalloff;
    Settings.Spacing = BrushSpacing;
    Settings.TargetAlpha = BrushTargetAlpha;
    Settings.bEnabled = true;
    PreviewViewport->SetPaintSettings(Settings);
}

void SWetClothingTransparencyBakePanel::RefreshTransparencyStrokeList()
{
    if (TransparencyStrokeListContainer.IsValid())
    {
        TransparencyStrokeListContainer->SetContent(BuildTransparencyStrokeList());
    }
    RefreshRevealColorStrokeList();
}

void SWetClothingTransparencyBakePanel::RefreshRevealColorStrokeList()
{
    RevealColorStrokeItems.Reset();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer != nullptr)
    {
        const int32 SlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
        for (const FDWCTransparencyRevealColorStroke& Stroke : Layer->GetRevealColorPaintStrokes())
        {
            if (Stroke.MaterialSlotIndex == SlotIndex)
            {
                RevealColorStrokeItems.Add(MakeShared<FGuid>(Stroke.StrokeGuid));
            }
        }
    }
    if (RevealColorStrokeListView.IsValid())
    {
        RevealColorStrokeListView->RequestListRefresh();
        if (!RevealColorStrokeItems.IsEmpty())
        {
            RevealColorStrokeListView->RequestScrollIntoView(RevealColorStrokeItems.Last());
        }
    }
}

FReply SWetClothingTransparencyBakePanel::HandleUndoLastStrokeClicked()
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const int32 BaselineStrokeCount = GetCurrentBaselineStrokeCount();
    if (Asset == nullptr || Layer == nullptr || Layer->GetEditableStrokes().Num() <= BaselineStrokeCount)
    {
        return FReply::Handled();
    }
    const FGuid StrokeGuid = Layer->GetEditableStrokes().Last().StrokeGuid;
    const TArray<FDWCTransparencyBrushStroke> InvalidatedStrokes = {Layer->GetEditableStrokes().Last()};
    if (!EditSelectedLayerFinal(
            LOCTEXT("RemoveLastTransparencyStroke", "Remove Last Transparency Stroke"),
            StrokeGuid,
            [BaselineStrokeCount](FWetClothingTransparencyLayerData& MutableLayer)
            {
                TArray<FDWCTransparencyBrushStroke>& Strokes = MutableLayer.GetMutableEditableStrokes();
                if (Strokes.Num() <= BaselineStrokeCount) return false;
                Strokes.Pop();
                return true;
            })) return FReply::Handled();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ReplayAlphaStrokeHistory(InvalidatedStrokes);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleClearStrokesClicked()
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const int32 BaselineStrokeCount = GetCurrentBaselineStrokeCount();
    if (Asset == nullptr || Layer == nullptr || Layer->GetEditableStrokes().Num() <= BaselineStrokeCount)
    {
        return FReply::Handled();
    }
    TArray<FDWCTransparencyBrushStroke> InvalidatedStrokes;
    const TArray<FDWCTransparencyBrushStroke>& ExistingStrokes = Layer->GetEditableStrokes();
    InvalidatedStrokes.Append(
        ExistingStrokes.GetData() + BaselineStrokeCount,
        ExistingStrokes.Num() - BaselineStrokeCount);
    if (!EditSelectedLayerFinal(
            LOCTEXT("ClearTransparencyStrokes", "Clear Transparency Strokes"),
            FGuid(),
            [BaselineStrokeCount](FWetClothingTransparencyLayerData& MutableLayer)
            {
                TArray<FDWCTransparencyBrushStroke>& Strokes = MutableLayer.GetMutableEditableStrokes();
                if (Strokes.Num() <= BaselineStrokeCount) return false;
                Strokes.RemoveAt(
                    BaselineStrokeCount,
                    Strokes.Num() - BaselineStrokeCount);
                return true;
            })) return FReply::Handled();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ReplayAlphaStrokeHistory(InvalidatedStrokes);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleDeleteStrokeClicked(const FGuid StrokeGuid)
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        return FReply::Handled();
    }
    const FDWCTransparencyBrushStroke* Stroke = Layer->GetEditableStrokes().FindByPredicate(
        [StrokeGuid](const FDWCTransparencyBrushStroke& Candidate)
        {
            return Candidate.StrokeGuid == StrokeGuid;
        });
    if (Stroke == nullptr)
    {
        return FReply::Handled();
    }
    const TArray<FDWCTransparencyBrushStroke> InvalidatedStrokes = {*Stroke};
    if (!EditSelectedLayerFinal(
            LOCTEXT("DeleteTransparencyStroke", "Delete Transparency Stroke"),
            StrokeGuid,
            [StrokeGuid](FWetClothingTransparencyLayerData& MutableLayer)
            {
                return MutableLayer.GetMutableEditableStrokes().RemoveAll(
                    [StrokeGuid](const FDWCTransparencyBrushStroke& Stroke)
                    {
                        return Stroke.StrokeGuid == StrokeGuid;
                    }) > 0;
            })) return FReply::Handled();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ReplayAlphaStrokeHistory(InvalidatedStrokes);
    }
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleStrokeEnabledChanged(
    const ECheckBoxState NewState,
    const FGuid StrokeGuid)
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        return;
    }
    if (const FDWCTransparencyBrushStroke* Stroke = Layer->GetEditableStrokes().FindByPredicate(
            [StrokeGuid](const FDWCTransparencyBrushStroke& Candidate) { return Candidate.StrokeGuid == StrokeGuid; }))
    {
        const TArray<FDWCTransparencyBrushStroke> InvalidatedStrokes = {*Stroke};
        const bool bEnabled = NewState == ECheckBoxState::Checked;
        if (!EditSelectedLayerFinal(
                LOCTEXT("ToggleTransparencyStroke", "Toggle Transparency Stroke"),
                StrokeGuid,
                [StrokeGuid, bEnabled](FWetClothingTransparencyLayerData& MutableLayer)
                {
                    FDWCTransparencyBrushStroke* MutableStroke = MutableLayer.GetMutableEditableStrokes().FindByPredicate(
                        [StrokeGuid](const FDWCTransparencyBrushStroke& Candidate)
                        {
                            return Candidate.StrokeGuid == StrokeGuid;
                        });
                    if (MutableStroke == nullptr || MutableStroke->bEnabled == bEnabled) return false;
                    MutableStroke->bEnabled = bEnabled;
                    return true;
                })) return;
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->ReplayAlphaStrokeHistory(InvalidatedStrokes);
        }
    }
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateVisualizationModeComboItem(
    TSharedPtr<EDWCTransparencyVisualizationMode> Item) const
{
    const EDWCTransparencyVisualizationMode Mode =
        Item.IsValid() ? *Item : EDWCTransparencyVisualizationMode::Final;
    return SNew(SBox)
        .IsEnabled_Lambda([this, Mode]()
        {
            return IsVisualizationModeAvailable(Mode, GetCurrentStage());
        })
        [SNew(STextBlock).Text(GetVisualizationModeLabel(Mode))];
}

void SWetClothingTransparencyBakePanel::HandleVisualizationModeChanged(
    TSharedPtr<EDWCTransparencyVisualizationMode> Item,
    ESelectInfo::Type,
    const EDWCTransparencyEditorStage Stage)
{
    if (!Item.IsValid())
    {
        return;
    }
    if (!IsVisualizationModeAvailable(*Item, Stage))
    {
        return;
    }

    SetVisualizationModeForStage(*Item, Stage);
}

FText SWetClothingTransparencyBakePanel::GetVisualizationModeLabel(
    const EDWCTransparencyVisualizationMode Mode) const
{
    switch (Mode)
    {
    case EDWCTransparencyVisualizationMode::BaseRevealColor: return LOCTEXT("TransparencyViewBaseRevealColor", "Base Reveal Color");
    case EDWCTransparencyVisualizationMode::InnerColor: return LOCTEXT("TransparencyViewCorrectedRevealColor", "Corrected Reveal Color");
    case EDWCTransparencyVisualizationMode::CorrectionDifference: return LOCTEXT("TransparencyViewCorrectionDifference", "Correction Difference");
    case EDWCTransparencyVisualizationMode::RaycastGaps: return LOCTEXT("TransparencyViewRaycastGaps", "Raycast Gaps");
    case EDWCTransparencyVisualizationMode::AutoAlpha: return LOCTEXT("TransparencyViewAutoAlpha", "Auto Alpha");
    case EDWCTransparencyVisualizationMode::WrinkleSeparation: return LOCTEXT("TransparencyViewWrinkleSeparation", "Wrinkle Separation");
    case EDWCTransparencyVisualizationMode::ValidHit: return LOCTEXT("TransparencyViewValidHit", "Valid Hit");
    case EDWCTransparencyVisualizationMode::HitDistance: return LOCTEXT("TransparencyViewHitDistance", "Hit Distance");
    case EDWCTransparencyVisualizationMode::SourcePriority: return LOCTEXT("TransparencyViewSourcePriority", "Source Priority");
    case EDWCTransparencyVisualizationMode::RevealNormalOnly: return LOCTEXT("TransparencyViewRevealNormalOnly", "Reveal Normal Only");
    case EDWCTransparencyVisualizationMode::RevealNormalTexture: return LOCTEXT("TransparencyViewRevealNormalTexture", "Reveal Normal Texture");
    case EDWCTransparencyVisualizationMode::SourceCoverage: return LOCTEXT("TransparencyViewSourceCoverage", "Source Coverage");
    default: return LOCTEXT("TransparencyViewFinal", "Final");
    }
}

TSharedPtr<EDWCTransparencyVisualizationMode> SWetClothingTransparencyBakePanel::FindVisualizationModeItem(
    const EDWCTransparencyVisualizationMode Mode,
    const EDWCTransparencyEditorStage Stage) const
{
    const TArray<TSharedPtr<EDWCTransparencyVisualizationMode>>& Items =
        Stage == EDWCTransparencyEditorStage::RevealEditing
        ? RevealVisualizationModeItems
        : FinalVisualizationModeItems;
    const TSharedPtr<EDWCTransparencyVisualizationMode>* Match = Items.FindByPredicate(
        [Mode](const TSharedPtr<EDWCTransparencyVisualizationMode>& Item)
        {
            return Item.IsValid() && *Item == Mode;
        });
    return Match != nullptr ? *Match : nullptr;
}

EDWCTransparencyVisualizationMode SWetClothingTransparencyBakePanel::GetVisualizationModeForStage(
    const EDWCTransparencyEditorStage Stage) const
{
    if (SessionStore.IsValid())
    {
        const FDWCEditorTransparencySessionState& Transparency =
            SessionStore->GetState().Transparency;
        return Stage == EDWCTransparencyEditorStage::RevealEditing
            ? Transparency.RevealVisualizationMode
            : Stage == EDWCTransparencyEditorStage::FinalEditing
            ? Transparency.FinalVisualizationMode
            : SelectedVisualizationMode;
    }
    return Stage == EDWCTransparencyEditorStage::RevealEditing
        ? EDWCTransparencyVisualizationMode::InnerColor
        : EDWCTransparencyVisualizationMode::Final;
}

void SWetClothingTransparencyBakePanel::SetVisualizationModeForStage(
    const EDWCTransparencyVisualizationMode Mode,
    const EDWCTransparencyEditorStage Stage)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const EDWCTransparencySourceType SourceType = Layer != nullptr
        ? Layer->SourceType
        : EDWCTransparencySourceType::SameMeshMaterialSlots;
    if (!DWCTransparencyWorkflow::IsVisualizationModeAllowed(Stage, SourceType, Mode))
    {
        return;
    }
    SelectedVisualizationMode = Mode;
    DispatchTransparencyPreviewState();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetVisualizationMode(Mode);
    }
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsPreviewModeChecked(EWetClothingTransparencyPreviewMode Mode) const
{
    return PreviewViewport.IsValid() && PreviewViewport->GetPreviewMode() == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}
void SWetClothingTransparencyBakePanel::HandlePreviewModeChanged(ECheckBoxState State, EWetClothingTransparencyPreviewMode Mode)
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    if (State == ECheckBoxState::Checked && PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewMode(Mode);
        DispatchTransparencyPreviewState();
    }
}

FString SWetClothingTransparencyBakePanel::GetGenerateDisabledReason() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr)
    {
        return TEXT("The Wet Clothing Asset is unavailable.");
    }
    if (Asset->GetDWCSkeletalMesh() == nullptr)
    {
        return TEXT("The WCA has no DWC Skeletal Mesh.");
    }
    if (Layer == nullptr)
    {
        return TEXT("Create a Transparency Target Part for the selected material slot first.");
    }
    if (Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion)
    {
        return TEXT("The Transparency authoring data version is not supported.");
    }
    if (!HasUsableTransparencyDataUV())
    {
        return TEXT("The WCA has no usable Transparency Data UV Channel.");
    }
    if (!IsSourceTypeAvailable(Layer->SourceType))
    {
        return TEXT("The selected Transparency source type is unavailable.");
    }
    if (!PreviewSlotStates.IsReady(Layer->TargetSurface.OuterMaterialSlotIndex))
    {
        return TEXT("The selected target slot is not ready for DWC preview and generation.");
    }
    if (Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        if (Layer->BlueprintSource.BlueprintClass.IsNull())
        {
            return TEXT("Assign a Source Blueprint.");
        }
        if (!BlueprintHierarchySession.IsValid())
        {
            return TEXT("The Source Blueprint hierarchy session is unavailable.");
        }
        const FDWCTransparencyType2Readiness Readiness =
            FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
                *Asset,
                *Layer,
                BlueprintHierarchySession->GetSnapshot());
        if (!Readiness.bReady)
        {
            return Readiness.DisabledReason;
        }
    }
    if (Layer->SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh)
    {
        for (const FWetClothingTransparencyExternalMeshEntry& Source :
             Layer->ExternalMeshSource.SourcePriority)
        {
            if (Source.BakeTransform.ContainsNaN())
            {
                return FString::Printf(
                    TEXT("External source '%s' has an invalid placement transform."),
                    Source.SkeletalMesh != nullptr ? *Source.SkeletalMesh->GetName() : TEXT("Missing"));
            }
            if (!Source.BakeTransform.GetScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
            {
                return FString::Printf(
                    TEXT("External source '%s' has a non-unit scale. Type 3 placement supports translation and rotation only."),
                    Source.SkeletalMesh != nullptr ? *Source.SkeletalMesh->GetName() : TEXT("Missing"));
            }
        }
    }
    TArray<FString> Errors;
    if (!FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
            Asset->GetDWCSkeletalMesh(), *Layer, Errors, GetTransparencyDataUVChannel()))
    {
        return Errors.IsEmpty()
            ? TEXT("The selected Transparency Target Part is not ready for generation.")
            : FString::Join(Errors, TEXT("\n"));
    }
    return FString();
}

bool SWetClothingTransparencyBakePanel::IsGenerateEnabled() const
{
    return GetGenerateDisabledReason().IsEmpty();
}
bool SWetClothingTransparencyBakePanel::IsBakeEditedEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr ||
        Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion ||
        !IsSourceTypeAvailable(Layer->SourceType) ||
        !HasUsableTransparencyDataUV() ||
        !PreviewSlotStates.IsReady(Layer->TargetSurface.OuterMaterialSlotIndex))
    {
        return false;
    }
    const TSharedPtr<FDWCTransparencySourcePayload>* Result = AutoBakeResults.Find(Layer->LayerGuid);
    if (Result == nullptr || !Result->IsValid())
    {
        return false;
    }
    FString Reason;
    return FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(*Layer, *Result->Get(), Reason);
}
bool SWetClothingTransparencyBakePanel::CanUseFullBlueprintPreview() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return Layer != nullptr &&
        (Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
         Layer->SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh);
}

void SWetClothingTransparencyBakePanel::UpdateInnerSourceStatus()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        InnerSourceStatusMessage = TEXT("Select a Transparency Target Part.");
        return;
    }
    if (Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        InnerSourceStatusMessage.Reset();
        return;
    }
    if (Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
    {
        InnerSourceStatusMessage = TEXT("Base reveal color and Reveal Color Paint are written directly to the target DWC UV Channel. No ray projection is used.");
        return;
    }
    if (Layer->SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh)
    {
        const int32 SourceCount = Layer->ExternalMeshSource.SourcePriority.Num();
        InnerSourceStatusMessage = SourceCount > 0
            ? FString::Printf(TEXT("%d external raycast source(s) configured. Use Full Preview and Place to align them with the target mesh."), SourceCount)
            : TEXT("Add at least one External Skeletal Mesh raycast source.");
        return;
    }
    if (Layer->BlueprintSource.BlueprintClass.IsNull())
    {
        InnerSourceStatusMessage = TEXT("Assign a Source Blueprint.");
        return;
    }
    if (!BlueprintHierarchySession.IsValid())
    {
        InnerSourceStatusMessage = TEXT("The Source Blueprint hierarchy session is unavailable.");
        return;
    }

    const FDWCTransparencyType2Readiness Readiness =
        FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
            *Asset,
            *Layer,
            BlueprintHierarchySession->GetSnapshot());
    InnerSourceStatusMessage = Readiness.bReady
        ? TEXT("Blueprint target and raycast source priority are ready.")
        : Readiness.DisabledReason;
}

FWetClothingTransparencyLayerData* SWetClothingTransparencyBakePanel::GetSelectedLayer()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr
        ? Asset->Authored.TransparencyData.FindTransparencyLayer(SelectedMaterialSlotIndex)
        : nullptr;
}
const FWetClothingTransparencyLayerData* SWetClothingTransparencyBakePanel::GetSelectedLayer() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr
        ? Asset->Authored.TransparencyData.FindTransparencyLayer(SelectedMaterialSlotIndex)
        : nullptr;
}

FGuid SWetClothingTransparencyBakePanel::GetSelectedLayerGuid() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return Layer != nullptr ? Layer->LayerGuid : FGuid();
}

SWetClothingTransparencyBakePanel::FMaterialSlotItemPtr SWetClothingTransparencyBakePanel::FindMaterialSlotItem(int32 SlotIndex) const
{
    const FMaterialSlotItemPtr* Match = MaterialSlotItems.FindByPredicate([SlotIndex](const FMaterialSlotItemPtr& Item) { return Item.IsValid() && Item->SlotIndex == SlotIndex; });
    return Match != nullptr ? *Match : nullptr;
}

const FDWCEditorPreviewSlotState* SWetClothingTransparencyBakePanel::FindPreviewSlotState(
    const int32 SlotIndex) const
{
    return PreviewSlotStates.Find(SlotIndex);
}

int32 SWetClothingTransparencyBakePanel::GetTransparencyDataUVChannel() const
{
    return UE::DWCEditor::TransparencyPanel::ResolveDataUVChannel(WetClothingAsset.Get());
}

bool SWetClothingTransparencyBakePanel::HasUsableTransparencyDataUV() const
{
    return GetTransparencyDataUVChannel() != INDEX_NONE;
}
TSharedPtr<int32> SWetClothingTransparencyBakePanel::FindUVChannelItem(int32 Index) const
{
    const TSharedPtr<int32>* Match = UVChannelItems.FindByPredicate([Index](const TSharedPtr<int32>& Item) { return Item.IsValid() && *Item == Index; });
    return Match != nullptr ? *Match : nullptr;
}
bool SWetClothingTransparencyBakePanel::EditSelectedLayer(
    const FText& Text,
    TFunctionRef<bool(FWetClothingTransparencyLayerData&)> Edit,
    const EDWCTransparencyPanelRefreshFlags RefreshFlags)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!AuthoringDocument.IsValid() || Layer == nullptr)
    {
        return false;
    }
    const FGuid LayerGuid = Layer->LayerGuid;
    FWetClothingTransparencyLayerData EditedLayer = *Layer;
    if (!Edit(EditedLayer))
    {
        return false;
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    Change.MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
    Change.LayerGuid = LayerGuid;
    const FDWCEditorAuthoringResult Result = AuthoringDocument->Edit(
        Text,
        Change,
        [LayerGuid, EditedLayer = MoveTemp(EditedLayer)](UWetClothingAsset& Asset) mutable
        {
            FWetClothingTransparencyLayerData* MutableLayer =
                Asset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            if (MutableLayer == nullptr)
            {
                return false;
            }
            *MutableLayer = MoveTemp(EditedLayer);
            return true;
        });
    if (!Result.bChanged)
    {
        return false;
    }
    AutoBakeResults.Remove(LayerGuid);
    RequestRefresh(RefreshFlags);
    return true;
}

bool SWetClothingTransparencyBakePanel::EditSelectedLayerFinal(
    const FText& Text,
    const FGuid& ElementGuid,
    TFunctionRef<bool(FWetClothingTransparencyLayerData&)> Edit)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!AuthoringDocument.IsValid() || Layer == nullptr)
    {
        return false;
    }

    const FGuid LayerGuid = Layer->LayerGuid;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::PreviewIncremental |
        EDWCEditorAuthoringImpact::TransparencyFinalBake;
    Change.MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
    Change.LayerGuid = LayerGuid;
    Change.ElementGuid = ElementGuid;
    UWetClothingAsset* MutableAsset = WetClothingAsset.Get();
    UDWCTransparencyLayerStrokeHistory* StrokeHistory = MutableAsset != nullptr
        ? MutableAsset->EnsureTransparencyLayerStrokeHistory(LayerGuid)
        : nullptr;
    if (StrokeHistory == nullptr)
    {
        return false;
    }
    return AuthoringDocument->Edit(
        Text,
        Change,
        StrokeHistory,
        [LayerGuid, &Edit](UWetClothingAsset& Asset)
        {
            FWetClothingTransparencyLayerData* MutableLayer =
                Asset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            return MutableLayer != nullptr && Edit(*MutableLayer);
        }).bChanged;
}

void SWetClothingTransparencyBakePanel::EditFinalBakeSettings(
    const FText& Text,
    TFunctionRef<bool(FWetClothingTransparencyData&)> Edit,
    const EDWCTransparencyFinalPreviewRefresh PreviewRefresh)
{
    if (!AuthoringDocument.IsValid())
    {
        return;
    }
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyFinalBake;
    const FDWCEditorAuthoringResult Result = AuthoringDocument->Edit(
        Text,
        Change,
        [&Edit](UWetClothingAsset& Asset)
        {
            return Edit(Asset.Authored.TransparencyData);
        });
    if (!Result.bChanged) return;
    if (PreviewViewport.IsValid())
    {
        if (PreviewRefresh == EDWCTransparencyFinalPreviewRefresh::WrinkleSuppression)
        {
            PreviewViewport->RefreshWrinkleSuppressionPreview();
        }
        else if (PreviewRefresh == EDWCTransparencyFinalPreviewRefresh::OuterEdgeFeather)
        {
            PreviewViewport->RefreshOuterEdgeFeatherPreview();
        }
    }
}

void SWetClothingTransparencyBakePanel::CommitTransparencyPreviewSettings(
    const FText& Text,
    const FDWCTransparencyPreviewSettings& Settings)
{
    if (!AuthoringDocument.IsValid())
    {
        return;
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::TransparencyFinalBake;
    AuthoringDocument->Edit(
        Text,
        Change,
        [&Settings](UWetClothingAsset& Asset)
        {
            FWetClothingTransparencyData& Data = Asset.Authored.TransparencyData;
            const bool bChanged =
                !FMath::IsNearlyEqual(Data.TransparencyPreviewStrength, Settings.TransparencyStrength) ||
                !FMath::IsNearlyEqual(Data.WrinkleSuppressionStrength, Settings.WrinkleSuppressionStrength) ||
                !FMath::IsNearlyEqual(Data.WrinkleSuppressionCoverageThreshold, Settings.WrinkleMaskThreshold) ||
                !FMath::IsNearlyEqual(Data.WrinkleSuppressionMaskSoftness, Settings.WrinkleMaskSoftness);
            if (!bChanged)
            {
                return false;
            }

            Data.TransparencyPreviewStrength = Settings.TransparencyStrength;
            Data.WrinkleSuppressionStrength = Settings.WrinkleSuppressionStrength;
            Data.WrinkleSuppressionCoverageThreshold = Settings.WrinkleMaskThreshold;
            Data.WrinkleSuppressionMaskSoftness = Settings.WrinkleMaskSoftness;
            return true;
        });
}

void SWetClothingTransparencyBakePanel::CommitRevealNormalRuntimeSettings(
    const FText& TransactionText,
    const bool bEnable,
    const float Strength)
{
    const FGuid LayerGuid = GetSelectedLayerGuid();
    if (!AuthoringDocument.IsValid() || !LayerGuid.IsValid())
    {
        return;
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::RuntimeBinding;
    Change.LayerGuid = LayerGuid;
    AuthoringDocument->Edit(
        TransactionText,
        Change,
        [LayerGuid, bEnable, SafeStrength = FMath::Clamp(Strength, 0.0f, 4.0f)](
            UWetClothingAsset& Asset)
        {
            FWetClothingTransparencyLayerData* Layer =
                Asset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            if (Layer == nullptr ||
                (Layer->bEnableRevealNormal == bEnable &&
                 FMath::IsNearlyEqual(Layer->RevealNormalStrength, SafeStrength)))
            {
                return false;
            }
            Layer->bEnableRevealNormal = bEnable;
            Layer->RevealNormalStrength = SafeStrength;
            return true;
        });
}

TSharedRef<ITableRow> SWetClothingTransparencyBakePanel::GenerateLayerRow(FLayerItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
    UMaterialInterface* Material = nullptr;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get();
        Asset != nullptr && Item.IsValid())
    {
        if (const USkeletalMesh* Mesh = Asset->GetDWCSkeletalMesh();
            Mesh != nullptr && Mesh->GetMaterials().IsValidIndex(Item->MaterialSlotIndex))
        {
            Material = Mesh->GetMaterials()[Item->MaterialSlotIndex].MaterialInterface;
        }
    }

    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));
    if (Material != nullptr && ThumbnailPool.IsValid() && Item.IsValid())
    {
        TSharedPtr<FAssetThumbnail>& Thumbnail = MaterialSlotThumbnails.FindOrAdd(Item->MaterialSlotIndex);
        if (!Thumbnail.IsValid())
        {
            Thumbnail = MakeShared<FAssetThumbnail>(Material, 48, 48, ThumbnailPool);
        }

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    const FDWCEditorPreviewSlotState* PreviewState = Item.IsValid()
        ? FindPreviewSlotState(Item->MaterialSlotIndex)
        : nullptr;
    const bool bPreviewReady = PreviewState != nullptr && PreviewState->bPreviewReady;
    const FText PreviewTooltip = PreviewState != nullptr
        ? FDWCEditorPreviewSlotResolver::GetIssueText(PreviewState->Issue)
        : FText::GetEmpty();
    FText ResolutionText = LOCTEXT("TransparencyNotConfigured", "Not Configured");
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get();
        Asset != nullptr && Item.IsValid())
    {
        if (const FWetClothingTransparencyLayerData* Layer =
                Asset->Authored.TransparencyData.FindTransparencyLayer(Item->MaterialSlotIndex))
        {
            if (Layer->Intent == EDWCTransparencyLayerIntent::Draft)
            {
                ResolutionText = LOCTEXT("TransparencyDraft", "Draft");
            }
            else if (Layer->Intent == EDWCTransparencyLayerIntent::Disabled)
            {
                ResolutionText = LOCTEXT("TransparencyDisabled", "Disabled");
            }
            else
            {
                const FDWCTransparencyResolvedOutputResolution Resolved =
                    FDWCTransparencyResolutionResolver::Resolve(*Asset, *Layer);
                ResolutionText = Layer->OutputResolutionMode == EDWCTransparencyOutputResolutionMode::Auto
                    ? FText::Format(
                        LOCTEXT("TransparencyAutoResolutionRow", "Auto  {0}"),
                        FText::AsNumber(Resolved.Size))
                    : FText::AsNumber(Resolved.Size);
            }
        }
    }

    return SNew(STableRow<FLayerItemPtr>, Owner)
        .Padding(4.0f)
        .IsEnabled(bPreviewReady)
        .ToolTipText(PreviewTooltip)
        [SNew(SHorizontalBox)
         + SHorizontalBox::Slot()
               .AutoWidth()
               .VAlign(VAlign_Center)
               .Padding(0.0f, 0.0f, 8.0f, 0.0f)
               [SNew(SBox)
                    .WidthOverride(52.0f)
                    .HeightOverride(52.0f)
                    [ThumbnailWidget]]
         // Matches the Material Slots row layout without paying the cost of per-row UV previews.
         + SHorizontalBox::Slot()
               .AutoWidth()
               .VAlign(VAlign_Center)
               .Padding(0.0f, 0.0f, 8.0f, 0.0f)
               [SNew(SBox)
                    .WidthOverride(52.0f)
                    .HeightOverride(52.0f)]
         + SHorizontalBox::Slot()
               .FillWidth(1.0f)
               .VAlign(VAlign_Center)
               .Padding(2.0f, 0.0f, 10.0f, 0.0f)
               [SNew(STextBlock)
                    .Text(Item.IsValid()
                              ? FText::Format(
                                    LOCTEXT("TransparencyTargetPartRow", "[{0}] {1}"),
                                    FText::AsNumber(Item->MaterialSlotIndex),
                                    FText::FromName(Item->MaterialSlotName))
                              : LOCTEXT("MissingTransparencyTargetPart", "Missing Target Part"))
                    .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
         + SHorizontalBox::Slot()
               .AutoWidth()
               .VAlign(VAlign_Center)
               [SNew(STextBlock)
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                    .Text(ResolutionText)]];
}

void SWetClothingTransparencyBakePanel::HandleLayerSelectionChanged(FLayerItemPtr Item, ESelectInfo::Type)
{
    if (bRefreshingLayerSelection)
    {
        return;
    }
    if (!Item.IsValid())
    {
        return;
    }

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || Asset->GetDWCSkeletalMesh() == nullptr)
    {
        return;
    }
    if (Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion)
    {
        StatusMessage = FString::Printf(
            TEXT("Unsupported Transparency data version %d (current: %d). Recreate the Transparency setup for this WCA."),
            Asset->Authored.TransparencyData.DataVersion,
            FWetClothingTransparencyData::CurrentDataVersion);
        PanelStatus = EDWCTransparencyPanelStatus::Error;
        return;
    }

    if (!Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured)
    {
        StatusMessage = TEXT("Choose a Character Structure Type in Stage 1 before selecting a Transparency Target Part.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return;
    }

    FDWCEditorAuthoringOperationScope DiagnosticScope(
        TEXT("Transparency.SelectTargetPart"),
        Asset);

    if (SelectedMaterialSlotIndex != Item->MaterialSlotIndex)
    {
        // Full 2048 intermediate results are intentionally scoped to the active target part.
        AutoBakeResults.Reset();
    }
    const FWetClothingTransparencyLayerData* NewLayer =
        Asset->Authored.TransparencyData.FindTransparencyLayer(Item->MaterialSlotIndex);
    const bool bHasBakedBaseline = UE::DWCEditor::TransparencyPanel::FindExactBakedMap(Asset, NewLayer) != nullptr;
    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState WorkflowState =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(
            Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured,
            NewLayer,
            bHasBakedBaseline);
    const EDWCTransparencyEditorStage Stage =
        [&]()
        {
            const FGuid NewLayerGuid = NewLayer != nullptr ? NewLayer->LayerGuid : FGuid();
            if (const EDWCTransparencyEditorStage* ExistingStage = StageByLayer.Find(NewLayerGuid))
            {
                return DWCTransparencyWorkflow::NormalizeRequestedStage(*ExistingStage, WorkflowState);
            }
            return WorkflowState.DefaultStage;
        }();
    SelectTransparencyTargetSlotWithResolvedStage(Item->MaterialSlotIndex, Stage);
    if (NewLayer != nullptr)
    {
        FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
        Settings.RevealNormalStrength = NewLayer->RevealNormalStrength;
        DispatchTransparencyPreviewSettings(MoveTemp(Settings));
    }
    if (LayerListView.IsValid())
    {
        LayerListView->RequestListRefresh();
    }
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
}

bool SWetClothingTransparencyBakePanel::CanCreateLayerForSelectedSlot() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr &&
           SelectedMaterialSlotIndex != INDEX_NONE &&
           GetSelectedLayer() == nullptr &&
           Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured &&
           GetTransparencyDataUVChannel() != INDEX_NONE &&
           PreviewSlotStates.IsReady(SelectedMaterialSlotIndex);
}

FReply SWetClothingTransparencyBakePanel::HandleCreateLayerClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !CanCreateLayerForSelectedSlot())
    {
        StatusMessage = TEXT("The selected Wettable slot is not ready to create a Transparency Target Part.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return FReply::Handled();
    }

    const FLayerItemPtr* SelectedItem = LayerItems.FindByPredicate(
        [this](const FLayerItemPtr& Item)
        {
            return Item.IsValid() && Item->MaterialSlotIndex == SelectedMaterialSlotIndex;
        });
    if (SelectedItem == nullptr)
    {
        return FReply::Handled();
    }

    const FGuid NewLayerGuid = FGuid::NewGuid();
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    Change.MaterialSlotIndex = SelectedMaterialSlotIndex;
    Change.LayerGuid = NewLayerGuid;
    const int32 SlotIndex = (*SelectedItem)->MaterialSlotIndex;
    const FName SlotName = (*SelectedItem)->MaterialSlotName;
    if (!AuthoringDocument.IsValid() ||
        !AuthoringDocument->Edit(
            LOCTEXT("CreateTransparencyTargetPart", "Create Transparency Target Part"),
            Change,
            [NewLayerGuid, SlotIndex, SlotName](UWetClothingAsset& MutableAsset)
            {
                if (MutableAsset.Authored.TransparencyData.FindTransparencyLayer(SlotIndex) != nullptr)
                {
                    return false;
                }
                FWetClothingTransparencyLayerData& NewLayer =
                    MutableAsset.Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
                NewLayer.LayerGuid = NewLayerGuid;
                NewLayer.Intent = EDWCTransparencyLayerIntent::Draft;
                NewLayer.TargetSurface.OuterMaterialSlotIndex = SlotIndex;
                NewLayer.TargetSurface.OuterMaterialSlotName = SlotName;
                NewLayer.SourceType = MutableAsset.Authored.TransparencyData.CharacterStructureType;
                NewLayer.bSourceTypeConfigured = true;
                return true;
            }).bChanged)
    {
        return FReply::Handled();
    }

    (*SelectedItem)->LayerGuid = NewLayerGuid;
    SelectTransparencyTargetSlotWithResolvedStage(
        SlotIndex,
        EDWCTransparencyEditorStage::MapGeneration);
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleRemoveLayerClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
    if (Asset == nullptr || SelectedLayer == nullptr) return FReply::Handled();
    if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("RemoveTransparencyLayerConfirm", "Remove the selected Transparency Target Part and its editable strokes?")) != EAppReturnType::Yes)
        return FReply::Handled();
    const FGuid RemovedLayerGuid = SelectedLayer->LayerGuid;
    const int32 RemovedSlot = SelectedLayer->TargetSurface.OuterMaterialSlotIndex;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    Change.MaterialSlotIndex = RemovedSlot;
    Change.LayerGuid = RemovedLayerGuid;
    if (!AuthoringDocument.IsValid() ||
        !AuthoringDocument->Edit(
            LOCTEXT("RemoveTransparencyLayer", "Remove Transparency Target Part"),
            Change,
            [RemovedLayerGuid](UWetClothingAsset& MutableAsset)
            {
                return MutableAsset.Authored.TransparencyData.TransparencyLayers.RemoveAll(
                    [RemovedLayerGuid](const FWetClothingTransparencyLayerData& Layer)
                    {
                        return Layer.LayerGuid == RemovedLayerGuid;
                    }) > 0;
            }).bChanged) return FReply::Handled();
    AutoBakeResults.Remove(RemovedLayerGuid);
    StageByLayer.Remove(RemovedLayerGuid);
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(
            FDWCSelectTransparencyTargetSlotAction{RemovedSlot});
    }
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}
bool SWetClothingTransparencyBakePanel::CanRemoveSelectedLayer() const { return GetSelectedLayer() != nullptr; }

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateUVChannelComboItem(TSharedPtr<int32> Item) const { return SNew(STextBlock).Text(FText::Format(LOCTEXT("UVChannelLabel", "UV {0}"), FText::AsNumber(Item.IsValid() ? *Item : 0))); }
bool SWetClothingTransparencyBakePanel::IsVisualizationModeAvailable(
    const EDWCTransparencyVisualizationMode Mode,
    const EDWCTransparencyEditorStage Stage) const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || !DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            Stage,
            Layer->SourceType,
            Mode))
    {
        return false;
    }
    if ((Mode == EDWCTransparencyVisualizationMode::RevealNormalOnly ||
         Mode == EDWCTransparencyVisualizationMode::SourceCoverage ||
         Mode == EDWCTransparencyVisualizationMode::RevealNormalTexture) &&
        GetTransparencyPreviewSettings().RevealNormalSource ==
            EDWCTransparencyRevealNormalPreviewSource::Baked)
    {
        return false;
    }

    const TSharedPtr<FDWCTransparencySourcePayload>* Result =
        AutoBakeResults.Find(GetSelectedLayerGuid());
    return Result != nullptr && Result->IsValid() &&
        (Stage != EDWCTransparencyEditorStage::RevealEditing ||
         !(*Result)->bIsFinalBakedBaseline);
}
FReply SWetClothingTransparencyBakePanel::HandleAddInnerSlotClicked()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr) return FReply::Handled();
    int32 SlotIndex = INDEX_NONE;
    for (const FMaterialSlotItemPtr& Item : MaterialSlotItems)
    {
        if (!Item.IsValid() || Item->SlotIndex == Layer->TargetSurface.OuterMaterialSlotIndex) continue;
        if (!Layer->SameMeshSource.InnerSlotPriority.ContainsByPredicate([Item](const auto& Slot) { return Slot.MaterialSlotIndex == Item->SlotIndex; })) { SlotIndex = Item->SlotIndex; break; }
    }
    if (SlotIndex == INDEX_NONE) return FReply::Handled();
    const FMaterialSlotItemPtr Item = FindMaterialSlotItem(SlotIndex);
    EditSelectedLayer(LOCTEXT("AddTransparencyInnerSlot", "Add Transparency Inner Material Slot"), [Item](auto& TargetLayer)
    {
        auto& Slot = TargetLayer.SameMeshSource.InnerSlotPriority.AddDefaulted_GetRef();
        Slot.bEnabled = true;
        Slot.MaterialSlotIndex = Item->SlotIndex;
        Slot.MaterialSlotName = Item->SlotName;
        return true;
    }, EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::SourceModel |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}
FReply SWetClothingTransparencyBakePanel::HandleRemoveInnerSlotClicked(int32 Index)
{
    EditSelectedLayer(LOCTEXT("RemoveTransparencyInnerSlot", "Remove Transparency Inner Material Slot"), [Index](auto& Layer)
    {
        if (!Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index)) return false;
        Layer.SameMeshSource.InnerSlotPriority.RemoveAt(Index);
        return true;
    }, EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::SourceModel |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}
FReply SWetClothingTransparencyBakePanel::HandleMoveInnerSlotClicked(int32 Index, int32 Direction)
{
    EditSelectedLayer(LOCTEXT("MoveTransparencyInnerSlot", "Reorder Transparency Inner Material Slot"), [Index, Direction](auto& Layer)
    {
        const int32 To = Index + Direction;
        if (!Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index) ||
            !Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(To)) return false;
        Layer.SameMeshSource.InnerSlotPriority.Swap(Index, To);
        return true;
    }, EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::SourceModel |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}
void SWetClothingTransparencyBakePanel::HandleInnerMaterialSlotChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type, int32 Index)
{
    if (!Item.IsValid()) return;
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || Item->SlotIndex == Layer->TargetSurface.OuterMaterialSlotIndex ||
        Layer->SameMeshSource.InnerSlotPriority.ContainsByPredicate(
            [Item, Index, Layer](const FWetClothingTransparencyInnerSlot& Slot)
            {
                const int32 CandidateIndex = static_cast<int32>(&Slot - Layer->SameMeshSource.InnerSlotPriority.GetData());
                return CandidateIndex != Index && Slot.MaterialSlotIndex == Item->SlotIndex;
            }))
    {
        StatusMessage = TEXT("The Inner Source Part must be unique and different from the Transparency Target Part.");
        return;
    }
    // The row thumbnail is material-specific, so rebuild only this Stage 2
    // content after a valid source material change.
    EditSelectedLayer(LOCTEXT("SetTransparencyInnerSlot", "Set Transparency Inner Material Slot"), [Item, Index](auto& Layer)
    {
        if (!Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index)) return false;
        auto& Slot = Layer.SameMeshSource.InnerSlotPriority[Index];
        if (Slot.MaterialSlotIndex == Item->SlotIndex && Slot.MaterialSlotName == Item->SlotName) return false;
        Slot.MaterialSlotIndex = Item->SlotIndex;
        Slot.MaterialSlotName = Item->SlotName;
        return true;
    }, EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::SourceModel |
        EDWCTransparencyPanelRefreshFlags::Viewport);
}
void SWetClothingTransparencyBakePanel::HandleInnerUVChannelChanged(TSharedPtr<int32> Item, ESelectInfo::Type, int32 Index)
{
    if (!Item.IsValid()) return;
    EditSelectedLayer(LOCTEXT("SetTransparencyInnerUV", "Set Transparency Inner UV Channel"), [Item, Index](auto& Layer)
    {
        if (!Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index) ||
            Layer.SameMeshSource.InnerSlotPriority[Index].SourceUVChannel == *Item) return false;
        Layer.SameMeshSource.InnerSlotPriority[Index].SourceUVChannel = *Item;
        return true;
    }, EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::SourceModel |
        EDWCTransparencyPanelRefreshFlags::Viewport);
}


#undef LOCTEXT_NAMESPACE
