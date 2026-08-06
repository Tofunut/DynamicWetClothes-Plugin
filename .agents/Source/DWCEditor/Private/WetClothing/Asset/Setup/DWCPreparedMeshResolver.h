#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;

enum class EDWCPreparedMeshPreflightAction : uint8
{
    ReuseReferencedMesh,
    ReuseDeterministicMesh,
    CreateNewMesh,
    ForceReplaceDeterministicMesh,
    OwnershipConflict,
    InvalidSourceMesh,
    InvalidReferencedMesh
};

struct FDWCPreparedMeshPreflightResult
{
    bool bCanProceed = false;
    EDWCPreparedMeshPreflightAction Action = EDWCPreparedMeshPreflightAction::InvalidSourceMesh;
    USkeletalMesh* ResolvedMesh = nullptr;
    FString TargetPackageName;
    FString TargetObjectPath;
    FString ErrorMessage;
};

struct FDWCPreparedMeshResolveResult
{
    USkeletalMesh* Mesh = nullptr;
    FString ErrorMessage;

    bool IsSuccess() const
    {
        return Mesh != nullptr;
    }
};

/** Resolves or creates the persistent Skeletal Mesh that owns the generated DWC UV Channel coordinates. */
class FDWCPreparedMeshResolver
{
public:
    /** Cheap build-time validation. Never scans the project and checks at most one deterministic path. */
    static FDWCPreparedMeshPreflightResult Preflight(
        UWetClothingAsset& Asset,
        bool bForceNewAsset);

    static FDWCPreparedMeshResolveResult Resolve(
        UWetClothingAsset& Asset,
        bool bForceNewAsset);

    /** Explicit recovery-only operation. This may query the project and must not be called by normal Build. */
    static USkeletalMesh* FindMovedOwnedPreparedMesh(UWetClothingAsset& Asset);
};
