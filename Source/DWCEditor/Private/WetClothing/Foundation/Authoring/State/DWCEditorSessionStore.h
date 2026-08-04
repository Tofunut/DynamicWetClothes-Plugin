#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionReducer.h"

DECLARE_MULTICAST_DELEGATE_ThreeParams(
    FDWCEditorSessionStateChanged,
    const FDWCEditorSessionState&,
    EDWCEditorSessionEffect,
    uint64);

/** Game-thread-only owner of transient WCA editor UI state. */
class FDWCEditorSessionStore final : public TSharedFromThis<FDWCEditorSessionStore>
{
  public:
    const FDWCEditorSessionState& GetState() const { return State; }
    uint64 GetRevision() const { return State.SessionRevision; }
    FDWCEditorSessionStateChanged& OnChanged() { return ChangedDelegate; }

    template <typename ActionType>
    void Dispatch(const ActionType& Action)
    {
        check(IsInGameThread());
        if (bDispatching)
        {
            DeferredActions.Add(
                [this, Action]()
                {
                    Dispatch(Action);
                });
            return;
        }

        bDispatching = true;
        const EDWCEditorSessionEffect Effects = FDWCEditorSessionReducer::Reduce(State, Action);
        if (Effects != EDWCEditorSessionEffect::None)
        {
            ++State.SessionRevision;
            ChangedDelegate.Broadcast(State, Effects, State.SessionRevision);
        }
        bDispatching = false;

        while (!DeferredActions.IsEmpty())
        {
            TUniqueFunction<void()> Deferred = MoveTemp(DeferredActions[0]);
            DeferredActions.RemoveAt(0, 1, EAllowShrinking::No);
            Deferred();
        }
    }

  private:
    FDWCEditorSessionState State;
    FDWCEditorSessionStateChanged ChangedDelegate;
    TArray<TUniqueFunction<void()>> DeferredActions;
    bool bDispatching = false;
};
