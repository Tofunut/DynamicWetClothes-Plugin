#include "DataAssets/WetWrinklePreset.h"

#include "DataAssets/WetWrinklePresetBuilder.h"
#include "Engine/Texture2D.h"

bool UWetWrinklePreset::HasValidSource() const
{
    return IsValid(SourceNormalTexture);
}

bool UWetWrinklePreset::HasGeneratedTextures() const
{
    return IsValid(CorrectedNormalTexture);
}

bool UWetWrinklePreset::IsBuildStale() const
{
    return BuildSignature.IsEmpty() || BuildSignature != FWetWrinklePresetBuilder::MakeBuildSignature(this);
}

bool UWetWrinklePreset::IsUsableForBrush(FString* OutReason) const
{
    if (!HasGeneratedTextures())
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("Generated corrected normal texture is missing. Rebuild Generated Textures in the Wet Wrinkle Preset Editor.");
        }
        return false;
    }

    if (OutReason != nullptr)
    {
        OutReason->Reset();
    }
    return true;
}

UTexture2D* UWetWrinklePreset::GetNormalTextureForBrush() const
{
    return HasGeneratedTextures() ? CorrectedNormalTexture.Get() : nullptr;
}
