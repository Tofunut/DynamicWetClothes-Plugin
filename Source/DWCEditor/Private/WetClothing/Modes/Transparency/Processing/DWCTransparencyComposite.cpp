//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAlphaSnapshotMaterializer.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"

namespace
{
    const FColor TransparencyPriorityColors[] =
    {
        FColor(230, 70, 70), FColor(70, 170, 240), FColor(80, 210, 120), FColor(235, 185, 65),
        FColor(180, 95, 225), FColor(65, 215, 205), FColor(240, 120, 185), FColor(180, 180, 180)
    };
}

bool FDWCTransparencyPixelComposeContext::IsValid() const
{
    if (SourcePayload == nullptr || SourcePayload->Resolution.X <= 0 || SourcePayload->Resolution.Y <= 0)
    {
        return false;
    }
    const int32 PixelCount = SourcePayload->Resolution.X * SourcePayload->Resolution.Y;
    return SourcePayload->InnerColorBuffer.Num() == PixelCount &&
        SourcePayload->AutoAlphaBuffer.Num() == PixelCount &&
        (RevealColorBuffer.IsEmpty() || RevealColorBuffer.Num() == PixelCount);
}

FColor FDWCTransparencyComposite::ApplyRevealMetallicDarkening(
    FColor RevealColor,
    const FDWCTransparencySourcePayload& SourcePayload,
    const int32 PixelIndex,
    const float Strength)
{
    const float SafeStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
    if (SafeStrength <= KINDA_SMALL_NUMBER ||
        !SourcePayload.RevealSurfaceAuthoring.IsValidForResolution(SourcePayload.Resolution) ||
        !SourcePayload.RevealSurfaceAuthoring.HasValidSource(PixelIndex))
    {
        return RevealColor;
    }

    const float Darkening = FMath::Clamp(
        SourcePayload.RevealSurfaceAuthoring.GetInnerMetallic(PixelIndex) *
        SourcePayload.RevealSurfaceAuthoring.GetSourceCoverage(PixelIndex) *
        SafeStrength,
        0.0f,
        1.0f);
    if (Darkening <= KINDA_SMALL_NUMBER)
    {
        return RevealColor;
    }

    const uint8 PreservedAlpha = RevealColor.A;
    FLinearColor LinearColor = FLinearColor::FromSRGBColor(RevealColor);
    LinearColor.R *= 1.0f - Darkening;
    LinearColor.G *= 1.0f - Darkening;
    LinearColor.B *= 1.0f - Darkening;
    RevealColor = LinearColor.ToFColorSRGB();
    RevealColor.A = PreservedAlpha;
    return RevealColor;
}

bool FDWCTransparencyComposite::ApplyRevealMetallicDarkening(
    TArray<FColor>& InOutRevealColors,
    const FDWCTransparencySourcePayload& SourcePayload,
    const float Strength,
    const FDWCEditorCancellationToken* CancellationToken)
{
    const int32 PixelCount = SourcePayload.Resolution.X * SourcePayload.Resolution.Y;
    if (PixelCount <= 0 || InOutRevealColors.Num() != PixelCount)
    {
        return false;
    }

    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        if ((PixelIndex & 4095) == 0 && CancellationToken != nullptr && CancellationToken->IsCanceled())
        {
            return false;
        }
        InOutRevealColors[PixelIndex] = ApplyRevealMetallicDarkening(
            InOutRevealColors[PixelIndex],
            SourcePayload,
            PixelIndex,
            Strength);
    }
    return true;
}

float FDWCTransparencyComposite::ComputeMaximumHitDistance(
    const FDWCTransparencySourcePayload& SourcePayload)
{
    float MaximumHitDistance = KINDA_SMALL_NUMBER;
    const int32 PixelCount = SourcePayload.Resolution.X * SourcePayload.Resolution.Y;
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        if (SourcePayload.ValidHitBuffer.IsValidIndex(PixelIndex) &&
            SourcePayload.ValidHitBuffer[PixelIndex] != 0 &&
            SourcePayload.HitDistanceBuffer.IsValidIndex(PixelIndex))
        {
            MaximumHitDistance = FMath::Max(
                MaximumHitDistance,
                SourcePayload.HitDistanceBuffer[PixelIndex]);
        }
    }
    return MaximumHitDistance;
}

