/*
 *  Material에서 미리보기와 Auto Partition에 적합한 텍스처 후보를 수집하고 점수화합니다.
 */

#include "WetClothing/Texture/WetClothingMaterialTextureResolver.h"

#include "Materials/MaterialInterface.h"
#include "WetClothing/Editor/WetClothingAssetEditorTypes.h"
#include "WetClothing/Texture/WetClothingTextureReadback.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include <initializer_list>

namespace
{
    struct FWetClothingTextureCandidate
    {
        UTexture* Texture = nullptr;
        FString   Label;
        int32     Score = MIN_int32;
    };

    FString NormalizeTextureSearchText(const FString& InText)
    {
        FString Result = InText.ToLower();
        Result.ReplaceInline(TEXT(" "), TEXT(""));
        Result.ReplaceInline(TEXT("_"), TEXT(""));
        Result.ReplaceInline(TEXT("-"), TEXT(""));
        return Result;
    }

    bool ContainsAnyTextureKeyword(const FString& SearchText, std::initializer_list<const TCHAR*> Keywords)
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

    int32 ScoreMaterialTextureCandidate(UTexture* Texture, const FString& ParameterName)
    {
        if (Texture == nullptr)
        {
            return MIN_int32;
        }

        const FString TextureName = NormalizeTextureSearchText(Texture->GetName());
        const FString NormalizedParameterName = NormalizeTextureSearchText(ParameterName);
        const FString CombinedText = TextureName + NormalizedParameterName;

        int32 Score = 0;

        if (ContainsAnyTextureKeyword(CombinedText, { TEXT("basecolor"), TEXT("diffuse"), TEXT("albedo"), TEXT("basecolour"), TEXT("colour"), TEXT("color") }))
        {
            Score += 500;
        }

        if (ContainsAnyTextureKeyword(CombinedText, { TEXT("normal"), TEXT("roughness"), TEXT("metallic"), TEXT("specular"), TEXT("orm"), TEXT("rma"), TEXT("ao"), TEXT("ambientocclusion"), TEXT("opacity"), TEXT("mask"), TEXT("height"), TEXT("displace"), TEXT("emissive") }))
        {
            Score -= 400;
        }

        if (NormalizedParameterName.Contains(TEXT("basecolor")) || NormalizedParameterName.Contains(TEXT("diffuse")) || NormalizedParameterName.Contains(TEXT("albedo")))
        {
            Score += 700;
        }

        if (NormalizedParameterName.Contains(TEXT("color")) || NormalizedParameterName.Contains(TEXT("colour")))
        {
            Score += 150;
        }

        if (Texture->SRGB)
        {
            Score += 50;
        }

        return Score;
    }

    void AddOrUpdateTextureCandidate(
        TMap<UTexture*, FWetClothingTextureCandidate>& InOutCandidates,
        UTexture*                                      Texture,
        const FString&                                 ParameterName)
    {
        if (Texture == nullptr)
        {
            return;
        }

        const int32                   CandidateScore = ScoreMaterialTextureCandidate(Texture, ParameterName);
        FWetClothingTextureCandidate& Candidate = InOutCandidates.FindOrAdd(Texture);
        if (Candidate.Texture == nullptr || CandidateScore > Candidate.Score)
        {
            Candidate.Texture = Texture;
            Candidate.Score = CandidateScore;
            Candidate.Label = Texture->GetName();
        }
    }
} // namespace

void FWetClothingMaterialTextureResolver::BuildTextureItems(
    UMaterialInterface*                          Material,
    TArray<TSharedPtr<FWetClothingTextureItem>>& OutItems)
{
    OutItems.Reset();

    TSharedPtr<FWetClothingTextureItem> NoneItem = MakeShared<FWetClothingTextureItem>();
    NoneItem->Label = TEXT("None");
    OutItems.Add(NoneItem);

    if (Material == nullptr)
    {
        return;
    }

    TMap<UTexture*, FWetClothingTextureCandidate> TextureCandidates;

    TArray<FMaterialParameterInfo> ParameterInfos;
    TArray<FGuid>                  ParameterIds;
    Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);

    for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
    {
        UTexture* ParameterTexture = nullptr;
        if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(ParameterInfo), ParameterTexture))
        {
            AddOrUpdateTextureCandidate(TextureCandidates, ParameterTexture, ParameterInfo.Name.ToString());
        }
    }

    TArray<UTexture*> UsedTextures;
    Material->GetUsedTextures(UsedTextures);
    for (UTexture* Texture : UsedTextures)
    {
        AddOrUpdateTextureCandidate(TextureCandidates, Texture, FString());
    }

    TArray<FWetClothingTextureCandidate> SortedCandidates;
    SortedCandidates.Reserve(TextureCandidates.Num());
    for (const TPair<UTexture*, FWetClothingTextureCandidate>& Pair : TextureCandidates)
    {
        SortedCandidates.Add(Pair.Value);
    }

    SortedCandidates.Sort([](const FWetClothingTextureCandidate& A, const FWetClothingTextureCandidate& B)
                          {
        if (A.Score != B.Score)
        {
            return A.Score > B.Score;
        }

        return A.Label < B.Label; });

    for (const FWetClothingTextureCandidate& Candidate : SortedCandidates)
    {
        if (Candidate.Texture == nullptr)
        {
            continue;
        }

        TSharedPtr<FWetClothingTextureItem> Item = MakeShared<FWetClothingTextureItem>();
        Item->Texture = Candidate.Texture;
        Item->Label = Candidate.Label;
        OutItems.Add(Item);
    }
}

