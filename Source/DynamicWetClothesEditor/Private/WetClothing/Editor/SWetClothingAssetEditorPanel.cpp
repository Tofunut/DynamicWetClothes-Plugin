#include "SWetClothingAssetEditorPanel.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/DynamicWetClothesEditorUtils.h"
#include "Core/DynamicWetClothesEditorStyle.h"
#include "WetClothing/Widgets/SWetClothingAssetUVView.h"
#include "WetClothingAsset.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Viewport/WetClothingAssetViewport.h"
#include "WetnessProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Materials/MaterialInterface.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "UObject/Package.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include <initializer_list>

#define LOCTEXT_NAMESPACE "WetClothingAssetEditorPanel"

namespace
{
    struct FWetClothingTextureReadback
    {
        int32                Width = 0;
        int32                Height = 0;
        int32                BytesPerPixel = 0;
        bool                 bSRGB = true;
        ETextureSourceFormat Format = TSF_Invalid;
        TArray64<uint8>      RawData;

        bool IsValid() const
        {
            return Width > 0 && Height > 0 && BytesPerPixel > 0 && RawData.Num() >= static_cast<int64>(Width) * Height * BytesPerPixel;
        }

        FLinearColor GetLinearColor(int32 X, int32 Y) const
        {
            if (!IsValid())
            {
                return FLinearColor::Black;
            }

            const int32  ClampedX = FMath::Clamp(X, 0, Width - 1);
            const int32  ClampedY = FMath::Clamp(Y, 0, Height - 1);
            const int64  PixelOffset = (static_cast<int64>(ClampedY) * Width + ClampedX) * BytesPerPixel;
            const uint8* PixelPtr = RawData.GetData() + PixelOffset;
            FColor       SRGBColor = FColor::Black;

            switch (Format)
            {
            case TSF_BGRA8:
                SRGBColor = *reinterpret_cast<const FColor*>(PixelPtr);
                break;

            case TSF_G8:
            {
                const uint8 Intensity = *PixelPtr;
                SRGBColor = FColor(Intensity, Intensity, Intensity, 255);
                break;
            }

            default:
                return FLinearColor::Black;
            }

            return bSRGB ? FLinearColor::FromSRGBColor(SRGBColor) : FLinearColor(SRGBColor);
        }
    };

    struct FWetClothingIslandColorStats
    {
        int32        IslandID = INDEX_NONE;
        double       UVArea = 0.0;
        double       SampleWeight = 0.0;
        FLinearColor AverageColor = FLinearColor::Black;
    };

    struct FWetClothingAutoPartitionCluster
    {
        TArray<int32> IslandIDs;
        FLinearColor  WeightedColorSum = FLinearColor::Black;
        double        SampleWeight = 0.0;
    };

    struct FWetClothingTextureCandidate
    {
        UTexture* Texture = nullptr;
        FString   Label;
        int32     Score = MIN_int32;
    };

    bool   TryReadTextureSourceData(UTexture2D* Texture, FWetClothingTextureReadback& OutTextureData, FString& OutErrorMessage);
    double ScoreTexturePreviewSuitability(UTexture* Texture);

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

    void BuildTextureItems(UMaterialInterface* Material, TArray<TSharedPtr<FWetClothingTextureItem>>& OutItems)
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

    UTexture* ResolveBestMaterialTexture(UMaterialInterface* Material)
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

    TArray<FWetClothingAssetUVTriangle> BuildMaterialSlotPreviewTriangles(const USkeletalMesh* SkeletalMesh, int32 MaterialSlotIndex)
    {
        TArray<FWetClothingAssetUVTriangle> PreviewTriangles;

        if (SkeletalMesh == nullptr || FWetClothingAssetMeshAnalyzer::GetNumUVChannels(SkeletalMesh, 0) <= 0)
        {
            return PreviewTriangles;
        }

        TArray<FWetClothingAssetUVIsland> BuiltIslands;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(SkeletalMesh, 0, 0, MaterialSlotIndex, BuiltIslands, nullptr))
        {
            return PreviewTriangles;
        }

        for (const FWetClothingAssetUVIsland& Island : BuiltIslands)
        {
            PreviewTriangles.Append(Island.UVTriangles);
        }

        return PreviewTriangles;
    }

    class SWetClothingMaterialSlotPreview : public SLeafWidget
    {
      public:
        SLATE_BEGIN_ARGS(SWetClothingMaterialSlotPreview) {}
        SLATE_ARGUMENT(TArray<FWetClothingAssetUVTriangle>, Triangles)
        SLATE_ARGUMENT(UTexture*, PreviewTexture)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            Triangles = InArgs._Triangles;
            PreviewTexture = InArgs._PreviewTexture;
            if (UTexture2D* PreviewTexture2D = Cast<UTexture2D>(PreviewTexture.Get()))
            {
                FString ErrorMessage;
                TryReadTextureSourceData(PreviewTexture2D, PreviewTextureData, ErrorMessage);
            }
        }

        virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
        {
            return FVector2D(48.0f, 48.0f);
        }

        virtual int32 OnPaint(
            const FPaintArgs&        Args,
            const FGeometry&         AllottedGeometry,
            const FSlateRect&        MyCullingRect,
            FSlateWindowElementList& OutDrawElements,
            int32                    LayerId,
            const FWidgetStyle&      InWidgetStyle,
            bool                     bParentEnabled) const override
        {
            const FSlateBrush*          WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
            const FVector2D             LocalSize = AllottedGeometry.GetLocalSize();
            const FSlateRenderTransform RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();

            FSlateDrawElement::MakeBox(
                OutDrawElements,
                LayerId,
                AllottedGeometry.ToPaintGeometry(),
                WhiteBrush,
                ESlateDrawEffect::None,
                FLinearColor(0.03f, 0.03f, 0.03f, 1.0f));

            if (Triangles.Num() == 0 || LocalSize.X <= 1.0f || LocalSize.Y <= 1.0f)
            {
                return LayerId + 1;
            }

            const FQuat ViewRotation = FRotator(-18.0f, -32.0f, 0.0f).Quaternion();
            struct FProjectedTriangle
            {
                FVector2D Positions[3];
                FVector2D UVs[3];
            };

            TArray<FProjectedTriangle> ProjectedTriangles;
            ProjectedTriangles.Reserve(Triangles.Num());

            bool      bHasBounds = false;
            FVector2D MinPoint = FVector2D::ZeroVector;
            FVector2D MaxPoint = FVector2D::ZeroVector;

            for (const FWetClothingAssetUVTriangle& Triangle : Triangles)
            {
                FProjectedTriangle ProjectedTriangle;

                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    const FVector   RotatedPosition = ViewRotation.RotateVector(Triangle.LocalPositions[CornerIndex]);
                    const FVector2D ProjectedPoint(RotatedPosition.Y, -RotatedPosition.Z);
                    ProjectedTriangle.Positions[CornerIndex] = ProjectedPoint;
                    ProjectedTriangle.UVs[CornerIndex] = Triangle.UVs[CornerIndex];

                    if (!bHasBounds)
                    {
                        MinPoint = ProjectedPoint;
                        MaxPoint = ProjectedPoint;
                        bHasBounds = true;
                    }
                    else
                    {
                        MinPoint.X = FMath::Min(MinPoint.X, ProjectedPoint.X);
                        MinPoint.Y = FMath::Min(MinPoint.Y, ProjectedPoint.Y);
                        MaxPoint.X = FMath::Max(MaxPoint.X, ProjectedPoint.X);
                        MaxPoint.Y = FMath::Max(MaxPoint.Y, ProjectedPoint.Y);
                    }
                }

                ProjectedTriangles.Add(ProjectedTriangle);
            }

            if (!bHasBounds)
            {
                return LayerId + 1;
            }

            const FVector2D BoundsSize = MaxPoint - MinPoint;
            const float     Padding = 5.0f;
            const float     AvailableWidth = FMath::Max(1.0f, LocalSize.X - Padding * 2.0f);
            const float     AvailableHeight = FMath::Max(1.0f, LocalSize.Y - Padding * 2.0f);
            const float     ScaleX = AvailableWidth / FMath::Max(BoundsSize.X, 1.0f);
            const float     ScaleY = AvailableHeight / FMath::Max(BoundsSize.Y, 1.0f);
            const float     UniformScale = FMath::Max(0.01f, FMath::Min(ScaleX, ScaleY));
            const FVector2D ScaledSize = BoundsSize * UniformScale;
            const FVector2D Offset(
                (LocalSize.X - ScaledSize.X) * 0.5f,
                (LocalSize.Y - ScaledSize.Y) * 0.5f);

            if (PreviewTextureData.IsValid())
            {
                const FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
                if (ResourceHandle.IsValid())
                {
                    TArray<FSlateVertex> FillVerts;
                    TArray<SlateIndex>   FillIndices;
                    FillVerts.Reserve(ProjectedTriangles.Num() * 3);
                    FillIndices.Reserve(ProjectedTriangles.Num() * 3);

                    for (const FProjectedTriangle& ProjectedTriangle : ProjectedTriangles)
                    {
                        const SlateIndex StartVertexIndex = static_cast<SlateIndex>(FillVerts.Num());

                        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                        {
                            const FVector2D PaintedPosition = (ProjectedTriangle.Positions[CornerIndex] - MinPoint) * UniformScale + Offset;
                            const FVector2D UV(
                                ProjectedTriangle.UVs[CornerIndex].X - FMath::FloorToDouble(ProjectedTriangle.UVs[CornerIndex].X),
                                ProjectedTriangle.UVs[CornerIndex].Y - FMath::FloorToDouble(ProjectedTriangle.UVs[CornerIndex].Y));
                            const int32  SampleX = FMath::RoundToInt(UV.X * (PreviewTextureData.Width - 1));
                            const int32  SampleY = FMath::RoundToInt((1.0f - UV.Y) * (PreviewTextureData.Height - 1));
                            const FColor VertexColor = PreviewTextureData.GetLinearColor(SampleX, SampleY).ToFColor(true);
                            FillVerts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                                RenderTransform,
                                FVector2f(PaintedPosition),
                                FVector2f::ZeroVector,
                                VertexColor));
                        }

                        FillIndices.Add(StartVertexIndex);
                        FillIndices.Add(StartVertexIndex + 1);
                        FillIndices.Add(StartVertexIndex + 2);
                    }

                    FSlateDrawElement::MakeCustomVerts(
                        OutDrawElements,
                        LayerId + 1,
                        ResourceHandle,
                        FillVerts,
                        FillIndices,
                        nullptr,
                        0,
                        0,
                        ESlateDrawEffect::None);
                }
            }

            const FLinearColor LineColor(0.86f, 0.86f, 0.86f, 1.0f);
            for (const FProjectedTriangle& ProjectedTriangle : ProjectedTriangles)
            {
                TArray<FVector2D> PaintedLinePoints;
                PaintedLinePoints.Reserve(4);

                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    PaintedLinePoints.Add((ProjectedTriangle.Positions[CornerIndex] - MinPoint) * UniformScale + Offset);
                }
                const FVector2D FirstPoint = PaintedLinePoints[0];
                PaintedLinePoints.Add(FirstPoint);

                FSlateDrawElement::MakeLines(
                    OutDrawElements,
                    LayerId + 2,
                    AllottedGeometry.ToPaintGeometry(),
                    PaintedLinePoints,
                    ESlateDrawEffect::None,
                    LineColor,
                    true,
                    1.0f);
            }

            return LayerId + 2;
        }

      private:
        TArray<FWetClothingAssetUVTriangle> Triangles;
        TWeakObjectPtr<UTexture>              PreviewTexture;
        FWetClothingTextureReadback           PreviewTextureData;
    };

    bool TryReadTextureSourceData(UTexture2D* Texture, FWetClothingTextureReadback& OutTextureData, FString& OutErrorMessage)
    {
#if WITH_EDITORONLY_DATA
        OutTextureData = FWetClothingTextureReadback();

        if (Texture == nullptr)
        {
            OutErrorMessage = TEXT("Turn on a texture image for the selected material slot before running Auto-Partitioning.");
            return false;
        }

        if (!Texture->Source.IsValid())
        {
            OutErrorMessage = FString::Printf(TEXT("Texture '%s' does not have readable source data."), *Texture->GetName());
            return false;
        }

        const ETextureSourceFormat SourceFormat = Texture->Source.GetFormat();
        if (SourceFormat != TSF_BGRA8 && SourceFormat != TSF_G8)
        {
            OutErrorMessage = FString::Printf(TEXT("Texture '%s' uses an unsupported source format for Auto-Partitioning."), *Texture->GetName());
            return false;
        }

        if (!Texture->Source.GetMipData(OutTextureData.RawData, 0))
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to read source pixels from texture '%s'."), *Texture->GetName());
            return false;
        }

        OutTextureData.Width = Texture->Source.GetSizeX();
        OutTextureData.Height = Texture->Source.GetSizeY();
        OutTextureData.BytesPerPixel = Texture->Source.GetBytesPerPixel();
        OutTextureData.bSRGB = Texture->SRGB;
        OutTextureData.Format = SourceFormat;

        if (!OutTextureData.IsValid())
        {
            OutErrorMessage = FString::Printf(TEXT("Texture '%s' returned invalid source pixel data."), *Texture->GetName());
            return false;
        }

        OutErrorMessage.Reset();
        return true;
