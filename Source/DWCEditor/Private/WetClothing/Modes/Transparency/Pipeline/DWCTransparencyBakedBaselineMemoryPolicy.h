// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FDWCTransparencyBakedBaselineMemoryPlan
{
    uint64 RawMipBytes = 0;
    uint64 RasterScratchBytes = 0;
    uint64 WorkerPeakBytes = 0;
    uint64 RetainedPayloadBytes = 0;

    bool IsValid() const
    {
        return RawMipBytes > 0 && WorkerPeakBytes >= RawMipBytes &&
            RetainedPayloadBytes > 0;
    }
};

/** Conservative pre-allocation policy for restoring a baked Stage 4 baseline. */
class FDWCTransparencyBakedBaselineMemoryPolicy final
{
public:
    static bool TryBuildPlan(
        FIntPoint Resolution,
        int32 BuildSignatureLength,
        FDWCTransparencyBakedBaselineMemoryPlan& OutPlan,
        FString& OutError);
};
