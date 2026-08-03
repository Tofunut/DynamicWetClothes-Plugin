#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;

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
    static FDWCPreparedMeshResolveResult Resolve(
        UWetClothingAsset& Asset,
        bool bForceNewAsset);
};