#else
        OutErrorMessage = TEXT("Auto-Partitioning requires editor-only texture source data.");
        return false;
#endif
    }

    double ScoreTexturePreviewSuitability(UTexture* Texture)
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
            if (TryReadTextureSourceData(const_cast<UTexture2D*>(Texture2D), TextureData, ErrorMessage))
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

    bool IsPointInsideTriangle(const FVector2D& Point, const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        const auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3)
        {
            return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y);
        };

        const double D1 = Sign(Point, A, B);
        const double D2 = Sign(Point, B, C);
        const double D3 = Sign(Point, C, A);
        const bool   bHasNegative = D1 < 0.0 || D2 < 0.0 || D3 < 0.0;
        const bool   bHasPositive = D1 > 0.0 || D2 > 0.0 || D3 > 0.0;
        return !(bHasNegative && bHasPositive);
    }

    bool TryComputeIslandAverageColor(
        const FWetClothingAssetUVIsland& Island,
        const FWetClothingTextureReadback& TextureData,
        FWetClothingIslandColorStats&      OutStats)
    {
        if (!TextureData.IsValid())
        {
            return false;
        }

        FLinearColor WeightedColorSum = FLinearColor::Black;
        double       SampleWeight = 0.0;

        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            const FVector2D& A = Triangle.UVs[0];
            const FVector2D& B = Triangle.UVs[1];
            const FVector2D& C = Triangle.UVs[2];

            const double MinU = FMath::Min3(A.X, B.X, C.X);
            const double MaxU = FMath::Max3(A.X, B.X, C.X);
            const double MinV = FMath::Min3(A.Y, B.Y, C.Y);
            const double MaxV = FMath::Max3(A.Y, B.Y, C.Y);

            const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * TextureData.Width), 0, TextureData.Width - 1);
            const int32 MaxX = FMath::Clamp(FMath::FloorToInt(MaxU * TextureData.Width), 0, TextureData.Width - 1);
            const int32 MinY = FMath::Clamp(FMath::FloorToInt((1.0 - MaxV) * TextureData.Height), 0, TextureData.Height - 1);
            const int32 MaxY = FMath::Clamp(FMath::FloorToInt((1.0 - MinV) * TextureData.Height), 0, TextureData.Height - 1);

            double TriangleSampleWeight = 0.0;
            for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
            {
                for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
                {
                    const FVector2D SampleUV(
                        (static_cast<double>(PixelX) + 0.5) / TextureData.Width,
                        1.0 - ((static_cast<double>(PixelY) + 0.5) / TextureData.Height));

                    if (!IsPointInsideTriangle(SampleUV, A, B, C))
                    {
                        continue;
                    }

                    WeightedColorSum += TextureData.GetLinearColor(PixelX, PixelY);
                    ++SampleWeight;
                    ++TriangleSampleWeight;
                }
            }

            if (TriangleSampleWeight <= 0.0)
            {
                const FVector2D TriangleCenter = (A + B + C) / 3.0f;
                const int32     FallbackX = FMath::Clamp(FMath::FloorToInt(TriangleCenter.X * TextureData.Width), 0, TextureData.Width - 1);
                const int32     FallbackY = FMath::Clamp(FMath::FloorToInt((1.0 - TriangleCenter.Y) * TextureData.Height), 0, TextureData.Height - 1);
                WeightedColorSum += TextureData.GetLinearColor(FallbackX, FallbackY);
                ++SampleWeight;
            }
        }

        if (SampleWeight <= 0.0)
        {
            return false;
        }

        OutStats.IslandID = Island.IslandID;
        OutStats.UVArea = Island.UVArea;
        OutStats.SampleWeight = SampleWeight;
        OutStats.AverageColor = WeightedColorSum / static_cast<float>(SampleWeight);
        return true;
    }

    FLinearColor GetClusterAverageColor(const FWetClothingAutoPartitionCluster& Cluster)
    {
        return Cluster.SampleWeight > 0.0
                   ? Cluster.WeightedColorSum / static_cast<float>(Cluster.SampleWeight)
                   : FLinearColor::Black;
    }

    double ComputeColorDistancePercent(const FLinearColor& A, const FLinearColor& B)
    {
        const double DeltaR = A.R - B.R;
        const double DeltaG = A.G - B.G;
        const double DeltaB = A.B - B.B;
        return FMath::Sqrt((DeltaR * DeltaR + DeltaG * DeltaG + DeltaB * DeltaB) / 3.0) * 100.0;
    }
} // namespace

void SWetClothingAssetEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;

    const FSlateFontInfo PanelHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16);
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);
    const FSlateFontInfo AssignButtonFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11);

    MaterialThumbnailPool = MakeShared<FAssetThumbnailPool>(32);

    UVSelectionToolItems.Reset();
    auto AddSelectionToolItem = [this](EWetClothingAssetUVSelectionTool Tool, const FText& Label, const FText& Tooltip, const FName IconBrushName)
    {
        FUVSelectionToolItemPtr ToolItem = MakeShared<FWetClothingUVSelectionToolItem>();
        ToolItem->Tool = Tool;
        ToolItem->Label = Label;
        ToolItem->Tooltip = Tooltip;
        ToolItem->IconBrushName = IconBrushName;
        UVSelectionToolItems.Add(ToolItem);
        return ToolItem;
    };

    FUVSelectionToolItemPtr SelectToolItem = AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::Select,
        LOCTEXT("UVSelectionToolSelect", "Select"),
        LOCTEXT("UVSelectionToolSelectTooltip", "Click a UV island to select it. Hold Shift to add to the current selection."),
        TEXT("DynamicWetClothesEditor.UVTool.Select"));
    AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::BoxSelect,
        LOCTEXT("UVSelectionToolBoxSelect", "Box Select"),
        LOCTEXT("UVSelectionToolBoxSelectTooltip", "Drag a box to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DynamicWetClothesEditor.UVTool.BoxSelect"));
    AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::EllipseSelect,
        LOCTEXT("UVSelectionToolEllipseSelect", "Ellipse Select"),
        LOCTEXT("UVSelectionToolEllipseSelectTooltip", "Drag an ellipse to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DynamicWetClothesEditor.UVTool.EllipseSelect"));
    AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::LassoSelect,
        LOCTEXT("UVSelectionToolLassoSelect", "Lasso Select"),
        LOCTEXT("UVSelectionToolLassoSelectTooltip", "Draw a freeform lasso to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DynamicWetClothesEditor.UVTool.LassoSelect"));

    SelectedUVSelectionToolItem = SelectToolItem;
    CurrentUVSelectionTool = EWetClothingAssetUVSelectionTool::Select;
    UVDisplayModeItems.Reset();
    UVDisplayModeItems.Add(MakeShared<EWetClothingAssetUVDisplayMode>(EWetClothingAssetUVDisplayMode::Normal));
    UVDisplayModeItems.Add(MakeShared<EWetClothingAssetUVDisplayMode>(EWetClothingAssetUVDisplayMode::OutlineOnly));
    SelectedUVDisplayModeItem = UVDisplayModeItems[0];
    CurrentUVDisplayMode = EWetClothingAssetUVDisplayMode::Normal;

    auto BuildSelectionToolButton = [this](FUVSelectionToolItemPtr ToolItem)
    {
        return SNew(SButton)
            .ButtonColorAndOpacity(this, &SWetClothingAssetEditorPanel::GetUVSelectionToolButtonColor, ToolItem)
            .ContentPadding(FMargin(2.0f))
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .OnClicked(this, &SWetClothingAssetEditorPanel::HandleUVSelectionToolButtonClicked, ToolItem)
            .ToolTipText(ToolItem->Tooltip)
                [SNew(SBox)
                     .WidthOverride(18.0f)
                     .HeightOverride(18.0f)
                     .HAlign(HAlign_Center)
                     .VAlign(VAlign_Center)
                         [SNew(SImage)
                              .Image(this, &SWetClothingAssetEditorPanel::GetUVSelectionToolBrush, ToolItem)
                              .ColorAndOpacity(this, &SWetClothingAssetEditorPanel::GetUVSelectionToolIconColor, ToolItem)]];
    };

    TSharedRef<SHorizontalBox> SelectionToolButtonRow = SNew(SHorizontalBox);
    for (int32 ToolIndex = 0; ToolIndex < UVSelectionToolItems.Num(); ++ToolIndex)
    {
        SelectionToolButtonRow->AddSlot()
            .AutoWidth()
            .Padding(ToolIndex + 1 < UVSelectionToolItems.Num() ? FMargin(0.0f, 0.0f, 4.0f, 0.0f) : FMargin(0.0f))
                [BuildSelectionToolButton(UVSelectionToolItems[ToolIndex])];
    }

    ChildSlot
        [SNew(SSplitter)

         // Column 1: Target Mesh / UV Channel / Material Slots / Wet Part Map.
         + SSplitter::Slot()
               .Value(0.25f)
                   [SNew(SBorder)
                        .Padding(10.0f)
                            [SNew(SVerticalBox)

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 10.0f, 0.0f, 4.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .FillWidth(1.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("ProfileDetailsLabel", "Wet Clothing Asset"))
                                                       .Font(PanelHeadingFont)]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                                  [SNew(SButton)
                                                       .Text(LOCTEXT("SaveAssetButton", "Save"))
                                                       .OnClicked(this, &SWetClothingAssetEditorPanel::HandleSaveAssetClicked)]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                                       [SNew(SSeparator)
                                            .Orientation(Orient_Horizontal)]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                       [DetailsView.IsValid()
                                            ? StaticCastSharedRef<SWidget>(DetailsView.ToSharedRef())
                                            : StaticCastSharedRef<SWidget>(
                                                  SNew(STextBlock)
                                                      .Text(LOCTEXT("MissingDetails", "Details view is unavailable.")))]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 16.0f)
                                       [SAssignNew(UVChannelComboBox, SComboBox<FUVChannelItemPtr>)
                                            .OptionsSource(&UVChannelItems)
                                            .OnGenerateWidget(this, &SWetClothingAssetEditorPanel::GenerateUVChannelComboItem)
                                            .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleUVChannelSelectionChanged)
                                                [SNew(STextBlock)
                                                     .Text(this, &SWetClothingAssetEditorPanel::GetSelectedUVChannelText)]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 14.0f, 0.0f, 4.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .FillWidth(1.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("MaterialSlotsLabel", "Material Slots"))
                                                       .Font(SectionHeadingFont)]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .VAlign(VAlign_Center)
                                                  [SNew(STextBlock)
                                                       .Text(this, &SWetClothingAssetEditorPanel::GetMaterialSlotCountText)]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                       [SNew(SSeparator)
                                            .Orientation(Orient_Horizontal)]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .VAlign(VAlign_Center)
                                                  [SNew(SButton)
                                                       .Text(LOCTEXT("AutoPartitionButton", "Auto-Partitioning"))
                                                       .IsEnabled(this, &SWetClothingAssetEditorPanel::IsAutoPartitionEnabled)
                                                       .OnClicked(this, &SWetClothingAssetEditorPanel::HandleAutoPartitionClicked)]

                                        + SHorizontalBox::Slot()
                                              .FillWidth(1.0f)
                                              .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                         .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("AutoPartitionToleranceLabel", "Color Tolerance"))]

                                                   + SHorizontalBox::Slot()
                                                         .FillWidth(1.0f)
                                                             [SNew(SSpinBox<float>)
                                                                  .MinValue(0.0f)
                                                                  .MaxValue(100.0f)
                                                                  .MinSliderValue(0.0f)
                                                                  .MaxSliderValue(100.0f)
                                                                  .Delta(0.1f)
                                                                  .Value(this, &SWetClothingAssetEditorPanel::GetAutoPartitionTolerance)
                                                                  .OnValueChanged(this, &SWetClothingAssetEditorPanel::HandleAutoPartitionToleranceChanged)]]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                       [SNew(SBox)
                                            .HeightOverride(230.0f)
                                                [SAssignNew(MaterialSlotListView, SListView<FMaterialSlotItemPtr>)
                                                     .ListItemsSource(&MaterialSlotItems)
                                                     .OnGenerateRow(this, &SWetClothingAssetEditorPanel::GenerateMaterialSlotRow)
                                                     .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleMaterialSlotSelectionChanged)
                                                     .SelectionMode(ESelectionMode::Single)]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 16.0f, 0.0f, 4.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .FillWidth(1.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(STextBlock)
                                                       .Text(this, &SWetClothingAssetEditorPanel::GetWetPartSectionText)
                                                       .Font(SectionHeadingFont)]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                                  [SNew(SButton)
                                                       .Text(LOCTEXT("AddWetPartButton", "+ Add Part"))
                                                       .OnClicked(this, &SWetClothingAssetEditorPanel::HandleAddWetPartClicked)]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                                  [SNew(SButton)
                                                       .Text(LOCTEXT("RemoveWetPartButton", "Remove"))
                                                       .IsEnabled(this, &SWetClothingAssetEditorPanel::IsWetPartRemoveEnabled)
                                                       .OnClicked(this, &SWetClothingAssetEditorPanel::HandleRemoveWetPartClicked)]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                       [SNew(SSeparator)
                                            .Orientation(Orient_Horizontal)]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                       [SNew(STextBlock)
                                            .AutoWrapText(true)
                                            .Text(this, &SWetClothingAssetEditorPanel::GetSelectedWetPartText)]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                       [SNew(STextBlock)
                                            .AutoWrapText(true)
                                            .Text(this, &SWetClothingAssetEditorPanel::GetWetnessProfileLibraryStatusText)
                                            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]

                             + SVerticalBox::Slot()
                                   .FillHeight(1.0f)
                                       [SAssignNew(WetPartListView, SListView<FWetPartEntryPtr>)
                                            .ListItemsSource(&CurrentWetPartItems)
                                            .OnGenerateRow(this, &SWetClothingAssetEditorPanel::GenerateWetPartRow)
                                            .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleWetPartSelectionChanged)
                                            .OnMouseButtonDoubleClick(this, &SWetClothingAssetEditorPanel::HandleWetPartItemDoubleClicked)
                                            .SelectionMode(ESelectionMode::Single)]]]

         // Column 2: UV View, with UV Islands directly underneath.
         + SSplitter::Slot()
               .Value(0.375f)
                   [SNew(SBorder)
                        .Padding(8.0f)
                            [SNew(SSplitter)
                                 .Orientation(Orient_Vertical)

                             + SSplitter::Slot()
                                   .Value(0.58f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 10.0f, 0.0f, 4.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("UVViewLabel", "UV View"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SNew(SSeparator)
                                                       .Orientation(Orient_Horizontal)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetClothingAssetEditorPanel::GetUVStatusText)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .FillWidth(1.0f)
                                                             [SNew(SBorder)
                                                                  .Padding(6.0f)
                                                                  .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                                                                      [SAssignNew(TextureSelectionContainer, SBox)]]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                         .Padding(10.0f, 0.0f, 4.0f, 0.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("UVSelectionToolLabel", "Tool:"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                             [SelectionToolButtonRow]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                         .Padding(10.0f, 0.0f, 4.0f, 0.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("UVDisplayModeLabel", "View:"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                             [SAssignNew(UVDisplayModeComboBox, SComboBox<FUVDisplayModeItemPtr>)
                                                                  .OptionsSource(&UVDisplayModeItems)
                                                                  .InitiallySelectedItem(SelectedUVDisplayModeItem)
                                                                  .OnGenerateWidget(this, &SWetClothingAssetEditorPanel::GenerateUVDisplayModeComboItem)
                                                                  .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleUVDisplayModeSelectionChanged)
                                                                      [SNew(STextBlock)
                                                                           .Text(this, &SWetClothingAssetEditorPanel::GetSelectedUVDisplayModeText)]]]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(UVView, SWetClothingAssetUVView)
                                                       .OnIslandSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleUVIslandSelectionChangedFromUVView)]]

                             + SSplitter::Slot()
                                   .Value(0.42f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 10.0f, 0.0f, 4.0f)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("UVIslandLabel", "UV Islands"))
                                                                  .Font(SectionHeadingFont)]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("AssignTargetLabel", "Target:"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(SBox)
                                                                  .WidthOverride(220.0f)
                                                                      [SAssignNew(AssignWetPartComboBox, SComboBox<FWetPartEntryPtr>)
                                                                           .OptionsSource(&CurrentWetPartItems)
                                                                           .OnGenerateWidget(this, &SWetClothingAssetEditorPanel::GenerateAssignWetPartComboItem)
                                                                           .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleAssignWetPartSelectionChanged)
                                                                               [SNew(SHorizontalBox)

                                                                                + SHorizontalBox::Slot()
                                                                                      .AutoWidth()
                                                                                      .VAlign(VAlign_Center)
                                                                                      .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                                                          [SNew(SBox)
                                                                                               .WidthOverride(14.0f)
                                                                                               .HeightOverride(14.0f)
                                                                                                   [SNew(SBorder)
                                                                                                        .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                                                                                        .BorderBackgroundColor(this, &SWetClothingAssetEditorPanel::GetSelectedAssignWetPartColor)]]

                                                                                + SHorizontalBox::Slot()
                                                                                      .FillWidth(1.0f)
                                                                                      .VAlign(VAlign_Center)
                                                                                          [SNew(STextBlock)
                                                                                               .Text(this, &SWetClothingAssetEditorPanel::GetSelectedAssignWetPartText)]]]]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(SButton)
                                                                  .ContentPadding(FMargin(10.0f, 4.0f))
                                                                  .HAlign(HAlign_Center)
                                                                  .VAlign(VAlign_Center)
                                                                  .OnClicked(this, &SWetClothingAssetEditorPanel::HandleAssignSelectedIslandToWetPartClicked)
                                                                      [SNew(STextBlock)
                                                                           .Text(this, &SWetClothingAssetEditorPanel::GetAssignIslandToWetPartText)
                                                                           .Font(AssignButtonFont)]]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(SSeparator)
                                                       .Orientation(Orient_Horizontal)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                  [SNew(STextBlock)
                                                       .Text(this, &SWetClothingAssetEditorPanel::GetUVIslandCountText)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetClothingAssetEditorPanel::GetSelectedUVIslandText)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(UVIslandListView, SListView<FUVIslandItemPtr>)
                                                       .ListItemsSource(&UVIslandItems)
                                                       .OnGenerateRow(this, &SWetClothingAssetEditorPanel::GenerateUVIslandRow)
                                                       .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleUVIslandSelectionChanged)
                                                       .SelectionMode(ESelectionMode::Multi)]]]]

         // Column 3: 3D Viewport.
         + SSplitter::Slot()
               .Value(0.375f)
                   [SNew(SBorder)
                        .Padding(8.0f)
                            [SNew(SVerticalBox)

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 10.0f, 0.0f, 4.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .FillWidth(1.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("Viewport3DLabel", "3D Viewport"))
                                                       .Font(SectionHeadingFont)]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("SelectionLineThicknessLabel", "Selection Line"))]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(SBox)
                                                       .WidthOverride(88.0f)
                                                           [SNew(SSpinBox<float>)
                                                                .MinValue(0.25f)
                                                                .MaxValue(4.0f)
                                                                .MinSliderValue(0.25f)
                                                                .MaxSliderValue(4.0f)
                                                                .Delta(0.05f)
                                                                .Value(this, &SWetClothingAssetEditorPanel::GetSelectionLineThicknessScale)
                                                                .OnValueChanged(this, &SWetClothingAssetEditorPanel::HandleSelectionLineThicknessChanged)]]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                                  [SNew(SButton)
                                                       .Text(LOCTEXT("FocusMeshButton", "Focus Mesh"))
                                                       .OnClicked(this, &SWetClothingAssetEditorPanel::HandleFocusPreviewClicked)]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                       [SNew(SSeparator)
                                            .Orientation(Orient_Horizontal)]

                             + SVerticalBox::Slot()
                                   .FillHeight(1.0f)
                                       [SAssignNew(PreviewViewport, SWetClothingAssetViewport)
                                            .WetClothingAsset(WetClothingAsset.Get())
                                            .OnIslandPicked(this, &SWetClothingAssetEditorPanel::HandleUVIslandPickedFromPreview)]]]];

    RefreshFromAsset();
}

