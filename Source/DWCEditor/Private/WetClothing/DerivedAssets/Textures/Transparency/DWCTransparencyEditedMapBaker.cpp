#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"

bool FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(
    const FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencyAutoBakeResult& AutoResult,
    FString& OutReason)
{
    OutReason.Reset();
    if (AutoResult.LayerGuid != Layer.LayerGuid ||
        AutoResult.MaterialSlotIndex != Layer.TargetSurface.OuterMaterialSlotIndex ||
        AutoResult.UVChannelIndex != Layer.TargetSurface.OuterUVChannel)
    {
        OutReason = TEXT("The generated transparency map no longer matches the selected layer.");
        return false;
    }

    const int32 PixelCount = AutoResult.Resolution.X * AutoResult.Resolution.Y;
    if (AutoResult.Resolution.X <= 0 || AutoResult.Resolution.Y <= 0 ||
        AutoResult.InnerColorBuffer.Num() != PixelCount ||
        AutoResult.AutoAlphaBuffer.Num() != PixelCount ||
        AutoResult.ValidHitBuffer.Num() != PixelCount)
    {
        OutReason = TEXT("The generated transparency map buffers are invalid.");
        return false;
    }

    return true;
}

bool FDWCTransparencyEditedMapBaker::Bake(
    UWetClothingAsset& WetClothingAsset,
    FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencyAutoBakeResult& AutoResult,
    FDWCTransparencyEditedMapBakeResult& OutResult,
    FString& OutErrorMessage)
{
    OutResult = FDWCTransparencyEditedMapBakeResult();
    OutErrorMessage.Reset();

    FString CompatibilityReason;
    if (!IsAutoResultCompatible(Layer, AutoResult, CompatibilityReason))
    {
        OutErrorMessage = CompatibilityReason;
        return false;
    }

    UTexture2D* Texture = UTexture2D::CreateTransient(AutoResult.Resolution.X, AutoResult.Resolution.Y, PF_B8G8R8A8);
    if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
    {
        OutErrorMessage = TEXT("Failed to create the transparency map texture.");
        return false;
    }

    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    void* RawData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(RawData, AutoResult.InnerColorBuffer.GetData(), AutoResult.InnerColorBuffer.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();
    Texture->SRGB = true;
    Texture->UpdateResource();

    FWetClothingBakedTransparencyMap* BakedMap = Layer.BakedMaps.FindByPredicate(
        [&AutoResult](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == AutoResult.MaterialSlotIndex &&
                   Candidate.UVChannelIndex == AutoResult.UVChannelIndex &&
                   Candidate.LODIndex == AutoResult.LODIndex;
        });
    if (BakedMap == nullptr)
    {
        BakedMap = &Layer.BakedMaps.AddDefaulted_GetRef();
    }

    BakedMap->MaterialSlotIndex = AutoResult.MaterialSlotIndex;
    BakedMap->UVChannelIndex = AutoResult.UVChannelIndex;
    BakedMap->LODIndex = AutoResult.LODIndex;
    BakedMap->TransparencyMap = Texture;
    BakedMap->Resolution = AutoResult.Resolution.X;
    BakedMap->PaddingPixels = WetClothingAsset.Authored.TransparencyData.TransparencyPaddingPixels;
    BakedMap->BakeGuid = FGuid::NewGuid();
    BakedMap->BuildSignature = AutoResult.BuildSignature;
    BakedMap->bContainsColorRGB = true;
    BakedMap->bContainsTransparencyAlpha = true;

    OutResult.TransparencyMap = Texture;
    OutResult.AppliedStrokeCount = Layer.EditableStrokes.Num();
    for (const FDWCTransparencyBrushStroke& Stroke : Layer.EditableStrokes)
    {
        if (Stroke.bEnabled)
        {
            OutResult.AppliedSampleCount += Stroke.Samples.Num();
        }
    }
    return true;
}
