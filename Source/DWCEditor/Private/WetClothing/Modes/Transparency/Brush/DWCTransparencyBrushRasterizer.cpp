//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"

#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"

namespace
{
    int32 WrapIndex(const int32 Value, const int32 Size)
    {
        return (Value % Size + Size) % Size;
    }

    float ResolveEditedAlphaInternal(
        const FDWCTransparencyAlphaRasterContext& AlphaContext,
        const TArray<uint8>& ManualPremultipliedBuffer,
        const TArray<uint8>& ManualWeightBuffer,
        const int32 PixelIndex)
    {
        const float AutoAlpha = AlphaContext.BaseAlpha.IsValidIndex(PixelIndex)
            ? AlphaContext.BaseAlpha[PixelIndex] / 255.0f
            : 0.0f;
        const float ManualPremultiplied = ManualPremultipliedBuffer.IsValidIndex(PixelIndex)
            ? ManualPremultipliedBuffer[PixelIndex] / 255.0f
            : 0.0f;
        const float ManualWeight = ManualWeightBuffer.IsValidIndex(PixelIndex)
            ? ManualWeightBuffer[PixelIndex] / 255.0f
            : 0.0f;
        return FMath::Clamp(AutoAlpha * (1.0f - ManualWeight) + ManualPremultiplied, 0.0f, 1.0f);
    }