void SWetClothingAssetEditorPanel::RefreshFromAsset()
{
    RebuildRuntimeDataIfStale();
    RefreshAvailableWetnessProfiles();
    RefreshMaterialSlotItems();
    RefreshUVChannels();
    RefreshMaterialTextures();

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreviewMesh();

        if (SelectedMaterialSlotIndex != INDEX_NONE)
        {
            PreviewViewport->SetHighlightedMaterialSlot(SelectedMaterialSlotIndex);
        }
        else
        {
            PreviewViewport->ClearMaterialSlotHighlight();
        }
    }

    RefreshPreviewIslandHighlight();
    RefreshPreviewWetPartOverlay();
}

void SWetClothingAssetEditorPanel::RebuildRuntimeDataAndMarkDirty()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr)
    {
        return;
    }

    FString ErrorMessage;
    if (Profile->TargetMesh != nullptr)
    {
        if (!Profile->RebuildRuntimeData(&ErrorMessage))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("WetClothingAssetEditor: Failed to rebuild runtime data for %s: %s"),
                *GetNameSafe(Profile),
                *ErrorMessage);
        }
    }
    else
    {
        Profile->ClearRuntimeData();
    }

    Profile->MarkPackageDirty();
}

void SWetClothingAssetEditorPanel::RebuildRuntimeDataIfStale()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr)
    {
        return;
    }

    if (Profile->TargetMesh == nullptr)
    {
        if (Profile->GetBakedRuntimeData().bIsValid)
        {
            Profile->Modify();
            Profile->ClearRuntimeData();
            Profile->MarkPackageDirty();
        }
        return;
    }

    if (!Profile->IsRuntimeDataValidForMesh(Profile->TargetMesh, 0))
    {
        Profile->Modify();
        RebuildRuntimeDataAndMarkDirty();
    }
}

void SWetClothingAssetEditorPanel::RefreshMaterialSlotItems()
{
    const int32 PreviousSelection = SelectedMaterialSlotIndex;

    MaterialSlotItems.Reset();
    MaterialSlotThumbnails.Reset();
    SelectedMaterialSlotIndex = INDEX_NONE;

    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        if (const USkeletalMesh* TargetMesh = Profile->TargetMesh)
        {
            const TArray<FSkeletalMaterial>& Materials = TargetMesh->GetMaterials();

            for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
            {
                const FSkeletalMaterial& SkeletalMaterial = Materials[MaterialIndex];

                FMaterialSlotItemPtr Item = MakeShared<FWetClothingMaterialSlotItem>();
                Item->SlotIndex = MaterialIndex;
                Item->SlotName = SkeletalMaterial.MaterialSlotName;
                Item->Material = SkeletalMaterial.MaterialInterface;
                MaterialSlotItems.Add(Item);
            }
        }
    }

    if (MaterialSlotItems.IsValidIndex(PreviousSelection))
    {
        SelectedMaterialSlotIndex = PreviousSelection;
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();

        if (MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex))
        {
            MaterialSlotListView->SetSelection(MaterialSlotItems[SelectedMaterialSlotIndex], ESelectInfo::Direct);
        }
        else
        {
            MaterialSlotListView->ClearSelection();
        }
    }
}

void SWetClothingAssetEditorPanel::RefreshMaterialTextures()
{
    const bool bPreviousShowMaterialTextureInUVView = bShowMaterialTextureInUVView;
    UTexture*  PreviousSelectedTexture = SelectedTextureItem.IsValid() ? SelectedTextureItem->Texture.Get() : nullptr;
    TextureItems.Reset();
    TextureThumbnails.Reset();
    SelectedTextureItem.Reset();

    if (MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex))
    {
        const FMaterialSlotItemPtr& MaterialSlotItem = MaterialSlotItems[SelectedMaterialSlotIndex];
        if (MaterialSlotItem.IsValid() && MaterialSlotItem->Material.IsValid())
        {
            BuildTextureItems(MaterialSlotItem->Material.Get(), TextureItems);

            for (const FTextureItemPtr& TextureItem : TextureItems)
            {
                if (TextureItem.IsValid() && TextureItem->Texture.Get() == PreviousSelectedTexture)
                {
                    SelectedTextureItem = TextureItem;
                    break;
                }
            }
        }
    }

    if (!SelectedTextureItem.IsValid())
    {
        for (const FTextureItemPtr& TextureItem : TextureItems)
        {
            if (TextureItem.IsValid() && TextureItem->Texture.IsValid())
            {
                SelectedTextureItem = TextureItem;
                break;
            }
        }
    }

    if (!SelectedTextureItem.IsValid() && TextureItems.Num() > 0)
    {
        SelectedTextureItem = TextureItems[0];
    }

    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid()
                                       ? bPreviousShowMaterialTextureInUVView
                                       : false;

    RefreshTextureToggleWidgets();
    RefreshUVView();
}

