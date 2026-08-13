// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"

class FScopedTransaction;
class UObject;
class UWetClothingAsset;

/**
 * Game-thread-only mutation boundary for one WCA editor session.
 *
 * Serialized authored structs remain owned by UWetClothingAsset. This document
 * centralizes transaction lifetime, dirty/stale policy, revision tracking, and
 * change notification without introducing another UObject or saved format.
 */
class FDWCEditorAuthoringDocument final
    : public TSharedFromThis<FDWCEditorAuthoringDocument>
{
  public:
    explicit FDWCEditorAuthoringDocument(UWetClothingAsset* InAsset);
    ~FDWCEditorAuthoringDocument();

    const UWetClothingAsset* GetAsset() const;
    bool                     IsValid() const;
    uint64                   GetRevision() const;
    bool                     HasInteractiveEdit() const;

    FDWCEditorAuthoringResult Edit(
        const FText&                           TransactionText,
        FDWCEditorAuthoringChange              Change,
        TFunctionRef<bool(UWetClothingAsset&)> Mutation);
    FDWCEditorAuthoringResult Edit(
        const FText&                           TransactionText,
        FDWCEditorAuthoringChange              Change,
        UObject*                               AdditionalTransactionTarget,
        TFunctionRef<bool(UWetClothingAsset&)> Mutation);

    bool BeginInteractiveEdit(
        const FText&              TransactionText,
        FDWCEditorAuthoringChange Change,
        FString*                  OutError = nullptr);
    bool BeginInteractiveEdit(
        const FText&              TransactionText,
        FDWCEditorAuthoringChange Change,
        UObject*                  AdditionalTransactionTarget,
        FString*                  OutError = nullptr);
    FDWCEditorAuthoringResult UpdateInteractiveEdit(
        FDWCEditorAuthoringChange              Change,
        TFunctionRef<bool(UWetClothingAsset&)> Mutation);
    FDWCEditorAuthoringResult CommitInteractiveEdit(
        FDWCEditorAuthoringChange Change);
    void CancelInteractiveEdit(
        FDWCEditorAuthoringChange              Change,
        TFunctionRef<void(UWetClothingAsset&)> RestoreMutation);

    void                        NotifyUndoRedo(FDWCEditorAuthoringChange Change);
    FDWCEditorAuthoringChanged& OnChanged();

  private:
    static bool IsOnGameThread(FString* OutError = nullptr);
    static bool ValidateMutationChange(
        const FDWCEditorAuthoringChange& Change,
        FString*                         OutError = nullptr);
    void ApplyCommittedImpact(
        UWetClothingAsset&               Asset,
        const FDWCEditorAuthoringChange& Change) const;
    FDWCEditorAuthoringResult MakeFailure(
        FDWCEditorAuthoringChange Change,
        FString                   Error) const;
    FDWCEditorAuthoringResult BroadcastCommittedChange(
        FDWCEditorAuthoringChange Change);
    void ResetInteractiveState();

    TWeakObjectPtr<UWetClothingAsset> Asset;
    TUniquePtr<FScopedTransaction>    InteractiveTransaction;
    FDWCEditorAuthoringChange         InteractiveChange;
    uint64                            Revision = 0;
    bool                              bInteractiveMutationChanged = false;
    FDWCEditorAuthoringChanged        ChangedDelegate;
};
