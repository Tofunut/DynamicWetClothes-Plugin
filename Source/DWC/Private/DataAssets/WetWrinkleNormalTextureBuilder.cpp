// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DataAssets/WetWrinkleNormalTextureBuilder.h"

#include "Engine/Texture2D.h"

namespace
{
    void ResizeNormalSourcePixels(
        const TArray<FColor>& SourcePixels,
        const FIntPoint&      SourceSize,
        const FIntPoint&      TargetSize,
        TArray<FColor>&       OutPixels)
    {
        OutPixels.SetNumUninitialized(TargetSize.X * TargetSize.Y);
        for (int32 TargetY = 0; TargetY < TargetSize.Y; ++TargetY)
        {
            const float SourceY = ((static_cast<float>(TargetY) + 0.5f) * SourceSize.Y / TargetSize.Y) - 0.5f;
            const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SourceY), 0, SourceSize.Y - 1);
            const int32 Y1 = FMath::Min(Y0 + 1, SourceSize.Y - 1);
            const float FracY = FMath::Clamp(SourceY - FMath::Floor(SourceY), 0.0f, 1.0f);
            for (int32 TargetX = 0; TargetX < TargetSize.X; ++TargetX)
            {
                const float        SourceX = ((static_cast<float>(TargetX) + 0.5f) * SourceSize.X / TargetSize.X) - 0.5f;
                const int32        X0 = FMath::Clamp(FMath::FloorToInt(SourceX), 0, SourceSize.X - 1);
                const int32        X1 = FMath::Min(X0 + 1, SourceSize.X - 1);
                const float        FracX = FMath::Clamp(SourceX - FMath::Floor(SourceX), 0.0f, 1.0f);
                const FLinearColor Top = FMath::Lerp(
                    SourcePixels[Y0 * SourceSize.X + X0].ReinterpretAsLinear(),
                    SourcePixels[Y0 * SourceSize.X + X1].ReinterpretAsLinear(),
                    FracX);
                const FLinearColor Bottom = FMath::Lerp(
                    SourcePixels[Y1 * SourceSize.X + X0].ReinterpretAsLinear(),
                    SourcePixels[Y1 * SourceSize.X + X1].ReinterpretAsLinear(),
                    FracX);
                OutPixels[TargetY * TargetSize.X + TargetX] = FMath::Lerp(Top, Bottom, FracY).ToFColor(false);
            }
        }
    }

    uint8 FloatToByte(float Value)
    {
        return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255));
    }

    FVector2f GetNormalXY(const FVector3f& Normal)
    {
        return FVector2f(Normal.X, Normal.Y);
    }

    FVector3f DecodeNormalFromColor(const FColor& Color, const bool bFlipGreen)
    {
        FVector3f Normal(
            static_cast<float>(Color.R) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.G) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.B) / 255.0f * 2.0f - 1.0f);

        if (bFlipGreen)
        {
            Normal.Y *= -1.0f;
        }

        return Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
    }

    FColor EncodeNormalToColor(const FVector3f& Normal)
    {
        const FVector3f SafeNormal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
        return FColor(
            FloatToByte(SafeNormal.X * 0.5f + 0.5f),
            FloatToByte(SafeNormal.Y * 0.5f + 0.5f),
            FloatToByte(SafeNormal.Z * 0.5f + 0.5f),
            255);
    }

    FColor ReadSourceColor(
        const TArray64<uint8>&     MipData,
        const ETextureSourceFormat Format,
        const int64                PixelIndex)
    {
        switch (Format)
        {
        case TSF_G8:
        {
            const uint8 Value = MipData[PixelIndex];
            return FColor(Value, Value, Value, 255);
        }

        case TSF_BGRA8:
        {
            const int64 ByteIndex = PixelIndex * 4;
            return FColor(
                MipData[ByteIndex + 2],
                MipData[ByteIndex + 1],
                MipData[ByteIndex + 0],
                MipData[ByteIndex + 3]);
        }

        case TSF_RGBA16:
        {
            const int64  ByteIndex = PixelIndex * 8;
            const uint16 R = static_cast<uint16>(MipData[ByteIndex + 0]) | (static_cast<uint16>(MipData[ByteIndex + 1]) << 8);
            const uint16 G = static_cast<uint16>(MipData[ByteIndex + 2]) | (static_cast<uint16>(MipData[ByteIndex + 3]) << 8);
            const uint16 B = static_cast<uint16>(MipData[ByteIndex + 4]) | (static_cast<uint16>(MipData[ByteIndex + 5]) << 8);
            const uint16 A = static_cast<uint16>(MipData[ByteIndex + 6]) | (static_cast<uint16>(MipData[ByteIndex + 7]) << 8);
            return FColor(
                static_cast<uint8>(R / 257),
                static_cast<uint8>(G / 257),
                static_cast<uint8>(B / 257),
                static_cast<uint8>(A / 257));
        }

        default:
            return FColor(128, 128, 255, 255);
        }
    }

    bool ReadTextureSourcePixelsInternal(
        UTexture2D*     Texture,
        TArray<FColor>& OutPixels,
        FIntPoint&      OutSize,
        FString&        OutError)
    {
        OutPixels.Reset();
        OutSize = FIntPoint::ZeroValue;

        if (Texture == nullptr)
        {
            OutError = TEXT("Wet wrinkle preset source texture is not set.");
            return false;
        }

#if WITH_EDITORONLY_DATA
        if (!Texture->Source.IsValid())
        {
            OutError = FString::Printf(TEXT("Source texture '%s' does not contain editor source data."), *Texture->GetName());
            return false;
        }

        const ETextureSourceFormat SourceFormat = Texture->Source.GetFormat();
        if (SourceFormat != TSF_BGRA8 && SourceFormat != TSF_G8 && SourceFormat != TSF_RGBA16)
        {
            UEnum*        SourceFormatEnum = StaticEnum<ETextureSourceFormat>();
            const FString FormatName = SourceFormatEnum != nullptr
                                           ? SourceFormatEnum->GetNameStringByValue(static_cast<int64>(SourceFormat))
                                           : FString::Printf(TEXT("%d"), static_cast<int32>(SourceFormat));
            OutError = FString::Printf(
                TEXT("Source texture '%s' uses unsupported source format '%s'. Supported formats are BGRA8, G8, and RGBA16."),
                *Texture->GetName(),
                *FormatName);
            return false;
        }

        TArray64<uint8> MipData;
        if (!Texture->Source.GetMipData(MipData, 0))
        {
            OutError = FString::Printf(TEXT("Failed to read source mip data from '%s'."), *Texture->GetName());
            return false;
        }

        OutSize = FIntPoint(IntCastChecked<int32>(Texture->Source.GetSizeX()), IntCastChecked<int32>(Texture->Source.GetSizeY()));
        const int64 PixelCount = static_cast<int64>(OutSize.X) * static_cast<int64>(OutSize.Y);
        if (OutSize.X <= 0 || OutSize.Y <= 0 || PixelCount <= 0)
        {
            OutError = FString::Printf(TEXT("Source texture '%s' has an invalid size."), *Texture->GetName());
            return false;
        }

        const int64 BytesPerPixel = FTextureSource::GetBytesPerPixel(SourceFormat);
        if (MipData.Num() < PixelCount * BytesPerPixel)
        {
            OutError = FString::Printf(TEXT("Source texture '%s' mip data is smaller than expected."), *Texture->GetName());
            return false;
        }

        OutPixels.SetNumUninitialized(static_cast<int32>(PixelCount));
        for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            OutPixels[static_cast<int32>(PixelIndex)] = ReadSourceColor(MipData, SourceFormat, PixelIndex);
        }

        return true;
