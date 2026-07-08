#include "WetClothing/PartMode/WetnessProfileMap/WetClothingWetnessProfileMapBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Texture/WetClothingTextureAddressUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"

bool FWetClothingWetnessProfileMapBaker::IsUVPointInsideTriangle(
    const FVector2D& Point,
    const FVector2D& A,
    const FVector2D& B,
    const FVector2D& C)
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

uint8 FWetClothingWetnessProfileMapBaker::PackUnitFloat(const float Value)
{
    return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
}

FColor FWetClothingWetnessProfileMapBaker::EncodeProfileParameters(const FWetnessProfileParameters& Parameters)
{
    return FColor(
        PackUnitFloat(Parameters.GetWetVisualStrength()),
        PackUnitFloat(Parameters.GetTransparencyStrength()),
        PackUnitFloat(Parameters.GetSurfaceWaterStrength()),
        255);
}

void FWetClothingWetnessProfileMapBaker::ApplyTextureAddressToIslands(
    TArray<FWetClothingAssetUVIsland>& Islands,
    const TextureAddress               AddressX,
    const TextureAddress               AddressY)
{
    for (FWetClothingAssetUVIsland& Island : Islands)
    {
        FBox2D SourceBounds(ForceInit);
        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            SourceBounds += Triangle.UVs[0];
            SourceBounds += Triangle.UVs[1];
            SourceBounds += Triangle.UVs[2];
        }

        if (!SourceBounds.bIsValid)
        {
            continue;
        }

        const FVector2D SourceCenter = (SourceBounds.Min + SourceBounds.Max) * 0.5f;
        Island.UVBounds.Init();
        for (FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
            {
                FVector2D& UV = Triangle.UVs[VertexIndex];
                UV.X = WetClothingTextureAddressUtils::Apply(UV.X, SourceCenter.X, AddressX);
                UV.Y = WetClothingTextureAddressUtils::Apply(UV.Y, SourceCenter.Y, AddressY);
                Island.UVBounds += UV;
            }
        }
    }
}

bool FWetClothingWetnessProfileMapBaker::ResolveWetPartParameters(
    const FWetClothingWetPartEntry& WetPartEntry,
    FWetnessProfileParameters&           OutParameters)
{
    if (WetPartEntry.ProfileAssignment.SourceProfile.IsValid())
    {
        if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(WetPartEntry.ProfileAssignment.SourceProfile.TryLoad()))
        {
            OutParameters = SourceProfile->GetParameters();
            return true;
        }
    }

    OutParameters = WetPartEntry.ProfileAssignment.Parameters;
    return true;
}

void FWetClothingWetnessProfileMapBaker::AppendProfileParametersSignature(
    FString&                         Signature,
    const FWetnessProfileParameters& Parameters)
{
    Signature += FString::Printf(
        TEXT("A=%.9g;S=%.9g;D=%.9g;G=%.9g;WV=%.9g;T=%.9g;SW=%.9g;R=%.9g;"),
        Parameters.Absorption,
        Parameters.SpreadRate,
        Parameters.DryRate,
        Parameters.GravityFlowStrength,
        Parameters.WetVisualStrength,
        Parameters.TransparencyStrength,
        Parameters.SurfaceWaterStrength,
        Parameters.RunoffStrength);
}

const FWetClothingWetPartEntry* FWetClothingWetnessProfileMapBaker::FindWetPartEntryForUVIsland(
    const UWetClothingAsset& WetClothingAsset,
    const int32              MaterialSlotIndex,
    const int32              UVChannelIndex,
    const int32              UVIslandID)
{
    for (const FWetClothingWetPartEntry& Entry : WetClothingAsset.PartData.EditableWetPartData.WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == MaterialSlotIndex &&
            Entry.UVChannelIndex == UVChannelIndex &&
            Entry.AssignedUVIslandIDs.Contains(UVIslandID))
        {
            return &Entry;
        }
    }

    return nullptr;
}

