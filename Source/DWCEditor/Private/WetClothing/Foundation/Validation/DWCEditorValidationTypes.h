// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"

/**
 * Declares the strongest data access a validation pass may perform.
 * Metadata-only evaluation is used by automatic editor status refreshes and
 * must not load packages, decode bulk payloads, read texture source data, or
 * mutate the WCA. Exact payload access is reserved for explicit validation and
 * build admission.
 */
enum class EDWCEditorValidationAccess : uint8
{
    MetadataOnly,
    ExactPayload
};

enum class EDWCEditorValidationDomain : uint8
{
    Asset,
    DataUV,
    WetPart,
    GeneratedMaterial,
    RuntimeCPU,
    RuntimeGPU,
    GPUSimulationMap,
    RenderProfile,
    Wrinkle,
    Transparency,
    Failure
};

enum class EDWCEditorValidationIntentState : uint8
{
    NotApplicable,
    NotConfigured,
    Draft,
    Enabled,
    Disabled
};

enum class EDWCEditorValidationInputState : uint8
{
    Unknown,
    Valid,
    Missing,
    Invalid
};

enum class EDWCEditorValidationDependencyState : uint8
{
    Ready,
    Blocked
};

enum class EDWCEditorValidationArtifactState : uint8
{
    NotRequired,
    Missing,
    Current,
    Stale,
    Partial,
    Invalid
};

enum class EDWCEditorValidationPersistenceState : uint8
{
    Saved,
    SavePending
};

enum class EDWCEditorValidationOperationState : uint8
{
    Idle,
    Running,
    Failed,
    Cancelled
};

enum class EDWCEditorValidationOverallState : uint8
{
    NotApplicable,
    NotConfigured,
    Draft,
    Disabled,
    Current,
    SavePending,
    Partial,
    Stale,
    Missing,
    Invalid,
    Blocked,
    Running,
    Failed,
    Cancelled
};

enum class EDWCEditorValidationSeverity : uint8
{
    Info,
    Warning,
    Error
};

enum class EDWCEditorValidationRemediation : uint8
{
    None,
    Manual,
    BuildAction
};

struct FDWCEditorValidationTargetKey
{
    EDWCEditorValidationDomain Domain = EDWCEditorValidationDomain::Asset;
    int32 MaterialSlotIndex = INDEX_NONE;
    FGuid LayerGuid;
    FName SubResource;

    bool operator==(const FDWCEditorValidationTargetKey& Other) const
    {
        return Domain == Other.Domain &&
               MaterialSlotIndex == Other.MaterialSlotIndex &&
               LayerGuid == Other.LayerGuid &&
               SubResource == Other.SubResource;
    }
};

FORCEINLINE uint32 GetTypeHash(const FDWCEditorValidationTargetKey& Key)
{
    uint32 Hash = GetTypeHash(static_cast<uint8>(Key.Domain));
    Hash = HashCombineFast(Hash, GetTypeHash(Key.MaterialSlotIndex));
    Hash = HashCombineFast(Hash, GetTypeHash(Key.LayerGuid));
    return HashCombineFast(Hash, GetTypeHash(Key.SubResource));
}