#else
        OutError = TEXT("Wet wrinkle preset building requires editor texture source data.");
        return false;
#endif
    }

    void BlurVectorField(
        const TArray<FVector2f>& Input,
        const FIntPoint          Size,
        const int32              Radius,
        TArray<FVector2f>&       Output)
    {
        if (Radius <= 0)
        {
            Output = Input;
            return;
        }

        const int32       Width = Size.X;
        const int32       Height = Size.Y;
        TArray<FVector2f> Scratch;
        Scratch.SetNumUninitialized(Input.Num());
        Output.SetNumUninitialized(Input.Num());

        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                FVector2f Sum = FVector2f::ZeroVector;
                int32     Count = 0;
                for (int32 Offset = -Radius; Offset <= Radius; ++Offset)
                {
                    const int32 SampleX = FMath::Clamp(X + Offset, 0, Width - 1);
                    Sum += Input[Y * Width + SampleX];
                    ++Count;
                }
                Scratch[Y * Width + X] = Sum / static_cast<float>(Count);
            }
        }

        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                FVector2f Sum = FVector2f::ZeroVector;
                int32     Count = 0;
                for (int32 Offset = -Radius; Offset <= Radius; ++Offset)
                {
                    const int32 SampleY = FMath::Clamp(Y + Offset, 0, Height - 1);
                    Sum += Scratch[SampleY * Width + X];
                    ++Count;
                }
                Output[Y * Width + X] = Sum / static_cast<float>(Count);
            }
        }
    }

    float FindRobustPositiveScale(const TArray<float>& Values)
    {
        constexpr int32 BinCount = 1024;
        float           Maximum = 0.0f;
        int32           PositiveCount = 0;
        for (const float Value : Values)
        {
            if (Value > UE_SMALL_NUMBER)
            {
                Maximum = FMath::Max(Maximum, Value);
                ++PositiveCount;
            }
        }

        if (PositiveCount == 0 || Maximum <= UE_SMALL_NUMBER)
        {
            return 1.0f;
        }

        TArray<int32> Histogram;
        Histogram.Init(0, BinCount);
        for (const float Value : Values)
        {
            if (Value > UE_SMALL_NUMBER)
            {
                const int32 Bin = FMath::Clamp(FMath::FloorToInt(Value / Maximum * (BinCount - 1)), 0, BinCount - 1);
                ++Histogram[Bin];
            }
        }

        const int32 TargetCount = FMath::Max(1, FMath::CeilToInt(static_cast<float>(PositiveCount) * 0.98f));
        int32       Accumulated = 0;
        for (int32 Bin = 0; Bin < BinCount; ++Bin)
        {
            Accumulated += Histogram[Bin];
            if (Accumulated >= TargetCount)
            {
                return FMath::Max(Maximum * static_cast<float>(Bin + 1) / static_cast<float>(BinCount), UE_SMALL_NUMBER);
            }
        }
        return Maximum;
    }

    void MorphologicalCloseBinary(TArray<uint8>& Mask, const FIntPoint Size)
    {
        const int32   Width = Size.X;
        const int32   Height = Size.Y;
        TArray<uint8> Dilated;
        Dilated.Init(0, Mask.Num());

        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                uint8 Value = 0;
                for (int32 OffsetY = -1; OffsetY <= 1 && Value == 0; ++OffsetY)
                {
                    for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                    {
                        const int32 SampleX = FMath::Clamp(X + OffsetX, 0, Width - 1);
                        const int32 SampleY = FMath::Clamp(Y + OffsetY, 0, Height - 1);
                        if (Mask[SampleY * Width + SampleX] != 0)
                        {
                            Value = 1;
                            break;
                        }
                    }
                }
                Dilated[Y * Width + X] = Value;
            }
        }

        TArray<uint8> Closed;
        Closed.Init(0, Mask.Num());
        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                uint8 Value = 1;
                for (int32 OffsetY = -1; OffsetY <= 1 && Value != 0; ++OffsetY)
                {
                    for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                    {
                        const int32 SampleX = FMath::Clamp(X + OffsetX, 0, Width - 1);
                        const int32 SampleY = FMath::Clamp(Y + OffsetY, 0, Height - 1);
                        if (Dilated[SampleY * Width + SampleX] == 0)
                        {
                            Value = 0;
                            break;
                        }
                    }
                }
                Closed[Y * Width + X] = Value;
            }
        }
        Mask = MoveTemp(Closed);
    }

    void RemoveSmallComponents(TArray<uint8>& Mask, const FIntPoint Size, const int32 MinimumPixels)
    {
        if (MinimumPixels <= 1)
        {
            return;
        }

        const int32   Width = Size.X;
        const int32   Height = Size.Y;
        TArray<uint8> Visited;
        Visited.Init(0, Mask.Num());
        TArray<int32> Queue;
        TArray<int32> Component;

        for (int32 StartIndex = 0; StartIndex < Mask.Num(); ++StartIndex)
        {
            if (Mask[StartIndex] == 0 || Visited[StartIndex] != 0)
            {
                continue;
            }

            Queue.Reset();
            Component.Reset();
            Queue.Add(StartIndex);
            Visited[StartIndex] = 1;
            for (int32 Head = 0; Head < Queue.Num(); ++Head)
            {
                const int32 Index = Queue[Head];
                Component.Add(Index);
                const int32 X = Index % Width;
                const int32 Y = Index / Width;
                for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                {
                    for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                    {
                        if (OffsetX == 0 && OffsetY == 0)
                        {
                            continue;
                        }
                        const int32 NeighborX = X + OffsetX;
                        const int32 NeighborY = Y + OffsetY;
                        if (NeighborX < 0 || NeighborX >= Width || NeighborY < 0 || NeighborY >= Height)
                        {
                            continue;
                        }
                        const int32 NeighborIndex = NeighborY * Width + NeighborX;
                        if (Mask[NeighborIndex] != 0 && Visited[NeighborIndex] == 0)
                        {
                            Visited[NeighborIndex] = 1;
                            Queue.Add(NeighborIndex);
                        }
                    }
                }
            }

            if (Component.Num() < MinimumPixels)
            {
                for (const int32 Index : Component)
                {
                    Mask[Index] = 0;
                }
            }
        }
    }

    bool BuildConvexSeparationFromPixels(
        const TArray<FColor>&                        CorrectedPixels,
        const FIntPoint                              Size,
        const bool                                   bFlipGreenChannel,
        const FWetWrinkleCoverageExtractionSettings& Settings,
        FWetWrinkleTextureScalarBuffer&              OutBuffer,
        FString&                                     OutError)
    {
        OutBuffer = FWetWrinkleTextureScalarBuffer();
        const int32 PixelCount = Size.X * Size.Y;
        if (Size.X <= 0 || Size.Y <= 0 || CorrectedPixels.Num() != PixelCount)
        {
            OutError = TEXT("Corrected normal pixels are unavailable for convex separation extraction.");
            return false;
        }

        TArray<FVector2f> Slopes;
        Slopes.SetNumUninitialized(PixelCount);
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const FColor& Color = CorrectedPixels[PixelIndex];
            const float   NormalX = static_cast<float>(Color.R) / 255.0f * 2.0f - 1.0f;
            float         NormalY = -(static_cast<float>(Color.G) / 255.0f * 2.0f - 1.0f);
            if (bFlipGreenChannel)
            {
                NormalY = -NormalY;
            }
            const float NormalZ = FMath::Max(static_cast<float>(Color.B) / 255.0f * 2.0f - 1.0f, 0.2f);
            Slopes[PixelIndex] = FVector2f(-NormalX / NormalZ, -NormalY / NormalZ);
        }

        TArray<FVector2f> BlurredSlopes;
        BlurVectorField(Slopes, Size, FMath::Clamp(Settings.InputBlurRadiusPixels, 0, 8), BlurredSlopes);

        TArray<float> Convexity;
        Convexity.Init(0.0f, PixelCount);
        for (int32 Y = 0; Y < Size.Y; ++Y)
        {
            for (int32 X = 0; X < Size.X; ++X)
            {
                const int32 LeftX = FMath::Max(X - 1, 0);
                const int32 RightX = FMath::Min(X + 1, Size.X - 1);
                const int32 UpY = FMath::Max(Y - 1, 0);
                const int32 DownY = FMath::Min(Y + 1, Size.Y - 1);
                const float DPdx = (BlurredSlopes[Y * Size.X + RightX].X - BlurredSlopes[Y * Size.X + LeftX].X) * 0.5f;
                const float DQdy = (BlurredSlopes[DownY * Size.X + X].Y - BlurredSlopes[UpY * Size.X + X].Y) * 0.5f;
                const float Laplacian = DPdx + DQdy;
                Convexity[Y * Size.X + X] = FMath::Max(Settings.bInvertConvexity ? Laplacian : -Laplacian, 0.0f);
            }
        }

        const float   RobustScale = FindRobustPositiveScale(Convexity);
        const float   HighThreshold = FMath::Clamp(Settings.ConvexityThreshold, 0.0f, 1.0f);
        const float   LowThreshold = HighThreshold * 0.4f;
        TArray<uint8> CandidateMask;
        TArray<uint8> ResultMask;
        CandidateMask.Init(0, PixelCount);
        ResultMask.Init(0, PixelCount);
        TArray<int32> Queue;
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const float Normalized = FMath::Clamp(Convexity[PixelIndex] / RobustScale, 0.0f, 1.0f);
            CandidateMask[PixelIndex] = Normalized >= LowThreshold ? 1 : 0;
            if (Normalized >= HighThreshold)
            {
                ResultMask[PixelIndex] = 1;
                Queue.Add(PixelIndex);
            }
        }

        for (int32 Head = 0; Head < Queue.Num(); ++Head)
        {
            const int32 Index = Queue[Head];
            const int32 X = Index % Size.X;
            const int32 Y = Index / Size.X;
            for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
            {
                for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                {
                    if (OffsetX == 0 && OffsetY == 0)
                    {
                        continue;
                    }
                    const int32 NeighborX = X + OffsetX;
                    const int32 NeighborY = Y + OffsetY;
                    if (NeighborX < 0 || NeighborX >= Size.X || NeighborY < 0 || NeighborY >= Size.Y)
                    {
                        continue;
                    }
                    const int32 NeighborIndex = NeighborY * Size.X + NeighborX;
                    if (CandidateMask[NeighborIndex] != 0 && ResultMask[NeighborIndex] == 0)
                    {
                        ResultMask[NeighborIndex] = 1;
                        Queue.Add(NeighborIndex);
                    }
                }
            }
        }

        MorphologicalCloseBinary(ResultMask, Size);
        RemoveSmallComponents(ResultMask, Size, FMath::Clamp(Settings.MinimumComponentPixels, 1, 1024));

        OutBuffer.Size = Size;
        OutBuffer.Values.SetNumUninitialized(PixelCount);
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            OutBuffer.Values[PixelIndex] = ResultMask[PixelIndex] != 0 ? 1.0f : 0.0f;
        }
        OutError.Reset();
        return true;
    }

    FVector2f EstimateBorderAverageXY(
        const TArray<FColor>& SourcePixels,
        const FIntPoint&      Size,
        const bool            bFlipGreen,
        const float           BorderPercent)
    {
        const int32 BorderPixels = FMath::Clamp(
            FMath::CeilToInt(static_cast<float>(FMath::Min(Size.X, Size.Y)) * FMath::Clamp(BorderPercent, 0.0f, 50.0f) * 0.01f),
            1,
            FMath::Max(1, FMath::Min(Size.X, Size.Y)));

        FVector2f Sum = FVector2f::ZeroVector;
        int32     SampleCount = 0;
        for (int32 Y = 0; Y < Size.Y; ++Y)
        {
            for (int32 X = 0; X < Size.X; ++X)
            {
                if (X >= BorderPixels && X < Size.X - BorderPixels &&
                    Y >= BorderPixels && Y < Size.Y - BorderPixels)
                {
                    continue;
                }

                const int32 PixelIndex = Y * Size.X + X;
                Sum += GetNormalXY(DecodeNormalFromColor(SourcePixels[PixelIndex], bFlipGreen));
                ++SampleCount;
            }
        }

        return SampleCount > 0 ? Sum / static_cast<float>(SampleCount) : FVector2f::ZeroVector;
    }

    float EstimateBorderNoiseThreshold(
        const TArray<FColor>& SourcePixels,
        const FIntPoint&      Size,
        const bool            bFlipGreen,
        const float           BorderPercent,
        const FVector2f&      BackgroundAverageXY,
        const float           UserFlatThreshold)
    {
        const int32 BorderPixels = FMath::Clamp(
            FMath::CeilToInt(static_cast<float>(FMath::Min(Size.X, Size.Y)) * FMath::Clamp(BorderPercent, 0.0f, 50.0f) * 0.01f),
            1,
            FMath::Max(1, FMath::Min(Size.X, Size.Y)));

        float Sum = 0.0f;
        float SumSquared = 0.0f;
        int32 SampleCount = 0;
        for (int32 Y = 0; Y < Size.Y; ++Y)
        {
            for (int32 X = 0; X < Size.X; ++X)
            {
                if (X >= BorderPixels && X < Size.X - BorderPixels &&
                    Y >= BorderPixels && Y < Size.Y - BorderPixels)
                {
                    continue;
                }

                const int32     PixelIndex = Y * Size.X + X;
                const FVector2f SourceXY = GetNormalXY(DecodeNormalFromColor(SourcePixels[PixelIndex], bFlipGreen));
                const float     Deviation = (SourceXY - BackgroundAverageXY).Length();
                Sum += Deviation;
                SumSquared += Deviation * Deviation;
                ++SampleCount;
            }
        }

        if (SampleCount <= 0)
        {
            return UserFlatThreshold;
        }

        const float Mean = Sum / static_cast<float>(SampleCount);
        const float Variance = FMath::Max(0.0f, SumSquared / static_cast<float>(SampleCount) - Mean * Mean);
        const float StdDev = FMath::Sqrt(Variance);
        const float BorderNoiseThreshold = Mean + StdDev * 3.0f;
        return FMath::Clamp(FMath::Max(UserFlatThreshold, BorderNoiseThreshold), 0.0f, 1.0f);
    }

} // namespace