void SWetClothingAssetEditorPanel::RefreshTextureToggleWidgets()
{
    TextureThumbnails.Reset();

    if (!TextureSelectionContainer.IsValid())
    {
        return;
    }

    if (!MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex))
    {
        TextureSelectionContainer->SetContent(
            SNew(STextBlock)
                .Text(LOCTEXT("SelectMaterialSlotForTextures", "Select a material slot to choose its texture."))
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover)));
        return;
    }

    const bool bHasActualTexture = TextureItems.ContainsByPredicate([](const FTextureItemPtr& TextureItem)
                                                                    { return TextureItem.IsValid() && TextureItem->Texture.IsValid(); });

    if (!bHasActualTexture)
    {
        TextureSelectionContainer->SetContent(
            SNew(STextBlock)
                .Text(LOCTEXT("NoMaterialTextures", "No textures were found on this material slot."))
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover)));
        return;
    }

    TextureSelectionContainer->SetContent(
        SAssignNew(TextureComboBox, SComboBox<FTextureItemPtr>)
            .OptionsSource(&TextureItems)
            .InitiallySelectedItem(SelectedTextureItem)
            .OnGenerateWidget(this, &SWetClothingAssetEditorPanel::GenerateTextureComboItem)
            .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleTextureSelectionChanged)
            .MaxListHeight(360.0f)
            .ContentPadding(FMargin(6.0f, 4.0f))
                [SAssignNew(SelectedTextureComboContentBox, SBox)
                     [BuildTextureComboContent(SelectedTextureItem, 24.0f, true)]]);
}

void SWetClothingAssetEditorPanel::RefreshUVChannels()
{
    const int32 PreviousUVChannelIndex = SelectedUVChannelItem.IsValid() ? *SelectedUVChannelItem : INDEX_NONE;

    UVChannelItems.Reset();
    SelectedUVChannelItem.Reset();

    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(Profile->TargetMesh, 0);
        for (int32 UVChannelIndex = 0; UVChannelIndex < NumUVChannels; ++UVChannelIndex)
        {
            UVChannelItems.Add(MakeShared<int32>(UVChannelIndex));
        }

        if (UVChannelItems.IsValidIndex(PreviousUVChannelIndex))
        {
            SelectedUVChannelItem = UVChannelItems[PreviousUVChannelIndex];
        }
        else if (UVChannelItems.Num() > 0)
        {
            SelectedUVChannelItem = UVChannelItems[0];
        }
    }

    if (UVChannelComboBox.IsValid())
    {
        UVChannelComboBox->RefreshOptions();

        if (SelectedUVChannelItem.IsValid())
        {
            UVChannelComboBox->SetSelectedItem(SelectedUVChannelItem);
        }
    }

    RefreshWetPartList();
    RefreshUVIslandList();
}

void SWetClothingAssetEditorPanel::RefreshUVIslandList()
{
    const int32       PreviousPrimaryIslandID = SelectedUVIslandID;
    const TSet<int32> PreviousSelectedIslandIDs = SelectedUVIslandIDs;

    UVIslandItems.Reset();
    ResetIslandSelection();
    UVStatusMessage = TEXT("Select a material slot to inspect its UV islands.");

    const UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || Profile->TargetMesh == nullptr)
    {
        UVStatusMessage = TEXT("Assign a TargetMesh to see its UV islands.");
    }
    else if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        UVStatusMessage = TEXT("Select a material slot to inspect its UV islands.");
    }
    else if (!SelectedUVChannelItem.IsValid())
    {
        UVStatusMessage = TEXT("No UV channels are available on LOD 0.");
    }
    else
    {
        TArray<FWetClothingAssetUVIsland> BuiltIslands;
        FString                             ErrorMessage;
        const bool                          bBuiltIslands = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(Profile->TargetMesh, 0, *SelectedUVChannelItem, SelectedMaterialSlotIndex, BuiltIslands, &ErrorMessage);
        if (!bBuiltIslands)
        {
            UVStatusMessage = ErrorMessage;
        }
        else if (BuiltIslands.Num() == 0)
        {
            UVStatusMessage = TEXT("No UV islands were found for the selected slot in LOD 0.");
        }
        else
        {
            for (const FWetClothingAssetUVIsland& Island : BuiltIslands)
            {
                UVIslandItems.Add(MakeShared<FWetClothingAssetUVIsland>(Island));
            }
            UVStatusMessage = FString::Printf(TEXT("LOD 0 / UV Channel %d / Slot %d / %d islands"), *SelectedUVChannelItem, SelectedMaterialSlotIndex, UVIslandItems.Num());
        }
    }

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (IslandItem.IsValid() && PreviousSelectedIslandIDs.Contains(IslandItem->IslandID))
        {
            SelectedUVIslandIDs.Add(IslandItem->IslandID);
        }
    }
    if (SelectedUVIslandIDs.Contains(PreviousPrimaryIslandID))
    {
        SelectedUVIslandID = PreviousPrimaryIslandID;
    }
    else if (SelectedUVIslandIDs.Num() > 0)
    {
        SelectedUVIslandID = *SelectedUVIslandIDs.CreateConstIterator();
    }

    if (UVIslandListView.IsValid())
    {
        UVIslandListView->RequestListRefresh();
        SyncUVIslandListSelectionToState();
    }

    RefreshWetPartList();
    RefreshUVView();
    RefreshPreviewIslandHighlight();
}

void SWetClothingAssetEditorPanel::RefreshUVView()
{
    if (!UVView.IsValid())
    {
        return;
    }

    UVView->SetBackgroundTexture(bShowMaterialTextureInUVView ? ResolveSelectedMaterialTexture() : nullptr);
    UVView->SetIslands(UVIslandItems);
    UVView->SetIslandColors(BuildIslandColorMap());
    UVView->SetSelectedIslands(SelectedUVIslandIDs);
    UVView->SetSelectionTool(CurrentUVSelectionTool);
    UVView->SetDisplayMode(CurrentUVDisplayMode);

    RefreshPreviewWetPartOverlay();
}

void SWetClothingAssetEditorPanel::RefreshPreviewIslandHighlight()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    PreviewViewport->SetSelectableIslands(UVIslandItems);
    PreviewViewport->SetHighlightedIslandIDs(SelectedUVIslandIDs);
}

void SWetClothingAssetEditorPanel::RefreshWetPartList()
{
    const int32 PreviousSelectedWetPart = SelectedWetPartID;
    const int32 PreviousAssignWetPartID = SelectedAssignWetPartID;

    CurrentWetPartItems.Reset();
    SelectedWetPartID = INDEX_NONE;
    SelectedAssignWetPartID = INDEX_NONE;
    WetPartInlineRenameWidgets.Reset();

    EnsureDefaultWetPartForSelectedScope();

    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex)
            {
                CurrentWetPartItems.Add(MakeShared<FWetClothingAssetWetPartEntry>(Entry));
            }
        }
    }

    CurrentWetPartItems.Sort([](const FWetPartEntryPtr& A, const FWetPartEntryPtr& B)
                             { return A.IsValid() && B.IsValid() ? A->WetPartID < B->WetPartID : A.IsValid(); });

    for (const FWetPartEntryPtr& Item : CurrentWetPartItems)
    {
        if (Item.IsValid() && Item->WetPartID == PreviousSelectedWetPart)
        {
            SelectedWetPartID = PreviousSelectedWetPart;
        }

        if (Item.IsValid() && Item->WetPartID == PreviousAssignWetPartID)
        {
            SelectedAssignWetPartID = PreviousAssignWetPartID;
        }
    }

    if (SelectedAssignWetPartID == INDEX_NONE)
    {
        for (const FWetPartEntryPtr& Item : CurrentWetPartItems)
        {
            if (Item.IsValid() && Item->WetPartID == 0)
            {
                SelectedAssignWetPartID = 0;
                break;
            }
        }
    }

    if (SelectedAssignWetPartID == INDEX_NONE && CurrentWetPartItems.Num() > 0 && CurrentWetPartItems[0].IsValid())
    {
        SelectedAssignWetPartID = CurrentWetPartItems[0]->WetPartID;
    }

    if (WetPartListView.IsValid())
    {
        WetPartListView->RequestListRefresh();

        if (SelectedWetPartID != INDEX_NONE)
        {
            for (const FWetPartEntryPtr& Item : CurrentWetPartItems)
            {
                if (Item.IsValid() && Item->WetPartID == SelectedWetPartID)
                {
                    WetPartListView->SetSelection(Item, ESelectInfo::Direct);
                    break;
                }
            }
        }
        else
        {
            WetPartListView->ClearSelection();
        }
    }

    if (AssignWetPartComboBox.IsValid())
    {
        AssignWetPartComboBox->RefreshOptions();
        AssignWetPartComboBox->SetSelectedItem(FindWetPartItemByID(SelectedAssignWetPartID));
    }

    RefreshUVView();
}

void SWetClothingAssetEditorPanel::RefreshPreviewWetPartOverlay()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectableIslands(UVIslandItems);
        PreviewViewport->SetWetPartIslandAssignments(BuildIslandWetPartIDMap(), BuildIslandColorMap());
    }
}

void SWetClothingAssetEditorPanel::RefreshWetPartWidgets()
{
    WetPartInlineRenameWidgets.Reset();

    if (WetPartListView.IsValid())
    {
        WetPartListView->RequestListRefresh();
    }
}

void SWetClothingAssetEditorPanel::RefreshAvailableWetnessProfiles()
{
    AvailableWetnessProfileItems.Reset();

    FARFilter Filter;
    Filter.ClassPaths.Add(UWetnessProfile::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;

    const TArray<FString> SearchPaths = GetProfileSearchPaths();
    for (const FString& SearchPath : SearchPaths)
    {
        Filter.PackagePaths.Add(*SearchPath);
    }

    TArray<FAssetData>    AssetDataList;
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    AssetRegistryModule.Get().ScanPathsSynchronous(SearchPaths, false);
    AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);

    AssetDataList.Sort([](const FAssetData& A, const FAssetData& B)
                       {
		const FString APath = A.PackagePath.ToString();
		const FString BPath = B.PackagePath.ToString();
		const bool bADefault =
			APath.StartsWith(DynamicWetClothesEditorUtils::DefaultWetnessProfileLibraryPath) ||
			APath.StartsWith(DynamicWetClothesEditorUtils::PluginWetnessProfileLibraryPath);
		const bool bBDefault =
			BPath.StartsWith(DynamicWetClothesEditorUtils::DefaultWetnessProfileLibraryPath) ||
			BPath.StartsWith(DynamicWetClothesEditorUtils::PluginWetnessProfileLibraryPath);
		if (bADefault != bBDefault)
		{
			return bADefault;
		}

		return A.AssetName.ToString() < B.AssetName.ToString(); });

    for (const FAssetData& AssetData : AssetDataList)
    {
        FWetnessProfileAssetItemPtr Item = MakeShared<FWetnessProfileAssetItem>();
        Item->AssetData = AssetData;
        Item->DisplayName = AssetData.AssetName.ToString();
        Item->ContentPath = AssetData.PackagePath.ToString();
        AvailableWetnessProfileItems.Add(Item);
    }
}

void SWetClothingAssetEditorPanel::EnsureDefaultWetPartForSelectedScope()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || !SelectedUVChannelItem.IsValid())
    {
        return;
    }
    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    for (FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID == 0)
        {
            Profile->Modify();
            Entry.Name = GetDefaultWetPartName(0);
            Entry.Color = GetDefaultWetPartColor(0);
            Entry.bViewEnabled = true;
            RebuildRuntimeDataAndMarkDirty();
            if (DetailsView.IsValid())
            {
                DetailsView->ForceRefresh();
            }
            return;
        }
    }

    Profile->Modify();
    FWetClothingAssetWetPartEntry NewEntry;
    NewEntry.MaterialSlotIndex = SelectedMaterialSlotIndex;
    NewEntry.UVChannelIndex = UVChannelIndex;
    NewEntry.WetPartID = 0;
    NewEntry.Name = GetDefaultWetPartName(NewEntry.WetPartID);
    NewEntry.Color = GetDefaultWetPartColor(NewEntry.WetPartID);
    NewEntry.bViewEnabled = true;
    Profile->WetPartEntries.Add(NewEntry);
    RebuildRuntimeDataAndMarkDirty();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
}

int32 SWetClothingAssetEditorPanel::GetSelectedUVChannelIndex() const
{
    return SelectedUVChannelItem.IsValid() ? *SelectedUVChannelItem : 0;
}

int32 SWetClothingAssetEditorPanel::FindNextWetPartForSelectedScope() const
{
    int32 MaxWetPartID = 0;
    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex)
            {
                MaxWetPartID = FMath::Max(MaxWetPartID, Entry.WetPartID);
            }
        }
    }

    return MaxWetPartID + 1;
}

FWetClothingAssetWetPartEntry* SWetClothingAssetEditorPanel::FindMutableWetPartEntry(int32 WetPartID) const
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr)
    {
        return nullptr;
    }

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    for (FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID == WetPartID)
        {
            return &Entry;
        }
    }

    return nullptr;
}

