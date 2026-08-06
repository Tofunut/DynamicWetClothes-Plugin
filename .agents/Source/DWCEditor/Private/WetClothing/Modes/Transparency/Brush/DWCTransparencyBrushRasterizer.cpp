#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"

#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"

namespace
{
    int32 WrapIndex(const int32 Value, const int32 Size)
    {
        return (Value % Size + Size) % Size;
    }

    float ResolveEditedAlphaInternal(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const TArray<uint8>& ManualPremultipliedBuffer,
        const TArray<uint8>& ManualWeightBuffer,
        const int32 PixelIndex)
    {
        const float AutoAlpha = AutoResult.AutoAlphaBuffer.IsValidIndex(PixelIndex)
            ? AutoResult.AutoAlphaBuffer[PixelIndex] / 255.0f
            : 0.0f;
        const float ManualPremultiplied = ManualPremultipliedBuffer.IsValidIndex(PixelIndex)
            ? ManualPremultipliedBuffer[PixelIndex] / 255.0f
            : 0.0f;
        const float ManualWeight = ManualWeightBuffer.IsValidIndex(PixelIndex)
            ? ManualWeightBuffer[PixelIndex] / 255.0f
            : 0.0f;
        return FMath::Clamp(AutoAlpha * (1.0f - ManualWeight) + ManualPremultiplied, 0.0f, 1.0f);
    }

    bool PassesIslandClip(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const int32 PixelIndex,
        const int32 UVIslandID)
    {
        if (UVIslandID == INDEX_NONE)
        {
            return true;
        }
        return AutoResult.OuterIslandIDBuffer.IsValidIndex(PixelIndex) &&
            FDWCTransparencyAutoBakeResult::MatchesOuterIslandID(
                AutoResult.OuterIslandIDBuffer[PixelIndex],
                UVIslandID);
    }

