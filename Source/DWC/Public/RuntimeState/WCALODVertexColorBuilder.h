#pragma once

#include "Async/DWCLODVertexColorTypes.h"
#include "CoreMinimal.h"
#include "DataAssets/WetClothingAsset.h"

class USkeletalMesh;

class DWC_API FWCALODVertexColorBuilder
{
public:
    static bool IsCurrent(
        const USkeletalMesh* Mesh,
        int32 FirstMappedLODIndex,
        int32 LastMappedLODIndex,
        const TArray<FWCALODVertexColorRuntimeData>& RuntimeData);

    static bool Build(
        const USkeletalMesh* Mesh,
        int32 FirstMappedLODIndex,
        int32 LastMappedLODIndex,
        TArray<FWCALODVertexColorRuntimeData>& OutRuntimeData,
        FString* OutErrorMessage = nullptr,
        const FDWCLODVertexColorTransferSettings& Settings = FDWCLODVertexColorTransferSettings());
};
