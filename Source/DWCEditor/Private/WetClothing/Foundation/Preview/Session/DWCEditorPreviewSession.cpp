//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Preview/Session/DWCEditorPreviewSession.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialParameters.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetRendering/DWCGPUResourceSubsystem.h"

namespace
{
    template <typename BindingType>
    const BindingType* FindBindingByName(
        const TArray<BindingType>& Bindings,
        const FName ParameterName)
    {
        return Bindings.FindByPredicate(
            [ParameterName](const BindingType& Binding)
            {
                return Binding.ParameterName == ParameterName;
            });
    }

    bool AreParameterSetsEqual(
        const FDWCEditorPreviewParameterSet& A,
        const FDWCEditorPreviewParameterSet& B)
    {
        if (A.Scalars.Num() != B.Scalars.Num() ||
            A.Vectors.Num() != B.Vectors.Num() ||
            A.Textures.Num() != B.Textures.Num())
        {
            return false;
        }
        for (const FDWCEditorPreviewScalarBinding& Binding : A.Scalars)
        {
            const FDWCEditorPreviewScalarBinding* Other =
                FindBindingByName(B.Scalars, Binding.ParameterName);
            if (Other == nullptr || !FMath::IsNearlyEqual(Binding.Value, Other->Value) ||
                !FMath::IsNearlyEqual(Binding.ResetValue, Other->ResetValue))
            {
                return false;
            }
        }
        for (const FDWCEditorPreviewVectorBinding& Binding : A.Vectors)
        {
            const FDWCEditorPreviewVectorBinding* Other =
                FindBindingByName(B.Vectors, Binding.ParameterName);
            if (Other == nullptr || Binding.Value != Other->Value ||
                Binding.ResetValue != Other->ResetValue)
            {
                return false;
            }
        }
        for (const FDWCEditorPreviewTextureBinding& Binding : A.Textures)
        {
            const FDWCEditorPreviewTextureBinding* Other =
                FindBindingByName(B.Textures, Binding.ParameterName);
            if (Other == nullptr || Binding.Value != Other->Value)
            {
                return false;
            }
        }
        return true;
    }
}

FDWCEditorPreviewSession::~FDWCEditorPreviewSession()
{
    Shutdown();
}

void FDWCEditorPreviewSession::Initialize(
    UWetClothingAsset* WetClothingAssetIn,
    UWorld* PreviewWorldIn,
    const FDWCEditorPreviewSessionConfig& Config)
{
    Shutdown();

    WetClothingAsset = WetClothingAssetIn;
    PreviewWorld = PreviewWorldIn;
    SessionConfig = Config;
    if (SessionConfig.ResetDiagnosticCounters)
    {
        SessionConfig.ResetDiagnosticCounters();
    }
    PreviewWetness = FMath::Clamp(Config.InitialPreviewWetness, 0.0f, 1.0f);
    SelectedMaterialSlotIndex = AllWettableSlots;
    PreviewMaterialScope = EDWCEditorPreviewMaterialScope::AllWettableSlots;
    bInitialized = WetClothingAssetIn != nullptr;
    bSuspended = false;

    if (!bInitialized)
    {
        return;
    }

    FDWCEditorPreviewDiagnostics::RegisterSession(this);

    CachedDataUVChannelIndex = INDEX_NONE;
    RefreshSlotStates();
    SetPreviewMaterialScope(EDWCEditorPreviewMaterialScope::AllWettableSlots);

    if (SessionConfig.bObserveRelevantObjectChanges)
    {
        ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
            this,
            &FDWCEditorPreviewSession::HandleObjectPropertyChanged);
    }
}

void FDWCEditorPreviewSession::Shutdown()
{
    FDWCEditorPreviewDiagnostics::UnregisterSession(this);

    if (ObjectPropertyChangedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
        ObjectPropertyChangedHandle.Reset();
    }

    MaterialCache.Reset();
    MaterialCache.ResetDiagnosticCounters();
    ResourceBindingMIDs.Reset();
    RuntimeSlots.Reset();
    ActivePreviewMaterialSlots.Reset();
    SlotCollection = FDWCEditorPreviewSlotCollection();
    SourceParameterRevisions.Reset();
    WetClothingAsset.Reset();
    PreviewWorld.Reset();
    SessionConfig = FDWCEditorPreviewSessionConfig();
    SelectedMaterialSlotIndex = AllWettableSlots;
    PreviewMaterialScope = EDWCEditorPreviewMaterialScope::AllWettableSlots;
    CachedDataUVChannelIndex = INDEX_NONE;
    PreviewWetness = 1.0f;
    bInitialized = false;
    bSuspended = false;
    bRenderResourcesDirty = false;
    SlotRefreshCount = 0;
    SlotStateChangeCount = 0;
    MaterialRequestCount = 0;
    ExistingMIDReuseCount = 0;
    IdleMIDTrimCount = 0;
    IdleMIDTrimmedEntryCount = 0;
    MaterialUseSerial = 0;
    LayerStackUpdateCount = 0;
    LayerStackNoChangeCount = 0;
    LayerParameterWriteCount = 0;
    SlotsChangedDelegate.Clear();
    MaterialReadyDelegate.Clear();
}

bool FDWCEditorPreviewSession::IsInitialized() const
{
    return bInitialized && WetClothingAsset.IsValid();
}

void FDWCEditorPreviewSession::Suspend(const EDWCEditorPreviewSuspendReason Reason)
{
    (void)Reason;
    if (!IsInitialized() || bSuspended)
    {
        return;
    }

    // Preview graph/MID objects are editor-session resources. Keeping the
    // slot collection lets the mode resume lazily without rebuilding topology.
    ClearBuiltMaterials();
    MaterialCache.Reset();
    FlushRenderResourceBindings();
    bSuspended = true;
}

void FDWCEditorPreviewSession::Resume()
{
    if (!IsInitialized() || !bSuspended)
    {
        return;
    }

    bSuspended = false;
}