    void ApplySample(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const FDWCTransparencyBrushStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample,
        TArray<uint8>& ManualPremultipliedBuffer,
        TArray<uint8>& ManualWeightBuffer)
    {
        const int32 Width = AutoResult.Resolution.X;
        const int32 Height = AutoResult.Resolution.Y;
        if (Width <= 0 || Height <= 0)
        {
            return;
        }

        const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
        const float RadiusPixelsX = FMath::Max(Sample.RadiusUV * Width, 1.0f);
        const float RadiusPixelsY = FMath::Max(Sample.RadiusUV * Height, 1.0f);
        const FVector2D CenterPixels(Sample.PositionUV.X * Width, Sample.PositionUV.Y * Height);
        const int32 MinX = FMath::FloorToInt(CenterPixels.X - RadiusPixelsX - 1.0f);
        const int32 MaxX = FMath::CeilToInt(CenterPixels.X + RadiusPixelsX + 1.0f);
        const int32 MinY = FMath::FloorToInt(CenterPixels.Y - RadiusPixelsY - 1.0f);
        const int32 MaxY = FMath::CeilToInt(CenterPixels.Y + RadiusPixelsY + 1.0f);
        const int32 ClipUVIslandID = AutoResult.ResolveOuterIslandIDAtUV(
            Sample.PositionUV,
            Sample.UVIslandID,
            bWrap);
        TArray<uint8> SmoothPremultipliedSnapshot;
        TArray<uint8> SmoothWeightSnapshot;
        if (Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth)
        {
            SmoothPremultipliedSnapshot = ManualPremultipliedBuffer;
            SmoothWeightSnapshot = ManualWeightBuffer;
        }

        for (int32 UnwrappedY = MinY; UnwrappedY <= MaxY; ++UnwrappedY)
        {
            for (int32 UnwrappedX = MinX; UnwrappedX <= MaxX; ++UnwrappedX)
            {
                if (!bWrap && (UnwrappedX < 0 || UnwrappedX >= Width || UnwrappedY < 0 || UnwrappedY >= Height))
                {
                    continue;
                }

                const float DX = (UnwrappedX + 0.5f - CenterPixels.X) / RadiusPixelsX;
                const float DY = (UnwrappedY + 0.5f - CenterPixels.Y) / RadiusPixelsY;
                const float Distance = FMath::Sqrt(DX * DX + DY * DY);
                if (Distance > 1.0f)
                {
                    continue;
                }

                const float InnerRadius = 1.0f - FMath::Clamp(Stroke.Falloff, 0.0f, 1.0f);
                const float RadialWeight = Distance <= InnerRadius || Stroke.Falloff <= KINDA_SMALL_NUMBER
                    ? 1.0f
                    : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
                const float BrushWeight = FMath::Clamp(RadialWeight * Sample.Strength, 0.0f, 1.0f);
                if (BrushWeight <= 0.0f)
                {
                    continue;
                }

                const int32 X = bWrap ? WrapIndex(UnwrappedX, Width) : UnwrappedX;
                const int32 Y = bWrap ? WrapIndex(UnwrappedY, Height) : UnwrappedY;
                const int32 PixelIndex = Y * Width + X;
                if (!PassesIslandClip(AutoResult, PixelIndex, ClipUVIslandID))
                {
                    continue;
                }

                const float OldPremultiplied = ManualPremultipliedBuffer[PixelIndex] / 255.0f;
                const float OldWeight = ManualWeightBuffer[PixelIndex] / 255.0f;
                float NewPremultiplied = OldPremultiplied;
                float NewWeight = OldWeight;

                if (Stroke.BrushMode == EDWCTransparencyBrushMode::ResetToAuto)
                {
                    NewPremultiplied *= 1.0f - BrushWeight;
                    NewWeight *= 1.0f - BrushWeight;
                }
                else
                {
                    float Target = Stroke.TargetAlpha;
                    if (Stroke.BrushMode == EDWCTransparencyBrushMode::Apply)
                    {
                        Target = 1.0f;
                    }
                    else if (Stroke.BrushMode == EDWCTransparencyBrushMode::Erase)
                    {
                        Target = 0.0f;
                    }
                    else if (Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth)
                    {
                        Target = 0.0f;
                        int32 SmoothSampleCount = 0;
                        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                        {
                            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                            {
                                int32 SampleX = X + OffsetX;
                                int32 SampleY = Y + OffsetY;
                                if (bWrap)
                                {
                                    SampleX = WrapIndex(SampleX, Width);
                                    SampleY = WrapIndex(SampleY, Height);
                                }
                                else
                                {
                                    SampleX = FMath::Clamp(SampleX, 0, Width - 1);
                                    SampleY = FMath::Clamp(SampleY, 0, Height - 1);
                                }
                                const int32 NeighborIndex = SampleY * Width + SampleX;
                                if (PassesIslandClip(AutoResult, NeighborIndex, ClipUVIslandID))
                                {
                                    Target += ResolveEditedAlphaInternal(
                                        AutoResult,
                                        SmoothPremultipliedSnapshot,
                                        SmoothWeightSnapshot,
                                        NeighborIndex);
                                    ++SmoothSampleCount;
                                }
                            }
                        }
                        Target = SmoothSampleCount > 0
                            ? Target / static_cast<float>(SmoothSampleCount)
                            : ResolveEditedAlphaInternal(
                                AutoResult,
                                SmoothPremultipliedSnapshot,
                                SmoothWeightSnapshot,
                                PixelIndex);
                    }

                    NewPremultiplied = Target * BrushWeight + OldPremultiplied * (1.0f - BrushWeight);
                    NewWeight = BrushWeight + OldWeight * (1.0f - BrushWeight);
                }

                ManualPremultipliedBuffer[PixelIndex] =
                    static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(NewPremultiplied, 0.0f, 1.0f) * 255.0f));
                ManualWeightBuffer[PixelIndex] =
                    static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(NewWeight, 0.0f, 1.0f) * 255.0f));
            }
        }
    }
}

