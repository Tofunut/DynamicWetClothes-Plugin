// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"

class FDWCEditorCancellationToken;

struct FDWCEditorNormalSourceSnapshot
{
    FWetClothingTextureReadback Texture;
    bool                        bFlipGreenChannel = false;

    bool      IsValid() const;
    FVector3f SampleBilinear(const FVector2f& UV) const;
};

struct FDWCEditorScalarSourceSnapshot
{
    FIntPoint                                            Size = FIntPoint::ZeroValue;
    TSharedPtr<const TArray<float>, ESPMode::ThreadSafe> Values;

    bool  IsValid() const;
    float SampleBilinear(const FVector2f& UV) const;
};

struct FDWCEditorBrushFootprint
{
    FVector2f CenterUV = FVector2f::ZeroVector;
    float     RadiusUV = 0.0f;
    float     RotationRadians = 0.0f;
    FVector2f Scale = FVector2f(1.0f, 1.0f);
    float     Falloff = 0.0f;
    bool      bWrap = true;
};

struct FDWCEditorNormalStampCommand
{
    FDWCEditorBrushFootprint       Footprint;
    FDWCEditorNormalSourceSnapshot NormalSource;
    FDWCEditorScalarSourceSnapshot CoverageSource;
    float                          Strength = 0.0f;
};

struct FDWCEditorNormalRasterSurface
{
    FIntPoint Size = FIntPoint::ZeroValue;
    // Tangent-space normals keep their positive Z hemisphere. Store XY as two
    // signed normalized 16-bit values and reconstruct Z when it is read. This
    // keeps a 4096 preview surface at 64 MiB instead of 192 MiB.
    TArray<uint32> PackedNormalXY;
    TArray<float>  Coverage;

    bool      Initialize(const FIntPoint& InSize, bool bWithCoverage);
    bool      IsValid() const;
    bool      HasCoverage() const;
    uint64    GetAllocatedSizeBytes() const;
    int32     GetPixelCount() const;
    FVector3f GetNormal(int32 Index) const;
    void      SetNormal(int32 Index, const FVector3f& Normal);

  private:
    static uint32    PackNormalXY(const FVector3f& Normal);
    static FVector3f UnpackNormalXY(uint32 PackedNormal);
};

/** Compact normal storage addressed in the coordinate system of a larger canvas. */
struct FDWCEditorNormalRasterRegion
{
    FIntPoint                     CanvasSize = FIntPoint::ZeroValue;
    FIntRect                      Rect;
    FDWCEditorNormalRasterSurface Surface;

    bool      Initialize(FIntPoint InCanvasSize, const FIntRect& InRect, bool bWithCoverage);
    bool      InitializeFromSurface(const FDWCEditorNormalRasterSurface& Source, const FIntRect& InRect);
    bool      IsValid() const;
    bool      Contains(int32 X, int32 Y) const;
    uint64    GetAllocatedSizeBytes() const;
    FVector3f GetNormal(int32 X, int32 Y) const;
    void      SetNormal(int32 X, int32 Y, const FVector3f& Normal);
    float     GetCoverage(int32 X, int32 Y) const;
    void      SetCoverage(int32 X, int32 Y, float Value);

  private:
    int32 ToLocalIndex(int32 X, int32 Y) const;
};

struct FDWCEditorRasterResult
{
    bool     bSucceeded = true;
    bool     bAffectedPixels = false;
    bool     bCanceled = false;
    FIntRect DirtyRect;
    int32    AffectedPixelCount = 0;
};