bool FDWCEditorPreviewSession::RefreshSlotStates()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorPreviewSession_RefreshSlotStates);
    ++SlotRefreshCount;

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (!bInitialized || Asset == nullptr)
    {
        return false;
    }

    const int32 NewDataUVChannelIndex = Asset->GetDWCDataUVChannelIndex();
    const bool bDataUVChanged = CachedDataUVChannelIndex != NewDataUVChannelIndex;
    FDWCEditorPreviewSlotCollection NewCollection = FDWCEditorPreviewSlotResolver::Resolve(Asset);
    if (!bDataUVChanged && NewCollection.StateSignature == SlotCollection.StateSignature)
    {
        return false;
    }

    TArray<FDWCEditorPreviewSessionSlot> NewRuntimeSlots;
    NewRuntimeSlots.Reserve(NewCollection.Slots.Num());

    // Data UV is part of every generated preview graph, so it invalidates all
    // cached parents. A source-material change is slot-local; invalidating the
    // whole cache there recompiles unrelated slots during routine refreshes.
    if (bDataUVChanged)
    {
        MaterialCache.Reset();
    }

    bool bInvalidatedSlotCacheEntry = false;
    for (const FDWCEditorPreviewSlotState& NewEligibility : NewCollection.Slots)
    {
        FDWCEditorPreviewSessionSlot& NewRuntime = NewRuntimeSlots.AddDefaulted_GetRef();
        NewRuntime.Eligibility = NewEligibility;

        const FDWCEditorPreviewSessionSlot* PreviousRuntime = FindSlot(NewEligibility.MaterialSlotIndex);
        const bool bCanKeepMaterial = !bDataUVChanged &&
            PreviousRuntime != nullptr &&
            PreviousRuntime->Eligibility.SourceMaterial == NewEligibility.SourceMaterial &&
            PreviousRuntime->Eligibility.bPreviewReady == NewEligibility.bPreviewReady;
        if (bCanKeepMaterial)
        {
            NewRuntime.PreviewMID = PreviousRuntime->PreviewMID;
            NewRuntime.LastMaterialUseSerial = PreviousRuntime->LastMaterialUseSerial;
            NewRuntime.bActiveInPreviewScope = PreviousRuntime->bActiveInPreviewScope;
            NewRuntime.bMaterialBuildPending = PreviousRuntime->bMaterialBuildPending;
            NewRuntime.bMaterialBuildFailed = PreviousRuntime->bMaterialBuildFailed;
            NewRuntime.MaterialBuildError = PreviousRuntime->MaterialBuildError;
            NewRuntime.DesiredLayerParameters = PreviousRuntime->DesiredLayerParameters;
            NewRuntime.AppliedLayerParameters = PreviousRuntime->AppliedLayerParameters;
        }
        else if (PreviousRuntime != nullptr)
        {
            MaterialCache.InvalidateSlot(Asset, NewEligibility.MaterialSlotIndex);
            bInvalidatedSlotCacheEntry = true;
        }
    }

    if (bInvalidatedSlotCacheEntry)
    {
        MaterialCache.PruneUnusedHierarchies();
    }

    SlotCollection = MoveTemp(NewCollection);
    RuntimeSlots = MoveTemp(NewRuntimeSlots);
    CachedDataUVChannelIndex = NewDataUVChannelIndex;
    RebuildActivePreviewMaterialSlots();
    ResourceBindingMIDs.SetNumZeroed(SlotCollection.Slots.Num());
    for (const FDWCEditorPreviewSessionSlot& Slot : RuntimeSlots)
    {
        if (Slot.Eligibility.bPreviewReady && Slot.bActiveInPreviewScope &&
            ResourceBindingMIDs.IsValidIndex(Slot.Eligibility.MaterialSlotIndex))
        {
            ResourceBindingMIDs[Slot.Eligibility.MaterialSlotIndex] = Slot.PreviewMID.Get();
        }
    }

    bRenderResourcesDirty = true;
    ++SlotStateChangeCount;
    SlotsChangedDelegate.Broadcast();
    return true;
}

const FDWCEditorPreviewSlotCollection& FDWCEditorPreviewSession::GetSlotStates() const
{
    return SlotCollection;
}

const FDWCEditorPreviewSessionSlot* FDWCEditorPreviewSession::FindSlot(
    const int32 MaterialSlotIndex) const
{
    if (!RuntimeSlots.IsValidIndex(MaterialSlotIndex))
    {
        return nullptr;
    }

    const FDWCEditorPreviewSessionSlot& Slot = RuntimeSlots[MaterialSlotIndex];
    return Slot.Eligibility.MaterialSlotIndex == MaterialSlotIndex ? &Slot : nullptr;
}

FDWCEditorPreviewSessionSlot* FDWCEditorPreviewSession::FindMutableSlot(
    const int32 MaterialSlotIndex)
{
    if (!RuntimeSlots.IsValidIndex(MaterialSlotIndex))
    {
        return nullptr;
    }

    FDWCEditorPreviewSessionSlot& Slot = RuntimeSlots[MaterialSlotIndex];
    return Slot.Eligibility.MaterialSlotIndex == MaterialSlotIndex ? &Slot : nullptr;
}

bool FDWCEditorPreviewSession::SetSelectedMaterialSlot(const int32 MaterialSlotIndex)
{
    return SetPreviewMaterialScope(
        MaterialSlotIndex == AllWettableSlots
            ? EDWCEditorPreviewMaterialScope::AllWettableSlots
            : EDWCEditorPreviewMaterialScope::SingleSlot,
        MaterialSlotIndex);
}

