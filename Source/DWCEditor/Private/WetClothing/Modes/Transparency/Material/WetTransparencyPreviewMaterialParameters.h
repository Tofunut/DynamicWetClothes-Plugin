// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace DWCTransparencyPreviewMaterialParameters
{
    inline const FName& TransparencyMap()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewMap"));
        return Name;
    }

    inline const FName& UseTransparencyMap()
    {
        static const FName Name(TEXT("DWC_UseTransparencyPreviewMap"));
        return Name;
    }

    inline const FName& TransparencyStrength()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewStrength"));
        return Name;
    }

    inline const FName& RevealSurfaceMap()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewRevealSurfaceMap"));
        return Name;
    }

    inline const FName& UseRevealSurfaceMap()
    {
        static const FName Name(TEXT("DWC_UseTransparencyPreviewRevealSurfaceMap"));
        return Name;
    }

    inline const FName& RevealMetallicDarkeningStrength()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewRevealMetallicDarkeningStrength"));
        return Name;
    }

    inline const FName& ShowInnerColor()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewShowInnerColor"));
        return Name;
    }

    inline const FName& WrinkleCoverageMap()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewWrinkleCoverageMap"));
        return Name;
    }

    inline const FName& UseWrinkleCoverageMap()
    {
        static const FName Name(TEXT("DWC_UseTransparencyPreviewWrinkleCoverage"));
        return Name;
    }

    inline const FName& WrinkleSuppressionStrength()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewWrinkleSuppressionStrength"));
        return Name;
    }

    inline const FName& WrinkleMaskThreshold()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewWrinkleThreshold"));
        return Name;
    }

    inline const FName& WrinkleMaskSoftness()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewWrinkleSoftness"));
        return Name;
    }

    inline const FName& VisualizationMode()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewVisualizationMode"));
        return Name;
    }

    inline const FName& HoverState0()
    {
        static const FName Name(TEXT("DWC_TransparencyHoverState0"));
        return Name;
    }

    inline const FName& HoverState1()
    {
        static const FName Name(TEXT("DWC_TransparencyHoverState1"));
        return Name;
    }

    inline const FName& HoverColor()
    {
        static const FName Name(TEXT("DWC_TransparencyHoverColor"));
        return Name;
    }

    inline const FName& HoverTarget()
    {
        static const FName Name(TEXT("DWC_TransparencyHoverTarget"));
        return Name;
    }

    inline const FName& HoverWrap()
    {
        static const FName Name(TEXT("DWC_TransparencyHoverWrap"));
        return Name;
    }

    inline const FName& HoverTexelSize()
    {
        static const FName Name(TEXT("DWC_TransparencyHoverTexelSize"));
        return Name;
    }

    inline const FName& HoverVisualizationMode()
    {
        static const FName Name(TEXT("DWC_TransparencyHoverVisualizationMode"));
        return Name;
    }

    inline const FName& HoverBaselineMap()
    {
        static const FName Name(TEXT("DWC_TransparencyHoverBaselineMap"));
        return Name;
    }

    inline const FName& UseHoverBaselineMap()
    {
        static const FName Name(TEXT("DWC_UseTransparencyHoverBaselineMap"));
        return Name;
    }

    inline const FName& HoverEdgeFeatherMap()
    {
        static const FName Name(TEXT("DWC_TransparencyHoverEdgeFeatherMap"));
        return Name;
    }

    inline const FName& UseHoverEdgeFeatherMap()
    {
        static const FName Name(TEXT("DWC_UseTransparencyHoverEdgeFeatherMap"));
        return Name;
    }
} // namespace DWCTransparencyPreviewMaterialParameters

/** Stable values consumed by the editor-only Transparency hover shader. */
enum class EDWCTransparencyMaterialHoverTarget : uint8
{
    None = 0,
    RevealColor = 1,
    TransparencyAlpha = 2
};

enum class EDWCTransparencyMaterialHoverOperation : uint8
{
    PaintOrApply = 0,
    Erase = 1,
    Reset = 2,
    Smooth = 3
};