int32 FWetClothingWetnessProfileMapBaker::PaintTriangle(
    TArray<FColor>&                    Pixels,
    TArray<bool>&                      PaintedMask,
    const int32                        Width,
    const int32                        Height,
    const FWetClothingAssetUVTriangle& Triangle,
    const FColor&                      Color)
{
    const FVector2D& A = Triangle.UVs[0];
    const FVector2D& B = Triangle.UVs[1];
    const FVector2D& C = Triangle.UVs[2];

    const double MinU = FMath::Min3(A.X, B.X, C.X);
    const double MaxU = FMath::Max3(A.X, B.X, C.X);
    const double MinV = FMath::Min3(A.Y, B.Y, C.Y);
    const double MaxV = FMath::Max3(A.Y, B.Y, C.Y);

    const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * Width), 0, Width - 1);
    const int32 MaxX = FMath::Clamp(FMath::FloorToInt(MaxU * Width), 0, Width - 1);
    const int32 MinY = FMath::Clamp(FMath::FloorToInt(MinV * Height), 0, Height - 1);
    const int32 MaxY = FMath::Clamp(FMath::FloorToInt(MaxV * Height), 0, Height - 1);

    int32 PaintedPixelCount = 0;
    for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
    {
        for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
        {
            const FVector2D SampleUV(
                (static_cast<double>(PixelX) + 0.5) / Width,
                (static_cast<double>(PixelY) + 0.5) / Height);

            if (!IsUVPointInsideTriangle(SampleUV, A, B, C))
            {
                continue;
            }

            const int32 PixelIndex = PixelY * Width + PixelX;
            if (!PaintedMask[PixelIndex])
            {
                ++PaintedPixelCount;
            }

            Pixels[PixelIndex] = Color;
            PaintedMask[PixelIndex] = true;
        }
    }

    if (PaintedPixelCount == 0)
    {
        const FVector2D CenterUV = (A + B + C) / 3.0f;
        const int32     FallbackX = FMath::Clamp(FMath::FloorToInt(CenterUV.X * Width), 0, Width - 1);
        const int32     FallbackY = FMath::Clamp(FMath::FloorToInt(CenterUV.Y * Height), 0, Height - 1);
        const int32     PixelIndex = FallbackY * Width + FallbackX;
        if (!PaintedMask[PixelIndex])
        {
            ++PaintedPixelCount;
        }
        Pixels[PixelIndex] = Color;
        PaintedMask[PixelIndex] = true;
    }

    return PaintedPixelCount;
}

void FWetClothingWetnessProfileMapBaker::DilatePaintedPixels(
    TArray<FColor>& Pixels,
    TArray<bool>&   PaintedMask,
    const int32     Width,
    const int32     Height,
    const int32     PaddingPixels)
{
    const int32 ClampedPaddingPixels = FMath::Clamp(PaddingPixels, 0, 64);
    for (int32 PaddingStep = 0; PaddingStep < ClampedPaddingPixels; ++PaddingStep)
    {
        TArray<FColor> PreviousPixels = Pixels;
        TArray<bool>   PreviousMask = PaintedMask;
        bool           bWrotePixel = false;

        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                const int32 PixelIndex = Y * Width + X;
                if (PreviousMask[PixelIndex])
                {
                    continue;
                }

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
                        if (NeighborX < 0 || NeighborY < 0 || NeighborX >= Width || NeighborY >= Height)
                        {
                            continue;
                        }

                        const int32 NeighborIndex = NeighborY * Width + NeighborX;
                        if (!PreviousMask[NeighborIndex])
                        {
                            continue;
                        }

                        Pixels[PixelIndex] = PreviousPixels[NeighborIndex];
                        PaintedMask[PixelIndex] = true;
                        bWrotePixel = true;
                        OffsetX = 2;
                        OffsetY = 2;
                    }
                }
            }
        }

        if (!bWrotePixel)
        {
            break;
        }
    }
}

FString FWetClothingWetnessProfileMapBaker::BuildWetnessProfileMapObjectName(
    const UWetClothingAsset& WetClothingAsset,
    const UTexture&          SourceTexture,
    const int32              UVChannelIndex)
{
    const FString RawName = FString::Printf(
        TEXT("T_%s_%s_UV%d_WetnessProfileMap0"),
        *WetClothingAsset.GetName(),
        *SourceTexture.GetName(),
        UVChannelIndex);
    return ObjectTools::SanitizeObjectName(RawName);
}