bool FDWCEditorPreviewSession::SetPreviewMaterialScope(
    const EDWCEditorPreviewMaterialScope Scope,
    const int32 MaterialSlotIndex)
{
    if (!IsInitialized())
    {
        return false;
    }
    if (Scope == EDWCEditorPreviewMaterialScope::SingleSlot && !SlotCollection.IsReady(MaterialSlotIndex))
    {
        return false;
    }

    const TArray<int32, TInlineAllocator<16>> PreviousActiveSlots = ActivePreviewMaterialSlots;
    PreviewMaterialScope = Scope;
    SelectedMaterialSlotIndex = Scope == EDWCEditorPreviewMaterialScope::SingleSlot
        ? MaterialSlotIndex
        : AllWettableSlots;
    RebuildActivePreviewMaterialSlots();

    if (PreviousActiveSlots == ActivePreviewMaterialSlots)
    {
        return false;
    }

    for (const int32 PreviousSlotIndex : PreviousActiveSlots)
    {
        if (!ActivePreviewMaterialSlots.Contains(PreviousSlotIndex) &&
            ResourceBindingMIDs.IsValidIndex(PreviousSlotIndex))
        {
            ResourceBindingMIDs[PreviousSlotIndex] = nullptr;
        }
    }
    bRenderResourcesDirty = true;
    TrimIdlePreviewMaterials();
    return true;
}

void FDWCEditorPreviewSession::SetPreviewWetness(const float PreviewWetnessIn)
{
    const float ClampedWetness = FMath::Clamp(PreviewWetnessIn, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(PreviewWetness, ClampedWetness))
    {
        return;
    }

    PreviewWetness = ClampedWetness;
    ForEachActiveBuiltMID(
        [this](const int32, UMaterialInstanceDynamic& MID)
        {
            ApplyCommonParameters(MID);
        });
}

bool FDWCEditorPreviewSession::SetLayerStack(
    const int32 MaterialSlotIndex,
    const FDWCEditorPreviewLayerStack& LayerStack)
{
    FDWCEditorPreviewSessionSlot* Slot = FindMutableSlot(MaterialSlotIndex);
    if (Slot == nullptr || !Slot->Eligibility.bPreviewReady)
    {
        return false;
    }

    FDWCEditorPreviewParameterSet NewParameters;
    LayerStack.BuildParameterSet(NewParameters);
    if (AreParameterSetsEqual(Slot->DesiredLayerParameters, NewParameters))
    {
        ++LayerStackNoChangeCount;
        return false;
    }

    ++LayerStackUpdateCount;
    Slot->DesiredLayerParameters = MoveTemp(NewParameters);
    if (UMaterialInstanceDynamic* MID = Slot->PreviewMID.Get())
    {
        ApplyLayerParameterDiff(*Slot, *MID);
    }
    return true;
}

void FDWCEditorPreviewSession::PreparePreviewMaterials(
    const TConstArrayView<int32> MaterialSlotIndices)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorPreviewSession_PreparePreviewMaterials);

    if (bSuspended)
    {
        return;
    }

    for (const int32 MaterialSlotIndex : MaterialSlotIndices)
    {
        if (IsPreviewMaterialSlotActive(MaterialSlotIndex))
        {
            GetOrCreatePreviewMaterialInternal(MaterialSlotIndex, false);
        }
    }
    FlushRenderResourceBindings();
}

void FDWCEditorPreviewSession::TickPendingMaterialCompilations()
{
    if (bSuspended || !IsInitialized())
    {
        return;
    }

    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorPreviewSession_TickPendingMaterialCompilations);
    TArray<int32, TInlineAllocator<16>> PendingSlotIndices;
    for (const FDWCEditorPreviewSessionSlot& Slot : RuntimeSlots)
    {
        if (Slot.bActiveInPreviewScope && Slot.bMaterialBuildPending)
        {
            PendingSlotIndices.Add(Slot.Eligibility.MaterialSlotIndex);
        }
    }
    for (const int32 MaterialSlotIndex : PendingSlotIndices)
    {
        GetOrCreatePreviewMaterialInternal(MaterialSlotIndex, false);
    }

    if (bRenderResourcesDirty)
    {
        FlushRenderResourceBindings();
    }
}

FDWCEditorPreviewSessionMaterialResult FDWCEditorPreviewSession::GetOrCreatePreviewMaterial(
    const int32 MaterialSlotIndex)
{
    return GetOrCreatePreviewMaterialInternal(MaterialSlotIndex, true);
}

