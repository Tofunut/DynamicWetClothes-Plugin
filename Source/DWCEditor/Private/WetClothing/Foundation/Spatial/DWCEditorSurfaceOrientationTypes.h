//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "PackedNormal.h"

enum class EDWCEditorSurfaceOrientationFieldBuildStatus : uint8
{
    Unbuilt,
    Ready,
    Degraded
};

struct FDWCEditorSurfaceOrientationDiagnostics
{
    int32 StableTriangleCount = 0;
    int32 BlendTriangleCount = 0;
    int32 FallbackTriangleCount = 0;
    int32 FallbackComponentCount = 0;
    int32 FullyDegenerateComponentCount = 0;
    int32 CrossedUVSeamEdgeCount = 0;
    float MaxAdjacentDirectionAngleDegrees = 0.0f;
};

struct FDWCEditorSurfaceOrientationFieldEntry
{
    int32 TriangleIndex = INDEX_NONE;
    FPackedNormal CornerFallbackV[3];

    bool IsValid() const;
};

/** Sparse, immutable fallback directions owned by a spatial cache entry. */
struct FDWCEditorSurfaceOrientationField
{
    EDWCEditorSurfaceOrientationFieldBuildStatus BuildStatus =
        EDWCEditorSurfaceOrientationFieldBuildStatus::Unbuilt;
    uint32 PolicySignature = 0;
    uint32 FieldLayoutVersion = 0;
    TArray<int32> EntryIndexByTriangle;
    TArray<FDWCEditorSurfaceOrientationFieldEntry> Entries;
    FDWCEditorSurfaceOrientationDiagnostics Diagnostics;

    void Reset();
    bool IsEmpty() const;
    bool IsCompatible(uint32 InPolicySignature) const;
    const FDWCEditorSurfaceOrientationFieldEntry* FindByTriangleIndex(int32 TriangleIndex) const;
    uint64 GetAllocatedSizeBytes() const;
    bool ValidateContract(int32 TriangleCount, FString* OutError = nullptr) const;
};