UTexture2D* FWetClothingWetnessProfileMapBaker::CreateOrUpdateTextureAsset(
    UWetClothingAsset&    WetClothingAsset,
    UTexture&             SourceTexture,
    const int32           UVChannelIndex,
    const int32           Width,
    const int32           Height,
    const TArray<FColor>& Pixels,
    FString&              OutErrorMessage)
{
#if WITH_EDITORONLY_DATA
    const FString AssetPackageName = WetClothingAsset.GetOutermost()->GetName();
    const FString PackagePath = FPackageName::GetLongPackagePath(AssetPackageName);
    if (PackagePath.IsEmpty())
    {
        OutErrorMessage = TEXT("Could not resolve a package path for the Wet Clothing Asset.");
        return nullptr;
    }

    const FString ObjectName = BuildWetnessProfileMapObjectName(WetClothingAsset, SourceTexture, UVChannelIndex);
    const FString TexturePackageName = PackagePath / ObjectName;
    const FString TextureObjectPath = TexturePackageName + TEXT(".") + ObjectName;

    UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TextureObjectPath);
    if (Texture == nullptr)
    {
        UPackage* Package = CreatePackage(*TexturePackageName);
        if (Package == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to create package '%s'."), *TexturePackageName);
            return nullptr;
        }

        Texture = NewObject<UTexture2D>(Package, *ObjectName, RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Texture);
    }
    else
    {
        Texture->Modify();
    }

    Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
    Texture->SRGB = false;
    Texture->CompressionSettings = TC_VectorDisplacementmap;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->Filter = TF_Nearest;
    if (const UTexture2D* SourceTexture2D = Cast<UTexture2D>(&SourceTexture))
    {
        Texture->AddressX = SourceTexture2D->AddressX;
        Texture->AddressY = SourceTexture2D->AddressY;
    }
    else
    {
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
    }
    Texture->PostEditChange();
    Texture->UpdateResource();
    Texture->MarkPackageDirty();

    OutErrorMessage.Reset();
    return Texture;
#else
    OutErrorMessage = TEXT("Wetness Profile Map baking requires editor-only texture source data.");
    return nullptr;
#endif
}