void FDWCTransparencyBrushRasterizer::BuildSampleRegions(
    const FDWCTransparencyBrushSample& Sample,
    const FIntPoint Resolution,
    const EDWCTransparencyUVAddressMode AddressMode,
    TArray<FIntRect>& OutRegions)
{
    OutRegions.Reset();
    if (Resolution.X <= 0 || Resolution.Y <= 0)
    {
        return;
    }
    const float RadiusX = FMath::Max(Sample.RadiusUV * Resolution.X, 1.0f);
    const float RadiusY = FMath::Max(Sample.RadiusUV * Resolution.Y, 1.0f);
    const FVector2D Center(Sample.PositionUV.X * Resolution.X, Sample.PositionUV.Y * Resolution.Y);
    FDWCEditorDirtyRegionSet DirtyRegions;
    DirtyRegions.Add(
        FIntRect(
            FMath::FloorToInt(Center.X - RadiusX - 1.0f),
            FMath::FloorToInt(Center.Y - RadiusY - 1.0f),
            FMath::CeilToInt(Center.X + RadiusX + 1.0f) + 1,
            FMath::CeilToInt(Center.Y + RadiusY + 1.0f) + 1),
        Resolution,
        AddressMode == EDWCTransparencyUVAddressMode::Wrap);
    OutRegions = DirtyRegions.GetRegions();
}

bool FDWCTransparencyBrushRasterizer::RasterizeSamplesToTiles(
    const FDWCTransparencyAutoBakeResult& AutoResult,
    const FDWCTransparencyBrushStroke& Stroke,
    const TArray<FDWCTransparencyBrushSample>& Samples,
    const TArray<FIntPoint>& OutputTileCoordinates,
    TArray<FDWCTransparencyAlphaTilePayload>& InOutTilePayloads)
{
    const int32 Width = AutoResult.Resolution.X;
    const int32 Height = AutoResult.Resolution.Y;
    if (Width <= 0 || Height <= 0 || Samples.IsEmpty() || OutputTileCoordinates.IsEmpty())
    {
        return false;
    }

    FDWCTransparencyAlphaTileStore WorkingStore;
    WorkingStore.Initialize(AutoResult.Resolution);
    if (!WorkingStore.Commit(WorkingStore.GetRevision(), InOutTilePayloads))
    {
        return false;
    }

    TSet<FIntPoint> OutputTiles;
    OutputTiles.Append(OutputTileCoordinates);
    const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    bool bChanged = false;
    for (const FDWCTransparencyBrushSample& Sample : Samples)
    {
        TOptional<FDWCTransparencyAlphaTileStore> SmoothSource;
        if (Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth)
        {
            SmoothSource.Emplace(WorkingStore);
        }
        const float RadiusX = FMath::Max(Sample.RadiusUV * Width, 1.0f);
        const float RadiusY = FMath::Max(Sample.RadiusUV * Height, 1.0f);
        const FVector2D Center(Sample.PositionUV.X * Width, Sample.PositionUV.Y * Height);
        const int32 MinX = FMath::FloorToInt(Center.X - RadiusX - 1.0f);
        const int32 MaxX = FMath::CeilToInt(Center.X + RadiusX + 1.0f);
        const int32 MinY = FMath::FloorToInt(Center.Y - RadiusY - 1.0f);
        const int32 MaxY = FMath::CeilToInt(Center.Y + RadiusY + 1.0f);
        const int32 IslandID = AutoResult.ResolveOuterIslandIDAtUV(
            Sample.PositionUV,
            Sample.UVIslandID,
            bWrap);
        const float InnerRadius = 1.0f - FMath::Clamp(Stroke.Falloff, 0.0f, 1.0f);

        for (int32 RawY = MinY; RawY <= MaxY; ++RawY)
        {
            for (int32 RawX = MinX; RawX <= MaxX; ++RawX)
            {
                if (!bWrap && (RawX < 0 || RawY < 0 || RawX >= Width || RawY >= Height))
                {
                    continue;
                }
                const float DX = (RawX + 0.5f - Center.X) / RadiusX;
                const float DY = (RawY + 0.5f - Center.Y) / RadiusY;
                const float Distance = FMath::Sqrt(DX * DX + DY * DY);
                if (Distance > 1.0f)
                {
                    continue;
                }
                const int32 X = bWrap ? WrapIndex(RawX, Width) : RawX;
                const int32 Y = bWrap ? WrapIndex(RawY, Height) : RawY;
                if (!OutputTiles.Contains(FIntPoint(
                        X / FDWCTransparencyAlphaTileStore::TileSize,
                        Y / FDWCTransparencyAlphaTileStore::TileSize)))
                {
                    continue;
                }
                const int32 PixelIndex = Y * Width + X;
                if (!PassesIslandClip(AutoResult, PixelIndex, IslandID))
                {
                    continue;
                }
                const float RadialWeight = Distance <= InnerRadius || Stroke.Falloff <= KINDA_SMALL_NUMBER
                    ? 1.0f
                    : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
                const float BrushWeight = FMath::Clamp(RadialWeight * Sample.Strength, 0.0f, 1.0f);
                if (BrushWeight <= 0.0f)
                {
                    continue;
                }

                const float OldPremultiplied = WorkingStore.GetPremultiplied(PixelIndex) / 255.0f;
                const float OldWeight = WorkingStore.GetWeight(PixelIndex) / 255.0f;
                float NewPremultiplied = OldPremultiplied;
                float NewWeight = OldWeight;
                if (Stroke.BrushMode == EDWCTransparencyBrushMode::ResetToAuto)
                {
                    NewPremultiplied *= 1.0f - BrushWeight;
                    NewWeight *= 1.0f - BrushWeight;
                }
                else
                {
                    float Target = Stroke.TargetAlpha;
                    if (Stroke.BrushMode == EDWCTransparencyBrushMode::Apply)
                    {
                        Target = 1.0f;
                    }
                    else if (Stroke.BrushMode == EDWCTransparencyBrushMode::Erase)
                    {
                        Target = 0.0f;
                    }
                    else if (Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth)
                    {
                        Target = 0.0f;
                        int32 Count = 0;
                        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                        {
                            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                            {
                                int32 NeighborX = RawX + OffsetX;
                                int32 NeighborY = RawY + OffsetY;
                                if (bWrap)
                                {
                                    NeighborX = WrapIndex(NeighborX, Width);
                                    NeighborY = WrapIndex(NeighborY, Height);
                                }
                                else
                                {
                                    NeighborX = FMath::Clamp(NeighborX, 0, Width - 1);
                                    NeighborY = FMath::Clamp(NeighborY, 0, Height - 1);
                                }
                                const int32 NeighborIndex = NeighborY * Width + NeighborX;
                                if (PassesIslandClip(AutoResult, NeighborIndex, IslandID))
                                {
                                    Target += ResolveEditedAlpha(AutoResult, SmoothSource.GetValue(), NeighborIndex);
                                    ++Count;
                                }
                            }
                        }
                        Target = Count > 0
                            ? Target / static_cast<float>(Count)
                            : ResolveEditedAlpha(AutoResult, SmoothSource.GetValue(), PixelIndex);
                    }
                    NewPremultiplied = Target * BrushWeight + OldPremultiplied * (1.0f - BrushWeight);
                    NewWeight = BrushWeight + OldWeight * (1.0f - BrushWeight);
                }
                WorkingStore.SetPixel(
                    X,
                    Y,
                    static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(NewPremultiplied, 0.0f, 1.0f) * 255.0f)),
                    static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(NewWeight, 0.0f, 1.0f) * 255.0f)));
                bChanged = true;
            }
        }
    }
    if (bChanged)
    {
        WorkingStore.SnapshotTiles(OutputTileCoordinates, InOutTilePayloads);
    }
    return bChanged;
}

