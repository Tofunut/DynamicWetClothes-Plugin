#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionProcessor.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "Engine/Texture2D.h"
#include "Misc/SecureHash.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"

namespace
{
    float SampleMaskBilinear(
        const FWetClothingTextureReadback& Readback,
        const float U,
        const float V)
    {
        const float SourceX = FMath::Clamp(U, 0.0f, 1.0f) * static_cast<float>(Readback.Width - 1);
        const float SourceY = FMath::Clamp(V, 0.0f, 1.0f) * static_cast<float>(Readback.Height - 1);
        const int32 X0 = FMath::FloorToInt(SourceX);
        const int32 Y0 = FMath::FloorToInt(SourceY);
        const int32 X1 = FMath::Min(X0 + 1, Readback.Width - 1);
        const int32 Y1 = FMath::Min(Y0 + 1, Readback.Height - 1);
        const float FracX = SourceX - static_cast<float>(X0);
        const float FracY = SourceY - static_cast<float>(Y0);
        const float Top = FMath::Lerp(
            Readback.GetLinearColor(X0, Y0).R,
            Readback.GetLinearColor(X1, Y0).R,
            FracX);
        const float Bottom = FMath::Lerp(
            Readback.GetLinearColor(X0, Y1).R,
            Readback.GetLinearColor(X1, Y1).R,
            FracX);
        return FMath::Clamp(FMath::Lerp(Top, Bottom, FracY), 0.0f, 1.0f);
    }

    float SmoothStep(const float MinValue, const float MaxValue, const float Value)
    {
        if (MaxValue <= MinValue + UE_SMALL_NUMBER)
        {
            return Value >= MinValue ? 1.0f : 0.0f;
        }

        const float T = FMath::Clamp((Value - MinValue) / (MaxValue - MinValue), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }
}

FDWCWrinkleSuppressionSource FDWCWrinkleSuppressionProcessor::FindExactSource(
    const UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex,
    const int32 LODIndex)
{
    FDWCWrinkleSuppressionSource Result;
    if (WetClothingAsset == nullptr)
    {
        return Result;
    }

    Result.BakedMap = WetClothingAsset->Authored.WrinkleData.BakedWrinkleMaps.FindByPredicate(
        [MaterialSlotIndex, UVChannelIndex, LODIndex](const FWetWrinkleBakedMapSet& Candidate)
        {
            return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                   Candidate.UVChannelIndex == UVChannelIndex &&
                   Candidate.LODIndex == LODIndex &&
                   Candidate.BakedWrinkleMask != nullptr;
        });
    Result.MaskTexture = Result.BakedMap != nullptr
        ? Result.BakedMap->BakedWrinkleMask.Get()
        : nullptr;
    return Result;
}

bool FDWCWrinkleSuppressionProcessor::BuildProcessedBuffer(
    const FDWCWrinkleSuppressionSource& Source,
    const FIntPoint OutputSize,
    const float CoverageThreshold,
    const float MaskSoftness,
    TArray<uint8>& OutBuffer,
    FString& OutErrorMessage)
{
    OutBuffer.Reset();
    OutErrorMessage.Reset();
    if (!Source.IsValid())
    {
        OutErrorMessage = TEXT("No exact baked wrinkle mask matches the transparency slot, UV channel, and LOD.");
        return false;
    }
    if (OutputSize.X <= 0 || OutputSize.Y <= 0)
    {
        OutErrorMessage = TEXT("The requested wrinkle suppression buffer size is invalid.");
        return false;
    }

    FWetClothingTextureReadback Readback;
    if (!FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
            Source.MaskTexture,
            Readback,
            OutErrorMessage))
    {
        return false;
    }
    if (Readback.Width <= 0 || Readback.Height <= 0)
    {
        OutErrorMessage = TEXT("The baked wrinkle mask has no readable source pixels.");
        return false;
    }

    const int32 PixelCount = OutputSize.X * OutputSize.Y;
    const float SafeThreshold = FMath::Clamp(CoverageThreshold, 0.0f, 1.0f);
    const float SafeSoftness = FMath::Clamp(MaskSoftness, 0.0f, 1.0f);
    const float TransitionEnd = FMath::Min(SafeThreshold + SafeSoftness, 1.0f);
    OutBuffer.SetNumUninitialized(PixelCount);
    for (int32 Y = 0; Y < OutputSize.Y; ++Y)
    {
        const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(OutputSize.Y);
        for (int32 X = 0; X < OutputSize.X; ++X)
        {
            const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(OutputSize.X);
            const int32 PixelIndex = Y * OutputSize.X + X;
            const float MaskValue = SampleMaskBilinear(Readback, U, V);
            const float ThresholdGate = SmoothStep(SafeThreshold, TransitionEnd, MaskValue);
            const float Suppression = FMath::Clamp(MaskValue * ThresholdGate, 0.0f, 1.0f);
            OutBuffer[PixelIndex] =
                static_cast<uint8>(FMath::RoundToInt(Suppression * 255.0f));
        }
    }
    return true;
}

FString FDWCWrinkleSuppressionProcessor::MakeSettingsSignature(
    const float CoverageThreshold,
    const float MaskSoftness,
    const float SuppressionStrength,
    const float TransparencyStrength)
{
    const FString Canonical = FString::Printf(
        TEXT("DWCTransparencySuppression_v2.DirectMask|threshold=%.9g|softness=%.9g|suppression=%.9g|transparency=%.9g"),
        CoverageThreshold,
        MaskSoftness,
        SuppressionStrength,
        TransparencyStrength);
    return FMD5::HashAnsiString(*Canonical);
}