FDWCEditorPreviewSessionMaterialResult FDWCEditorPreviewSession::GetOrCreatePreviewMaterialInternal(
    const int32 MaterialSlotIndex,
    const bool bFlushRenderResourceBindings)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorPreviewSession_GetOrCreatePreviewMaterial);
    ++MaterialRequestCount;

    FDWCEditorPreviewSessionMaterialResult SessionResult;
    if (bSuspended)
    {
        SessionResult.Message = TEXT("The editor preview session is suspended.");
        return SessionResult;
    }
    FDWCEditorPreviewSessionSlot* Slot = FindMutableSlot(MaterialSlotIndex);
    if (Slot == nullptr)
    {
        SessionResult.Message = TEXT("The requested material slot does not exist in this preview session.");
        return SessionResult;
    }

    SessionResult.FallbackMaterial = Slot->Eligibility.SourceMaterial.Get();
    if (!Slot->Eligibility.bPreviewReady)
    {
        SessionResult.Message = FDWCEditorPreviewSlotResolver::GetIssueText(
            Slot->Eligibility.Issue).ToString();
        return SessionResult;
    }

    if (Slot->bMaterialBuildFailed)
    {
        SessionResult.Message = Slot->MaterialBuildError;
        return SessionResult;
    }

    if (UMaterialInstanceDynamic* ExistingMID = Slot->PreviewMID.Get())
    {
        ++ExistingMIDReuseCount;
        Slot->LastMaterialUseSerial = ++MaterialUseSerial;
        ApplyCommonParameters(*ExistingMID);
        ApplyLayerParameterDiff(*Slot, *ExistingMID);
        if (Slot->bActiveInPreviewScope && ResourceBindingMIDs.IsValidIndex(MaterialSlotIndex))
        {
            ResourceBindingMIDs[MaterialSlotIndex] = ExistingMID;
            bRenderResourcesDirty = true;
            if (bFlushRenderResourceBindings)
            {
                FlushRenderResourceBindings();
            }
        }
        SessionResult.State = EDWCEditorPreviewMaterialState::Ready;
        SessionResult.bSucceeded = true;
        SessionResult.PreviewMID = ExistingMID;
        return SessionResult;
    }

    UMaterialInterface* SourceMaterial = Slot->Eligibility.SourceMaterial.Get();
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (SourceMaterial == nullptr || Asset == nullptr)
    {
        SessionResult.Message = TEXT("The preview source material or Wet Clothing Asset is no longer available.");
        return SessionResult;
    }

    FDWCEditorPreviewMaterialRequest Request;
    Request.SourceMaterial = SourceMaterial;
    Request.SlotOwner = Asset;
    Request.MaterialSlotIndex = MaterialSlotIndex;
    Request.DWCDataUVChannelIndex = Asset->GetDWCDataUVChannelIndex();
    Request.SurfaceWaterNormalUVChannelIndex = SessionConfig.SurfaceWaterNormalUVChannelIndex;
    Request.FeatureMask = SessionConfig.FeatureMask;
    Request.FeatureSchemaVersion = SessionConfig.FeatureSchemaVersion;
    Request.SourceParameterRevision = GetSourceParameterRevision(SourceMaterial);
    Request.ExtendGraph = SessionConfig.ExtendGraph;

    const FDWCEditorPreviewMaterialResult MaterialResult = MaterialCache.GetOrCreate(Request);
    SessionResult.State = MaterialResult.State;
    SessionResult.bPending = MaterialResult.bPending;
    if (MaterialResult.State == EDWCEditorPreviewMaterialState::Compiling)
    {
        Slot->bMaterialBuildPending = true;
        Slot->bMaterialBuildFailed = false;
        Slot->MaterialBuildError.Reset();
        SessionResult.Message = MaterialResult.Message;
        return SessionResult;
    }
    if (!MaterialResult.bSucceeded || MaterialResult.PreviewMID == nullptr)
    {
        Slot->bMaterialBuildPending = false;
        Slot->bMaterialBuildFailed = true;
        Slot->MaterialBuildError = MaterialResult.Message;
        UE_LOG(
            LogDWCEditorPreview,
            Warning,
            TEXT("Editor preview material failed for slot %d (%s): %s"),
            MaterialSlotIndex,
            *GetNameSafe(SourceMaterial),
            *MaterialResult.Message);
        SessionResult.Message = MaterialResult.Message;
        return SessionResult;
    }

    Slot->PreviewMID = MaterialResult.PreviewMID;
    Slot->LastMaterialUseSerial = ++MaterialUseSerial;
    Slot->bMaterialBuildPending = false;
    Slot->bMaterialBuildFailed = false;
    Slot->MaterialBuildError.Reset();
    ApplyCommonParameters(*MaterialResult.PreviewMID);
    if (SessionConfig.InitializeMID)
    {
        SessionConfig.InitializeMID(MaterialSlotIndex, *MaterialResult.PreviewMID);
    }
    Slot->AppliedLayerParameters = FDWCEditorPreviewParameterSet();
    ApplyLayerParameterDiff(*Slot, *MaterialResult.PreviewMID);

    if (Slot->bActiveInPreviewScope && ResourceBindingMIDs.IsValidIndex(MaterialSlotIndex))
    {
        ResourceBindingMIDs[MaterialSlotIndex] = MaterialResult.PreviewMID;
        bRenderResourcesDirty = true;
        if (bFlushRenderResourceBindings)
        {
            FlushRenderResourceBindings();
        }
    }

    SessionResult.bSucceeded = true;
    SessionResult.bCreated = !MaterialResult.bMIDCacheHit;
    SessionResult.PreviewMID = MaterialResult.PreviewMID;
    SessionResult.Message = MaterialResult.Message;
    MaterialReadyDelegate.Broadcast(MaterialSlotIndex, MaterialResult.PreviewMID);
    return SessionResult;
}

void FDWCEditorPreviewSession::ForEachActiveBuiltMID(
    TFunctionRef<void(int32, UMaterialInstanceDynamic&)> Visitor) const
{
    for (const FDWCEditorPreviewSessionSlot& Slot : RuntimeSlots)
    {
        if (Slot.bActiveInPreviewScope)
        {
            if (UMaterialInstanceDynamic* MID = Slot.PreviewMID.Get())
            {
                Visitor(Slot.Eligibility.MaterialSlotIndex, *MID);
            }
        }
    }
}

void FDWCEditorPreviewSession::NotifySourceMaterialChanged(UMaterialInterface* SourceMaterial)
{
    if (SourceMaterial == nullptr)
    {
        return;
    }
    ++SourceMaterialInvalidationCount;

    UMaterial* ChangedBaseMaterial = SourceMaterial->GetMaterial();
    const bool bBaseMaterialChanged = SourceMaterial == ChangedBaseMaterial;
    MaterialCache.InvalidateSource(SourceMaterial, bBaseMaterialChanged);

    TSet<FObjectKey> RevisedSources;
    bool bInvalidatedAnySlot = false;
    for (FDWCEditorPreviewSessionSlot& Slot : RuntimeSlots)
    {
        UMaterialInterface* SlotSourceMaterial = Slot.Eligibility.SourceMaterial.Get();
        const bool bAffected = SlotSourceMaterial == SourceMaterial ||
            (bBaseMaterialChanged && SlotSourceMaterial != nullptr &&
             SlotSourceMaterial->GetMaterial() == ChangedBaseMaterial);
        if (bAffected)
        {
            const FObjectKey SourceKey(SlotSourceMaterial);
            if (!RevisedSources.Contains(SourceKey))
            {
                ++SourceParameterRevisions.FindOrAdd(SourceKey);
                RevisedSources.Add(SourceKey);
            }
            Slot.PreviewMID.Reset();
            Slot.bMaterialBuildPending = false;
            Slot.bMaterialBuildFailed = false;
            Slot.MaterialBuildError.Reset();
            if (ResourceBindingMIDs.IsValidIndex(Slot.Eligibility.MaterialSlotIndex))
            {
                ResourceBindingMIDs[Slot.Eligibility.MaterialSlotIndex] = nullptr;
            }
            bInvalidatedAnySlot = true;
        }
    }

    if (bInvalidatedAnySlot)
    {
        MaterialCache.PruneUnusedHierarchies();
        bRenderResourcesDirty = true;
        SlotsChangedDelegate.Broadcast();
    }
}

