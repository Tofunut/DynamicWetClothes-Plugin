#pragma once

#include "CoreMinimal.h"

struct FDynamicWetReceiverContext;

class FDynamicWetReceiverMeshSampler
{
public:
    void ResetPositions();
    void ResetNormals();

    bool UpdateSkinnedPositions(FDynamicWetReceiverContext& Receiver);
    bool UpdateSkinnedNormals(FDynamicWetReceiverContext& Receiver);

    TArray<FVector3f> CachedSkinnedPositions;
    TArray<FVector3f> CachedSkinnedNormals;
    TArray<FMatrix44f> CachedRefToLocalMatrices;
};
