//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace DWCEditorSurfaceOrientationDefaults
{
    inline const FVector3f PrimaryAxis(0.0f, 0.0f, 1.0f);
    inline const FVector3f SecondaryAxis(1.0f, 0.0f, 0.0f);
    inline constexpr float FallbackFullQuality = 0.10f;
    inline constexpr float FallbackBeginQuality = 0.25f;
}

/** Immutable-after-normalization policy shared by field building and hit resolution. */
struct FDWCEditorSurfaceOrientationPolicy
{
    FVector3f PrimaryAxis = DWCEditorSurfaceOrientationDefaults::PrimaryAxis;
    FVector3f SecondaryAxis = DWCEditorSurfaceOrientationDefaults::SecondaryAxis;
    float FallbackFullQuality = DWCEditorSurfaceOrientationDefaults::FallbackFullQuality;
    float FallbackBeginQuality = DWCEditorSurfaceOrientationDefaults::FallbackBeginQuality;

    void Normalize();
    bool IsValid() const;
    uint32 BuildSignature() const;
};