void FDWCEditorPreviewSession::NotifyWCADataChanged()
{
    ++WCAInvalidationCount;
    RefreshSlotStates();
}

void FDWCEditorPreviewSession::InvalidateMaterialGraphs()
{
    ++GraphInvalidationCount;
    MaterialCache.Reset();
    ClearBuiltMaterials();
    SlotsChangedDelegate.Broadcast();
}

void FDWCEditorPreviewSession::FlushRenderResourceBindings()
{
    if (!bRenderResourcesDirty)
    {
        return;
    }
    ++RenderBindingFlushCount;

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    UWorld* World = PreviewWorld.Get();
    if (Asset == nullptr || World == nullptr)
    {
        return;
    }

    UDWCGPUResourceSubsystem* ResourceSubsystem = World->GetSubsystem<UDWCGPUResourceSubsystem>();
    if (ResourceSubsystem == nullptr)
    {
        return;
    }

    ResourceSubsystem->ApplyResourcesToMaterials(
        Asset,
        ResourceBindingMIDs,
        EDWCRenderResourceUsage::AbsorbedOnly);
    bRenderResourcesDirty = false;
}

void FDWCEditorPreviewSession::DumpDiagnostics(const int32 SessionIndex) const
{
    int32 BuiltMIDCount = 0;
    int32 PendingMaterialCount = 0;
    int32 FailedMaterialCount = 0;
    const uint64 SessionContainerBytes = CalculateSessionContainerBytes();
    for (const FDWCEditorPreviewSessionSlot& Slot : RuntimeSlots)
    {
        BuiltMIDCount += Slot.PreviewMID.IsValid() ? 1 : 0;
        PendingMaterialCount += Slot.bMaterialBuildPending ? 1 : 0;
        FailedMaterialCount += Slot.bMaterialBuildFailed ? 1 : 0;
    }

    const FDWCEditorPreviewMaterialCacheStats CacheStats = MaterialCache.GetStats();
    const FString Label = SessionConfig.DiagnosticLabel.IsEmpty()
        ? TEXT("Unnamed")
        : SessionConfig.DiagnosticLabel;
    const FString SelectedSlot = SelectedMaterialSlotIndex == AllWettableSlots
        ? TEXT("All Wettable Slots")
        : FString::FromInt(SelectedMaterialSlotIndex);

    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("[%d] %s: WCA='%s', selected=%s, wetness=%.3f, ready=%d, active=%d, builtMIDs=%d, pending=%d, failed=%d."),
        SessionIndex,
        *Label,
        *GetNameSafe(WetClothingAsset.Get()),
        *SelectedSlot,
        PreviewWetness,
        SlotCollection.ReadyWettableSlotIndices.Num(),
        ActivePreviewMaterialSlots.Num(),
        BuiltMIDCount,
        PendingMaterialCount,
        FailedMaterialCount);
    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("    Session: memory=%s, refresh=%llu, stateChanges=%llu, materialRequests=%llu, existingMIDReuse=%llu."),
        *FDWCEditorPreviewDiagnostics::FormatBytes(SessionContainerBytes),
        SlotRefreshCount,
        SlotStateChangeCount,
        MaterialRequestCount,
        ExistingMIDReuseCount);
    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("    Invalidations: objectEvents=%llu (%llu relevant), WCA=%llu, sourceMaterial=%llu, graph=%llu, resourceFlush=%llu."),
        ObjectPropertyChangeCount,
        RelevantObjectPropertyChangeCount,
        WCAInvalidationCount,
        SourceMaterialInvalidationCount,
        GraphInvalidationCount,
        RenderBindingFlushCount);
    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("    Material cache: graphs=%d (%d pending, %d failed), parents=%d (%d failed), MIDs=%d, memory=%s."),
        CacheStats.GraphEntryCount,
        CacheStats.PendingGraphEntryCount,
        CacheStats.FailedGraphEntryCount,
        CacheStats.ParentEntryCount,
        CacheStats.FailedParentEntryCount,
        CacheStats.SlotMIDEntryCount,
        *FDWCEditorPreviewDiagnostics::FormatBytes(CacheStats.EstimatedContainerBytes));
    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("    Material compile: requested=%llu, completed=%llu, failed=%llu."),
        CacheStats.GraphCompileRequestCount,
        CacheStats.GraphCompileCompleteCount,
        CacheStats.GraphCompileFailureCount);
    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("    Cache events: graph=%llu/%llu, parent=%llu/%llu, MID=%llu/%llu (hit/miss), invalidation=%llu source + %llu slot, trimmed=%llu MID/%llu parent/%llu graph, reset=%llu."),
        CacheStats.GraphHitCount,
        CacheStats.GraphMissCount,
        CacheStats.ParentHitCount,
        CacheStats.ParentMissCount,
        CacheStats.MIDHitCount,
        CacheStats.MIDMissCount,
        CacheStats.SourceInvalidationCount,
        CacheStats.SlotInvalidationCount,
        CacheStats.SlotMIDPruneCount,
        CacheStats.ParentPruneCount,
        CacheStats.GraphPruneCount,
        CacheStats.ResetCount);
    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("    Build time: graph total/max=%.2f/%.2f ms, parent=%.2f/%.2f ms, MID=%.2f/%.2f ms."),
        CacheStats.TotalGraphBuildMilliseconds,
        CacheStats.MaxGraphBuildMilliseconds,
        CacheStats.TotalParentBuildMilliseconds,
        CacheStats.MaxParentBuildMilliseconds,
        CacheStats.TotalMIDBuildMilliseconds,
        CacheStats.MaxMIDBuildMilliseconds);
    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("    Layers: updates=%llu, noChange=%llu, MID parameter writes=%llu, idleTrim=%llu/%llu."),
        LayerStackUpdateCount,
        LayerStackNoChangeCount,
        LayerParameterWriteCount,
        IdleMIDTrimCount,
        IdleMIDTrimmedEntryCount);

    if (SessionConfig.CollectMemoryStats)
    {
        TArray<FDWCEditorPreviewMemoryBucket> Buckets;
        SessionConfig.CollectMemoryStats(Buckets);
        for (const FDWCEditorPreviewMemoryBucket& Bucket : Buckets)
        {
            const FString BudgetText = Bucket.BudgetBytes > 0
                ? FString::Printf(
                    TEXT("/%s"),
                    *FDWCEditorPreviewDiagnostics::FormatBytes(Bucket.BudgetBytes))
                : FString();
            UE_LOG(
                LogDWCEditorPreview,
                Display,
                TEXT("    %s: memory=%s%s, entries=%d, leases=%d, retired=%d, hit/miss/evict=%llu/%llu/%llu."),
                *Bucket.Name,
                *FDWCEditorPreviewDiagnostics::FormatBytes(Bucket.UsedBytes),
                *BudgetText,
                Bucket.EntryCount,
                Bucket.ActiveLeaseCount,
                Bucket.RetiredEntryCount,
                Bucket.HitCount,
                Bucket.MissCount,
                Bucket.EvictionCount);
        }
    }

    if (SessionConfig.CollectOperationStats)
    {
        TArray<FDWCEditorPreviewOperationCounter> Counters;
        SessionConfig.CollectOperationStats(Counters);
        for (const FDWCEditorPreviewOperationCounter& Counter : Counters)
        {
            const FString BytesText = Counter.Bytes > 0
                ? FString::Printf(
                    TEXT(", bytes=%s"),
                    *FDWCEditorPreviewDiagnostics::FormatBytes(Counter.Bytes))
                : FString();
            UE_LOG(
                LogDWCEditorPreview,
                Display,
                TEXT("    %s: count=%llu%s."),
                *Counter.Name,
                Counter.Count,
                *BytesText);
        }
    }
}

