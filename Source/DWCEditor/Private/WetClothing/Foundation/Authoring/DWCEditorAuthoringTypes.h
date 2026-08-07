//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

enum class EDWCEditorAuthoringDomain : uint8
{
    None,
    Wrinkle,
    Transparency
};

enum class EDWCEditorAuthoringChangePhase : uint8
{
    Interactive,
    Committed,
    Canceled,
    UndoRedo
};

enum class EDWCEditorAuthoringImpact : uint32
{
    None = 0,
    AssetDirty = 1 << 0,
    ElementList = 1 << 1,
    Preview = 1 << 2,
    HitTopology = 1 << 3,
    WrinkleBake = 1 << 4,
    TransparencyAutoBake = 1 << 5,
    TransparencyFinalBake = 1 << 6,
    RuntimeBinding = 1 << 7,
    Details = 1 << 8,
    /** A newly committed element can be appended to the live preview without a full rebuild. */
    PreviewIncremental = 1 << 9
};
ENUM_CLASS_FLAGS(EDWCEditorAuthoringImpact);

struct FDWCEditorAuthoringChange
{
    EDWCEditorAuthoringDomain Domain = EDWCEditorAuthoringDomain::None;
    EDWCEditorAuthoringChangePhase Phase = EDWCEditorAuthoringChangePhase::Committed;
    EDWCEditorAuthoringImpact Impact = EDWCEditorAuthoringImpact::None;
    int32 MaterialSlotIndex = INDEX_NONE;
    FGuid LayerGuid;
    FGuid ElementGuid;
    uint64 Revision = 0;
};

struct FDWCEditorAuthoringResult
{
    bool bChanged = false;
    FString Error;
    FDWCEditorAuthoringChange Change;

    explicit operator bool() const
    {
        return bChanged;
    }
};

DECLARE_MULTICAST_DELEGATE_OneParam(
    FDWCEditorAuthoringChanged,
    const FDWCEditorAuthoringChange&);