float FDWCTransparencyComposite::ResolveEditedAlpha(
    const FDWCTransparencyPixelComposeContext& Context,
    const int32 PixelIndex)
{
    const uint8* BaseAlpha = nullptr;
    if (Context.AlphaDomain != nullptr && Context.AlphaDomain->BaseAlpha.IsValidIndex(PixelIndex))
    {
        BaseAlpha = &Context.AlphaDomain->BaseAlpha[PixelIndex];
    }
    else if (Context.SourcePayload != nullptr &&
             Context.SourcePayload->AutoAlphaBuffer.IsValidIndex(PixelIndex))
    {
        BaseAlpha = &Context.SourcePayload->AutoAlphaBuffer[PixelIndex];
    }
    if (BaseAlpha == nullptr)
    {
        return 0.0f;
    }
    const float AutoAlpha = *BaseAlpha / 255.0f;
    const float ManualPremultiplied = Context.AlphaSnapshotView != nullptr
        ? Context.AlphaSnapshotView->GetPremultiplied(PixelIndex) / 255.0f
        : Context.ManualAlphaTileStore != nullptr
        ? Context.ManualAlphaTileStore->GetPremultiplied(PixelIndex) / 255.0f
        : Context.ManualPremultipliedBuffer.IsValidIndex(PixelIndex)
            ? Context.ManualPremultipliedBuffer[PixelIndex] / 255.0f
            : 0.0f;
    const float ManualWeight = Context.AlphaSnapshotView != nullptr
        ? Context.AlphaSnapshotView->GetWeight(PixelIndex) / 255.0f
        : Context.ManualAlphaTileStore != nullptr
        ? Context.ManualAlphaTileStore->GetWeight(PixelIndex) / 255.0f
        : Context.ManualWeightBuffer.IsValidIndex(PixelIndex)
            ? Context.ManualWeightBuffer[PixelIndex] / 255.0f
            : 0.0f;
    return FMath::Clamp(AutoAlpha * (1.0f - ManualWeight) + ManualPremultiplied, 0.0f, 1.0f);
}

