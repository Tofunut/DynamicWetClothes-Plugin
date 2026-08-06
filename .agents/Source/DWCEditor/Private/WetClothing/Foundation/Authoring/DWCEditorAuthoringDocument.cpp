#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "ScopedTransaction.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCEditorAuthoring, Log, All);

namespace
{
    constexpr double SlowModifyThresholdMilliseconds = 50.0;

    void ModifyAssetForAuthoringTransaction(
        UWetClothingAsset& Asset,
        const FText& TransactionText)
    {
        const double StartSeconds = FPlatformTime::Seconds();
        Asset.Modify();
        const double ModifyMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

        if (ModifyMilliseconds >= SlowModifyThresholdMilliseconds)
        {
            // This diagnostic walks the complete CPU/GPU runtime payload. Do
            // it only after a genuinely slow snapshot so ordinary authoring
            // commands do not pay the diagnostic traversal cost themselves.
            const uint64 ResidentBulkBytes = Asset.GetResidentRuntimeBulkPayloadBytesForEditor();
            UE_LOG(
                LogDWCEditorAuthoring,
                Warning,
                TEXT("Slow WCA undo snapshot for '%s': transaction='%s', modify=%.1f ms, residentRuntimeBulk=%.1f MiB."),
                *GetNameSafe(&Asset),
                *TransactionText.ToString(),
                ModifyMilliseconds,
                static_cast<double>(ResidentBulkBytes) / (1024.0 * 1024.0));
        }
        else
        {
            UE_LOG(
                LogDWCEditorAuthoring,
                VeryVerbose,
                TEXT("WCA undo snapshot for '%s': transaction='%s', modify=%.1f ms."),
                *GetNameSafe(&Asset),
                *TransactionText.ToString(),
                ModifyMilliseconds);
        }
    }
}

FDWCEditorAuthoringDocument::FDWCEditorAuthoringDocument(UWetClothingAsset* InAsset)
    : Asset(InAsset)
{
}

FDWCEditorAuthoringDocument::~FDWCEditorAuthoringDocument()
{
    if (InteractiveTransaction.IsValid())
    {
        InteractiveTransaction->Cancel();
    }
    ResetInteractiveState();
}

const UWetClothingAsset* FDWCEditorAuthoringDocument::GetAsset() const
{
    return Asset.Get();
}

bool FDWCEditorAuthoringDocument::IsValid() const
{
    return Asset.IsValid();
}

uint64 FDWCEditorAuthoringDocument::GetRevision() const
{
    return Revision;
}

bool FDWCEditorAuthoringDocument::HasInteractiveEdit() const
{
    return InteractiveTransaction.IsValid();
}

FDWCEditorAuthoringResult FDWCEditorAuthoringDocument::Edit(
    const FText& TransactionText,
    FDWCEditorAuthoringChange Change,
    TFunctionRef<bool(UWetClothingAsset&)> Mutation)
{
    FString Error;
    if (!IsOnGameThread(&Error))
    {
        return MakeFailure(Change, MoveTemp(Error));
    }
    if (!ValidateMutationChange(Change, &Error))
    {
        return MakeFailure(Change, MoveTemp(Error));
    }
    if (HasInteractiveEdit())
    {
        return MakeFailure(Change, TEXT("A continuous authoring edit is already active."));
    }

    UWetClothingAsset* MutableAsset = Asset.Get();
    if (MutableAsset == nullptr)
    {
        return MakeFailure(Change, TEXT("The Wet Clothing Asset is unavailable."));
    }

    FScopedTransaction Transaction(TransactionText);
    ModifyAssetForAuthoringTransaction(*MutableAsset, TransactionText);
    if (!Mutation(*MutableAsset))
    {
        Transaction.Cancel();
        return MakeFailure(Change, TEXT("The authoring command did not change the asset."));
    }

    ApplyCommittedImpact(*MutableAsset, Change);
    return BroadcastCommittedChange(Change);
}

bool FDWCEditorAuthoringDocument::BeginInteractiveEdit(
    const FText& TransactionText,
    FDWCEditorAuthoringChange Change,
    FString* OutError)
{
    if (!IsOnGameThread(OutError))
    {
        return false;
    }
    if (!ValidateMutationChange(Change, OutError))
    {
        return false;
    }
    if (HasInteractiveEdit())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("A continuous authoring edit is already active.");
        }
        return false;
    }

    UWetClothingAsset* MutableAsset = Asset.Get();
    if (MutableAsset == nullptr)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The Wet Clothing Asset is unavailable.");
        }
        return false;
    }

    InteractiveTransaction = MakeUnique<FScopedTransaction>(TransactionText);
    ModifyAssetForAuthoringTransaction(*MutableAsset, TransactionText);
    InteractiveChange = Change;
    InteractiveChange.Phase = EDWCEditorAuthoringChangePhase::Interactive;
    bInteractiveMutationChanged = false;
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    return true;
}

