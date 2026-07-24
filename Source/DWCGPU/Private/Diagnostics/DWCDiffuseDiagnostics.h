#pragma once

#include "CoreMinimal.h"
#include "RenderGraphDefinitions.h"
#include "RHIGPUReadback.h"

class FRDGBuilder;

/**
 * Optional GPU-side diagnostics for the destination-oriented DiffuseDry passes.
 *
 * The shader writes counters and positive-float maxima into a small structured
 * buffer. This class owns the asynchronous readback and converts the values to
 * one compact log line per material slot.
 *
 * Enable at runtime with:
 *   dwc.DiffuseDiagnostics 1
 */
class FDWCDiffuseDiagnostics final
{
public:
    enum class EStat : uint32
    {
        ValidTexels = 0,
        ConnectedNeighbors,
        SourceWetTexels,
        DryTexels,
        DryTexelsWithIncoming,
        LowWetnessTexels,
        IncomingPositiveEdges,
        CenterWetnessMaxBits,
        CenterAreaMaxBits,
        OutFractionMaxBits,
        InFractionMaxBits,
        OutgoingMassMaxBits,
        IncomingMassMaxBits,
        IncomingMassOnDryMaxBits,
        IncomingMassOnLowWetnessMaxBits,
        NewWetnessMaxBits,
        NewWetnessOnDryTexelsMaxBits,
        NewWetnessOnLowWetnessMaxBits,
        InvalidNumberCount,
        Count
    };

    static constexpr uint32 StatCount = static_cast<uint32>(EStat::Count);

    static bool IsEnabled();

    /** Creates and clears the per-dispatch statistics buffer. */
    FRDGBufferRef CreateStatsBuffer(FRDGBuilder& GraphBuilder, const TCHAR* Name) const;

    /** Queues a non-blocking copy, unless this slot already has one pending. */
    void QueueReadback(
        FRDGBuilder& GraphBuilder,
        FRDGBufferRef StatsBuffer,
        int32 MaterialSlotIndex,
        FIntPoint TextureSize);

    /** Polls completed copies. Must be called from the render thread. */
    void Poll();

private:
    struct FPendingReadback
    {
        TUniquePtr<FRHIGPUBufferReadback> Readback;
        int32 MaterialSlotIndex = INDEX_NONE;
        FIntPoint TextureSize = FIntPoint::ZeroValue;
    };

    static float BitsToFloat(uint32 Bits);
    bool HasPendingReadback(int32 MaterialSlotIndex) const;

    TArray<FPendingReadback> PendingReadbacks;
};