const FWetClothingAssetWetPartEntry* SWetClothingAssetEditorPanel::FindWetPartEntry(int32 WetPartID) const
{
    return FindMutableWetPartEntry(WetPartID);
}

SWetClothingAssetEditorPanel::FWetPartEntryPtr SWetClothingAssetEditorPanel::FindWetPartItemByID(int32 WetPartID) const
{
    for (const FWetPartEntryPtr& Item : CurrentWetPartItems)
    {
        if (Item.IsValid() && Item->WetPartID == WetPartID)
        {
            return Item;
        }
    }

    return nullptr;
}

const FWetClothingAssetWetPartEntry* SWetClothingAssetEditorPanel::FindWetPartEntryForIsland(int32 IslandID) const
{
    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.AssignedIslandIDs.Contains(IslandID))
            {
                return &Entry;
            }
        }
    }
    return nullptr;
}

const FWetClothingAssetWetPartEntry* SWetClothingAssetEditorPanel::FindEffectiveWetPartEntryForIsland(int32 IslandID) const
{
    if (const FWetClothingAssetWetPartEntry* AssignedEntry = FindWetPartEntryForIsland(IslandID))
    {
        return AssignedEntry;
    }
    return FindWetPartEntry(0);
}

TSet<int32> SWetClothingAssetEditorPanel::GetIslandIDsForWetPart(int32 WetPartID) const
{
    TSet<int32> Result;

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (IslandItem.IsValid() && GetEffectiveWetPartForIsland(IslandItem->IslandID) == WetPartID)
        {
            Result.Add(IslandItem->IslandID);
        }
    }

    return Result;
}

int32 SWetClothingAssetEditorPanel::GetEffectiveWetPartForIsland(int32 IslandID) const
{
    if (const FWetClothingAssetWetPartEntry* EffectiveEntry = FindEffectiveWetPartEntryForIsland(IslandID))
    {
        return EffectiveEntry->WetPartID;
    }
    return 0;
}

FLinearColor SWetClothingAssetEditorPanel::GetDefaultWetPartColor(int32 WetPartID) const
{
    if (WetPartID == 0)
    {
        return FLinearColor(0.62f, 0.62f, 0.62f, 1.0f);
    }

    static const FLinearColor Palette[] = {
        FLinearColor(1.00f, 0.00f, 1.00f, 1.0f),
        FLinearColor(0.00f, 0.25f, 1.00f, 1.0f),
        FLinearColor(0.00f, 1.00f, 0.05f, 1.0f),
        FLinearColor(1.00f, 1.00f, 0.00f, 1.0f),
        FLinearColor(1.00f, 0.35f, 0.00f, 1.0f),
        FLinearColor(0.55f, 0.00f, 1.00f, 1.0f),
        FLinearColor(0.00f, 1.00f, 1.00f, 1.0f),
        FLinearColor(1.00f, 0.00f, 0.20f, 1.0f),
        FLinearColor(0.35f, 1.00f, 0.00f, 1.0f),
        FLinearColor(1.00f, 0.00f, 0.65f, 1.0f),
        FLinearColor(0.00f, 0.65f, 1.00f, 1.0f),
        FLinearColor(0.75f, 1.00f, 0.00f, 1.0f),
        FLinearColor(1.00f, 0.60f, 0.00f, 1.0f),
        FLinearColor(0.35f, 0.00f, 1.00f, 1.0f),
        FLinearColor(0.00f, 1.00f, 0.55f, 1.0f),
        FLinearColor(1.00f, 0.00f, 0.00f, 1.0f)
    };

    const int32 PaletteIndex = FMath::Abs(WetPartID - 1) % UE_ARRAY_COUNT(Palette);
    return Palette[PaletteIndex];
}

FString SWetClothingAssetEditorPanel::GetDefaultWetPartName(int32 WetPartID) const
{
    return WetPartID == 0 ? TEXT("Part Default") : FString::Printf(TEXT("Part %d"), WetPartID);
}

FString SWetClothingAssetEditorPanel::GetWetPartDisplayName(const FWetClothingAssetWetPartEntry& Entry) const
{
    const FString TrimmedName = Entry.Name.TrimStartAndEnd();
    if (!TrimmedName.IsEmpty())
    {
        return TrimmedName;
    }

    return GetDefaultWetPartName(Entry.WetPartID);
}

FString SWetClothingAssetEditorPanel::GetAssignedProfileLabel(const FWetClothingAssetWetPartEntry& Entry) const
{
    const FString TrimmedLabel = Entry.ProfileAssignment.SourceProfileName.TrimStartAndEnd();
    return TrimmedLabel.IsEmpty() ? TEXT("Select Profile") : TrimmedLabel;
}

TArray<FString> SWetClothingAssetEditorPanel::GetProfileSearchPaths() const
{
    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
#if WITH_EDITORONLY_DATA
        return DynamicWetClothesEditorUtils::BuildUniqueProfileSearchPaths(Profile->AdditionalProfileSearchPaths);
#endif
    }

    const TArray<FString> EmptyPaths;
    return DynamicWetClothesEditorUtils::BuildUniqueProfileSearchPaths(EmptyPaths);
}

TMap<int32, int32> SWetClothingAssetEditorPanel::BuildIslandWetPartIDMap() const
{
    TMap<int32, int32> Result;
    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        const int32 EffectiveWetPartID = GetEffectiveWetPartForIsland(IslandItem->IslandID);
        if (EffectiveWetPartID == 0)
        {
            continue;
        }

        if (FindWetPartEntry(EffectiveWetPartID) != nullptr)
        {
            Result.Add(IslandItem->IslandID, EffectiveWetPartID);
        }
    }
    return Result;
}

TMap<int32, FLinearColor> SWetClothingAssetEditorPanel::BuildIslandColorMap() const
{
    TMap<int32, FLinearColor> Result;
    const FLinearColor        HiddenColor(0.45f, 0.45f, 0.45f, 1.0f);

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        if (const FWetClothingAssetWetPartEntry* Entry = FindEffectiveWetPartEntryForIsland(IslandItem->IslandID))
        {
            if (Entry->WetPartID == 0)
            {
                continue;
            }

            FLinearColor Color = Entry->bViewEnabled ? Entry->Color : HiddenColor;
            Color.A = 1.0f;
            Result.Add(IslandItem->IslandID, Color);
        }
    }

    return Result;
}

TSharedRef<ITableRow> SWetClothingAssetEditorPanel::GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    UMaterialInterface* MaterialObject = Item.IsValid() ? Item->Material.Get() : nullptr;
    const FText         SlotTitle = Item.IsValid()
                                        ? FText::Format(
                                      LOCTEXT("MaterialSlotThumbnailTitle", "[{0}] {1}"),
                                      FText::AsNumber(Item->SlotIndex),
                                      FText::FromName(Item->SlotName))
                                        : LOCTEXT("InvalidMaterialSlotTitle", "Invalid Material Slot");

    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
            .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));

    TArray<FWetClothingAssetUVTriangle> SlotPreviewTriangles;
    UTexture*                             SlotPreviewTexture = ResolveBestMaterialTexture(MaterialObject);
    if (Item.IsValid() && Item->SlotIndex == SelectedMaterialSlotIndex && SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid())
    {
        SlotPreviewTexture = SelectedTextureItem->Texture.Get();
    }
    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        SlotPreviewTriangles = BuildMaterialSlotPreviewTriangles(Profile->TargetMesh, Item.IsValid() ? Item->SlotIndex : INDEX_NONE);
    }

    TSharedRef<SWidget> SlotPreviewWidget =
        SNew(SBorder)
            .Padding(0.0f)
            .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Panel")))
                [SNew(SWetClothingMaterialSlotPreview)
                     .Triangles(MoveTemp(SlotPreviewTriangles))
                     .PreviewTexture(SlotPreviewTexture)];

    if (MaterialObject != nullptr && MaterialThumbnailPool.IsValid())
    {
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(MaterialObject, 48, 48, MaterialThumbnailPool);
        MaterialSlotThumbnails.Add(Thumbnail);

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    return SNew(STableRow<FMaterialSlotItemPtr>, OwnerTable)
        .Padding(4.0f)
            [SNew(SHorizontalBox)

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                       [SNew(SBox)
                            .WidthOverride(52.0f)
                            .HeightOverride(52.0f)
                                [ThumbnailWidget]]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                       [SNew(SBox)
                            .WidthOverride(52.0f)
                            .HeightOverride(52.0f)
                                [SlotPreviewWidget]]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                       [SNew(SVerticalBox)

                        + SVerticalBox::Slot()
                              .AutoHeight()
                                  [SNew(STextBlock)
                                       .Text(SlotTitle)]]];
}

void SWetClothingAssetEditorPanel::HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedMaterialSlotIndex = Item.IsValid() ? Item->SlotIndex : INDEX_NONE;
    SelectedWetPartID = INDEX_NONE;
    ResetIslandSelection();
    if (PreviewViewport.IsValid())
    {
        if (SelectedMaterialSlotIndex != INDEX_NONE)
        {
            PreviewViewport->SetHighlightedMaterialSlot(SelectedMaterialSlotIndex);
        }
        else
        {
            PreviewViewport->ClearMaterialSlotHighlight();
        }
    }
    RefreshMaterialTextures();
    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }
    RefreshWetPartList();
    RefreshUVIslandList();
}

TSharedRef<SWidget> SWetClothingAssetEditorPanel::GenerateTextureComboItem(FTextureItemPtr Item)
{
    return BuildTextureComboContent(Item, 36.0f, false);
}

void SWetClothingAssetEditorPanel::HandleTextureSelectionChanged(FTextureItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedTextureItem = Item;
    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid();

    if (SelectedTextureComboContentBox.IsValid())
    {
        SelectedTextureComboContentBox->SetContent(BuildTextureComboContent(SelectedTextureItem, 24.0f, true));
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }

    RefreshUVView();
}

TSharedRef<SWidget> SWetClothingAssetEditorPanel::BuildTextureComboContent(FTextureItemPtr Item, float ThumbnailSize, bool bCompactLayout)
{
    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
            .Padding(0.0f)
            .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));

    if (Item.IsValid() && Item->Texture.IsValid() && MaterialThumbnailPool.IsValid())
    {
        const uint32                ThumbnailDimension = static_cast<uint32>(FMath::RoundToInt(ThumbnailSize));
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(Item->Texture.Get(), ThumbnailDimension, ThumbnailDimension, MaterialThumbnailPool);
        TextureThumbnails.Add(Thumbnail);

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    return SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(ThumbnailSize).HeightOverride(ThumbnailSize)[ThumbnailWidget]] + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(bCompactLayout ? FMargin(8.0f, 0.0f, 18.0f, 0.0f) : FMargin(8.0f, 0.0f, 6.0f, 0.0f))[SNew(STextBlock).Text(Item.IsValid() ? FText::FromString(Item->Label) : LOCTEXT("InvalidTextureComboItem", "Invalid Texture")).OverflowPolicy(ETextOverflowPolicy::Ellipsis)];
}

TSharedRef<SWidget> SWetClothingAssetEditorPanel::GenerateUVChannelComboItem(FUVChannelItemPtr Item)
{
    const FString Label = Item.IsValid()
                              ? FString::Printf(TEXT("UV Channel %d"), *Item)
                              : TEXT("Invalid UV Channel");

    return SNew(STextBlock)
        .Text(FText::FromString(Label));
}

void SWetClothingAssetEditorPanel::HandleUVChannelSelectionChanged(FUVChannelItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedUVChannelItem = Item;
    SelectedWetPartID = INDEX_NONE;
    ResetIslandSelection();
    RefreshMaterialTextures();
    RefreshWetPartList();
    RefreshUVIslandList();
}

TSharedRef<SWidget> SWetClothingAssetEditorPanel::GenerateUVDisplayModeComboItem(FUVDisplayModeItemPtr Item)
{
    const FText Label = (!Item.IsValid() || *Item == EWetClothingAssetUVDisplayMode::Normal)
                            ? LOCTEXT("UVDisplayModeNormal", "Normal")
                            : LOCTEXT("UVDisplayModeOutline", "Outline");

    return SNew(STextBlock)
        .Text(Label);
}

void SWetClothingAssetEditorPanel::HandleUVDisplayModeSelectionChanged(FUVDisplayModeItemPtr Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid())
    {
        return;
    }

    SelectedUVDisplayModeItem = Item;
    CurrentUVDisplayMode = *Item;

    if (UVView.IsValid())
    {
        UVView->SetDisplayMode(CurrentUVDisplayMode);
    }
}