FColor FDWCTransparencyComposite::ComposeVisualizationPixel(
    const FDWCTransparencyPixelComposeContext& Context,
    const int32 PixelIndex,
    const TOptional<float> EditedAlphaOverride,
    const TOptional<FColor> RevealColorOverride,
    const TOptional<uint8> WrinkleSuppressionOverride,
    const TOptional<uint8> OuterEdgeFeatherOverride)
{
    if (!Context.IsValid() || !Context.SourcePayload->InnerColorBuffer.IsValidIndex(PixelIndex))
    {
        return FColor::Black;
    }

    const FDWCTransparencySourcePayload& Result = *Context.SourcePayload;
    const float EditedAlpha = EditedAlphaOverride.IsSet()
        ? FMath::Clamp(EditedAlphaOverride.GetValue(), 0.0f, 1.0f)
        : ResolveEditedAlpha(Context, PixelIndex);
    const bool bUseDynamicFinalComposition =
        Context.VisualizationMode == EDWCTransparencyVisualizationMode::Final &&
        !Result.bIsFinalBakedBaseline;
    const uint8 Alpha = Result.bIsFinalBakedBaseline || bUseDynamicFinalComposition ||
        Context.bDeferPresentationToMaterial
        ? static_cast<uint8>(FMath::RoundToInt(EditedAlpha * 255.0f))
        : ResolveFinalAlpha8(
            EditedAlpha,
            Context.TransparencyStrength,
            WrinkleSuppressionOverride.IsSet()
                ? WrinkleSuppressionOverride.GetValue()
                : Context.WrinkleSuppressionBuffer.IsValidIndex(PixelIndex)
                ? Context.WrinkleSuppressionBuffer[PixelIndex]
                : 0,
            Context.WrinkleSuppressionStrength);
    const uint8 FeatheredAlpha = !Result.bIsFinalBakedBaseline &&
        (OuterEdgeFeatherOverride.IsSet() || Context.OuterEdgeFeatherBuffer.IsValidIndex(PixelIndex))
        ? static_cast<uint8>(
            (static_cast<uint32>(Alpha) *
             (OuterEdgeFeatherOverride.IsSet()
                 ? OuterEdgeFeatherOverride.GetValue()
                 : Context.OuterEdgeFeatherBuffer[PixelIndex]) + 127u) / 255u)
        : Alpha;

    FColor Pixel = RevealColorOverride.IsSet()
        ? RevealColorOverride.GetValue()
        : Context.RevealColorTileStore != nullptr
        ? Context.RevealColorTileStore->GetColor(PixelIndex, MakeArrayView(Result.InnerColorBuffer))
        : Context.RevealColorBuffer.IsValidIndex(PixelIndex)
        ? Context.RevealColorBuffer[PixelIndex]
        : Result.InnerColorBuffer[PixelIndex];
    Pixel = ApplyRevealMetallicDarkening(
        Pixel,
        Result,
        PixelIndex,
        Context.RevealMetallicDarkeningStrength);
    Pixel.A = FeatheredAlpha;
    switch (Context.VisualizationMode)
    {
    case EDWCTransparencyVisualizationMode::BaseRevealColor:
        Pixel = Result.InnerColorBuffer[PixelIndex];
        Pixel.A = 255;
        break;
    case EDWCTransparencyVisualizationMode::InnerColor:
        Pixel.A = 255;
        break;
    case EDWCTransparencyVisualizationMode::CorrectionDifference:
    {
        const FColor BaseColor = Result.InnerColorBuffer[PixelIndex];
        Pixel = FColor(
            FMath::Min(255, FMath::Abs(static_cast<int32>(Pixel.R) - BaseColor.R) * 2),
            FMath::Min(255, FMath::Abs(static_cast<int32>(Pixel.G) - BaseColor.G) * 2),
            FMath::Min(255, FMath::Abs(static_cast<int32>(Pixel.B) - BaseColor.B) * 2),
            255);
        break;
    }
    case EDWCTransparencyVisualizationMode::AutoAlpha:
        if (!Context.bDeferPresentationToMaterial)
        {
            Pixel = FColor(FeatheredAlpha, FeatheredAlpha, FeatheredAlpha, FeatheredAlpha);
        }
        break;
    case EDWCTransparencyVisualizationMode::WrinkleSeparation:
    {
        if (!Context.bDeferPresentationToMaterial)
        {
            const uint8 Separation = WrinkleSuppressionOverride.IsSet()
                ? WrinkleSuppressionOverride.GetValue()
                : Context.WrinkleSuppressionBuffer.IsValidIndex(PixelIndex)
                ? Context.WrinkleSuppressionBuffer[PixelIndex]
                : 0;
            Pixel = FColor(Separation, Separation, Separation, 255);
        }
        break;
    }
    case EDWCTransparencyVisualizationMode::ValidHit:
        Pixel = Result.ValidHitBuffer.IsValidIndex(PixelIndex) && Result.ValidHitBuffer[PixelIndex] != 0
            ? FColor(70, 210, 95, 255)
            : FColor(25, 25, 25, 255);
        break;
    case EDWCTransparencyVisualizationMode::RaycastGaps:
    {
        const bool bCovered = Result.OuterCoverageBuffer.IsValidIndex(PixelIndex) &&
            Result.OuterCoverageBuffer[PixelIndex] != 0;
        const bool bValidHit = Result.ValidHitBuffer.IsValidIndex(PixelIndex) &&
            Result.ValidHitBuffer[PixelIndex] != 0;
        Pixel = bCovered && !bValidHit
            ? FColor(255, 185, 0, 255)
            : FColor(24, 24, 24, 255);
        break;
    }
    case EDWCTransparencyVisualizationMode::HitDistance:
    {
        const float Distance = Result.HitDistanceBuffer.IsValidIndex(PixelIndex)
            ? Result.HitDistanceBuffer[PixelIndex]
            : 0.0f;
        const uint8 Value = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(
            Distance / FMath::Max(Context.MaximumHitDistance, KINDA_SMALL_NUMBER),
            0.0f,
            1.0f) * 255.0f));
        Pixel = FColor(Value, 32, 255 - Value, 255);
        break;
    }
    case EDWCTransparencyVisualizationMode::SourcePriority:
    {
        const int32 Priority = Result.SourcePriorityBuffer.IsValidIndex(PixelIndex)
            ? Result.SourcePriorityBuffer[PixelIndex]
            : INDEX_NONE;
        Pixel = Priority >= 0
            ? TransparencyPriorityColors[Priority % UE_ARRAY_COUNT(TransparencyPriorityColors)]
            : FColor(20, 20, 20, 255);
        break;
    }
    default:
        break;
    }
    return Pixel;
}

bool FDWCTransparencyComposite::ComposeVisualizationPixels(
    const FDWCTransparencyPixelComposeContext& Context,
    TArray<FColor>& OutPixels,
    const FDWCEditorCancellationToken* CancellationToken)
{
    OutPixels.Reset();
    if (!Context.IsValid())
    {
        return false;
    }
    const int32 PixelCount = Context.SourcePayload->Resolution.X * Context.SourcePayload->Resolution.Y;
    OutPixels.SetNumUninitialized(PixelCount);
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        if ((PixelIndex & 4095) == 0 && CancellationToken != nullptr && CancellationToken->IsCanceled())
        {
            OutPixels.Reset();
            return false;
        }
        OutPixels[PixelIndex] = ComposeVisualizationPixel(Context, PixelIndex);
    }
    return true;
}

