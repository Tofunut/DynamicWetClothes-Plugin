// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"

class UWetClothingAsset;

enum class EDWCWrinkleBuildSourceMode : uint8
{
    None,
    BakedAuthoring,
    CustomTexture
};

enum class EDWCWrinkleBuildRequirement : uint8
{
    None,
    Bake,
    ManualRepair
};

struct FDWCWrinkleBuildTarget
{
    int32 MaterialSlotIndex = INDEX_NONE;
    EDWCWrinkleBuildSourceMode SourceMode = EDWCWrinkleBuildSourceMode::None;
    EDWCWrinkleBuildRequirement Requirement = EDWCWrinkleBuildRequirement::None;
    FName DiagnosticCode;
    bool bWettable = false;
    bool bConfigured = false;
    bool bInputValid = false;
    bool bHasAuthoredContent = false;
    bool bHasBakeableContent = false;
    bool bHasRuntimeNormal = false;
    bool bHasBakedNormal = false;
    bool bHasCoverageMask = false;
    bool bOutputCurrent = false;
    bool bSavePending = false;
    bool bOrphanOutput = false;
    int32 PatchCount = 0;
    int32 RidgeStrokeCount = 0;
    FString Detail;

    bool RequiresBakedOutput() const
    {
        return SourceMode == EDWCWrinkleBuildSourceMode::BakedAuthoring &&
            bConfigured && bWettable;
    }
    bool IsBuildable() const
    {
        return RequiresBakedOutput() && bInputValid && bHasBakeableContent &&
            Requirement == EDWCWrinkleBuildRequirement::Bake;
    }
};

struct FDWCWrinkleBuildTargetSnapshot
{
    TArray<FDWCWrinkleBuildTarget> Targets;
    EDWCEditorBuildActionState BakeState = EDWCEditorBuildActionState::Unavailable;
    FString BakeReason;

    bool HasBakedAuthoringTargets() const;
    const FDWCWrinkleBuildTarget* FindByMaterialSlot(int32 MaterialSlotIndex) const;
    void CollectBuildMaterialSlots(TArray<int32>& OutMaterialSlots) const;
};

/** Side-effect-free source of truth for wrinkle source, output, and dependency state. */
class FDWCWrinkleBuildTargetResolver
{
public:
    static FDWCWrinkleBuildTargetSnapshot Resolve(
        const UWetClothingAsset& Asset,
        bool bDeepValidation);
};