TSharedRef<ITableRow> SWetClothingAssetEditorPanel::GenerateUVIslandRow(FUVIslandItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    FText        RowText = LOCTEXT("InvalidUVIsland", "Invalid UV island");
    FLinearColor SwatchColor(0.06f, 0.06f, 0.06f, 1.0f);
    if (Item.IsValid())
    {
        if (const FWetClothingAssetWetPartEntry* EffectiveEntry = FindEffectiveWetPartEntryForIsland(Item->IslandID))
        {
            SwatchColor = (EffectiveEntry->WetPartID == 0 || EffectiveEntry->bViewEnabled)
                              ? EffectiveEntry->Color
                              : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f);
            SwatchColor.A = 1.0f;
            RowText = FText::Format(LOCTEXT("UVIslandAssignedRow", "Island {0}  |  {1} tris  |  ID {2}"), FText::AsNumber(Item->IslandID), FText::AsNumber(Item->TriangleCount), FText::AsNumber(EffectiveEntry->WetPartID));
        }
    }

    return SNew(STableRow<FUVIslandItemPtr>, OwnerTable)
        .Padding(4.0f)
            [SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SBox).WidthOverride(18.0f).HeightOverride(18.0f)[SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(SwatchColor)]] + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).AutoWrapText(true).Text(RowText)]];
}

void SWetClothingAssetEditorPanel::HandleUVIslandSelectionChanged(FUVIslandItemPtr Item, ESelectInfo::Type SelectInfo)
{
    if (bSyncingUVIslandListSelection || !UVIslandListView.IsValid())
    {
        return;
    }
    TArray<FUVIslandItemPtr> SelectedItems;
    UVIslandListView->GetSelectedItems(SelectedItems);
    TSet<int32> NewSelectedIDs;
    for (const FUVIslandItemPtr& SelectedItem : SelectedItems)
    {
        if (SelectedItem.IsValid())
        {
            NewSelectedIDs.Add(SelectedItem->IslandID);
        }
    }
    const int32 NewPrimaryID = Item.IsValid() ? Item->IslandID : (NewSelectedIDs.Num() > 0 ? *NewSelectedIDs.CreateConstIterator() : INDEX_NONE);
    SetSelectedIslandIDs(NewSelectedIDs, NewPrimaryID, false);
}

void SWetClothingAssetEditorPanel::HandleUVIslandSelectionChangedFromUVView(const TArray<int32>& IslandIDs, EWetClothingAssetUVSelectionOp SelectionOp)
{
    ApplyIslandSelection(IslandIDs, SelectionOp == EWetClothingAssetUVSelectionOp::Add);
}

void SWetClothingAssetEditorPanel::HandleUVIslandPickedFromPreview(int32 IslandID, bool bAppendSelection)
{
    if (IslandID == INDEX_NONE)
    {
        if (!bAppendSelection)
        {
            SetSelectedIslandIDs(TSet<int32>(), INDEX_NONE);
        }
        return;
    }
    ApplyIslandSelection({ IslandID }, bAppendSelection);
}

void SWetClothingAssetEditorPanel::ApplyIslandSelection(const TArray<int32>& HitIslandIDs, bool bAppendSelection)
{
    TSet<int32> NewSelection = bAppendSelection ? SelectedUVIslandIDs : TSet<int32>();
    for (int32 IslandID : HitIslandIDs)
    {
        if (IslandID != INDEX_NONE)
        {
            NewSelection.Add(IslandID);
        }
    }
    const int32 NewPrimaryID = HitIslandIDs.Num() > 0 ? HitIslandIDs.Last() : INDEX_NONE;
    SetSelectedIslandIDs(NewSelection, NewPrimaryID);
}

void SWetClothingAssetEditorPanel::SetSelectedIslandIDs(const TSet<int32>& InSelectedIslandIDs, int32 InPrimarySelectedIslandID, bool bSyncListSelection)
{
    SelectedUVIslandIDs = InSelectedIslandIDs;
    SelectedUVIslandID = SelectedUVIslandIDs.Contains(InPrimarySelectedIslandID) ? InPrimarySelectedIslandID : (SelectedUVIslandIDs.Num() > 0 ? *SelectedUVIslandIDs.CreateConstIterator() : INDEX_NONE);

    bool bKeepSelectedWetPart = SelectedWetPartID == INDEX_NONE;
    if (!bKeepSelectedWetPart)
    {
        const TSet<int32> WetPartIslandIDs = GetIslandIDsForWetPart(SelectedWetPartID);
        bKeepSelectedWetPart = WetPartIslandIDs.Num() == SelectedUVIslandIDs.Num();
        if (bKeepSelectedWetPart)
        {
            for (int32 IslandID : WetPartIslandIDs)
            {
                if (!SelectedUVIslandIDs.Contains(IslandID))
                {
                    bKeepSelectedWetPart = false;
                    break;
                }
            }
        }
    }

    if (!bKeepSelectedWetPart)
    {
        SelectedWetPartID = INDEX_NONE;
        if (WetPartListView.IsValid())
        {
            WetPartListView->ClearSelection();
        }
    }

    if (bSyncListSelection)
    {
        SyncUVIslandListSelectionToState();
    }
    RefreshUVView();
    RefreshPreviewIslandHighlight();
}

void SWetClothingAssetEditorPanel::SyncUVIslandListSelectionToState()
{
    if (!UVIslandListView.IsValid())
    {
        return;
    }
    bSyncingUVIslandListSelection = true;
    UVIslandListView->ClearSelection();
    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (IslandItem.IsValid() && SelectedUVIslandIDs.Contains(IslandItem->IslandID))
        {
            UVIslandListView->SetItemSelection(IslandItem, true, ESelectInfo::Direct);
            if (IslandItem->IslandID == SelectedUVIslandID)
            {
                UVIslandListView->RequestScrollIntoView(IslandItem);
            }
        }
    }
    bSyncingUVIslandListSelection = false;
}

void SWetClothingAssetEditorPanel::ResetIslandSelection()
{
    SelectedUVIslandID = INDEX_NONE;
    SelectedUVIslandIDs.Reset();
    if (UVIslandListView.IsValid())
    {
        UVIslandListView->ClearSelection();
    }
}

TSharedRef<ITableRow> SWetClothingAssetEditorPanel::GenerateWetPartRow(FWetPartEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    const FLinearColor                   Color = Item.IsValid() ? ((Item->WetPartID == 0 || Item->bViewEnabled) ? Item->Color : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f)) : FLinearColor::White;
    TSharedPtr<SInlineEditableTextBlock> InlineTextBlock;

    TSharedRef<ITableRow> Row = SNew(STableRow<FWetPartEntryPtr>, OwnerTable)
                                    .Padding(4.0f)
                                        [SNew(SHorizontalBox)

                                         + SHorizontalBox::Slot()
                                               .AutoWidth()
                                               .VAlign(VAlign_Center)
                                               .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                   [SNew(SButton)
                                                        .ContentPadding(FMargin(2.0f))
                                                        .OnClicked(this, &SWetClothingAssetEditorPanel::HandleToggleWetPartViewClicked, Item)
                                                        .IsEnabled_Lambda([Item]()
                                                                          { return Item.IsValid() && Item->WetPartID != 0; })
                                                            [SNew(SImage)
                                                                 .Image(this, &SWetClothingAssetEditorPanel::GetWetPartVisibilityBrush, Item)]]

                                         + SHorizontalBox::Slot()
                                               .AutoWidth()
                                               .VAlign(VAlign_Center)
                                               .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                                   [SNew(SBox)
                                                        .WidthOverride(30.0f)
                                                        .HeightOverride(30.0f)
                                                            [SNew(SBorder)
                                                                 .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                                                 .BorderBackgroundColor(Color)]]

                                         + SHorizontalBox::Slot()
                                               .FillWidth(1.0f)
                                               .VAlign(VAlign_Center)
                                                   [SNew(SVerticalBox)

                                                    + SVerticalBox::Slot()
                                                          .AutoHeight()
                                                              [SNew(SHorizontalBox)

                                                               + SHorizontalBox::Slot()
                                                                     .AutoWidth()
                                                                     .VAlign(VAlign_Center)
                                                                         [SAssignNew(InlineTextBlock, SInlineEditableTextBlock)
                                                                              .Text_Lambda([this, Item]()
                                                                                           { return Item.IsValid()
                                                                                                        ? FText::FromString(GetWetPartDisplayName(*Item))
                                                                                                        : LOCTEXT("InvalidWetPartName", "Invalid Part"); })
                                                                              .OnTextCommitted(this, &SWetClothingAssetEditorPanel::HandleWetPartNameCommitted, Item)]

                                                               + SHorizontalBox::Slot()
                                                                     .AutoWidth()
                                                                     .VAlign(VAlign_Center)
                                                                     .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                                         [SNew(STextBlock)
                                                                              .Text(Item.IsValid()
                                                                                        ? FText::Format(LOCTEXT("WetPartRowIDLabel", "| ID {0}"), FText::AsNumber(Item->WetPartID))
                                                                                        : LOCTEXT("InvalidWetPartIDLabel", "| Invalid"))
                                                                              .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]]

                                                    + SVerticalBox::Slot()
                                                          .AutoHeight()
                                                          .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                                                              [SNew(SHorizontalBox)

                                                               + SHorizontalBox::Slot()
                                                                     .FillWidth(1.0f)
                                                                     .VAlign(VAlign_Center)
                                                                         [SNew(SComboButton)
                                                                              .ContentPadding(FMargin(8.0f, 3.0f))
                                                                              .OnGetMenuContent(this, &SWetClothingAssetEditorPanel::BuildWetnessProfilePickerMenu, Item)
                                                                              .ButtonContent()
                                                                                  [SNew(STextBlock)
                                                                                       .Text(this, &SWetClothingAssetEditorPanel::GetWetnessProfileButtonText, Item)]]]]];

    if (Item.IsValid() && InlineTextBlock.IsValid())
    {
        WetPartInlineRenameWidgets.Add(Item->WetPartID, InlineTextBlock);
    }

    return Row;
}

void SWetClothingAssetEditorPanel::HandleWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedWetPartID = Item.IsValid() ? Item->WetPartID : INDEX_NONE;

    if (WetPartListView.IsValid() && Item.IsValid() && !WetPartListView->IsItemSelected(Item))
    {
        WetPartListView->SetSelection(Item);
    }

    if (Item.IsValid())
    {
        const TSet<int32> IslandsForWetPart = GetIslandIDsForWetPart(Item->WetPartID);
        const int32       PrimaryIslandID = IslandsForWetPart.Num() > 0 ? *IslandsForWetPart.CreateConstIterator() : INDEX_NONE;
        SetSelectedIslandIDs(IslandsForWetPart, PrimaryIslandID);
        return;
    }

    RefreshUVView();
}

void SWetClothingAssetEditorPanel::HandleWetPartItemDoubleClicked(FWetPartEntryPtr Item)
{
    if (!Item.IsValid())
    {
        return;
    }

    HandleWetPartSelectionChanged(Item, ESelectInfo::Direct);

    if (const TWeakPtr<SInlineEditableTextBlock>* InlineWidget = WetPartInlineRenameWidgets.Find(Item->WetPartID))
    {
        if (InlineWidget->IsValid())
        {
            InlineWidget->Pin()->EnterEditingMode();
        }
    }
}

void SWetClothingAssetEditorPanel::HandleWetPartNameCommitted(const FText& InText, ETextCommit::Type CommitType, FWetPartEntryPtr Item)
{
    if (!Item.IsValid())
    {
        return;
    }

    UWetClothingAsset*             Profile = WetClothingAsset.Get();
    FWetClothingAssetWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (Profile == nullptr || Entry == nullptr)
    {
        return;
    }

    Profile->Modify();
    const FString TrimmedName = InText.ToString().TrimStartAndEnd();
    Entry->Name = TrimmedName.IsEmpty() ? GetDefaultWetPartName(Entry->WetPartID) : TrimmedName;
    Profile->MarkPackageDirty();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    RefreshWetPartList();
    RefreshUVView();
}

FReply SWetClothingAssetEditorPanel::HandleToggleWetPartViewClicked(FWetPartEntryPtr Item)
{
    if (!Item.IsValid() || Item->WetPartID == 0)
    {
        return FReply::Handled();
    }

    UWetClothingAsset*             Profile = WetClothingAsset.Get();
    FWetClothingAssetWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (Profile != nullptr && Entry != nullptr)
    {
        Profile->Modify();
        Entry->bViewEnabled = !Entry->bViewEnabled;
        Profile->MarkPackageDirty();

        if (DetailsView.IsValid())
        {
            DetailsView->ForceRefresh();
        }
    }

    RefreshWetPartList();
    RefreshUVView();
    return FReply::Handled();
}

const FSlateBrush* SWetClothingAssetEditorPanel::GetWetPartVisibilityBrush(FWetPartEntryPtr Item) const
{
    const bool bVisible = Item.IsValid() ? Item->bViewEnabled : false;
    return FAppStyle::Get().GetBrush(bVisible ? TEXT("Icons.Visible") : TEXT("Icons.Hidden"));
}