uint64 FDWCEditorPreviewSession::CalculateSessionContainerBytes() const
{
    uint64 Bytes =
        static_cast<uint64>(SlotCollection.Slots.GetAllocatedSize()) +
        static_cast<uint64>(SlotCollection.ReadyWettableSlotIndices.GetAllocatedSize()) +
        static_cast<uint64>(RuntimeSlots.GetAllocatedSize()) +
        static_cast<uint64>(ResourceBindingMIDs.GetAllocatedSize()) +
        static_cast<uint64>(SourceParameterRevisions.GetAllocatedSize());
    for (const FDWCEditorPreviewSessionSlot& Slot : RuntimeSlots)
    {
        Bytes += static_cast<uint64>(Slot.MaterialBuildError.GetAllocatedSize());
        Bytes += Slot.DesiredLayerParameters.GetAllocatedSize();
        Bytes += Slot.AppliedLayerParameters.GetAllocatedSize();
    }
    return Bytes;
}

void FDWCEditorPreviewSession::AppendGlobalMemoryOwners(
    const int32 SessionIndex,
    TSet<FString>& SeenOwnerIdentifiers,
    TArray<FDWCEditorMemoryOwnerRecord>& OutOwners) const
{
    const FString Label = SessionConfig.DiagnosticLabel.IsEmpty()
        ? TEXT("Unnamed")
        : SessionConfig.DiagnosticLabel;
    const FString SessionIdentifier = FString::Printf(
        TEXT("PreviewSession/%p"),
        static_cast<const void*>(this));
    const FString Context = FString::Printf(
        TEXT("session=%d label='%s' WCA='%s'"),
        SessionIndex,
        *Label,
        *GetNameSafe(WetClothingAsset.Get()));

    auto AddOwner = [&OutOwners, &SeenOwnerIdentifiers](FDWCEditorMemoryOwnerRecord&& Owner)
    {
        if (!SeenOwnerIdentifiers.Contains(Owner.Identifier))
        {
            SeenOwnerIdentifiers.Add(Owner.Identifier);
            OutOwners.Add(MoveTemp(Owner));
        }
    };

    FDWCEditorMemoryOwnerRecord SessionOwner;
    SessionOwner.Identifier = SessionIdentifier + TEXT("/Container");
    SessionOwner.Subsystem = TEXT("PreviewSession");
    SessionOwner.Resource = TEXT("SessionContainer");
    SessionOwner.Category = EDWCEditorMemoryCategory::PersistentEditorCPU;
    SessionOwner.CurrentBytes = CalculateSessionContainerBytes();
    SessionOwner.EntryCount = RuntimeSlots.Num();
    SessionOwner.Context = Context;
    AddOwner(MoveTemp(SessionOwner));

    const FDWCEditorPreviewMaterialCacheStats CacheStats = MaterialCache.GetStats();
    FDWCEditorMemoryOwnerRecord MaterialOwner;
    MaterialOwner.Identifier = SessionIdentifier + TEXT("/MaterialCache");
    MaterialOwner.Subsystem = TEXT("PreviewSession");
    MaterialOwner.Resource = TEXT("MaterialCacheContainer");
    MaterialOwner.Category = EDWCEditorMemoryCategory::SharedCacheCPU;
    MaterialOwner.CurrentBytes = CacheStats.EstimatedContainerBytes;
    MaterialOwner.EntryCount =
        CacheStats.GraphEntryCount + CacheStats.ParentEntryCount + CacheStats.SlotMIDEntryCount;
    MaterialOwner.Context = Context;
    AddOwner(MoveTemp(MaterialOwner));

    if (!SessionConfig.CollectMemoryStats)
    {
        return;
    }

    TArray<FDWCEditorPreviewMemoryBucket> Buckets;
    SessionConfig.CollectMemoryStats(Buckets);
    for (const FDWCEditorPreviewMemoryBucket& Bucket : Buckets)
    {
        if (!Bucket.bIncludeInGlobalSnapshot)
        {
            continue;
        }

        FDWCEditorMemoryOwnerRecord Owner;
        Owner.Identifier = Bucket.GlobalOwnerIdentifier.IsEmpty()
            ? SessionIdentifier + TEXT("/") + Bucket.Name
            : Bucket.GlobalOwnerIdentifier;
        Owner.Subsystem = FName(*Label);
        Owner.Resource = FName(*Bucket.Name);
        Owner.Category = Bucket.GlobalCategory;
        Owner.CurrentBytes = Bucket.UsedBytes;
        Owner.EntryCount = Bucket.EntryCount;
        Owner.Context = FString::Printf(
            TEXT("%s leases=%d retired=%d budget=%llu"),
            *Context,
            Bucket.ActiveLeaseCount,
            Bucket.RetiredEntryCount,
            Bucket.BudgetBytes);
        AddOwner(MoveTemp(Owner));
    }
}