FDWCEditorAuthoringResult FDWCEditorAuthoringDocument::UpdateInteractiveEdit(
    FDWCEditorAuthoringChange Change,
    TFunctionRef<bool(UWetClothingAsset&)> Mutation)
{
    FString Error;
    if (!IsOnGameThread(&Error))
    {
        return MakeFailure(Change, MoveTemp(Error));
    }
    if (!HasInteractiveEdit())
    {
        return MakeFailure(Change, TEXT("No continuous authoring edit is active."));
    }

    UWetClothingAsset* MutableAsset = Asset.Get();
    if (MutableAsset == nullptr)
    {
        return MakeFailure(Change, TEXT("The Wet Clothing Asset is unavailable."));
    }
    if (!Mutation(*MutableAsset))
    {
        return MakeFailure(Change, TEXT("The interactive authoring update did not change the asset."));
    }

    bInteractiveMutationChanged = true;
    Change.Phase = EDWCEditorAuthoringChangePhase::Interactive;
    Change.Revision = Revision;
    ChangedDelegate.Broadcast(Change);

    FDWCEditorAuthoringResult Result;
    Result.bChanged = true;
    Result.Change = Change;
    return Result;
}

FDWCEditorAuthoringResult FDWCEditorAuthoringDocument::CommitInteractiveEdit(
    FDWCEditorAuthoringChange Change)
{
    FString Error;
    if (!IsOnGameThread(&Error))
    {
        return MakeFailure(Change, MoveTemp(Error));
    }
    if (!HasInteractiveEdit())
    {
        return MakeFailure(Change, TEXT("No continuous authoring edit is active."));
    }

    UWetClothingAsset* MutableAsset = Asset.Get();
    if (MutableAsset == nullptr)
    {
        InteractiveTransaction->Cancel();
        ResetInteractiveState();
        return MakeFailure(Change, TEXT("The Wet Clothing Asset is unavailable."));
    }
    if (!bInteractiveMutationChanged)
    {
        InteractiveTransaction->Cancel();
        ResetInteractiveState();
        return MakeFailure(Change, TEXT("The continuous authoring edit made no changes."));
    }

    Change.Impact |= InteractiveChange.Impact;
    if (Change.Domain == EDWCEditorAuthoringDomain::None)
    {
        Change.Domain = InteractiveChange.Domain;
    }
    if (Change.MaterialSlotIndex == INDEX_NONE)
    {
        Change.MaterialSlotIndex = InteractiveChange.MaterialSlotIndex;
    }
    if (!Change.LayerGuid.IsValid())
    {
        Change.LayerGuid = InteractiveChange.LayerGuid;
    }
    if (!Change.ElementGuid.IsValid())
    {
        Change.ElementGuid = InteractiveChange.ElementGuid;
    }

    ApplyCommittedImpact(*MutableAsset, Change);
    ResetInteractiveState();
    return BroadcastCommittedChange(Change);
}

void FDWCEditorAuthoringDocument::CancelInteractiveEdit(
    FDWCEditorAuthoringChange Change,
    TFunctionRef<void(UWetClothingAsset&)> RestoreMutation)
{
    if (!HasInteractiveEdit())
    {
        return;
    }
    if (UWetClothingAsset* MutableAsset = Asset.Get())
    {
        RestoreMutation(*MutableAsset);
    }
    InteractiveTransaction->Cancel();
    ResetInteractiveState();
    Change.Phase = EDWCEditorAuthoringChangePhase::Canceled;
    Change.Revision = Revision;
    ChangedDelegate.Broadcast(Change);
}

void FDWCEditorAuthoringDocument::NotifyUndoRedo(FDWCEditorAuthoringChange Change)
{
    if (!IsOnGameThread())
    {
        return;
    }
    if (InteractiveTransaction.IsValid())
    {
        InteractiveTransaction->Cancel();
    }
    ResetInteractiveState();
    Change.Phase = EDWCEditorAuthoringChangePhase::UndoRedo;
    Change.Revision = ++Revision;
    ChangedDelegate.Broadcast(Change);
}

FDWCEditorAuthoringChanged& FDWCEditorAuthoringDocument::OnChanged()
{
    return ChangedDelegate;
}

