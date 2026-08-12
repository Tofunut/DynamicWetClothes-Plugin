//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionCoverageService.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "Engine/Texture2D.h"
#include "Misc/SecureHash.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"

namespace
{
    constexpr int32 SuppressionSamplingVersion = 1;

    bool FailCoverageDependency(FString* OutError, const TCHAR* Message)
    {
        if (OutError != nullptr)
        {
            *OutError = Message;
        }
        return false;
    }
}

bool FDWCWrinkleSuppressionDependencySnapshot::IsAvailable() const
{
    return Status == EDWCWrinkleSuppressionDependencyStatus::Ready &&
        MaterialSlotIndex != INDEX_NONE && DataUVChannelIndex >= 0 && LODIndex == 0 &&
        MaskTexture.IsValid() && !MaskTexturePath.IsEmpty() &&
        !BuildSignature.IsEmpty() && BakeGuid.IsValid() && TextureSourceId.IsValid() &&
        SourceResolution.X > 0 && SourceResolution.Y > 0;
}

bool FDWCWrinkleSuppressionDependencySnapshot::IsValid(FString* OutError) const
{
    if (Status == EDWCWrinkleSuppressionDependencyStatus::Missing)
    {
        return true;
    }
    if (!IsAvailable())
    {
        return FailCoverageDependency(
            OutError,
            TEXT("The wrinkle suppression dependency snapshot is incomplete or unreadable."));
    }
    return true;
}

UTexture2D* FDWCWrinkleSuppressionDependencySnapshot::ResolveTexture() const
{
    return IsAvailable() ? MaskTexture.Get() : nullptr;
}

FString FDWCWrinkleSuppressionDependencySnapshot::BuildCacheSignature() const
{
    if (!IsAvailable())
    {
        return FString();
    }
    const FString Canonical = FString::Printf(
        TEXT("DWCWrinkleCoverage_v%d|slot=%d|uv=%d|lod=%d|bake=%s|build=%s|source=%s|size=%dx%d|path=%s"),
        SuppressionSamplingVersion,
        MaterialSlotIndex,
        DataUVChannelIndex,
        LODIndex,
        *BakeGuid.ToString(EGuidFormats::Digits),
        *BuildSignature,
        *TextureSourceId.ToString(EGuidFormats::Digits),
        SourceResolution.X,
        SourceResolution.Y,
        *MaskTexturePath);
    return FMD5::HashAnsiString(*Canonical);
}

FName FDWCWrinkleCoverageCacheValue::StaticCacheTypeName()
{
    static const FName Name(TEXT("DWCWrinkleCoverage"));
    return Name;
}

uint64 FDWCWrinkleCoverageCacheValue::GetAllocatedSizeBytes() const
{
    return Readback.GetAllocatedBytes();
}

bool FDWCWrinkleCoverageCacheValue::IsValid() const
{
    return Dependency.IsAvailable() && Readback.IsValid() &&
        Readback.Width == Dependency.SourceResolution.X &&
        Readback.Height == Dependency.SourceResolution.Y;
}

float FDWCWrinkleCoverageCacheValue::SampleCoverage(const FVector2f& UV) const
{
    if (!IsValid())
    {
        return 0.0f;
    }
    const float SourceX = FMath::Clamp(UV.X, 0.0f, 1.0f) * static_cast<float>(Readback.Width - 1);
    const float SourceY = FMath::Clamp(UV.Y, 0.0f, 1.0f) * static_cast<float>(Readback.Height - 1);
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

FDWCWrinkleSuppressionCoverageService::FDWCWrinkleSuppressionCoverageService(
    TSharedRef<FDWCEditorCacheStore> InCacheStore)
    : CacheStore(MoveTemp(InCacheStore))
{
}

FName FDWCWrinkleSuppressionCoverageService::CacheNamespace()
{
    static const FName Name(TEXT("DWC.Transparency.WrinkleCoverage"));
    return Name;
}

FDWCWrinkleSuppressionDependencySnapshot
FDWCWrinkleSuppressionCoverageService::ResolveDependency(
    const UWetClothingAsset* Asset,
    const int32 MaterialSlotIndex,
    const bool bExactCurrentness)
{
    check(IsInGameThread());
    FDWCWrinkleSuppressionDependencySnapshot Result;
    Result.MaterialSlotIndex = MaterialSlotIndex;
    Result.LODIndex = 0;
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        Result.Detail = TEXT("No WCA or material slot is selected.");
        return Result;
    }
    Result.DataUVChannelIndex = Asset->GetDWCDataUVChannelIndex();
    const FWetWrinkleBakedMapSet* BakedMap =
        Asset->Authored.WrinkleData.BakedWrinkleMaps.FindByPredicate(
            [MaterialSlotIndex](const FWetWrinkleBakedMapSet& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                    Candidate.BakedWrinkleMask != nullptr;
            });
    if (BakedMap == nullptr)
    {
        Result.Detail = TEXT("No baked wrinkle coverage mask matches this material slot.");
        return Result;
    }
    const FWetWrinkleMaterialSlotBakeState Currentness =
        FWetWrinkleNormalMapBaker::EvaluateMaterialSlotBakeState(
            Asset, MaterialSlotIndex, bExactCurrentness);
    if (!Currentness.IsCurrent() ||
        (!bExactCurrentness && !DWCBuildStatus::IsUsable(Asset->GetBakeState().WrinkleMaps)))
    {
        Result.Status = EDWCWrinkleSuppressionDependencyStatus::Stale;
        Result.Detail = Currentness.Detail.IsEmpty()
            ? TEXT("The baked wrinkle coverage mask is out of date.")
            : Currentness.Detail;
        return Result;
    }
    UTexture2D* Texture = BakedMap->BakedWrinkleMask.Get();
    Result.MaskTexture = Texture;
    Result.MaskTexturePath = Texture != nullptr ? Texture->GetPathName() : FString();
    Result.BuildSignature = BakedMap->BuildSignature;
    Result.BakeGuid = BakedMap->BakeGuid;
    if (Texture == nullptr || !Texture->Source.IsValid())
    {
        Result.Status = EDWCWrinkleSuppressionDependencyStatus::Unreadable;
        Result.Detail = TEXT("The baked wrinkle coverage mask has no readable source data.");
        return Result;
    }
    Result.TextureSourceId = Texture->Source.GetId();
    Result.SourceResolution = FIntPoint(Texture->Source.GetSizeX(), Texture->Source.GetSizeY());
    Result.Status = EDWCWrinkleSuppressionDependencyStatus::Ready;
    Result.Detail.Reset();
    return Result;
}