FString FWetClothingWetnessProfileMapBaker::MakeBuildSignature(
    const UWetClothingAsset* WetClothingAsset,
    const UTexture*          SourceTexture,
    const int32              UVChannelIndex,
    const TArray<int32>&     MaterialSlotIndices)
{
    if (WetClothingAsset == nullptr || SourceTexture == nullptr || UVChannelIndex == INDEX_NONE)
    {
        return FString();
    }

    TArray<int32> SortedMaterialSlotIndices = MaterialSlotIndices;
    SortedMaterialSlotIndices.Sort();

    FString Canonical;
    Canonical.Reserve(4096);
    Canonical += TEXT("DWC.WetnessProfileMap0.v2|");
    Canonical += SourceTexture->GetPathName();

    const int32 SourceWidth = FMath::Max(FMath::RoundToInt(SourceTexture->GetSurfaceWidth()), 1);
    const int32 SourceHeight = FMath::Max(FMath::RoundToInt(SourceTexture->GetSurfaceHeight()), 1);
    Canonical += FString::Printf(
        TEXT("|UV=%d|Source=%dx%d"),
        UVChannelIndex,
        SourceWidth,
        SourceHeight);

    if (const UTexture2D* SourceTexture2D = Cast<UTexture2D>(SourceTexture))
    {
        Canonical += FString::Printf(
            TEXT(";Address=%d,%d"),
            static_cast<int32>(SourceTexture2D->AddressX.GetValue()),
            static_cast<int32>(SourceTexture2D->AddressY.GetValue()));
    }

    Canonical += TEXT("|Slots=");
    for (const int32 MaterialSlotIndex : SortedMaterialSlotIndices)
    {
        Canonical += FString::FromInt(MaterialSlotIndex);
        Canonical += TEXT(",");
    }

    TArray<const FWetClothingWetPartEntry*> RelevantEntries;
    for (const FWetClothingWetPartEntry& Entry : WetClothingAsset->PartData.EditableWetPartData.WetPartEntries)
    {
        if (Entry.UVChannelIndex == UVChannelIndex &&
            Entry.MaterialSlotIndex != INDEX_NONE &&
            Entry.AssignedUVIslandIDs.Num() > 0 &&
            SortedMaterialSlotIndices.Contains(Entry.MaterialSlotIndex))
        {
            RelevantEntries.Add(&Entry);
        }
    }

    RelevantEntries.Sort(
        [](const FWetClothingWetPartEntry& A, const FWetClothingWetPartEntry& B)
        {
            if (A.MaterialSlotIndex != B.MaterialSlotIndex)
            {
                return A.MaterialSlotIndex < B.MaterialSlotIndex;
            }
            if (A.UVChannelIndex != B.UVChannelIndex)
            {
                return A.UVChannelIndex < B.UVChannelIndex;
            }
            return A.WetPartID < B.WetPartID;
        });

    for (const FWetClothingWetPartEntry* Entry : RelevantEntries)
    {
        TArray<int32> AssignedUVIslandIDs = Entry->AssignedUVIslandIDs;
        AssignedUVIslandIDs.Sort();

        Canonical += FString::Printf(
            TEXT("|Entry:Slot=%d;UV=%d;Part=%d;Profile=%s;Blend=%d;Islands="),
            Entry->MaterialSlotIndex,
            Entry->UVChannelIndex,
            Entry->WetPartID,
            *Entry->ProfileAssignment.SourceProfile.ToString(),
            static_cast<int32>(Entry->ProfileAssignment.BlendMode));

        for (const int32 UVIslandID : AssignedUVIslandIDs)
        {
            Canonical += FString::FromInt(UVIslandID);
            Canonical += TEXT(",");
        }

        FWetnessProfileParameters Parameters;
        ResolveWetPartParameters(*Entry, Parameters);
        Canonical += TEXT(";Params=");
        AppendProfileParametersSignature(Canonical, Parameters);
    }

    return FMD5::HashAnsiString(*Canonical);
}