bool FDWCEditorAuthoringDocument::IsOnGameThread(FString* OutError)
{
    if (IsInGameThread())
    {
        return true;
    }
    if (OutError != nullptr)
    {
        *OutError = TEXT("WCA authoring mutations must run on the game thread.");
    }
    return false;
}

bool FDWCEditorAuthoringDocument::ValidateMutationChange(
    const FDWCEditorAuthoringChange& Change,
    FString* OutError)
{
    auto Fail = [OutError](const TCHAR* Message)
    {
        if (OutError != nullptr)
        {
            *OutError = Message;
        }
        return false;
    };

    if (Change.Domain == EDWCEditorAuthoringDomain::None)
    {
        return Fail(TEXT("An authoring mutation must declare its owning domain."));
    }
    if (!EnumHasAnyFlags(Change.Impact, EDWCEditorAuthoringImpact::AssetDirty))
    {
        return Fail(TEXT("An authoring mutation must declare AssetDirty impact."));
    }

    const bool bTouchesWrinkleBake =
        EnumHasAnyFlags(Change.Impact, EDWCEditorAuthoringImpact::WrinkleBake);
    const bool bTouchesTransparencyBake =
        EnumHasAnyFlags(
            Change.Impact,
            EDWCEditorAuthoringImpact::TransparencyAutoBake |
                EDWCEditorAuthoringImpact::TransparencyFinalBake);
    if (Change.Domain == EDWCEditorAuthoringDomain::Wrinkle && bTouchesTransparencyBake)
    {
        return Fail(TEXT("A wrinkle authoring mutation cannot invalidate transparency bake state."));
    }
    if (Change.Domain == EDWCEditorAuthoringDomain::Transparency && bTouchesWrinkleBake)
    {
        return Fail(TEXT("A transparency authoring mutation cannot invalidate wrinkle bake state."));
    }
    return true;
}

void FDWCEditorAuthoringDocument::ApplyCommittedImpact(
    UWetClothingAsset& MutableAsset,
    const FDWCEditorAuthoringChange& Change) const
{
    if (EnumHasAnyFlags(Change.Impact, EDWCEditorAuthoringImpact::WrinkleBake))
    {
        MutableAsset.MarkWrinkleBakeOutOfDate();
    }

    FWetClothingTransparencyData& TransparencyData = MutableAsset.Authored.TransparencyData;
    const auto ApplyToTransparencyLayers =
        [&Change, &TransparencyData](TFunctionRef<void(FWetClothingTransparencyLayerData&)> Visitor)
        {
            for (FWetClothingTransparencyLayerData& Layer : TransparencyData.TransparencyLayers)
            {
                if (!Change.LayerGuid.IsValid() || Layer.LayerGuid == Change.LayerGuid)
                {
                    Visitor(Layer);
                }
            }
        };

    if (EnumHasAnyFlags(Change.Impact, EDWCEditorAuthoringImpact::TransparencyAutoBake))
    {
        ApplyToTransparencyLayers(
            [](FWetClothingTransparencyLayerData& Layer)
            {
                Layer.MarkAutoBakeStale();
            });
    }
    else if (EnumHasAnyFlags(Change.Impact, EDWCEditorAuthoringImpact::TransparencyFinalBake))
    {
        ApplyToTransparencyLayers(
            [](FWetClothingTransparencyLayerData& Layer)
            {
                Layer.MarkFinalBakeStale();
            });
    }

    if (EnumHasAnyFlags(Change.Impact, EDWCEditorAuthoringImpact::AssetDirty))
    {
        MutableAsset.MarkPackageDirty();
    }
}

FDWCEditorAuthoringResult FDWCEditorAuthoringDocument::MakeFailure(
    FDWCEditorAuthoringChange Change,
    FString Error) const
{
    FDWCEditorAuthoringResult Result;
    Result.Error = MoveTemp(Error);
    Result.Change = Change;
    return Result;
}

FDWCEditorAuthoringResult FDWCEditorAuthoringDocument::BroadcastCommittedChange(
    FDWCEditorAuthoringChange Change)
{
    Change.Phase = EDWCEditorAuthoringChangePhase::Committed;
    Change.Revision = ++Revision;
    ChangedDelegate.Broadcast(Change);

    FDWCEditorAuthoringResult Result;
    Result.bChanged = true;
    Result.Change = Change;
    return Result;
}

void FDWCEditorAuthoringDocument::ResetInteractiveState()
{
    InteractiveTransaction.Reset();
    InteractiveChange = FDWCEditorAuthoringChange();
    bInteractiveMutationChanged = false;
}
