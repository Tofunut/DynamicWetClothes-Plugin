//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewOrchestrator.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Foundation/Preview/Layers/DWCEditorPreviewLayerResolver.h"
#include "WetClothing/Foundation/Preview/Session/DWCEditorPreviewSession.h"
#include "WetRendering/WetMaterialParameters.h"

void FDWCEditorPreviewOrchestrator::Initialize(
    UWetClothingAsset* WetClothingAssetIn,
    FDWCEditorPreviewSession* PreviewSessionIn,
    const EDWCEditorAuthoringDomain ActiveDomainIn,
    TSharedPtr<FDWCEditorSessionStore> SessionStoreIn)
{
    Shutdown();
    WetClothingAsset = WetClothingAssetIn;
    PreviewSession = PreviewSessionIn;
    ActiveDomain = ActiveDomainIn;
    SessionStore = MoveTemp(SessionStoreIn);

    if (SessionStore.IsValid())
    {
        SessionStateChangedHandle = SessionStore->OnChanged().AddRaw(
            this,
            &FDWCEditorPreviewOrchestrator::HandleSessionStateChanged);
        HandleSessionStateChanged(
            SessionStore->GetState(),
            EDWCEditorSessionEffect::UpdatePreviewParameters,
            SessionStore->GetRevision());
    }
}

void FDWCEditorPreviewOrchestrator::Shutdown()
{
    if (SessionStore.IsValid() && SessionStateChangedHandle.IsValid())
    {
        SessionStore->OnChanged().Remove(SessionStateChangedHandle);
    }
    SessionStateChangedHandle.Reset();
    SessionStore.Reset();
    LiveLayersBySlot.Reset();
    WetClothingAsset.Reset();
    PreviewSession = nullptr;
    ActiveDomain = EDWCEditorAuthoringDomain::None;
    bShowSavedCrossLayer = true;
    ComposeCount = 0;
    NoChangeCount = 0;
}

void FDWCEditorPreviewOrchestrator::SetShowSavedCrossLayer(const bool bShow)
{
    if (bShowSavedCrossLayer == bShow)
    {
        return;
    }
    bShowSavedCrossLayer = bShow;
    RecomposeBuiltSlots();
}

void FDWCEditorPreviewOrchestrator::SetPreviewWetness(const float PreviewWetness)
{
    if (PreviewSession != nullptr)
    {
        PreviewSession->SetPreviewWetness(PreviewWetness);
    }
}

void FDWCEditorPreviewOrchestrator::SetLiveLayers(
    const int32 MaterialSlotIndex,
    TArray<FDWCEditorPreviewLayer> Layers)
{
    Layers.RemoveAll(
        [MaterialSlotIndex](const FDWCEditorPreviewLayer& Layer)
        {
            return Layer.MaterialSlotIndex != MaterialSlotIndex;
        });
    LiveLayersBySlot.Add(MaterialSlotIndex, MoveTemp(Layers));
    RecomposeSlot(MaterialSlotIndex);
}

void FDWCEditorPreviewOrchestrator::SetLiveLayer(
    const int32 MaterialSlotIndex,
    FDWCEditorPreviewLayer Layer)
{
    if (Layer.MaterialSlotIndex != MaterialSlotIndex)
    {
        return;
    }

    TArray<FDWCEditorPreviewLayer>& Layers = LiveLayersBySlot.FindOrAdd(MaterialSlotIndex);
    if (FDWCEditorPreviewLayer* Existing = Layers.FindByPredicate(
            [&Layer](const FDWCEditorPreviewLayer& Candidate)
            {
                return Candidate.Kind == Layer.Kind;
            }))
    {
        *Existing = MoveTemp(Layer);
    }
    else
    {
        Layers.Add(MoveTemp(Layer));
    }
    RecomposeSlot(MaterialSlotIndex);
}

void FDWCEditorPreviewOrchestrator::ClearLiveLayer(
    const int32 MaterialSlotIndex,
    const EDWCEditorPreviewLayerKind LayerKind)
{
    TArray<FDWCEditorPreviewLayer>* Layers = LiveLayersBySlot.Find(MaterialSlotIndex);
    if (Layers == nullptr)
    {
        return;
    }

    const int32 RemovedCount = Layers->RemoveAll(
        [LayerKind](const FDWCEditorPreviewLayer& Layer)
        {
            return Layer.Kind == LayerKind;
        });
    if (RemovedCount == 0)
    {
        return;
    }
    if (Layers->IsEmpty())
    {
        LiveLayersBySlot.Remove(MaterialSlotIndex);
    }
    RecomposeSlot(MaterialSlotIndex);
}

void FDWCEditorPreviewOrchestrator::ClearLiveLayers(const int32 MaterialSlotIndex)
{
    if (LiveLayersBySlot.Remove(MaterialSlotIndex) > 0)
    {
        RecomposeSlot(MaterialSlotIndex);
    }
}

void FDWCEditorPreviewOrchestrator::ClearAllLiveLayers()
{
    TArray<int32> AffectedSlots;
    LiveLayersBySlot.GenerateKeyArray(AffectedSlots);
    LiveLayersBySlot.Reset();
    for (const int32 MaterialSlotIndex : AffectedSlots)
    {
        RecomposeSlot(MaterialSlotIndex);
    }
}