UTexture* FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(UMaterialInterface* Material)
{
    TArray<TSharedPtr<FWetClothingTextureItem>> TextureItemCandidates;
    BuildTextureItems(Material, TextureItemCandidates);

    UTexture* BestTexture = nullptr;
    double    BestScore = -TNumericLimits<double>::Max();
    int32     CandidateIndex = 0;

    for (const TSharedPtr<FWetClothingTextureItem>& TextureItem : TextureItemCandidates)
    {
        if (TextureItem.IsValid() && TextureItem->Texture.IsValid())
        {
            const double CandidateScore = ScoreTexturePreviewSuitability(TextureItem->Texture.Get()) - CandidateIndex * 0.01;
            if (CandidateScore > BestScore)
            {
                BestScore = CandidateScore;
                BestTexture = TextureItem->Texture.Get();
            }
        }

        ++CandidateIndex;
    }

    return BestTexture;
}

double FWetClothingMaterialTextureResolver::ScoreTexturePreviewSuitability(UTexture* Texture)
{
    if (Texture == nullptr)
    {
        return -TNumericLimits<double>::Max();
    }

    double Score = Texture->SRGB ? 120.0 : -40.0;

    if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
    {
        FWetClothingTextureReadback TextureData;
        FString                     ErrorMessage;
        if (FWetClothingTextureReadbackUtils::TryReadTextureSourceData(const_cast<UTexture2D*>(Texture2D), TextureData, ErrorMessage))
        {
            constexpr int32 SampleGridSize = 6;
            FLinearColor    MeanColor = FLinearColor::Black;
            double          SaturationSum = 0.0;
            double          ValueSum = 0.0;
            int32           SampleCount = 0;

            for (int32 SampleY = 0; SampleY < SampleGridSize; ++SampleY)
            {
                for (int32 SampleX = 0; SampleX < SampleGridSize; ++SampleX)
                {
                    const int32        PixelX = FMath::RoundToInt((static_cast<float>(SampleX) / (SampleGridSize - 1)) * (TextureData.Width - 1));
                    const int32        PixelY = FMath::RoundToInt((static_cast<float>(SampleY) / (SampleGridSize - 1)) * (TextureData.Height - 1));
                    const FLinearColor Color = TextureData.GetLinearColor(PixelX, PixelY);
                    const FLinearColor HSV = Color.LinearRGBToHSV();
                    MeanColor += Color;
                    SaturationSum += HSV.G;
                    ValueSum += HSV.B;
                    ++SampleCount;
                }
            }

            if (SampleCount > 0)
            {
                MeanColor /= static_cast<float>(SampleCount);
                const double AvgSaturation = SaturationSum / SampleCount;
                const double AvgValue = ValueSum / SampleCount;
                double       VarianceSum = 0.0;

                for (int32 SampleY = 0; SampleY < SampleGridSize; ++SampleY)
                {
                    for (int32 SampleX = 0; SampleX < SampleGridSize; ++SampleX)
                    {
                        const int32        PixelX = FMath::RoundToInt((static_cast<float>(SampleX) / (SampleGridSize - 1)) * (TextureData.Width - 1));
                        const int32        PixelY = FMath::RoundToInt((static_cast<float>(SampleY) / (SampleGridSize - 1)) * (TextureData.Height - 1));
                        const FLinearColor Color = TextureData.GetLinearColor(PixelX, PixelY);
                        const FVector3f    Delta(
                            static_cast<float>(Color.R - MeanColor.R),
                            static_cast<float>(Color.G - MeanColor.G),
                            static_cast<float>(Color.B - MeanColor.B));
                        VarianceSum += Delta.SizeSquared();
                    }
                }

                const double AvgVariance = VarianceSum / SampleCount;
                Score += AvgSaturation * 320.0;
                Score += AvgVariance * 420.0;
                Score += FMath::Log2(static_cast<double>(TextureData.Width) * TextureData.Height) * 4.0;

                if (AvgSaturation < 0.05)
                {
                    Score -= 180.0;
                }

                if (AvgVariance < 0.003)
                {
                    Score -= 180.0;
                }

                if (AvgValue > 0.9 && AvgSaturation < 0.08)
                {
                    Score -= 220.0;
                }
            }
        }
    }

    return Score;
}
