// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyBakedBaselineMemoryPolicy.h"

#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"

namespace
{
    constexpr uint64 BaselineRestoreMiB = 1024ull * 1024ull;
    constexpr uint64 RasterFixedGuardBytes = 16ull * BaselineRestoreMiB;
    constexpr uint64 RetainedAllocationSlackBytes = 1ull * BaselineRestoreMiB;

    bool TryAdd(const uint64 A, const uint64 B, uint64& Out)
    {
        if (B > MAX_uint64 - A)
        {
            return false;
        }
        Out = A + B;
        return true;
    }

    bool TryMultiply(const uint64 A, const uint64 B, uint64& Out)
    {
        if (A != 0 && B > MAX_uint64 / A)
        {
            return false;
        }
        Out = A * B;
        return true;
    }
}

bool FDWCTransparencyBakedBaselineMemoryPolicy::TryBuildPlan(
    const FIntPoint Resolution,
    const int32 BuildSignatureLength,
    FDWCTransparencyBakedBaselineMemoryPlan& OutPlan,
    FString& OutError)
{
    OutPlan = {};
    OutError.Reset();
    if (Resolution.X <= 0 || Resolution.Y <= 0 || BuildSignatureLength < 0)
    {
        OutError = TEXT("Baked baseline restore requires a valid resolution and signature.");
        return false;
    }

    uint64 PixelCount = 0;
    if (!TryMultiply(
            static_cast<uint64>(Resolution.X),
            static_cast<uint64>(Resolution.Y),
            PixelCount) ||
        PixelCount > static_cast<uint64>(MAX_int32))
    {
        OutError = TEXT("Baked baseline restore resolution exceeds the supported pixel count.");
        return false;
    }

    if (!TryMultiply(PixelCount, sizeof(FColor), OutPlan.RawMipBytes))
    {
        OutError = TEXT("Baked baseline raw mip memory estimate overflowed.");
        return false;
    }

    // The mask raster uses one int32 owner/island entry plus one byte of
    // subpixel flags per texel. The fixed guard covers topology copies and
    // eligible-triangle containers without scaling them to image resolution.
    uint64 RasterPixelBytes = 0;
    if (!TryMultiply(PixelCount, sizeof(int32) + sizeof(uint8), RasterPixelBytes) ||
        !TryAdd(RasterPixelBytes, RasterFixedGuardBytes, OutPlan.RasterScratchBytes))
    {
        OutError = TEXT("Baked baseline raster scratch memory estimate overflowed.");
        return false;
    }
    OutPlan.WorkerPeakBytes = FMath::Max(
        OutPlan.RawMipBytes,
        OutPlan.RasterScratchBytes);

    // Final baselines retain color, alpha, coverage, island, valid-hit,
    // distance, and priority domains. TBitArray stores one bit per texel.
    constexpr uint64 DenseBytesPerPixel =
        sizeof(FColor) + sizeof(uint8) + sizeof(uint8) + sizeof(uint16) +
        sizeof(float) + sizeof(int16);
    uint64 DenseBytes = 0;
    uint64 BitBytes = 0;
    uint64 SignatureBytes = 0;
    uint64 RetainedBytes = sizeof(FDWCTransparencySourcePayload);
    if (!TryMultiply(PixelCount, DenseBytesPerPixel, DenseBytes) ||
        !TryAdd(PixelCount, 7, BitBytes))
    {
        OutError = TEXT("Baked baseline retained memory estimate overflowed.");
        return false;
    }
    BitBytes /= 8;
    if (!TryMultiply(
            static_cast<uint64>(BuildSignatureLength) + 1ull,
            sizeof(TCHAR),
            SignatureBytes) ||
        !TryAdd(RetainedBytes, DenseBytes, RetainedBytes) ||
        !TryAdd(RetainedBytes, BitBytes, RetainedBytes) ||
        !TryAdd(RetainedBytes, SignatureBytes, RetainedBytes) ||
        !TryAdd(RetainedBytes, RetainedAllocationSlackBytes, RetainedBytes))
    {
        OutError = TEXT("Baked baseline retained memory estimate overflowed.");
        return false;
    }
    OutPlan.RetainedPayloadBytes = RetainedBytes;
    return true;
}