TSharedRef<SWidget> SWetClothingAssetEditorPanel::BuildWetnessProfilePickerMenu(FWetPartEntryPtr Item)
{
    TSharedRef<SVerticalBox> MenuContent = SNew(SVerticalBox);

    MenuContent->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
            [SNew(STextBlock)
                 .Text(LOCTEXT("ProfileMenuHeader", "Choose a Wetness Profile"))
                 .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))];

    MenuContent->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [SNew(STextBlock)
                 .Text(this, &SWetClothingAssetEditorPanel::GetWetnessProfileLibraryStatusText)
                 .AutoWrapText(true)];

    MenuContent->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [SNew(SButton)
                 .Text(LOCTEXT("ClearProfileAssignment", "Clear Profile"))
                 .OnClicked_Lambda([this, Item]()
                                   {
			HandleWetnessProfilePicked(Item, nullptr);
			FSlateApplication::Get().DismissAllMenus();
			return FReply::Handled(); })];

    TSharedRef<SVerticalBox> ProfileButtons = SNew(SVerticalBox);
    for (const FWetnessProfileAssetItemPtr& ProfileItem : AvailableWetnessProfileItems)
    {
        ProfileButtons->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                [SNew(SButton)
                     .HAlign(HAlign_Left)
                     .ContentPadding(FMargin(8.0f, 4.0f))
                     .OnClicked_Lambda([this, Item, ProfileItem]()
                                       {
				HandleWetnessProfilePicked(Item, ProfileItem);
				FSlateApplication::Get().DismissAllMenus();
				return FReply::Handled(); })
                         [SNew(SVerticalBox)

                          + SVerticalBox::Slot()
                                .AutoHeight()
                                    [SNew(STextBlock)
                                         .Text(FText::FromString(ProfileItem.IsValid() ? ProfileItem->DisplayName : TEXT("Invalid Profile")))]

                          + SVerticalBox::Slot()
                                .AutoHeight()
                                    [SNew(STextBlock)
                                         .Text(FText::FromString(ProfileItem.IsValid() ? ProfileItem->ContentPath : TEXT("")))
                                         .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]]];
    }

    MenuContent->AddSlot()
        .FillHeight(1.0f)
        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [SNew(SBox)
                 .MaxDesiredHeight(280.0f)
                 .MinDesiredWidth(280.0f)
                     [SNew(SScrollBox) + SScrollBox::Slot()
                                             [ProfileButtons]]];

    MenuContent->AddSlot()
        .AutoHeight()
            [SNew(SButton)
                 .Text(LOCTEXT("AddProfileFolderButton", "Add Content Folder"))
                 .OnClicked_Lambda([this]()
                                   {
			HandleAddProfileSearchPathClicked();
			FSlateApplication::Get().DismissAllMenus();
			return FReply::Handled(); })];

    return SNew(SBorder)
        .Padding(8.0f)
            [MenuContent];
}

void SWetClothingAssetEditorPanel::HandleWetnessProfilePicked(FWetPartEntryPtr Item, FWetnessProfileAssetItemPtr ProfileItem)
{
    if (!Item.IsValid())
    {
        return;
    }

    UWetClothingAsset*             Profile = WetClothingAsset.Get();
    FWetClothingAssetWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (Profile == nullptr || Entry == nullptr)
    {
        return;
    }

    Profile->Modify();

    if (ProfileItem.IsValid())
    {
        if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(ProfileItem->AssetData.GetAsset()))
        {
            Entry->ProfileAssignment.SourceProfile = FSoftObjectPath(SourceProfile);
            Entry->ProfileAssignment.SourceProfileName = SourceProfile->GetName();
            Entry->ProfileAssignment.Parameters = SourceProfile->Parameters;
        }
    }
    else
    {
        Entry->ProfileAssignment.SourceProfile = FSoftObjectPath();
        Entry->ProfileAssignment.SourceProfileName.Reset();
        Entry->ProfileAssignment.Parameters = FWetnessProfileParameters();
    }

    Profile->MarkPackageDirty();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    RefreshWetPartList();
}

FReply SWetClothingAssetEditorPanel::HandleAddProfileSearchPathClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr)
    {
        return FReply::Handled();
    }

    FString NewContentPath;
    if (!DynamicWetClothesEditorUtils::PromptForContentFolder(NewContentPath))
    {
        return FReply::Handled();
    }

#if WITH_EDITORONLY_DATA
    if (!Profile->AdditionalProfileSearchPaths.Contains(NewContentPath))
    {
        Profile->Modify();
        Profile->AdditionalProfileSearchPaths.Add(NewContentPath);
        Profile->MarkPackageDirty();
    }
#endif

    RefreshAvailableWetnessProfiles();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    return FReply::Handled();
}

TSharedRef<SWidget> SWetClothingAssetEditorPanel::GenerateAssignWetPartComboItem(FWetPartEntryPtr Item)
{
    const FLinearColor Color = Item.IsValid()
                                   ? ((Item->WetPartID == 0 || Item->bViewEnabled) ? Item->Color : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f))
                                   : FLinearColor::White;

    return SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SBox).WidthOverride(14.0f).HeightOverride(14.0f)[SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(Color)]]

           + SHorizontalBox::Slot()
                 .FillWidth(1.0f)
                 .VAlign(VAlign_Center)
                     [SNew(STextBlock)
                          .Text(Item.IsValid()
                                    ? FText::Format(
                                          LOCTEXT("AssignWetPartOption", "{0}  |  ID {1}"),
                                          FText::FromString(GetWetPartDisplayName(*Item)),
                                          FText::AsNumber(Item->WetPartID))
                                    : LOCTEXT("AssignWetPartInvalid", "Invalid Part"))];
}

void SWetClothingAssetEditorPanel::HandleAssignWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedAssignWetPartID = Item.IsValid() ? Item->WetPartID : INDEX_NONE;
}

FReply SWetClothingAssetEditorPanel::HandleAddWetPartClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || !SelectedUVChannelItem.IsValid())
    {
        return FReply::Handled();
    }

    const int32 NewWetPartID = FindNextWetPartForSelectedScope();
    Profile->Modify();

    FWetClothingAssetWetPartEntry NewEntry;
    NewEntry.MaterialSlotIndex = SelectedMaterialSlotIndex;
    NewEntry.UVChannelIndex = GetSelectedUVChannelIndex();
    NewEntry.WetPartID = NewWetPartID;
    NewEntry.Name = GetDefaultWetPartName(NewWetPartID);
    NewEntry.Color = GetDefaultWetPartColor(NewWetPartID);
    NewEntry.bViewEnabled = true;

    Profile->WetPartEntries.Add(NewEntry);
    RebuildRuntimeDataAndMarkDirty();
    SelectedWetPartID = INDEX_NONE;
    SelectedAssignWetPartID = NewWetPartID;

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    RefreshWetPartList();
    RefreshUVView();
    return FReply::Handled();
}

FReply SWetClothingAssetEditorPanel::HandleRemoveWetPartClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || SelectedWetPartID == INDEX_NONE || SelectedWetPartID == 0)
    {
        return FReply::Handled();
    }

    const int32 RemovedWetPartID = SelectedWetPartID;
    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    Profile->Modify();
    for (int32 Index = Profile->WetPartEntries.Num() - 1; Index >= 0; --Index)
    {
        const FWetClothingAssetWetPartEntry& Entry = Profile->WetPartEntries[Index];
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID == SelectedWetPartID)
        {
            Profile->WetPartEntries.RemoveAt(Index);
            break;
        }
    }
    SelectedWetPartID = INDEX_NONE;
    if (SelectedAssignWetPartID == RemovedWetPartID)
    {
        SelectedAssignWetPartID = INDEX_NONE;
    }
    RebuildRuntimeDataAndMarkDirty();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    EnsureDefaultWetPartForSelectedScope();
    RefreshWetPartList();
    RefreshUVIslandList();
    return FReply::Handled();
}

bool SWetClothingAssetEditorPanel::IsWetPartRemoveEnabled() const
{
    return SelectedWetPartID != INDEX_NONE && SelectedWetPartID != 0;
}

bool SWetClothingAssetEditorPanel::IsAutoPartitionEnabled() const
{
    return WetClothingAsset.IsValid() && SelectedMaterialSlotIndex != INDEX_NONE && SelectedUVChannelItem.IsValid() && UVIslandItems.Num() > 0;
}

bool SWetClothingAssetEditorPanel::HasAutoPartitionDataToReplace() const
{
    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID != 0)
            {
                return true;
            }
        }
    }

    return false;
}

FReply SWetClothingAssetEditorPanel::HandleAutoPartitionClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || !IsAutoPartitionEnabled())
    {
        return FReply::Handled();
    }

    if (HasAutoPartitionDataToReplace())
    {
        const EAppReturnType::Type Response = FMessageDialog::Open(
            EAppMsgType::YesNo,
            LOCTEXT("ConfirmAutoPartitionReplace", "This material slot already has authored part data. Delete it all and regenerate parts automatically?"));

        if (Response != EAppReturnType::Yes)
        {
            return FReply::Handled();
        }
    }

    UTexture2D*                 PartitionTexture = Cast<UTexture2D>(ResolveSelectedMaterialTexture());
    FWetClothingTextureReadback TextureData;
    FString                     TextureErrorMessage;
    if (!TryReadTextureSourceData(PartitionTexture, TextureData, TextureErrorMessage))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TextureErrorMessage));
        return FReply::Handled();
    }

    TArray<FWetClothingIslandColorStats> IslandStats;
    IslandStats.Reserve(UVIslandItems.Num());

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        FWetClothingIslandColorStats Stats;
        if (TryComputeIslandAverageColor(*IslandItem, TextureData, Stats))
        {
            IslandStats.Add(Stats);
        }
    }

    if (IslandStats.Num() == 0)
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("AutoPartitionNoIslands", "Auto-Partitioning could not extract average colors from the selected UV islands."));
        return FReply::Handled();
    }

    IslandStats.Sort([](const FWetClothingIslandColorStats& A, const FWetClothingIslandColorStats& B)
                     {
		if (!FMath::IsNearlyEqual(A.UVArea, B.UVArea))
		{
			return A.UVArea > B.UVArea;
		}

		return A.IslandID < B.IslandID; });

    TArray<FWetClothingAutoPartitionCluster> Clusters;
    const double                             TolerancePercent = AutoPartitionTolerancePercent;

    for (const FWetClothingIslandColorStats& Stats : IslandStats)
    {
        int32  BestClusterIndex = INDEX_NONE;
        double BestDistance = TNumericLimits<double>::Max();

        for (int32 ClusterIndex = 0; ClusterIndex < Clusters.Num(); ++ClusterIndex)
        {
            const double Distance = ComputeColorDistancePercent(Stats.AverageColor, GetClusterAverageColor(Clusters[ClusterIndex]));
            if (Distance <= TolerancePercent && Distance < BestDistance)
            {
                BestDistance = Distance;
                BestClusterIndex = ClusterIndex;
            }
        }

        if (BestClusterIndex == INDEX_NONE)
        {
            BestClusterIndex = Clusters.AddDefaulted();
        }

        FWetClothingAutoPartitionCluster& Cluster = Clusters[BestClusterIndex];
        Cluster.IslandIDs.Add(Stats.IslandID);
        Cluster.WeightedColorSum += Stats.AverageColor * static_cast<float>(Stats.SampleWeight);
        Cluster.SampleWeight += Stats.SampleWeight;
    }

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    Profile->Modify();

    for (int32 EntryIndex = Profile->WetPartEntries.Num() - 1; EntryIndex >= 0; --EntryIndex)
    {
        const FWetClothingAssetWetPartEntry& Entry = Profile->WetPartEntries[EntryIndex];
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID != 0)
        {
            Profile->WetPartEntries.RemoveAt(EntryIndex);
        }
    }

    EnsureDefaultWetPartForSelectedScope();
    if (FWetClothingAssetWetPartEntry* DefaultEntry = FindMutableWetPartEntry(0))
    {
        DefaultEntry->AssignedIslandIDs.Reset();
        DefaultEntry->Name = GetDefaultWetPartName(0);
        DefaultEntry->Color = GetDefaultWetPartColor(0);
        DefaultEntry->bViewEnabled = true;
    }

    for (int32 ClusterIndex = 0; ClusterIndex < Clusters.Num(); ++ClusterIndex)
    {
        const int32 NewWetPartID = ClusterIndex + 1;

        FWetClothingAssetWetPartEntry NewEntry;
        NewEntry.MaterialSlotIndex = SelectedMaterialSlotIndex;
        NewEntry.UVChannelIndex = UVChannelIndex;
        NewEntry.WetPartID = NewWetPartID;
        NewEntry.Name = GetDefaultWetPartName(NewWetPartID);
        NewEntry.Color = GetDefaultWetPartColor(NewWetPartID);
        NewEntry.bViewEnabled = true;
        NewEntry.AssignedIslandIDs = Clusters[ClusterIndex].IslandIDs;
        Profile->WetPartEntries.Add(NewEntry);
    }

    RebuildRuntimeDataAndMarkDirty();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    SelectedWetPartID = Clusters.Num() > 0 ? 1 : 0;
    SelectedAssignWetPartID = SelectedWetPartID;

    if (MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex) && MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->SetSelection(MaterialSlotItems[SelectedMaterialSlotIndex], ESelectInfo::Direct);
    }

    RefreshWetPartList();
    RefreshUVIslandList();
    return FReply::Handled();
}