FDWCEditorCacheKey FDWCWrinkleSuppressionCoverageService::BuildCacheKey(
    const UWetClothingAsset& Asset,
    const FDWCWrinkleSuppressionDependencySnapshot& Dependency)
{
    FDWCEditorCacheKey Key;
    Key.Namespace = CacheNamespace();
    Key.Owner = FObjectKey(&Asset);
    Key.ResourceIdentity = Dependency.ResolveTexture();
    Key.LODIndex = Dependency.LODIndex;
    Key.UVChannelIndex = Dependency.DataUVChannelIndex;
    Key.MaterialSlotIndex = Dependency.MaterialSlotIndex;
    Key.Signature = Dependency.BuildCacheSignature();
    return Key;
}

bool FDWCWrinkleSuppressionCoverageService::AcquireCoverage(
    const UWetClothingAsset& Asset,
    const FDWCWrinkleSuppressionDependencySnapshot& Dependency,
    FDWCEditorCacheLease& OutLease,
    FString& OutError)
{
    check(IsInGameThread());
    OutLease.Reset();
    OutError.Reset();
    if (!Dependency.IsAvailable())
    {
        OutError = Dependency.Detail.IsEmpty()
            ? TEXT("No usable wrinkle suppression dependency is available.")
            : Dependency.Detail;
        return false;
    }
    const FDWCEditorCacheKey Key = BuildCacheKey(Asset, Dependency);
    OutLease = CacheStore->FindLease<FDWCWrinkleCoverageCacheValue>(Key);
    if (OutLease.IsValid())
    {
        return true;
    }

    FWetClothingTextureReadback Readback;
    if (!FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
            Dependency.ResolveTexture(), Readback, OutError))
    {
        return false;
    }
    TSharedRef<FDWCWrinkleCoverageCacheValue, ESPMode::ThreadSafe> Value =
        MakeShared<FDWCWrinkleCoverageCacheValue, ESPMode::ThreadSafe>();
    Value->Dependency = Dependency;
    Value->Readback = MoveTemp(Readback);
    if (!Value->IsValid())
    {
        OutError = TEXT("The wrinkle coverage cache payload is invalid.");
        return false;
    }
    CacheStore->Put(Key, Value);
    OutLease = CacheStore->FindLease<FDWCWrinkleCoverageCacheValue>(Key);
    if (!OutLease.IsValid())
    {
        OutError = TEXT("The wrinkle coverage cache entry could not be leased.");
        return false;
    }
    return true;
}

void FDWCWrinkleSuppressionCoverageService::InvalidateAssetSlot(
    const UWetClothingAsset* Asset,
    const int32 MaterialSlotIndex)
{
    CacheStore->InvalidateOwnerNamespace(Asset, CacheNamespace(), MaterialSlotIndex);
}

void FDWCWrinkleSuppressionCoverageService::InvalidateAsset(const UWetClothingAsset* Asset)
{
    CacheStore->InvalidateOwnerNamespace(Asset, CacheNamespace());
}

float FDWCWrinkleSuppressionCoverageService::EvaluateSuppression(
    const float Coverage,
    const float CoverageThreshold,
    const float MaskSoftness)
{
    const float SafeCoverage = FMath::Clamp(Coverage, 0.0f, 1.0f);
    const float SafeThreshold = FMath::Clamp(CoverageThreshold, 0.0f, 1.0f);
    const float SafeSoftness = FMath::Clamp(MaskSoftness, 0.0f, 1.0f);
    const float TransitionEnd = FMath::Min(SafeThreshold + SafeSoftness, 1.0f);
    float Gate = SafeCoverage >= SafeThreshold ? 1.0f : 0.0f;
    if (SafeSoftness > UE_SMALL_NUMBER && TransitionEnd > SafeThreshold + UE_SMALL_NUMBER)
    {
        const float T = FMath::Clamp(
            (SafeCoverage - SafeThreshold) / (TransitionEnd - SafeThreshold),
            0.0f,
            1.0f);
        Gate = T * T * (3.0f - 2.0f * T);
    }
    return FMath::Clamp(SafeCoverage * Gate, 0.0f, 1.0f);
}
