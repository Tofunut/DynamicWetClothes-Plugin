#pragma once

#include "CoreMinimal.h"

struct FDynamicWetReceiverContext;

class FDynamicWetReceiverRenderApplier
{
public:
    void ResetCachedVertexColors();
    void InitializeCachedVertexColors(int32 VertexCount);

    void InitializeWetMaterialInstance(FDynamicWetReceiverContext& Receiver);
    void ApplyWetMaterialParameters(FDynamicWetReceiverContext& Receiver);
    void ApplyWetnessProfileMapParameters(FDynamicWetReceiverContext& Receiver);
    void ApplyWetnessToMaterial(FDynamicWetReceiverContext& Receiver);
    FLinearColor MakeWetVertexColor(const FDynamicWetReceiverContext& Receiver, int32 VertexIndex, float Wetness) const;

    TArray<FLinearColor> CachedWetVertexColors;
};