float FWetWrinkleTextureScalarBuffer::SampleBilinear(const FVector2D& UV) const
{
    if (!IsValid())
    {
        return 0.0f;
    }

    const float SampleX = FMath::Clamp(static_cast<float>(UV.X), 0.0f, 1.0f) * static_cast<float>(Size.X - 1);
    const float SampleY = FMath::Clamp(static_cast<float>(UV.Y), 0.0f, 1.0f) * static_cast<float>(Size.Y - 1);
    const int32 X0 = FMath::FloorToInt(SampleX);
    const int32 Y0 = FMath::FloorToInt(SampleY);
    const int32 X1 = FMath::Min(X0 + 1, Size.X - 1);
    const int32 Y1 = FMath::Min(Y0 + 1, Size.Y - 1);
    const float FracX = SampleX - static_cast<float>(X0);
    const float FracY = SampleY - static_cast<float>(Y0);
    return FMath::Lerp(
        FMath::Lerp(Values[Y0 * Size.X + X0], Values[Y0 * Size.X + X1], FracX),
        FMath::Lerp(Values[Y1 * Size.X + X0], Values[Y1 * Size.X + X1], FracX),
        FracY);
}

bool FWetWrinkleNormalTextureBuilder::BuildTextureBuffers(
    UTexture2D*                                  SourceNormalTexture,
    const bool                                   bUseCorrection,
    const FWetWrinkleNormalCorrectionSettings&   Settings,
    const FWetWrinkleCoverageExtractionSettings& CoverageSettings,
    FWetWrinkleNormalBuildOutput&                OutOutput,
    FString&                                     OutError,
    const int32                                  MaxOutputDimension)
{
    OutOutput = FWetWrinkleNormalBuildOutput();
    OutError.Reset();

    TArray<FColor> SourcePixels;
    FIntPoint      SourceSize = FIntPoint::ZeroValue;
    if (!ReadTextureSourcePixelsInternal(SourceNormalTexture, SourcePixels, SourceSize, OutError))
    {
        return false;
    }

    if (MaxOutputDimension > 0 && FMath::Max(SourceSize.X, SourceSize.Y) > MaxOutputDimension)
    {
        const float     Scale = static_cast<float>(MaxOutputDimension) / static_cast<float>(FMath::Max(SourceSize.X, SourceSize.Y));
        const FIntPoint PreviewSize(
            FMath::Max(1, FMath::RoundToInt(SourceSize.X * Scale)),
            FMath::Max(1, FMath::RoundToInt(SourceSize.Y * Scale)));
        TArray<FColor> PreviewPixels;
        ResizeNormalSourcePixels(SourcePixels, SourceSize, PreviewSize, PreviewPixels);
        SourcePixels = MoveTemp(PreviewPixels);
        SourceSize = PreviewSize;
    }

    const bool      bApplyGreenFlip = bUseCorrection && Settings.bFlipGreen;
    const FVector2f BackgroundAverageXY = bUseCorrection
                                              ? EstimateBorderAverageXY(SourcePixels, SourceSize, bApplyGreenFlip, Settings.BorderPercent)
                                              : FVector2f::ZeroVector;

    const int32 PixelCount = SourceSize.X * SourceSize.Y;
    OutOutput.CorrectedNormal.Size = SourceSize;
    OutOutput.DeviationPreview.Size = SourceSize;
    OutOutput.CorrectedDeviationPreview.Size = SourceSize;
    OutOutput.ConvexSeparationPreview.Size = SourceSize;
    OutOutput.CorrectedNormal.Pixels.SetNumUninitialized(PixelCount);
    OutOutput.DeviationPreview.Pixels.SetNumUninitialized(PixelCount);
    OutOutput.CorrectedDeviationPreview.Pixels.SetNumUninitialized(PixelCount);
    OutOutput.ConvexSeparationPreview.Pixels.SetNumUninitialized(PixelCount);

    const float FlatThreshold = FMath::Clamp(Settings.FlatThreshold, 0.0f, 1.0f);
    const float EffectiveFlatThreshold = bUseCorrection
                                             ? EstimateBorderNoiseThreshold(SourcePixels, SourceSize, bApplyGreenFlip, Settings.BorderPercent, BackgroundAverageXY, FlatThreshold)
                                             : FlatThreshold;
    const float DeviationAmplify = FMath::Max(Settings.DeviationPreviewAmplify, 0.0f);
    int32       FlatPixelCount = 0;
    float       MaxXYDeviation = 0.0f;

    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const FColor    SourceColor = SourcePixels[PixelIndex];
        const FVector3f SourceNormal = DecodeNormalFromColor(SourceColor, bApplyGreenFlip);
        const FVector2f SourceXY = GetNormalXY(SourceNormal);
        const FVector2f CorrectedXY = bUseCorrection ? SourceXY - BackgroundAverageXY : SourceXY;
        const float     XYDeviation = CorrectedXY.Length();

        FVector3f CorrectedNormal;
        if (bUseCorrection)
        {
            if (XYDeviation < EffectiveFlatThreshold)
            {
                CorrectedNormal = FVector3f(0.0f, 0.0f, 1.0f);
            }
            else
            {
                const float CorrectedZ = FMath::Sqrt(FMath::Max(0.0f, 1.0f - CorrectedXY.SizeSquared()));
                CorrectedNormal = FVector3f(CorrectedXY.X, CorrectedXY.Y, CorrectedZ).GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
            }
        }
        else
        {
            CorrectedNormal = SourceNormal;
        }

        const float SourceDeviation = SourceXY.Length();
        const uint8 DeviationByte = FloatToByte(FMath::Clamp(SourceDeviation * DeviationAmplify, 0.0f, 1.0f));
        const float CorrectedDeviation = GetNormalXY(CorrectedNormal).Length();
        const uint8 CorrectedDeviationByte = FloatToByte(FMath::Clamp(CorrectedDeviation * DeviationAmplify, 0.0f, 1.0f));

        if (XYDeviation <= EffectiveFlatThreshold)
        {
            ++FlatPixelCount;
        }
        MaxXYDeviation = FMath::Max(MaxXYDeviation, XYDeviation);

        OutOutput.CorrectedNormal.Pixels[PixelIndex] = bUseCorrection ? EncodeNormalToColor(CorrectedNormal) : SourceColor;
        OutOutput.DeviationPreview.Pixels[PixelIndex] = FColor(DeviationByte, DeviationByte, DeviationByte, 255);
        OutOutput.CorrectedDeviationPreview.Pixels[PixelIndex] = FColor(CorrectedDeviationByte, CorrectedDeviationByte, CorrectedDeviationByte, 255);
    }

    if (!BuildConvexSeparationFromPixels(
            OutOutput.CorrectedNormal.Pixels,
            SourceSize,
            false,
            CoverageSettings,
            OutOutput.ConvexSeparation,
            OutError))
    {
        return false;
    }
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const uint8 Value = FloatToByte(OutOutput.ConvexSeparation.Values[PixelIndex]);
        OutOutput.ConvexSeparationPreview.Pixels[PixelIndex] = FColor(Value, Value, Value, 255);
    }

    OutOutput.Stats.SourceSize = SourceSize;
    OutOutput.Stats.BackgroundAverageXY = FVector2D(BackgroundAverageXY.X, BackgroundAverageXY.Y);
    OutOutput.Stats.FlatPixelRatio = PixelCount > 0 ? static_cast<float>(FlatPixelCount) / static_cast<float>(PixelCount) : 0.0f;
    OutOutput.Stats.MaxXYDeviation = MaxXYDeviation;

    return true;
}

