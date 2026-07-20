#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAsset.h"

class USkeletalMesh;

class DWC_API FWCALODVertexColorBuilder
{
public:
    static bool Build(
        const USkeletalMesh* Mesh,
        int32 FirstMappedLODIndex,
        int32 LastMappedLODIndex,
        TArray<FWCALODVertexColorRuntimeData>& OutRuntimeData,
        FString* OutErrorMessage = nullptr);
};
