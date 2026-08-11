//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"

class FDWCEditorCancellationToken;
struct FDWCTransparencySourcePayload;

/** Compact immutable lookup over a sparse alpha snapshot. */
class FDWCTransparencyAlphaSnapshotView
{
  public:
    bool Initialize(const FDWCTransparencyAlphaWorkingSnapshot& InSnapshot, FString* OutError = nullptr);
    bool IsValid() const { return Snapshot != nullptr && !TileLookup.IsEmpty(); }
    uint8 GetPremultiplied(int32 PixelIndex) const;
    uint8 GetWeight(int32 PixelIndex) const;
    uint64 GetAllocatedBytes() const { return TileLookup.GetAllocatedSize(); }

  private:
    const FDWCTransparencyAlphaWorkingSnapshot* Snapshot = nullptr;
    FIntPoint TileGrid = FIntPoint::ZeroValue;
    TArray<int32> TileLookup;
};

/** Converts canonical stroke fallback into the same sparse representation used by live preview. */
class FDWCTransparencyAlphaSnapshotMaterializer
{
  public:
    static bool Materialize(
        const FDWCTransparencyAlphaDomainSnapshot& AlphaDomain,
        const FDWCTransparencyAlphaWorkingSnapshot& Input,
        FDWCTransparencyAlphaWorkingSnapshot& OutSparseSnapshot,
        FString& OutError,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    static bool Materialize(
        const FDWCTransparencyAlphaDomainSnapshot& AlphaDomain,
        FDWCTransparencyAlphaWorkingSnapshot&& Input,
        FDWCTransparencyAlphaWorkingSnapshot& OutSparseSnapshot,
        FString& OutError,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    /** Compatibility entry point for Stage 2/3 callers that have not extracted an alpha domain yet. */
    static bool Materialize(
        const FDWCTransparencySourcePayload& SourcePayload,
        const FDWCTransparencyAlphaWorkingSnapshot& Input,
        FDWCTransparencyAlphaWorkingSnapshot& OutSparseSnapshot,
        FString& OutError,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    static bool Materialize(
        const FDWCTransparencySourcePayload& SourcePayload,
        FDWCTransparencyAlphaWorkingSnapshot&& Input,
        FDWCTransparencyAlphaWorkingSnapshot& OutSparseSnapshot,
        FString& OutError,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);
};