void FDWCEditorPreviewSession::ResetDiagnosticCounters()
{
    MaterialCache.ResetDiagnosticCounters();
    SlotRefreshCount = 0;
    SlotStateChangeCount = 0;
    MaterialRequestCount = 0;
    ExistingMIDReuseCount = 0;
    IdleMIDTrimCount = 0;
    IdleMIDTrimmedEntryCount = 0;
    MaterialUseSerial = 0;
    ObjectPropertyChangeCount = 0;
    RelevantObjectPropertyChangeCount = 0;
    WCAInvalidationCount = 0;
    SourceMaterialInvalidationCount = 0;
    GraphInvalidationCount = 0;
    RenderBindingFlushCount = 0;
    LayerStackUpdateCount = 0;
    LayerStackNoChangeCount = 0;
    LayerParameterWriteCount = 0;
    if (SessionConfig.ResetDiagnosticCounters)
    {
        SessionConfig.ResetDiagnosticCounters();
    }
}

FDWCEditorPreviewSessionSlotsChanged& FDWCEditorPreviewSession::OnSlotsChanged()
{
    return SlotsChangedDelegate;
}

FDWCEditorPreviewSessionMaterialReady& FDWCEditorPreviewSession::OnMaterialReady()
{
    return MaterialReadyDelegate;
}

uint32 FDWCEditorPreviewSession::GetSourceParameterRevision(
    UMaterialInterface* SourceMaterial) const
{
    return SourceMaterial != nullptr
               ? SourceParameterRevisions.FindRef(FObjectKey(SourceMaterial))
               : 0;
}

void FDWCEditorPreviewSession::ApplyCommonParameters(UMaterialInstanceDynamic& MID) const
{
    MID.SetScalarParameterValue(
        DWCEditorPreviewMaterialParameters::PreviewWetness(),
        PreviewWetness);
}

bool FDWCEditorPreviewSession::ApplyLayerParameterDiff(
    FDWCEditorPreviewSessionSlot& Slot,
    UMaterialInstanceDynamic& MID)
{
    bool bChanged = false;
    const FDWCEditorPreviewParameterSet& Desired = Slot.DesiredLayerParameters;
    const FDWCEditorPreviewParameterSet& Applied = Slot.AppliedLayerParameters;

    for (const FDWCEditorPreviewScalarBinding& Previous : Applied.Scalars)
    {
        if (FindBindingByName(Desired.Scalars, Previous.ParameterName) == nullptr)
        {
            MID.SetScalarParameterValue(Previous.ParameterName, Previous.ResetValue);
            ++LayerParameterWriteCount;
            bChanged = true;
        }
    }
    for (const FDWCEditorPreviewVectorBinding& Previous : Applied.Vectors)
    {
        if (FindBindingByName(Desired.Vectors, Previous.ParameterName) == nullptr)
        {
            MID.SetVectorParameterValue(Previous.ParameterName, Previous.ResetValue);
            ++LayerParameterWriteCount;
            bChanged = true;
        }
    }
    for (const FDWCEditorPreviewTextureBinding& Previous : Applied.Textures)
    {
        if (FindBindingByName(Desired.Textures, Previous.ParameterName) == nullptr)
        {
            MID.SetTextureParameterValue(Previous.ParameterName, nullptr);
            ++LayerParameterWriteCount;
            bChanged = true;
        }
    }

    for (const FDWCEditorPreviewScalarBinding& Binding : Desired.Scalars)
    {
        const FDWCEditorPreviewScalarBinding* Previous =
            FindBindingByName(Applied.Scalars, Binding.ParameterName);
        if (Previous == nullptr || !FMath::IsNearlyEqual(Previous->Value, Binding.Value))
        {
            MID.SetScalarParameterValue(Binding.ParameterName, Binding.Value);
            ++LayerParameterWriteCount;
            bChanged = true;
        }
    }
    for (const FDWCEditorPreviewVectorBinding& Binding : Desired.Vectors)
    {
        const FDWCEditorPreviewVectorBinding* Previous =
            FindBindingByName(Applied.Vectors, Binding.ParameterName);
        if (Previous == nullptr || Previous->Value != Binding.Value)
        {
            MID.SetVectorParameterValue(Binding.ParameterName, Binding.Value);
            ++LayerParameterWriteCount;
            bChanged = true;
        }
    }
    for (const FDWCEditorPreviewTextureBinding& Binding : Desired.Textures)
    {
        const FDWCEditorPreviewTextureBinding* Previous =
            FindBindingByName(Applied.Textures, Binding.ParameterName);
        if (Previous == nullptr || Previous->Value != Binding.Value)
        {
            MID.SetTextureParameterValue(Binding.ParameterName, Binding.Value.Get());
            ++LayerParameterWriteCount;
            bChanged = true;
        }
    }

    Slot.AppliedLayerParameters = Desired;
    return bChanged;
}

