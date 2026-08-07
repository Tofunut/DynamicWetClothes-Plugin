//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSourceResolver.h"

#include "Materials/MaterialInterface.h"
#include "Runtime/Engine/Classes/Engine/Texture.h"
#include "Runtime/Engine/Classes/Engine/Texture2D.h"

FString FDWCRevealBakeSourceResolver::NormalizeTextureSearchText(const FString& InText)
{
    FString Result = InText.ToLower();
    Result.ReplaceInline(TEXT(" "), TEXT(""));
    Result.ReplaceInline(TEXT("_"), TEXT(""));
    Result.ReplaceInline(TEXT("-"), TEXT(""));
    return Result;
}

bool FDWCRevealBakeSourceResolver::ContainsAnyTextureKeyword(
    const FString& SearchText,
    std::initializer_list<const TCHAR*> Keywords)
{
    for (const TCHAR* Keyword : Keywords)
    {
        if (SearchText.Contains(Keyword))
        {
            return true;
        }
    }

    return false;
}

UTexture2D* FDWCRevealBakeSourceResolver::ResolveRevealSourceBaseColorTexture(const FDWCBakeResolvedLayer& SourceLayer)
{
    UTexture2D* BestTexture = nullptr;
    int32       BestScore = MIN_int32;

    for (UMaterialInterface* Material : SourceLayer.Materials)
    {
        if (Material == nullptr)
        {
            continue;
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid>                  ParameterIds;
        Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
        for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
        {
            UTexture* Texture = nullptr;
            if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Texture))
            {
                if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
                {
                    const int32 Score = ScoreRevealBaseColorTexture(Texture2D, ParameterInfo.Name.ToString());
                    if (Score > BestScore)
                    {
                        BestScore = Score;
                        BestTexture = Texture2D;
                    }
                }
            }
        }

        TArray<UTexture*> UsedTextures;
        Material->GetUsedTextures(UsedTextures);
        for (UTexture* Texture : UsedTextures)
        {
            if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
            {
                const int32 Score = ScoreRevealBaseColorTexture(Texture2D, FString());
                if (Score > BestScore)
                {
                    BestScore = Score;
                    BestTexture = Texture2D;
                }
            }
        }
    }

    return BestScore > -500 ? BestTexture : nullptr;
}

UTexture2D* FDWCRevealBakeSourceResolver::ResolveRevealSourceBaseColorTexture(UMaterialInterface* SourceMaterial)
{
    if (SourceMaterial == nullptr)
    {
        return nullptr;
    }

    FDWCBakeResolvedLayer TemporaryLayer;
    TemporaryLayer.Materials.Add(SourceMaterial);
    return ResolveRevealSourceBaseColorTexture(TemporaryLayer);
}

int32 FDWCRevealBakeSourceResolver::ScoreRevealBaseColorTexture(UTexture* Texture, const FString& ParameterName)
{
    if (Texture == nullptr)
    {
        return MIN_int32;
    }

    const FString TextureName = NormalizeTextureSearchText(Texture->GetName());
    const FString NormalizedParameterName = NormalizeTextureSearchText(ParameterName);
    const FString CombinedText = TextureName + NormalizedParameterName;

    int32 Score = Texture->SRGB ? 100 : -100;
    if (ContainsAnyTextureKeyword(CombinedText, { TEXT("basecolor"), TEXT("basecolour"), TEXT("albedo"), TEXT("diffuse") }))
    {
        Score += 1000;
    }
    if (ContainsAnyTextureKeyword(CombinedText, { TEXT("color"), TEXT("colour") }))
    {
        Score += 250;
    }
    if (ContainsAnyTextureKeyword(
            CombinedText,
            { TEXT("normal"), TEXT("roughness"), TEXT("metallic"), TEXT("specular"), TEXT("orm"), TEXT("rma"), TEXT("ao"),
              TEXT("ambientocclusion"), TEXT("mask"), TEXT("opacity"), TEXT("height"), TEXT("emissive") }))
    {
        Score -= 1000;
    }

    return Score;
}