FReply SWetClothingAssetEditorPanel::HandleAssignSelectedIslandToWetPartClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVIslandIDs.Num() == 0 || SelectedAssignWetPartID == INDEX_NONE)
    {
        return FReply::Handled();
    }
    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    Profile->Modify();
    for (FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex)
        {
            for (int32 IslandID : SelectedUVIslandIDs)
            {
                Entry.AssignedIslandIDs.Remove(IslandID);
            }
        }
    }
    if (FWetClothingAssetWetPartEntry* SelectedEntry = FindMutableWetPartEntry(SelectedAssignWetPartID))
    {
        for (int32 IslandID : SelectedUVIslandIDs)
        {
            if (SelectedAssignWetPartID != 0)
            {
                SelectedEntry->AssignedIslandIDs.AddUnique(IslandID);
            }
        }
    }
    RebuildRuntimeDataAndMarkDirty();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    RefreshWetPartList();
    RefreshUVIslandList();
    return FReply::Handled();
}

FText SWetClothingAssetEditorPanel::GetMaterialSlotCountText() const
{
    return FText::Format(
        LOCTEXT("MaterialSlotCount", "{0} Slots"),
        FText::AsNumber(MaterialSlotItems.Num()));
}

FText SWetClothingAssetEditorPanel::GetSelectedMaterialSlotText() const
{
    if (!MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex))
    {
        return LOCTEXT("NoMaterialSlotSelected", "Select a material slot to isolate its coverage on the preview mesh.");
    }

    const FMaterialSlotItemPtr& Item = MaterialSlotItems[SelectedMaterialSlotIndex];
    const FString               MaterialName = Item->Material.IsValid() ? Item->Material->GetName() : TEXT("None");

    return FText::Format(
        LOCTEXT("SelectedMaterialSlot", "Selected: [{0}] {1} ({2})"),
        FText::AsNumber(Item->SlotIndex),
        FText::FromName(Item->SlotName),
        FText::FromString(MaterialName));
}

FText SWetClothingAssetEditorPanel::GetSelectedUVChannelText() const
{
    if (!SelectedUVChannelItem.IsValid())
    {
        return LOCTEXT("NoUVChannelSelected", "No UV Channel");
    }

    return FText::Format(
        LOCTEXT("SelectedUVChannel", "UV Channel {0}"),
        FText::AsNumber(*SelectedUVChannelItem));
}

FText SWetClothingAssetEditorPanel::GetSelectedUVDisplayModeText() const
{
    if (!SelectedUVDisplayModeItem.IsValid() || *SelectedUVDisplayModeItem == EWetClothingAssetUVDisplayMode::Normal)
    {
        return LOCTEXT("SelectedUVDisplayModeNormal", "Normal");
    }

    return LOCTEXT("SelectedUVDisplayModeOutline", "Outline");
}

FText SWetClothingAssetEditorPanel::GetSelectedTextureText() const
{
    if (!SelectedTextureItem.IsValid())
    {
        return LOCTEXT("NoTextureSelected", "No Texture");
    }

    return FText::FromString(SelectedTextureItem->Label);
}

FText SWetClothingAssetEditorPanel::GetUVIslandCountText() const
{
    return FText::Format(
        LOCTEXT("UVIslandCount", "Island Count: {0}"),
        FText::AsNumber(UVIslandItems.Num()));
}

FText SWetClothingAssetEditorPanel::GetSelectedUVIslandText() const
{
    if (SelectedUVIslandIDs.Num() == 0)
    {
        return LOCTEXT("NoUVIslandSelected", "Select UV islands from the list, UV view, or 3D preview. Hold Shift for multi-select.");
    }
    if (SelectedUVIslandIDs.Num() > 1)
    {
        return FText::Format(LOCTEXT("SelectedUVIslandMulti", "Selected Islands: {0}  |  Primary: {1}"), FText::AsNumber(SelectedUVIslandIDs.Num()), FText::AsNumber(SelectedUVIslandID));
    }
    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (IslandItem.IsValid() && IslandItem->IslandID == SelectedUVIslandID)
        {
            return FText::Format(LOCTEXT("SelectedUVIsland", "Selected Island: {0}  |  Bounds Min({1}, {2}) Max({3}, {4})"), FText::AsNumber(IslandItem->IslandID), FText::AsNumber(IslandItem->UVBounds.Min.X), FText::AsNumber(IslandItem->UVBounds.Min.Y), FText::AsNumber(IslandItem->UVBounds.Max.X), FText::AsNumber(IslandItem->UVBounds.Max.Y));
        }
    }
    return LOCTEXT("NoUVIslandSelectedFallback", "Select UV islands from the list, UV view, or 3D preview.");
}

FText SWetClothingAssetEditorPanel::GetUVStatusText() const
{
    return FText::FromString(UVStatusMessage);
}

FText SWetClothingAssetEditorPanel::GetWetPartSectionText() const
{
    if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("WetPartSectionNoSlot", "Part Map: No slot");
    }

    return FText::Format(
        LOCTEXT("WetPartSection", "Part Map / Slot {0}"),
        FText::AsNumber(SelectedMaterialSlotIndex));
}

FText SWetClothingAssetEditorPanel::GetAssignIslandToWetPartText() const
{
    return LOCTEXT("AssignSelectedIslands", "Assign");
}

FText SWetClothingAssetEditorPanel::GetSelectedAssignWetPartText() const
{
    if (const FWetPartEntryPtr Item = FindWetPartItemByID(SelectedAssignWetPartID))
    {
        return FText::Format(
            LOCTEXT("SelectedAssignWetPart", "{0}  |  ID {1}"),
            FText::FromString(GetWetPartDisplayName(*Item)),
            FText::AsNumber(Item->WetPartID));
    }

    return LOCTEXT("SelectedAssignWetPartNone", "Select Part");
}

FSlateColor SWetClothingAssetEditorPanel::GetSelectedAssignWetPartColor() const
{
    if (const FWetPartEntryPtr Item = FindWetPartItemByID(SelectedAssignWetPartID))
    {
        return FSlateColor((Item->WetPartID == 0 || Item->bViewEnabled) ? Item->Color : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f));
    }

    return FSlateColor(FLinearColor(0.08f, 0.08f, 0.08f, 1.0f));
}

FText SWetClothingAssetEditorPanel::GetSelectedWetPartText() const
{
    if (SelectedWetPartID == INDEX_NONE)
    {
        return LOCTEXT("SelectedWetPartNone", "Selected Part: None. Pick a row to select all islands assigned to that ID.");
    }

    if (const FWetClothingAssetWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID))
    {
        return FText::Format(
            LOCTEXT("SelectedWetPart", "Selected Part: {0}  |  Double-click name to rename"),
            FText::FromString(GetWetPartDisplayName(*Entry)));
    }

    return LOCTEXT("SelectedWetPartInvalid", "Selected Part: Invalid");
}

FText SWetClothingAssetEditorPanel::GetWetnessProfileLibraryStatusText() const
{
    const TArray<FString> SearchPaths = GetProfileSearchPaths();
    const FString         PathsLabel = FString::Join(SearchPaths, TEXT(", "));

    return FText::Format(
        LOCTEXT("WetnessProfileLibraryStatus", "Profile folders: {0}  |  Candidates: {1}"),
        FText::FromString(PathsLabel),
        FText::AsNumber(AvailableWetnessProfileItems.Num()));
}

FText SWetClothingAssetEditorPanel::GetBlendModeText(FWetPartEntryPtr Item) const
{
    if (!Item.IsValid())
    {
        return LOCTEXT("InvalidBlendMode", "Standard");
    }

    const UEnum* BlendModeEnum = StaticEnum<EWetClothingPartBlendMode>();
    return BlendModeEnum != nullptr
               ? BlendModeEnum->GetDisplayNameTextByValue(static_cast<int64>(Item->ProfileAssignment.BlendMode))
               : LOCTEXT("BlendModeFallback", "Standard");
}

FText SWetClothingAssetEditorPanel::GetWetnessProfileButtonText(FWetPartEntryPtr Item) const
{
    return Item.IsValid()
               ? FText::FromString(GetAssignedProfileLabel(*Item))
               : LOCTEXT("NoProfileSelected", "Select Profile");
}

FReply SWetClothingAssetEditorPanel::HandleUVSelectionToolButtonClicked(FUVSelectionToolItemPtr Item)
{
    if (Item.IsValid())
    {
        SetCurrentUVSelectionTool(Item->Tool);
    }

    return FReply::Handled();
}

void SWetClothingAssetEditorPanel::SetCurrentUVSelectionTool(EWetClothingAssetUVSelectionTool InTool)
{
    CurrentUVSelectionTool = InTool;
    SelectedUVSelectionToolItem.Reset();

    for (const FUVSelectionToolItemPtr& ToolItem : UVSelectionToolItems)
    {
        if (ToolItem.IsValid() && ToolItem->Tool == InTool)
        {
            SelectedUVSelectionToolItem = ToolItem;
            break;
        }
    }

    if (UVView.IsValid())
    {
        UVView->SetSelectionTool(CurrentUVSelectionTool);
    }
}

const FSlateBrush* SWetClothingAssetEditorPanel::GetUVSelectionToolBrush(FUVSelectionToolItemPtr Item) const
{
    if (Item.IsValid() && Item->IconBrushName != NAME_None)
    {
        return FDynamicWetClothesEditorStyle::GetBrush(Item->IconBrushName);
    }

    return FAppStyle::GetBrush(TEXT("ClassIcon.Default"));
}

FSlateColor SWetClothingAssetEditorPanel::GetUVSelectionToolIconColor(FUVSelectionToolItemPtr Item) const
{
    const bool bIsSelected = Item.IsValid() && Item->Tool == CurrentUVSelectionTool;
    return FSlateColor(bIsSelected
                           ? FLinearColor::White
                           : FStyleColors::Foreground);
}

FSlateColor SWetClothingAssetEditorPanel::GetUVSelectionToolButtonColor(FUVSelectionToolItemPtr Item) const
{
    const bool bIsSelected = Item.IsValid() && Item->Tool == CurrentUVSelectionTool;
    return FSlateColor(bIsSelected
                           ? FStyleColors::AccentBlue
                           : FStyleColors::Header);
}

float SWetClothingAssetEditorPanel::GetAutoPartitionTolerance() const
{
    return AutoPartitionTolerancePercent;
}

void SWetClothingAssetEditorPanel::HandleAutoPartitionToleranceChanged(float InValue)
{
    AutoPartitionTolerancePercent = FMath::Clamp(InValue, 0.0f, 100.0f);
}

float SWetClothingAssetEditorPanel::GetSelectionLineThicknessScale() const
{
    return PreviewViewport.IsValid() ? PreviewViewport->GetSelectionOverlayThicknessScale() : 1.0f;
}

void SWetClothingAssetEditorPanel::HandleSelectionLineThicknessChanged(float InValue)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectionOverlayThicknessScale(InValue);
    }
}

FReply SWetClothingAssetEditorPanel::HandleFocusPreviewClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusOnPreviewMesh();
    }

    return FReply::Handled();
}

FReply SWetClothingAssetEditorPanel::HandleSaveAssetClicked()
{
    DynamicWetClothesEditorUtils::SaveAsset(WetClothingAsset.Get());
    return FReply::Handled();
}

UTexture* SWetClothingAssetEditorPanel::ResolveSelectedMaterialTexture() const
{
    return SelectedTextureItem.IsValid() ? SelectedTextureItem->Texture.Get() : nullptr;
}

void SWetClothingAssetEditorPanel::SaveSelectedTexture()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    UTexture*   Texture = ResolveSelectedMaterialTexture();

    Profile->Modify();

    for (FWetClothingAssetTextureSelection& Selection : Profile->TextureSelections)
    {
        if (Selection.MaterialSlotIndex == SelectedMaterialSlotIndex && Selection.UVChannelIndex == UVChannelIndex)
        {
            Selection.Texture = Texture;
            Profile->MarkPackageDirty();
            return;
        }
    }

    FWetClothingAssetTextureSelection NewSelection;
    NewSelection.MaterialSlotIndex = SelectedMaterialSlotIndex;
    NewSelection.UVChannelIndex = UVChannelIndex;
    NewSelection.Texture = Texture;
    Profile->TextureSelections.Add(NewSelection);
    Profile->MarkPackageDirty();
}

#undef LOCTEXT_NAMESPACE