bool FDWCTransparencyBrushRasterizer::RasterizeRevealColorSamplesToTiles(
    const FDWCTransparencyAutoBakeResult& AutoResult,
    const FDWCTransparencyRevealColorStroke& Stroke,
    const TArray<FDWCTransparencyBrushSample>& Samples,
    const FLinearColor& BaseRevealColor,
    const TArray<FIntPoint>& OutputTileCoordinates,
    TArray<FDWCTransparencyRevealColorTilePayload>& InOutTilePayloads)
{
    const int32 Width = AutoResult.Resolution.X;
    const int32 Height = AutoResult.Resolution.Y;
    if (Width <= 0 || Height <= 0 || AutoResult.InnerColorBuffer.Num() != Width * Height ||
        Samples.IsEmpty() || OutputTileCoordinates.IsEmpty())
    {
        return false;
    }

    FDWCTransparencyRevealColorTileStore WorkingStore;
    WorkingStore.Initialize(AutoResult.Resolution);
    if (!WorkingStore.Commit(
            WorkingStore.GetRevision(),
            InOutTilePayloads,
            MakeArrayView(AutoResult.InnerColorBuffer)))
    {
        return false;
    }

    TSet<FIntPoint> OutputTiles;
    OutputTiles.Append(OutputTileCoordinates);
    const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    const FLinearColor BaseColor = BaseRevealColor.CopyWithNewOpacity(1.0f);
    const FLinearColor PaintColor = Stroke.PaintColor.CopyWithNewOpacity(1.0f);
    bool bChanged = false;
    for (const FDWCTransparencyBrushSample& Sample : Samples)
    {
        TOptional<FDWCTransparencyRevealColorTileStore> SmoothSource;
        if (Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::Smooth)
        {
            SmoothSource.Emplace(WorkingStore);
        }
        const float RadiusX = FMath::Max(Sample.RadiusUV * Width, 1.0f);
        const float RadiusY = FMath::Max(Sample.RadiusUV * Height, 1.0f);
        const FVector2D Center(Sample.PositionUV.X * Width, Sample.PositionUV.Y * Height);
        const int32 MinX = FMath::FloorToInt(Center.X - RadiusX - 1.0f);
        const int32 MaxX = FMath::CeilToInt(Center.X + RadiusX + 1.0f);
        const int32 MinY = FMath::FloorToInt(Center.Y - RadiusY - 1.0f);
        const int32 MaxY = FMath::CeilToInt(Center.Y + RadiusY + 1.0f);
        const int32 IslandID = AutoResult.ResolveOuterIslandIDAtUV(
            Sample.PositionUV,
            Sample.UVIslandID,
            bWrap);
        const float InnerRadius = 1.0f - FMath::Clamp(Stroke.Falloff, 0.0f, 1.0f);

        for (int32 RawY = MinY; RawY <= MaxY; ++RawY)
        {
            for (int32 RawX = MinX; RawX <= MaxX; ++RawX)
            {
                if (!bWrap && (RawX < 0 || RawY < 0 || RawX >= Width || RawY >= Height))
                {
                    continue;
                }
                const float DX = (RawX + 0.5f - Center.X) / RadiusX;
                const float DY = (RawY + 0.5f - Center.Y) / RadiusY;
                const float Distance = FMath::Sqrt(DX * DX + DY * DY);
                if (Distance > 1.0f)
                {
                    continue;
                }
                const int32 X = bWrap ? WrapIndex(RawX, Width) : RawX;
                const int32 Y = bWrap ? WrapIndex(RawY, Height) : RawY;
                if (!OutputTiles.Contains(FIntPoint(
                        X / FDWCTransparencyRevealColorTileStore::TileSize,
                        Y / FDWCTransparencyRevealColorTileStore::TileSize)))
                {
                    continue;
                }
                const int32 PixelIndex = Y * Width + X;
                if (!AutoResult.OuterCoverageBuffer.IsValidIndex(PixelIndex) ||
                    AutoResult.OuterCoverageBuffer[PixelIndex] == 0 ||
                    !PassesIslandClip(AutoResult, PixelIndex, IslandID))
                {
                    continue;
                }
                const float RadialWeight = Distance <= InnerRadius || Stroke.Falloff <= KINDA_SMALL_NUMBER
                    ? 1.0f
                    : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
                const float Weight = FMath::Clamp(RadialWeight * Sample.Strength, 0.0f, 1.0f);
                if (Weight <= 0.0f)
                {
                    continue;
                }

                FLinearColor TargetColor = PaintColor;
                if (Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::EraseToBase)
                {
                    TargetColor = BaseColor;
                }
                else if (Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::Smooth)
                {
                    TargetColor = FLinearColor::Black;
                    for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                    {
                        for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                        {
                            int32 NeighborX = RawX + OffsetX;
                            int32 NeighborY = RawY + OffsetY;
                            if (bWrap)
                            {
                                NeighborX = WrapIndex(NeighborX, Width);
                                NeighborY = WrapIndex(NeighborY, Height);
                            }
                            else
                            {
                                NeighborX = FMath::Clamp(NeighborX, 0, Width - 1);
                                NeighborY = FMath::Clamp(NeighborY, 0, Height - 1);
                            }
                            const int32 NeighborIndex = NeighborY * Width + NeighborX;
                            TargetColor += PassesIslandClip(AutoResult, NeighborIndex, IslandID)
                                ? FLinearColor(SmoothSource->GetColor(
                                    NeighborIndex,
                                    MakeArrayView(AutoResult.InnerColorBuffer)))
                                : FLinearColor(SmoothSource->GetColor(
                                    PixelIndex,
                                    MakeArrayView(AutoResult.InnerColorBuffer)));
                        }
                    }
                    TargetColor /= 9.0f;
                    TargetColor.A = 1.0f;
                }

                const FColor OldColor = WorkingStore.GetColor(
                    PixelIndex,
                    MakeArrayView(AutoResult.InnerColorBuffer));
                WorkingStore.SetColor(
                    X,
                    Y,
                    FMath::Lerp(
                        FLinearColor(OldColor),
                        TargetColor.CopyWithNewOpacity(1.0f),
                        Weight).ToFColor(true),
                    MakeArrayView(AutoResult.InnerColorBuffer));
                bChanged = true;
            }
        }
    }
    if (bChanged)
    {
        WorkingStore.SnapshotTiles(
            OutputTileCoordinates,
            MakeArrayView(AutoResult.InnerColorBuffer),
            InOutTilePayloads);
    }
    return bChanged;
}

void FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
    const FDWCTransparencyAutoBakeResult& AutoResult,
    const TArray<FDWCTransparencyBrushStroke>& Strokes,
    const int32 BaselineStrokeCount,
    const int32 MaterialSlotIndex,
    const int32 /*UVChannelIndex*/,
    TArray<uint8>& OutManualPremultipliedBuffer,
    TArray<uint8>& OutManualWeightBuffer)
{
    const int32 PixelCount = AutoResult.Resolution.X * AutoResult.Resolution.Y;
    if (PixelCount <= 0)
    {
        return;
    }

    const int32 FirstStrokeIndex = FMath::Clamp(
        BaselineStrokeCount,
        0,
        Strokes.Num());

    bool bHasRelevantStrokeSamples = false;
    for (int32 StrokeIndex = FirstStrokeIndex; StrokeIndex < Strokes.Num(); ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = Strokes[StrokeIndex];
        if (Stroke.bEnabled &&
            Stroke.MaterialSlotIndex == MaterialSlotIndex &&
            !Stroke.Samples.IsEmpty())
        {
            bHasRelevantStrokeSamples = true;
            break;
        }
    }

    if (!bHasRelevantStrokeSamples)
    {
        return;
    }

    OutManualPremultipliedBuffer.Init(0, PixelCount);
    OutManualWeightBuffer.Init(0, PixelCount);
    for (int32 StrokeIndex = FirstStrokeIndex; StrokeIndex < Strokes.Num(); ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = Strokes[StrokeIndex];
        if (!Stroke.bEnabled || Stroke.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
        {
            ApplySample(AutoResult, Stroke, Sample, OutManualPremultipliedBuffer, OutManualWeightBuffer);
        }
    }
}

float FDWCTransparencyBrushRasterizer::ResolveEditedAlpha(
    const FDWCTransparencyAutoBakeResult& AutoResult,
    const TArray<uint8>& ManualPremultipliedBuffer,
    const TArray<uint8>& ManualWeightBuffer,
    const int32 PixelIndex)
{
    return ResolveEditedAlphaInternal(AutoResult, ManualPremultipliedBuffer, ManualWeightBuffer, PixelIndex);
}

float FDWCTransparencyBrushRasterizer::ResolveEditedAlpha(
    const FDWCTransparencyAutoBakeResult& AutoResult,
    const FDWCTransparencyAlphaTileStore& TileStore,
    const int32 PixelIndex)
{
    const float AutoAlpha = AutoResult.AutoAlphaBuffer.IsValidIndex(PixelIndex)
        ? AutoResult.AutoAlphaBuffer[PixelIndex] / 255.0f
        : 0.0f;
    return FMath::Clamp(
        AutoAlpha * (1.0f - TileStore.GetWeight(PixelIndex) / 255.0f) +
        TileStore.GetPremultiplied(PixelIndex) / 255.0f,
        0.0f,
        1.0f);
}
