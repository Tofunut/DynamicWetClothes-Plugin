#include "Diagnostics/DWCDiffuseDiagnostics.h"

#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCDiffuseDiagnostics, Log, All);

namespace DWCDiffuseDiagnosticsPrivate
{
static TAutoConsoleVariable<int32> CVarDWCDiffuseDiagnostics(
    TEXT("dwc.DiffuseDiagnostics"),
    0,
    TEXT("Enable GPU DiffuseDry statistics and asynchronous CPU readback. 0=off, 1=on."),
    ECVF_RenderThreadSafe);

uint32 StatIndex(const FDWCDiffuseDiagnostics::EStat Stat)
{
    return static_cast<uint32>(Stat);
}
} // namespace DWCDiffuseDiagnosticsPrivate

bool FDWCDiffuseDiagnostics::IsEnabled()
{
    return DWCDiffuseDiagnosticsPrivate::CVarDWCDiffuseDiagnostics.GetValueOnAnyThread() != 0;
}

FRDGBufferRef FDWCDiffuseDiagnostics::CreateStatsBuffer(
    FRDGBuilder& GraphBuilder,
    const TCHAR* Name) const
{
    const FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), StatCount);
    FRDGBufferRef StatsBuffer = GraphBuilder.CreateBuffer(Desc, Name);
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StatsBuffer), 0u);
    return StatsBuffer;
}

bool FDWCDiffuseDiagnostics::HasPendingReadback(const int32 MaterialSlotIndex) const
{
    return PendingReadbacks.ContainsByPredicate(
        [MaterialSlotIndex](const FPendingReadback& Pending)
        {
            return Pending.MaterialSlotIndex == MaterialSlotIndex;
        });
}

void FDWCDiffuseDiagnostics::QueueReadback(
    FRDGBuilder& GraphBuilder,
    FRDGBufferRef StatsBuffer,
    const int32 MaterialSlotIndex,
    const FIntPoint TextureSize)
{
    if (!StatsBuffer || HasPendingReadback(MaterialSlotIndex))
    {
        return;
    }

    FPendingReadback Pending;
    Pending.Readback = MakeUnique<FRHIGPUBufferReadback>(
        FName(TEXT("DWC.DiffuseDiagnostics.Readback")));
    Pending.MaterialSlotIndex = MaterialSlotIndex;
    Pending.TextureSize = TextureSize;

    AddEnqueueCopyPass(
        GraphBuilder,
        Pending.Readback.Get(),
        StatsBuffer,
        StatCount * sizeof(uint32));

    PendingReadbacks.Add(MoveTemp(Pending));
}

float FDWCDiffuseDiagnostics::BitsToFloat(const uint32 Bits)
{
    float Value = 0.0f;
    FMemory::Memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

void FDWCDiffuseDiagnostics::Poll()
{
    for (int32 Index = PendingReadbacks.Num() - 1; Index >= 0; --Index)
    {
        FPendingReadback& Pending = PendingReadbacks[Index];
        if (!Pending.Readback.IsValid() || !Pending.Readback->IsReady())
        {
            continue;
        }

        const uint32* Stats = static_cast<const uint32*>(
            Pending.Readback->Lock(StatCount * sizeof(uint32)));
        if (Stats != nullptr)
        {
            UE_LOG(
                LogDWCDiffuseDiagnostics,
                Log,
                TEXT("DWC DiffuseDiagnostics: slot=%d size=%dx%d valid=%u connected=%u sourceWet=%u dry=%u dryWithIncoming=%u lowWet=%u incomingPositive=%u centerMax=%.9g areaMax=%.9g outFractionMax=%.9g inFractionMax=%.9g outgoingMassMax=%.9g incomingMassMax=%.9g incomingMassOnDryMax=%.9g incomingMassOnLowWetMax=%.9g newWetnessMax=%.9g newWetnessOnDryMax=%.9g newWetnessOnLowWetMax=%.9g invalid=%u."),
                Pending.MaterialSlotIndex,
                Pending.TextureSize.X,
                Pending.TextureSize.Y,
                Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::ValidTexels)],
                Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::ConnectedNeighbors)],
                Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::SourceWetTexels)],
                Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::DryTexels)],
                Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::DryTexelsWithIncoming)],
                Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::LowWetnessTexels)],
                Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::IncomingPositiveEdges)],
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::CenterWetnessMaxBits)]),
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::CenterAreaMaxBits)]),
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::OutFractionMaxBits)]),
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::InFractionMaxBits)]),
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::OutgoingMassMaxBits)]),
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::IncomingMassMaxBits)]),
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::IncomingMassOnDryMaxBits)]),
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::IncomingMassOnLowWetnessMaxBits)]),
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::NewWetnessMaxBits)]),
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::NewWetnessOnDryTexelsMaxBits)]),
                BitsToFloat(Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::NewWetnessOnLowWetnessMaxBits)]),
                Stats[DWCDiffuseDiagnosticsPrivate::StatIndex(EStat::InvalidNumberCount)]);
        }

        Pending.Readback->Unlock();
        PendingReadbacks.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    }
}