void FDWCEditorPreviewOrchestrator::PreparePreviewMaterials(
    const TConstArrayView<int32> MaterialSlotIndices)
{
    if (PreviewSession == nullptr)
    {
        return;
    }

    for (const int32 MaterialSlotIndex : MaterialSlotIndices)
    {
        PreviewSession->SetLayerStack(MaterialSlotIndex, BuildStack(MaterialSlotIndex));
    }
    PreviewSession->PreparePreviewMaterials(MaterialSlotIndices);
}

void FDWCEditorPreviewOrchestrator::RecomposeSlot(const int32 MaterialSlotIndex)
{
    if (PreviewSession == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    ++ComposeCount;
    if (!PreviewSession->SetLayerStack(MaterialSlotIndex, BuildStack(MaterialSlotIndex)))
    {
        ++NoChangeCount;
    }
}

void FDWCEditorPreviewOrchestrator::RecomposeBuiltSlots()
{
    if (PreviewSession == nullptr)
    {
        return;
    }
    PreviewSession->ForEachActiveBuiltMID(
        [this](const int32 MaterialSlotIndex, class UMaterialInstanceDynamic&)
        {
            RecomposeSlot(MaterialSlotIndex);
        });
}

FDWCEditorPreviewLayerStack FDWCEditorPreviewOrchestrator::BuildStack(
    const int32 MaterialSlotIndex) const
{
    FDWCEditorPreviewLayerStack Stack;
    Stack.MaterialSlotIndex = MaterialSlotIndex;
    Stack.AddOrReplace(BuildSavedCrossLayer(MaterialSlotIndex));

    if (const TArray<FDWCEditorPreviewLayer>* LiveLayers = LiveLayersBySlot.Find(MaterialSlotIndex))
    {
        for (const FDWCEditorPreviewLayer& Layer : *LiveLayers)
        {
            Stack.AddOrReplace(Layer);
        }
    }
    return Stack;
}

FDWCEditorPreviewLayer FDWCEditorPreviewOrchestrator::BuildSavedCrossLayer(
    const int32 MaterialSlotIndex) const
{
    FDWCEditorPreviewLayer Layer;
    Layer.MaterialSlotIndex = MaterialSlotIndex;
    const FDWCEditorPreviewSavedLayers Saved =
        FDWCEditorPreviewLayerResolver::Resolve(WetClothingAsset.Get(), MaterialSlotIndex);

    if (ActiveDomain == EDWCEditorAuthoringDomain::Wrinkle)
    {
        Layer.Kind = EDWCEditorPreviewLayerKind::SavedTransparency;
        UTexture2D* Texture = bShowSavedCrossLayer ? Saved.TransparencyMap.Get() : nullptr;
        Layer.AddTexture(DWCWetMaterialParameters::TransparencyMap(), Texture);
        Layer.AddScalar(DWCWetMaterialParameters::UseTransparencyMap(), Texture != nullptr ? 1.0f : 0.0f);
    }
    else
    {
        Layer.Kind = EDWCEditorPreviewLayerKind::SavedWrinkle;
        UTexture2D* Texture = bShowSavedCrossLayer ? Saved.WrinkleNormal.Get() : nullptr;
        Layer.AddTexture(DWCWetMaterialParameters::WrinkleNormalMap(), Texture);
        Layer.AddScalar(DWCWetMaterialParameters::UseWrinkleNormalMap(), Texture != nullptr ? 1.0f : 0.0f);
    }
    return Layer;
}

void FDWCEditorPreviewOrchestrator::HandleSessionStateChanged(
    const FDWCEditorSessionState& State,
    const EDWCEditorSessionEffect Effects,
    const uint64 SessionRevision)
{
    (void)SessionRevision;
    bool bSavedCrossLayerChanged = false;
    if (ActiveDomain == EDWCEditorAuthoringDomain::Wrinkle)
    {
        bSavedCrossLayerChanged = bShowSavedCrossLayer != State.Wrinkle.bShowBakedTransparency;
        bShowSavedCrossLayer = State.Wrinkle.bShowBakedTransparency;
        SetPreviewWetness(State.Wrinkle.Brush.PreviewWetness);
    }
    else if (ActiveDomain == EDWCEditorAuthoringDomain::Transparency)
    {
        bSavedCrossLayerChanged = bShowSavedCrossLayer != State.Transparency.bShowSavedWrinkle;
        bShowSavedCrossLayer = State.Transparency.bShowSavedWrinkle;
        SetPreviewWetness(State.Transparency.WetnessPreviewPercent / 100.0f);
    }

    if (EnumHasAnyFlags(
            Effects,
            EDWCEditorSessionEffect::RebuildPreviewContent |
                EDWCEditorSessionEffect::RebuildPreviewMaterials))
    {
        RecomposeBuiltSlots();
    }
    else if (bSavedCrossLayerChanged)
    {
        // Slider-only updates, including Preview Wetness, are already applied
        // directly to each MID. Recompose only when the saved cross-layer
        // texture binding itself changed.
        RecomposeBuiltSlots();
    }
}