bool FWetWrinkleNormalTextureBuilder::BuildConvexSeparationBuffer(
    UTexture2D*                                  CorrectedNormalTexture,
    const FWetWrinkleCoverageExtractionSettings& Settings,
    FWetWrinkleTextureScalarBuffer&              OutBuffer,
    FString&                                     OutError)
{
    FWetWrinkleTexturePixelBuffer CorrectedNormal;
    if (!ReadTextureSourcePixels(CorrectedNormalTexture, CorrectedNormal, OutError))
    {
        return false;
    }

    bool bFlipGreenChannel = false;
#if WITH_EDITORONLY_DATA
    bFlipGreenChannel = CorrectedNormalTexture != nullptr &&
                        CorrectedNormalTexture->bFlipGreenChannel;
#endif

    return BuildConvexSeparationBufferFromPixels(
        CorrectedNormal,
        bFlipGreenChannel,
        Settings,
        OutBuffer,
        OutError);
}

bool FWetWrinkleNormalTextureBuilder::ReadTextureSourcePixels(
    UTexture2D*                    Texture,
    FWetWrinkleTexturePixelBuffer& OutBuffer,
    FString&                       OutError)
{
    OutBuffer = FWetWrinkleTexturePixelBuffer();
    return ReadTextureSourcePixelsInternal(Texture, OutBuffer.Pixels, OutBuffer.Size, OutError);
}

bool FWetWrinkleNormalTextureBuilder::BuildConvexSeparationBufferFromPixels(
    const FWetWrinkleTexturePixelBuffer&         CorrectedNormal,
    const bool                                   bFlipGreenChannel,
    const FWetWrinkleCoverageExtractionSettings& Settings,
    FWetWrinkleTextureScalarBuffer&              OutBuffer,
    FString&                                     OutError)
{
    if (!CorrectedNormal.IsValid())
    {
        OutError = TEXT("Corrected wrinkle normal pixels are unavailable.");
        return false;
    }

    return BuildConvexSeparationFromPixels(
        CorrectedNormal.Pixels,
        CorrectedNormal.Size,
        bFlipGreenChannel,
        Settings,
        OutBuffer,
        OutError);
}