bool FWetClothingWetnessProfileMapBaker::BakeWetnessProfileMap0(
    UWetClothingAsset*                               WetClothingAsset,
    UTexture*                                        SourceTexture,
    int32                                            UVChannelIndex,
    const TArray<int32>&                             MaterialSlotIndices,
    const FWetClothingWetnessProfileMapBakeSettings& Settings,
    FWetClothingWetnessProfileMapBakeResult&         OutResult,
    FString&                                         OutErrorMessage)
{
    OutResult = FWetClothingWetnessProfileMapBakeResult();

    if (WetClothingAsset == nullptr)
    {
        OutErrorMessage = TEXT("Wet Clothing Asset is unavailable.");
        return false;
    }

    if (WetClothingAsset->TargetMesh == nullptr)
    {
        OutErrorMessage = TEXT("Assign a TargetMesh before baking a Wetness Profile Map.");
        return false;
    }

    if (SourceTexture == nullptr)
    {
        OutErrorMessage = TEXT("Select a source texture before baking a Wetness Profile Map.");
        return false;
    }

    const int32    MaxResolution = FMath::Clamp(Settings.Resolution, 16, 8192);
    const int32    SourceWidth = FMath::Max(SourceTexture->GetSurfaceWidth(), 1);
    const int32    SourceHeight = FMath::Max(SourceTexture->GetSurfaceHeight(), 1);
    const double   ResolutionScale = static_cast<double>(MaxResolution) / FMath::Max(SourceWidth, SourceHeight);
    const int32    Width = FMath::Clamp(FMath::RoundToInt(SourceWidth * ResolutionScale), 1, 8192);
    const int32    Height = FMath::Clamp(FMath::RoundToInt(SourceHeight * ResolutionScale), 1, 8192);
    TArray<FColor> Pixels;
    TArray<bool>   PaintedMask;
    Pixels.Init(FColor(0, 0, 0, 0), Width * Height);
    PaintedMask.Init(false, Width * Height);

    const UTexture2D*    SourceTexture2D = Cast<UTexture2D>(SourceTexture);
    const TextureAddress SourceAddressX = SourceTexture2D != nullptr ? static_cast<TextureAddress>(SourceTexture2D->AddressX.GetValue()) : TA_Clamp;
    const TextureAddress SourceAddressY = SourceTexture2D != nullptr ? static_cast<TextureAddress>(SourceTexture2D->AddressY.GetValue()) : TA_Clamp;

    int32         PaintedPixelCount = 0;
    TArray<int32> SortedMaterialSlotIndices = MaterialSlotIndices;
    SortedMaterialSlotIndices.Sort();

    for (const int32 MaterialSlotIndex : SortedMaterialSlotIndices)
    {
        TArray<FWetClothingAssetUVIsland> Islands;
        FString                           BuildUVIslandError;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
                WetClothingAsset->TargetMesh,
                0,
                UVChannelIndex,
                MaterialSlotIndex,
                Islands,
                &BuildUVIslandError))
        {
            OutErrorMessage = BuildUVIslandError;
            return false;
        }

        ApplyTextureAddressToIslands(Islands, SourceAddressX, SourceAddressY);

        for (const FWetClothingAssetUVIsland& Island : Islands)
        {
            const FWetClothingWetPartEntry* WetPartEntry =
                FindWetPartEntryForUVIsland(*WetClothingAsset, MaterialSlotIndex, UVChannelIndex, Island.UVIslandID);
            if (WetPartEntry == nullptr)
            {
                continue;
            }

            FWetnessProfileParameters Parameters;
            if (!ResolveWetPartParameters(*WetPartEntry, Parameters))
            {
                continue;
            }

            const FColor EncodedParameters = EncodeProfileParameters(Parameters);
            for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
            {
                PaintedPixelCount += PaintTriangle(Pixels, PaintedMask, Width, Height, Triangle, EncodedParameters);
            }
        }
    }

    DilatePaintedPixels(Pixels, PaintedMask, Width, Height, Settings.PaddingPixels);

    UTexture2D* WetnessProfileMap0 = CreateOrUpdateTextureAsset(
        *WetClothingAsset,
        *SourceTexture,
        UVChannelIndex,
        Width,
        Height,
        Pixels,
        OutErrorMessage);
    if (WetnessProfileMap0 == nullptr)
    {
        return false;
    }

    WetClothingAsset->Modify();

    FWetClothingBakedWetnessProfileMap* BakedWetnessProfileMap = WetClothingAsset->PartData.BakedWetnessProfileMaps.FindByPredicate(
        [SourceTexture, UVChannelIndex](const FWetClothingBakedWetnessProfileMap& ExistingWetnessProfileMap)
        {
            return ExistingWetnessProfileMap.SourceTexture == SourceTexture && ExistingWetnessProfileMap.UVChannelIndex == UVChannelIndex;
        });

    if (BakedWetnessProfileMap == nullptr)
    {
        BakedWetnessProfileMap = &WetClothingAsset->PartData.BakedWetnessProfileMaps.AddDefaulted_GetRef();
    }

    BakedWetnessProfileMap->SourceTexture = SourceTexture;
    BakedWetnessProfileMap->UVChannelIndex = UVChannelIndex;
    BakedWetnessProfileMap->MaterialSlotIndices = SortedMaterialSlotIndices;
    BakedWetnessProfileMap->WetnessProfileMap0 = WetnessProfileMap0;
    BakedWetnessProfileMap->Resolution = MaxResolution;
    BakedWetnessProfileMap->PaddingPixels = Settings.PaddingPixels;
    BakedWetnessProfileMap->BuildSignature = MakeBuildSignature(WetClothingAsset, SourceTexture, UVChannelIndex, SortedMaterialSlotIndices);
    BakedWetnessProfileMap->BakeGuid = FGuid::NewGuid();

    WetClothingAsset->MarkPackageDirty();

    OutResult.WetnessProfileMap0 = WetnessProfileMap0;
    OutResult.MaterialSlotIndices = SortedMaterialSlotIndices;
    OutResult.PaintedPixelCount = PaintedPixelCount;
    OutErrorMessage.Reset();
    return true;
}
