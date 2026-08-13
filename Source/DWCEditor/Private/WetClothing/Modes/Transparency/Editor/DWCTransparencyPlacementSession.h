//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

enum class EDWCTransparencyPlacementSelectionType : uint8
{
    None,
    Target,
    ExternalSource
};

struct FDWCTransparencyPlacementSelection
{
    EDWCTransparencyPlacementSelectionType Type =
        EDWCTransparencyPlacementSelectionType::None;
    FGuid SourceGuid;

    static FDWCTransparencyPlacementSelection Target()
    {
        FDWCTransparencyPlacementSelection Result;
        Result.Type = EDWCTransparencyPlacementSelectionType::Target;
        return Result;
    }

    static FDWCTransparencyPlacementSelection Source(const FGuid& InSourceGuid)
    {
        FDWCTransparencyPlacementSelection Result;
        Result.Type = EDWCTransparencyPlacementSelectionType::ExternalSource;
        Result.SourceGuid = InSourceGuid;
        return Result;
    }

    bool IsSource() const
    {
        return Type == EDWCTransparencyPlacementSelectionType::ExternalSource &&
            SourceGuid.IsValid();
    }

    bool operator==(const FDWCTransparencyPlacementSelection& Other) const
    {
        return Type == Other.Type && SourceGuid == Other.SourceGuid;
    }
};

/**
 * Transient Type 3 placement state shared by the panel and viewport.
 * Only source TargetLocalTransforms are persisted to the WCA. Presentation,
 * selection, visibility and assembly placement remain editor-session state.
 */
class FDWCTransparencyPlacementSession final
{
  public:
    const FDWCTransparencyPlacementSelection& GetSelection() const { return Selection; }
    void SetSelection(const FDWCTransparencyPlacementSelection& InSelection)
    {
        Selection = InSelection;
    }
    void ClearSelection() { Selection = {}; }

    const FTransform& GetAssemblyTransform() const { return AssemblyTransform; }
    void SetAssemblyTransform(const FTransform& InTransform) { AssemblyTransform = InTransform; }

    void SynchronizeSources(const TMap<FGuid, FTransform>& CanonicalTransforms)
    {
        for (auto It = TargetLocalTransforms.CreateIterator(); It; ++It)
        {
            if (!CanonicalTransforms.Contains(It.Key()))
            {
                It.RemoveCurrent();
            }
        }
        for (const TPair<FGuid, FTransform>& Pair : CanonicalTransforms)
        {
            TargetLocalTransforms.Add(Pair.Key, Pair.Value);
        }
        for (auto It = HiddenSources.CreateIterator(); It; ++It)
        {
            if (!CanonicalTransforms.Contains(*It))
            {
                It.RemoveCurrent();
            }
        }
        for (auto It = LockedSources.CreateIterator(); It; ++It)
        {
            if (!CanonicalTransforms.Contains(*It))
            {
                It.RemoveCurrent();
            }
        }
        if (SoloSourceGuid.IsSet() && !CanonicalTransforms.Contains(SoloSourceGuid.GetValue()))
        {
            SoloSourceGuid.Reset();
        }
        if (Selection.IsSource() && !CanonicalTransforms.Contains(Selection.SourceGuid))
        {
            ClearSelection();
        }
    }

    FTransform GetSourceTransform(const FGuid& SourceGuid) const
    {
        const FTransform* Transform = TargetLocalTransforms.Find(SourceGuid);
        return Transform != nullptr ? *Transform : FTransform::Identity;
    }
    void SetSourceTransform(const FGuid& SourceGuid, const FTransform& InTransform)
    {
        if (SourceGuid.IsValid())
        {
            TargetLocalTransforms.Add(SourceGuid, InTransform);
        }
    }
    void RemoveSource(const FGuid& SourceGuid)
    {
        TargetLocalTransforms.Remove(SourceGuid);
        HiddenSources.Remove(SourceGuid);
        LockedSources.Remove(SourceGuid);
        if (SoloSourceGuid.IsSet() && SoloSourceGuid.GetValue() == SourceGuid)
        {
            SoloSourceGuid.Reset();
        }
        if (Selection.IsSource() && Selection.SourceGuid == SourceGuid)
        {
            ClearSelection();
        }
    }

    bool IsSourceHidden(const FGuid& SourceGuid) const { return HiddenSources.Contains(SourceGuid); }
    void SetSourceHidden(const FGuid& SourceGuid, const bool bHidden)
    {
        if (bHidden) HiddenSources.Add(SourceGuid);
        else HiddenSources.Remove(SourceGuid);
    }
    bool IsSourceLocked(const FGuid& SourceGuid) const { return LockedSources.Contains(SourceGuid); }
    void SetSourceLocked(const FGuid& SourceGuid, const bool bLocked)
    {
        if (bLocked) LockedSources.Add(SourceGuid);
        else LockedSources.Remove(SourceGuid);
    }
    bool IsSourceSolo(const FGuid& SourceGuid) const
    {
        return SoloSourceGuid.IsSet() && SoloSourceGuid.GetValue() == SourceGuid;
    }
    void ToggleSourceSolo(const FGuid& SourceGuid)
    {
        if (IsSourceSolo(SourceGuid))
        {
            SoloSourceGuid.Reset();
        }
        else
        {
            SoloSourceGuid = SourceGuid;
        }
    }
    bool ShouldShowSource(const FGuid& SourceGuid) const
    {
        return !IsSourceHidden(SourceGuid) &&
            (!SoloSourceGuid.IsSet() || SoloSourceGuid.GetValue() == SourceGuid);
    }

    bool IsSelectionLocked() const
    {
        return Selection.IsSource() && IsSourceLocked(Selection.SourceGuid);
    }

  private:
    FDWCTransparencyPlacementSelection Selection;
    FTransform AssemblyTransform = FTransform::Identity;
    TMap<FGuid, FTransform> TargetLocalTransforms;
    TSet<FGuid> HiddenSources;
    TSet<FGuid> LockedSources;
    TOptional<FGuid> SoloSourceGuid;
};