float FDWCTransparencyComposite::ResolveFinalAlpha(
    const float EditedAlpha,
    const float TransparencyStrength,
    const float WrinkleSuppression,
    const float WrinkleSuppressionStrength)
{
    const float Suppression = FMath::Clamp(
        WrinkleSuppression * FMath::Max(WrinkleSuppressionStrength, 0.0f),
        0.0f,
        1.0f);
    return FMath::Clamp(
        FMath::Clamp(EditedAlpha, 0.0f, 1.0f) *
        FMath::Max(TransparencyStrength, 0.0f) *
        (1.0f - Suppression),
        0.0f,
        1.0f);
}

uint8 FDWCTransparencyComposite::ResolveFinalAlpha8(
    const float EditedAlpha,
    const float TransparencyStrength,
    const uint8 WrinkleSuppression,
    const float WrinkleSuppressionStrength)
{
    return static_cast<uint8>(FMath::RoundToInt(
        ResolveFinalAlpha(
            EditedAlpha,
            TransparencyStrength,
            WrinkleSuppression / 255.0f,
            WrinkleSuppressionStrength) * 255.0f));
}

bool FDWCTransparencyComposite::BuildCoverageEdgeFeatherBuffer(
    const FIntPoint Resolution,
    const TArray<uint8>& OuterCoverage,
    const float FeatherPixels,
    TArray<uint8>& OutBuffer)
{
    const int32 PixelCount = Resolution.X * Resolution.Y;
    if (Resolution.X <= 0 || Resolution.Y <= 0 || OuterCoverage.Num() != PixelCount)
    {
        OutBuffer.Reset();
        return false;
    }

    OutBuffer.Init(255, PixelCount);
    const int32 FeatherSteps = FMath::CeilToInt(FMath::Max(FeatherPixels, 0.0f));
    if (FeatherSteps <= 0)
    {
        return true;
    }

    // The old erosion loop rescanned the full image once for every feather pixel.
    // A two-pass Manhattan distance transform gives the same four-neighbor edge
    // distance in O(width * height), regardless of the configured feather radius.
    constexpr uint16 InfiniteDistance = MAX_uint16;
    TArray<uint16> DistanceToCoverageEdge;
    DistanceToCoverageEdge.Init(InfiniteDistance, PixelCount);

    for (int32 Y = 0; Y < Resolution.Y; ++Y)
    {
        for (int32 X = 0; X < Resolution.X; ++X)
        {
            const int32 PixelIndex = Y * Resolution.X + X;
            if (OuterCoverage[PixelIndex] == 0)
            {
                DistanceToCoverageEdge[PixelIndex] = 0;
                continue;
            }

            // The virtual uncovered texels immediately outside the texture are
            // distance zero, so covered border texels begin at distance one.
            if (X == 0 || Y == 0 || X == Resolution.X - 1 || Y == Resolution.Y - 1)
            {
                DistanceToCoverageEdge[PixelIndex] = 1;
            }
        }
    }

    auto RelaxDistance = [&DistanceToCoverageEdge](const int32 PixelIndex, const int32 NeighborIndex)
    {
        const uint16 NeighborDistance = DistanceToCoverageEdge[NeighborIndex];
        if (NeighborDistance < InfiniteDistance)
        {
            DistanceToCoverageEdge[PixelIndex] = FMath::Min<uint16>(
                DistanceToCoverageEdge[PixelIndex],
                static_cast<uint16>(NeighborDistance + 1));
        }
    };

    for (int32 Y = 0; Y < Resolution.Y; ++Y)
    {
        for (int32 X = 0; X < Resolution.X; ++X)
        {
            const int32 PixelIndex = Y * Resolution.X + X;
            if (OuterCoverage[PixelIndex] == 0)
            {
                continue;
            }
            if (X > 0)
            {
                RelaxDistance(PixelIndex, PixelIndex - 1);
            }
            if (Y > 0)
            {
                RelaxDistance(PixelIndex, PixelIndex - Resolution.X);
            }
        }
    }

    for (int32 Y = Resolution.Y - 1; Y >= 0; --Y)
    {
        for (int32 X = Resolution.X - 1; X >= 0; --X)
        {
            const int32 PixelIndex = Y * Resolution.X + X;
            if (OuterCoverage[PixelIndex] == 0)
            {
                continue;
            }
            if (X + 1 < Resolution.X)
            {
                RelaxDistance(PixelIndex, PixelIndex + 1);
            }
            if (Y + 1 < Resolution.Y)
            {
                RelaxDistance(PixelIndex, PixelIndex + Resolution.X);
            }
        }
    }

    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const uint16 Distance = DistanceToCoverageEdge[PixelIndex];
        if (OuterCoverage[PixelIndex] == 0 || Distance == InfiniteDistance || Distance > FeatherSteps)
        {
            continue;
        }

        OutBuffer[PixelIndex] = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(
            static_cast<float>(Distance) / (static_cast<float>(FeatherSteps) + 1.0f),
            0.0f,
            1.0f) * 255.0f));
    }
    return true;
}