    void ApplySample(
        const FDWCTransparencyAlphaRasterContext& AlphaContext,
        const FDWCTransparencyBrushStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample,
        TArray<uint8>& ManualPremultipliedBuffer,
        TArray<uint8>& ManualWeightBuffer)
    {
        const int32 Width = AlphaContext.Resolution.X;
        const int32 Height = AlphaContext.Resolution.Y;
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
        const int32 ClipUVIslandID = AlphaContext.ResolveOuterIslandIDAtUV(
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
                if (!AlphaContext.PassesIslandClip(PixelIndex, ClipUVIslandID))
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
                                if (AlphaContext.PassesIslandClip(NeighborIndex, ClipUVIslandID))
                                {
                                    Target += ResolveEditedAlphaInternal(
                                        AlphaContext,
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
                                AlphaContext,
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

FDWCTransparencyAlphaRasterContext FDWCTransparencyAlphaRasterContext::FromSourcePayload(
    const FDWCTransparencySourcePayload& SourcePayload)
{
    FDWCTransparencyAlphaRasterContext Result;
    Result.Resolution = SourcePayload.Resolution;
    Result.MaterialSlotIndex = SourcePayload.MaterialSlotIndex;
    Result.BaseAlpha = MakeArrayView(SourcePayload.AutoAlphaBuffer);
    Result.OuterCoverage = MakeArrayView(SourcePayload.OuterCoverageBuffer);
    Result.OuterIslandIDs = MakeArrayView(SourcePayload.OuterIslandIDBuffer);
    return Result;
}

FDWCTransparencyAlphaRasterContext FDWCTransparencyAlphaRasterContext::FromAlphaDomain(
    const FDWCTransparencyAlphaDomainSnapshot& AlphaDomain)
{
    FDWCTransparencyAlphaRasterContext Result;
    Result.Resolution = AlphaDomain.Resolution;
    Result.MaterialSlotIndex = AlphaDomain.MaterialSlotIndex;
    Result.BaseAlpha = MakeArrayView(AlphaDomain.BaseAlpha);
    Result.OuterCoverage = MakeArrayView(AlphaDomain.OuterCoverage);
    Result.OuterIslandIDs = MakeArrayView(AlphaDomain.OuterIslandIDs);
    return Result;
}

bool FDWCTransparencyAlphaRasterContext::IsValid() const
{
    const int64 PixelCount = static_cast<int64>(Resolution.X) * Resolution.Y;
    return Resolution.X > 0 && Resolution.Y > 0 && MaterialSlotIndex != INDEX_NONE &&
        BaseAlpha.Num() == PixelCount && OuterCoverage.Num() == PixelCount &&
        OuterIslandIDs.Num() == PixelCount;
}

int32 FDWCTransparencyAlphaRasterContext::ResolveOuterIslandIDAtUV(
    const FVector2D& PositionUV,
    const int32 FallbackUVIslandID,
    const bool bWrap) const
{
    if (!IsValid())
    {
        return FallbackUVIslandID;
    }
    int32 X = FMath::FloorToInt(PositionUV.X * Resolution.X);
    int32 Y = FMath::FloorToInt(PositionUV.Y * Resolution.Y);
    if (bWrap)
    {
        X = WrapIndex(X, Resolution.X);
        Y = WrapIndex(Y, Resolution.Y);
    }
    else
    {
        X = FMath::Clamp(X, 0, Resolution.X - 1);
        Y = FMath::Clamp(Y, 0, Resolution.Y - 1);
    }
    const int32 PixelIndex = Y * Resolution.X + X;
    return OuterIslandIDs.IsValidIndex(PixelIndex)
        ? FDWCTransparencySourcePayload::DecodeOuterIslandID(OuterIslandIDs[PixelIndex])
        : FallbackUVIslandID;
}

bool FDWCTransparencyAlphaRasterContext::PassesIslandClip(
    const int32 PixelIndex,
    const int32 UVIslandID) const
{
    return OuterCoverage.IsValidIndex(PixelIndex) && OuterCoverage[PixelIndex] != 0 &&
        (UVIslandID == INDEX_NONE ||
         (OuterIslandIDs.IsValidIndex(PixelIndex) &&
          FDWCTransparencySourcePayload::MatchesOuterIslandID(
              OuterIslandIDs[PixelIndex], UVIslandID)));
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
    const FDWCTransparencySourcePayload& SourcePayload,
    const FDWCTransparencyBrushStroke& Stroke,
    const TArray<FDWCTransparencyBrushSample>& Samples,
    const TArray<FIntPoint>& OutputTileCoordinates,
    TArray<FDWCTransparencyAlphaTilePayload>& InOutTilePayloads)
{
    return RasterizeSamplesToTiles(
        FDWCTransparencyAlphaRasterContext::FromSourcePayload(SourcePayload),
        Stroke,
        Samples,
        OutputTileCoordinates,
        InOutTilePayloads);
}

bool FDWCTransparencyBrushRasterizer::RasterizeSamplesToTiles(
    const FDWCTransparencyAlphaRasterContext& AlphaContext,
    const FDWCTransparencyBrushStroke& Stroke,
    const TArray<FDWCTransparencyBrushSample>& Samples,
    const TArray<FIntPoint>& OutputTileCoordinates,
    TArray<FDWCTransparencyAlphaTilePayload>& InOutTilePayloads)
{
    const int32 Width = AlphaContext.Resolution.X;
    const int32 Height = AlphaContext.Resolution.Y;
    if (!AlphaContext.IsValid() || Width <= 0 || Height <= 0 ||
        Samples.IsEmpty() || OutputTileCoordinates.IsEmpty())
    {
        return false;
    }

    FDWCTransparencyAlphaTileStore WorkingStore;
    WorkingStore.Initialize(AlphaContext.Resolution);
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
        const int32 IslandID = AlphaContext.ResolveOuterIslandIDAtUV(
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
                if (!AlphaContext.PassesIslandClip(PixelIndex, IslandID))
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
                                if (AlphaContext.PassesIslandClip(NeighborIndex, IslandID))
                                {
                                    Target += ResolveEditedAlpha(AlphaContext, SmoothSource.GetValue(), NeighborIndex);
                                    ++Count;
                                }
                            }
                        }
                        Target = Count > 0
                            ? Target / static_cast<float>(Count)
                            : ResolveEditedAlpha(AlphaContext, SmoothSource.GetValue(), PixelIndex);
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
    const FDWCTransparencySourcePayload& SourcePayload,
    const FDWCTransparencyRevealColorStroke& Stroke,
    const TArray<FDWCTransparencyBrushSample>& Samples,
    const FLinearColor& BaseRevealColor,
    const TArray<FIntPoint>& OutputTileCoordinates,
    TArray<FDWCTransparencyRevealColorTilePayload>& InOutTilePayloads)
{
    const FDWCTransparencyAlphaRasterContext SurfaceContext =
        FDWCTransparencyAlphaRasterContext::FromSourcePayload(SourcePayload);
    const int32 Width = SourcePayload.Resolution.X;
    const int32 Height = SourcePayload.Resolution.Y;
    if (Width <= 0 || Height <= 0 || SourcePayload.InnerColorBuffer.Num() != Width * Height ||
        Samples.IsEmpty() || OutputTileCoordinates.IsEmpty())
    {
        return false;
    }

    FDWCTransparencyRevealColorTileStore WorkingStore;
    WorkingStore.Initialize(SourcePayload.Resolution);
    if (!WorkingStore.Commit(
            WorkingStore.GetRevision(),
            InOutTilePayloads,
            MakeArrayView(SourcePayload.InnerColorBuffer)))
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
        const int32 IslandID = SourcePayload.ResolveOuterIslandIDAtUV(
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
                if (!SourcePayload.OuterCoverageBuffer.IsValidIndex(PixelIndex) ||
                    SourcePayload.OuterCoverageBuffer[PixelIndex] == 0 ||
                    !SurfaceContext.PassesIslandClip(PixelIndex, IslandID))
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
                            TargetColor += SurfaceContext.PassesIslandClip(NeighborIndex, IslandID)
                                ? FLinearColor(SmoothSource->GetColor(
                                    NeighborIndex,
                                    MakeArrayView(SourcePayload.InnerColorBuffer)))
                                : FLinearColor(SmoothSource->GetColor(
                                    PixelIndex,
                                    MakeArrayView(SourcePayload.InnerColorBuffer)));
                        }
                    }
                    TargetColor /= 9.0f;
                    TargetColor.A = 1.0f;
                }

                const FColor OldColor = WorkingStore.GetColor(
                    PixelIndex,
                    MakeArrayView(SourcePayload.InnerColorBuffer));
                WorkingStore.SetColor(
                    X,
                    Y,
                    FMath::Lerp(
                        FLinearColor(OldColor),
                        TargetColor.CopyWithNewOpacity(1.0f),
                        Weight).ToFColor(true),
                    MakeArrayView(SourcePayload.InnerColorBuffer));
                bChanged = true;
            }
        }
    }
    if (bChanged)
    {
        WorkingStore.SnapshotTiles(
            OutputTileCoordinates,
            MakeArrayView(SourcePayload.InnerColorBuffer),
            InOutTilePayloads);
    }
    return bChanged;
}

void FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
    const FDWCTransparencySourcePayload& SourcePayload,
    const TArray<FDWCTransparencyBrushStroke>& Strokes,
    const int32 BaselineStrokeCount,
    const int32 MaterialSlotIndex,
    const int32 /*UVChannelIndex*/,
    TArray<uint8>& OutManualPremultipliedBuffer,
    TArray<uint8>& OutManualWeightBuffer)
{
    RebuildFromStrokes(
        FDWCTransparencyAlphaRasterContext::FromSourcePayload(SourcePayload),
        Strokes,
        BaselineStrokeCount,
        MaterialSlotIndex,
        INDEX_NONE,
        OutManualPremultipliedBuffer,
        OutManualWeightBuffer);
}

void FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
    const FDWCTransparencyAlphaRasterContext& AlphaContext,
    const TArray<FDWCTransparencyBrushStroke>& Strokes,
    const int32 BaselineStrokeCount,
    const int32 MaterialSlotIndex,
    const int32 /*UVChannelIndex*/,
    TArray<uint8>& OutManualPremultipliedBuffer,
    TArray<uint8>& OutManualWeightBuffer)
{
    const int32 PixelCount = AlphaContext.Resolution.X * AlphaContext.Resolution.Y;
    if (!AlphaContext.IsValid() || PixelCount <= 0)
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
            Stroke.HasSamples())
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

        Stroke.ForEachSample(
            [&AlphaContext, &Stroke, &OutManualPremultipliedBuffer, &OutManualWeightBuffer](
                const FDWCTransparencyBrushSample& Sample)
            {
                ApplySample(
                    AlphaContext,
                    Stroke,
                    Sample,
                    OutManualPremultipliedBuffer,
                    OutManualWeightBuffer);
            });
    }
}

float FDWCTransparencyBrushRasterizer::ResolveEditedAlpha(
    const FDWCTransparencySourcePayload& SourcePayload,
    const TArray<uint8>& ManualPremultipliedBuffer,
    const TArray<uint8>& ManualWeightBuffer,
    const int32 PixelIndex)
{
    return ResolveEditedAlphaInternal(
        FDWCTransparencyAlphaRasterContext::FromSourcePayload(SourcePayload),
        ManualPremultipliedBuffer,
        ManualWeightBuffer,
        PixelIndex);
}

float FDWCTransparencyBrushRasterizer::ResolveEditedAlpha(
    const FDWCTransparencySourcePayload& SourcePayload,
    const FDWCTransparencyAlphaTileStore& TileStore,
    const int32 PixelIndex)
{
    return ResolveEditedAlpha(
        FDWCTransparencyAlphaRasterContext::FromSourcePayload(SourcePayload),
        TileStore,
        PixelIndex);
}

float FDWCTransparencyBrushRasterizer::ResolveEditedAlpha(
    const FDWCTransparencyAlphaRasterContext& AlphaContext,
    const FDWCTransparencyAlphaTileStore& TileStore,
    const int32 PixelIndex)
{
    const float AutoAlpha = AlphaContext.BaseAlpha.IsValidIndex(PixelIndex)
        ? AlphaContext.BaseAlpha[PixelIndex] / 255.0f
        : 0.0f;
    return FMath::Clamp(
        AutoAlpha * (1.0f - TileStore.GetWeight(PixelIndex) / 255.0f) +
        TileStore.GetPremultiplied(PixelIndex) / 255.0f,
        0.0f,
        1.0f);
}