void FDWCEditorPreviewSession::ClearBuiltMaterials()
{
    for (FDWCEditorPreviewSessionSlot& Slot : RuntimeSlots)
    {
        Slot.PreviewMID.Reset();
        Slot.bMaterialBuildPending = false;
        Slot.bMaterialBuildFailed = false;
        Slot.MaterialBuildError.Reset();
        Slot.AppliedLayerParameters = FDWCEditorPreviewParameterSet();
    }
    for (TObjectPtr<UMaterialInstanceDynamic>& MID : ResourceBindingMIDs)
    {
        MID = nullptr;
    }
    bRenderResourcesDirty = true;
}

void FDWCEditorPreviewSession::TrimIdlePreviewMaterials(const bool bReleaseAllIdle)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

    TArray<FDWCEditorPreviewSessionSlot*> IdleSlots;
    for (FDWCEditorPreviewSessionSlot& Slot : RuntimeSlots)
    {
        if (!Slot.bActiveInPreviewScope && Slot.PreviewMID.IsValid())
        {
            IdleSlots.Add(&Slot);
        }
    }
    IdleSlots.Sort(
        [](const FDWCEditorPreviewSessionSlot& A, const FDWCEditorPreviewSessionSlot& B)
        {
            return A.LastMaterialUseSerial < B.LastMaterialUseSerial;
        });

    const int32 RemoveCount = bReleaseAllIdle
        ? IdleSlots.Num()
        : FMath::Max(0, IdleSlots.Num() - MaxIdleSlotMIDCount);
    if (RemoveCount == 0)
    {
        MaterialCache.PruneUnusedHierarchies();
        return;
    }

    ++IdleMIDTrimCount;
    for (int32 Index = 0; Index < RemoveCount; ++Index)
    {
        FDWCEditorPreviewSessionSlot* Slot = IdleSlots[Index];
        const int32 MaterialSlotIndex = Slot->Eligibility.MaterialSlotIndex;
        Slot->PreviewMID.Reset();
        Slot->AppliedLayerParameters = FDWCEditorPreviewParameterSet();
        Slot->bMaterialBuildPending = false;
        Slot->bMaterialBuildFailed = false;
        Slot->MaterialBuildError.Reset();
        if (ResourceBindingMIDs.IsValidIndex(MaterialSlotIndex))
        {
            ResourceBindingMIDs[MaterialSlotIndex] = nullptr;
        }
        MaterialCache.InvalidateSlot(Asset, MaterialSlotIndex);
        ++IdleMIDTrimmedEntryCount;
    }
    MaterialCache.PruneUnusedHierarchies();
    bRenderResourcesDirty = true;
}

void FDWCEditorPreviewSession::RebuildActivePreviewMaterialSlots()
{
    ActivePreviewMaterialSlots.Reset();
    switch (PreviewMaterialScope)
    {
    case EDWCEditorPreviewMaterialScope::SingleSlot:
        if (SlotCollection.IsReady(SelectedMaterialSlotIndex))
        {
            ActivePreviewMaterialSlots.Add(SelectedMaterialSlotIndex);
        }
        else
        {
            PreviewMaterialScope = EDWCEditorPreviewMaterialScope::None;
            SelectedMaterialSlotIndex = INDEX_NONE;
        }
        break;
    case EDWCEditorPreviewMaterialScope::AllWettableSlots:
        ActivePreviewMaterialSlots.Append(
            SlotCollection.ReadyWettableSlotIndices.GetData(),
            SlotCollection.ReadyWettableSlotIndices.Num());
        SelectedMaterialSlotIndex = AllWettableSlots;
        break;
    case EDWCEditorPreviewMaterialScope::None:
    default:
        SelectedMaterialSlotIndex = INDEX_NONE;
        break;
    }

    for (FDWCEditorPreviewSessionSlot& Slot : RuntimeSlots)
    {
        Slot.bActiveInPreviewScope = ActivePreviewMaterialSlots.Contains(Slot.Eligibility.MaterialSlotIndex);
        if (Slot.bActiveInPreviewScope)
        {
            Slot.LastMaterialUseSerial = ++MaterialUseSerial;
        }
    }
}

bool FDWCEditorPreviewSession::IsPreviewMaterialSlotActive(const int32 MaterialSlotIndex) const
{
    return ActivePreviewMaterialSlots.Contains(MaterialSlotIndex);
}

void FDWCEditorPreviewSession::HandleObjectPropertyChanged(
    UObject* Object,
    FPropertyChangedEvent& Event)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorPreviewSession_HandleObjectPropertyChanged);
    if (!IsInitialized() || Object == nullptr)
    {
        return;
    }
    ++ObjectPropertyChangeCount;

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Object == Asset || Object == Asset->GetDWCSkeletalMesh())
    {
        ++RelevantObjectPropertyChangeCount;
        NotifyWCADataChanged();
        return;
    }

    if (UMaterialInterface* ChangedMaterial = Cast<UMaterialInterface>(Object))
    {
        const bool bRelevantMaterial = RuntimeSlots.ContainsByPredicate(
            [this, Object](const FDWCEditorPreviewSessionSlot& Slot)
            {
                return IsSourceOrBaseMaterial(Object, Slot.Eligibility.SourceMaterial.Get());
            });
        if (bRelevantMaterial)
        {
            ++RelevantObjectPropertyChangeCount;
            NotifySourceMaterialChanged(ChangedMaterial);
        }
        return;
    }

    if (const UMaterialFunctionInterface* Function = Cast<UMaterialFunctionInterface>(Object);
        Function != nullptr && Function->GetName().StartsWith(TEXT("MF_DWC_")))
    {
        ++RelevantObjectPropertyChangeCount;
        InvalidateMaterialGraphs();
    }
}

bool FDWCEditorPreviewSession::IsSourceOrBaseMaterial(
    const UObject* Object,
    UMaterialInterface* SourceMaterial) const
{
    return SourceMaterial != nullptr &&
           (Object == SourceMaterial || Object == SourceMaterial->GetMaterial());
}
