//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

enum class EDWCTransparencyStage : uint8
{
    Source,
    Reveal,
    Final
};

enum class EDWCTransparencyStaleReason : uint8
{
    None,
    MissingArtifact,
    SourceInputsChanged,
    RevealEditsChanged,
    AlphaEditsChanged,
    WrinkleDependencyChanged,
    OutputSettingsChanged
};

struct FDWCTransparencyStageIdentity
{
    FGuid LayerGuid;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 DataUVChannelIndex = INDEX_NONE;
    int32 LODIndex = 0;
    FIntPoint Resolution = FIntPoint::ZeroValue;
    uint64 Revision = 0;

    bool IsValid() const;
};

struct FDWCTransparencyStageStatus
{
    EDWCTransparencyStage Stage = EDWCTransparencyStage::Source;
    EDWCTransparencyStaleReason Reason = EDWCTransparencyStaleReason::None;
    FString Detail;

    bool IsCurrent() const { return Reason == EDWCTransparencyStaleReason::None; }
};
