// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetPartDataTextureBaker.h"
#include "Core/DWCGeneratedAssetPaths.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

namespace
{
    FString MakeTextureBuildKey(const UTexture2D* Texture)
    {
        if (Texture == nullptr)
        {
            return TEXT("None");
        }
#if WITH_EDITORONLY_DATA
        return FString::Printf(
            TEXT("%s:%dx%d:%d:FlipG=%d"),
            *Texture->Source.GetId().ToString(EGuidFormats::Digits),
            Texture->Source.GetSizeX(),
            Texture->Source.GetSizeY(),
            static_cast<int32>(Texture->Source.GetFormat()),
            Texture->bFlipGreenChannel ? 1 : 0);
#else
        return FString::Printf(
            TEXT("%dx%d:%d:%d:FlipG=%d"),
            Texture->GetSizeX(),
            Texture->GetSizeY(),
            static_cast<int32>(Texture->GetPixelFormat()),
            static_cast<int32>(Texture->CompressionSettings),
            Texture->bFlipGreenChannel ? 1 : 0);
#endif
    }

    FString MakeParametersKey(const FWetnessProfileParameters& Parameters)
    {
        const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
        return FString::Printf(
            TEXT("AbsorbedDarkening=%.9g|")
                TEXT("AbsorbedGlossiness=%.9g|")
                    TEXT("SurfaceEnabled=%d|SecondaryEnabled=%d|Droplet1Spawn=%.9g|Droplet1Width=%.9g|Droplet1Height=%.9g|")
                        TEXT("Droplet1Normal=%s|Droplet1Mask=%s|")
                            TEXT("Droplet2Spawn=%.9g|Droplet2Width=%.9g|Droplet2Height=%.9g|Droplet2SpawnSpread=%.9g|")
                                TEXT("Droplet2Normal=%s|Droplet2Mask=%s|")
                                    TEXT("SurfaceWaterTargetRoughness=%.9g|")
                                        TEXT("SurfaceWaterNormalStrength=%.9g|")
                                            TEXT("SurfaceWaterRoughnessBlend=%.9g|")
                                                TEXT("SurfaceWaterTotalStrength=%.9g|")
                                                    TEXT("SurfaceWaterColorBlend=%.9g|")
                                                        TEXT("SurfaceWaterSpecular=%.9g|")
                                                            TEXT("FlowTargetRoughness=%.9g|FlowRoughnessBlend=%.9g|")
                                                                TEXT("FlowTotalStrength=%.9g|FlowColorBlend=%.9g|")
                                                                    TEXT("FlowNormalStrength=%.9g|FlowSpecular=%.9g"),
            Parameters.GetAbsorbedDarkeningStrength(),
            Parameters.GetAbsorbedGlossinessStrength(),
            Surface.bEnabled ? 1 : 0,
            Surface.bUseSecondaryDroplets ? 1 : 0,
            Surface.DropletSpawnProbability,
            Surface.DropletRadiusPixels,
            Surface.DropletHeightPixels,
            *MakeTextureBuildKey(Surface.DropletNormalTexture),
            *MakeTextureBuildKey(Surface.DropletMaskTexture),
            Surface.DropletFlowSpawnProbability,
            Surface.DropletFlowRadiusPixels,
            Surface.DropletFlowHeightPixels,
            Surface.DropletFlowSpawnPositionSpread,
            *MakeTextureBuildKey(Surface.DropletFlowNormalTexture),
            *MakeTextureBuildKey(Surface.DropletFlowMaskTexture),
            Surface.SurfaceWaterTargetRoughness,
            Surface.SurfaceWaterNormalStrength,
            Surface.SurfaceWaterRoughnessBlend,
            Surface.SurfaceWaterTotalStrength,
            Surface.SurfaceWaterColorBlend,
            Surface.SurfaceWaterSpecular,
            Surface.DropletFlowTargetRoughness,
            Surface.DropletFlowRoughnessBlend,
            Surface.DropletFlowTotalStrength,
            Surface.DropletFlowColorBlend,
            Surface.DropletFlowNormalStrength,
            Surface.DropletFlowSpecular);
    }

    struct FProfileBakeEntry
    {
        int32                            MaterialSlotIndex = INDEX_NONE;
        const FWetClothingWetPartEntry*  Entry = nullptr;
        const FWetPartProfileAssignment* Profile = nullptr;
    };

    bool IsEntryBakeable(const FWetClothingAuthoredMaterialSlot& Slot, const FWetClothingWetPartEntry& Entry)
    {
        return Slot.bIsWettableSlot &&
               Slot.MaterialSlotIndex != INDEX_NONE &&
               Entry.WetPartID != 0 &&
               !Entry.AssignedUVIslandIDs.IsEmpty();
    }

    void CollectBakeEntries(const UWetClothingAsset& Asset, TArray<FProfileBakeEntry>& OutEntries)
    {
        OutEntries.Reset();
        const FWetClothingEditableWetPartData& EditableData = Asset.Authored.PartData.EditableWetPartData;
        for (const FWetClothingAuthoredMaterialSlot& Slot : EditableData.MaterialSlots)
        {
            for (const FWetClothingWetPartEntry& Entry : Slot.WetPartEntries)
            {
                if (!IsEntryBakeable(Slot, Entry))
                {
                    continue;
                }

                FProfileBakeEntry& BakeEntry = OutEntries.AddDefaulted_GetRef();
                BakeEntry.MaterialSlotIndex = Slot.MaterialSlotIndex;
                BakeEntry.Entry = &Entry;
                BakeEntry.Profile = EditableData.FindProfile(Entry);
            }
        }

        OutEntries.Sort([](const FProfileBakeEntry& A, const FProfileBakeEntry& B)
                        {
            if (A.MaterialSlotIndex != B.MaterialSlotIndex)
            {
                return A.MaterialSlotIndex < B.MaterialSlotIndex;
            }
            return A.Entry != nullptr && B.Entry != nullptr ? A.Entry->WetPartID < B.Entry->WetPartID : A.Entry != nullptr; });
    }

} // namespace

FString FWetClothingWetPartDataTextureBaker::MakeSlotBuildSignature(
    const FString& GlobalSignature,
    const int32    MaterialSlotIndex)
{
    if (GlobalSignature.IsEmpty() || MaterialSlotIndex == INDEX_NONE)
    {
        return FString();
    }

    return FMD5::HashAnsiString(*FString::Printf(
        TEXT("DWC.WetPartDataTexture.Slot.v8|Global=%s|Slot=%d"),
        *GlobalSignature,
        MaterialSlotIndex));
}

bool FWetClothingWetPartDataTextureBaker::ResolveProfileParameters(
    const FWetPartProfileAssignment* ProfileAssignment,
    FWetnessProfileParameters&       OutParameters)
{
    if (ProfileAssignment != nullptr && ProfileAssignment->HasSourceProfile())
    {
        UObject* SourceObject = ProfileAssignment->GetSourceProfilePath().ResolveObject();
        if (SourceObject == nullptr)
        {
            SourceObject = ProfileAssignment->GetSourceProfilePath().TryLoad();
        }

        if (const UWetnessProfile* Profile = Cast<UWetnessProfile>(SourceObject))
        {
            OutParameters = Profile->GetParameters();
            FWetnessProfileEditorPolicy::SanitizeParameters(OutParameters);
            return true;
        }
    }

    OutParameters = ProfileAssignment != nullptr ? ProfileAssignment->Parameters : FWetnessProfileParameters();
    FWetnessProfileEditorPolicy::SanitizeParameters(OutParameters);
    return true;
}

FString FWetClothingWetPartDataTextureBaker::MakeProfileStableKey(
    const FWetPartProfileAssignment* ProfileAssignment,
    const FWetnessProfileParameters& Parameters)
{
    const FString ParameterHash = FMD5::HashAnsiString(*FString::Printf(
        TEXT("DWC.RenderProfile.v8.PathIndependent|SurfaceTextureNormalization=%d|%s"),
        DWCSurfaceTextureNormalization::Version,
        *MakeParametersKey(Parameters)));
    // Runtime rows are keyed only by resolved content. Moving or renaming the
    // source profile must not invalidate or duplicate a bake with identical parameters.
    return FString::Printf(TEXT("ProfileContent:%s"), *ParameterHash);
}

bool FWetClothingWetPartDataTextureBaker::IsUVPointInsideTriangle(
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

int32 FWetClothingWetPartDataTextureBaker::PaintTriangle(
    TArray<FColor>&                    Pixels,
    TArray<bool>&                      PaintedMask,
    const int32                        Width,
    const int32                        Height,
    const FWetClothingAssetUVTriangle& Triangle,
    const FColor&                      PackedPartData)
{
    const FVector2D& A = Triangle.UVs[0];
    const FVector2D& B = Triangle.UVs[1];
    const FVector2D& C = Triangle.UVs[2];

    const int32 MinX = IntCastChecked<int32>(FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X) * Width), 0, Width - 1));
    const int32 MaxX = IntCastChecked<int32>(FMath::Clamp(FMath::FloorToInt(FMath::Max3(A.X, B.X, C.X) * Width), 0, Width - 1));
    const int32 MinY = IntCastChecked<int32>(FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y) * Height), 0, Height - 1));
    const int32 MaxY = IntCastChecked<int32>(FMath::Clamp(FMath::FloorToInt(FMath::Max3(A.Y, B.Y, C.Y) * Height), 0, Height - 1));

    int32 PaintedPixelCount = 0;
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    {
        for (int32 X = MinX; X <= MaxX; ++X)
        {
            const FVector2D SampleUV(
                (static_cast<double>(X) + 0.5) / Width,
                (static_cast<double>(Y) + 0.5) / Height);
            if (!IsUVPointInsideTriangle(SampleUV, A, B, C))
            {
                continue;
            }

            const int32 PixelIndex = Y * Width + X;
            PaintedPixelCount += PaintedMask[PixelIndex] ? 0 : 1;
            Pixels[PixelIndex] = PackedPartData;
            PaintedMask[PixelIndex] = true;
        }
    }

    if (PaintedPixelCount == 0)
    {
        const FVector2D Center = (A + B + C) / 3.0;
        const int32     X = IntCastChecked<int32>(FMath::Clamp(FMath::FloorToInt(Center.X * Width), 0, Width - 1));
        const int32     Y = IntCastChecked<int32>(FMath::Clamp(FMath::FloorToInt(Center.Y * Height), 0, Height - 1));
        const int32     PixelIndex = Y * Width + X;
        PaintedPixelCount += PaintedMask[PixelIndex] ? 0 : 1;
        Pixels[PixelIndex] = PackedPartData;
        PaintedMask[PixelIndex] = true;
    }

    return PaintedPixelCount;
}

void FWetClothingWetPartDataTextureBaker::DilatePaintedPixels(
    TArray<FColor>& Pixels,
    TArray<bool>&   PaintedMask,
    const int32     Width,
    const int32     Height,
    const int32     PaddingPixels)
{
    for (int32 Step = 0; Step < FMath::Clamp(PaddingPixels, 0, 32); ++Step)
    {
        const TArray<FColor> PreviousPixels = Pixels;
        const TArray<bool>   PreviousMask = PaintedMask;
        bool                 bChanged = false;

        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                const int32 Index = Y * Width + X;
                if (PreviousMask[Index])
                {
                    continue;
                }

                for (int32 DY = -1; DY <= 1 && !PaintedMask[Index]; ++DY)
                {
                    for (int32 DX = -1; DX <= 1 && !PaintedMask[Index]; ++DX)
                    {
                        if (DX == 0 && DY == 0)
                        {
                            continue;
                        }
                        const int32 NX = X + DX;
                        const int32 NY = Y + DY;
                        if (NX < 0 || NY < 0 || NX >= Width || NY >= Height)
                        {
                            continue;
                        }
                        const int32 NeighborIndex = NY * Width + NX;
                        if (PreviousMask[NeighborIndex])
                        {
                            Pixels[Index] = PreviousPixels[NeighborIndex];
                            PaintedMask[Index] = true;
                            bChanged = true;
                        }
                    }
                }
            }
        }

        if (!bChanged)
        {
            break;
        }
    }
}

UTexture2D* FWetClothingWetPartDataTextureBaker::CreateOrUpdateTextureAsset(
    UWetClothingAsset&    WetClothingAsset,
    const int32           MaterialSlotIndex,
    const TArray<FColor>& Pixels,
    const int32           Width,
    const int32           Height,
    FString&              OutErrorMessage)
{
#if WITH_EDITORONLY_DATA
    const FString WcaPackageName = WetClothingAsset.GetOutermost()->GetName();
    const FString WcaFolder = FPackageName::GetLongPackagePath(WcaPackageName);
    if (WcaFolder.IsEmpty())
    {
        OutErrorMessage = TEXT("Could not resolve a package path for the Wet Clothing Asset.");
        return nullptr;
    }

    const FString ObjectName = ObjectTools::SanitizeObjectName(
        FString::Printf(TEXT("T_%s_Slot%d_WetPartData"), *WetClothingAsset.GetName(), MaterialSlotIndex));
    const FString GeneratedFolder =
        DWCGeneratedAssetPaths::MakeAssetRoot(WcaFolder, WetClothingAsset.GetName()) / TEXT("LUT") / TEXT("Profiles");
    const FString PackageName = GeneratedFolder / ObjectName;
    const FString ObjectPath = PackageName + TEXT(".") + ObjectName;

    // Prefer the serialized hard reference. A Content Browser move changes the package
    // location but must not cause the baker to create or delete a second texture.
    UTexture2D* Texture = nullptr;
    if (const FWetClothingBakedWetPartDataSlotTexture* ExistingSlot =
            WetClothingAsset.Derived.Inline.BakedWetPartData.FindSlot(MaterialSlotIndex))
    {
        Texture = ExistingSlot->WetPartDataTexture.Get();
    }

    if (Texture == nullptr)
    {
        UObject* ExistingObject = LoadObject<UObject>(nullptr, *ObjectPath);
        if (ExistingObject != nullptr && !ExistingObject->IsA<UTexture2D>())
        {
            OutErrorMessage = FString::Printf(
                TEXT("Wet Part Data Texture path '%s' is occupied by an incompatible asset of type '%s'. Move or rename that asset and try again."),
                *ObjectPath,
                *GetNameSafe(ExistingObject->GetClass()));
            return nullptr;
        }

        Texture = Cast<UTexture2D>(ExistingObject);
        if (Texture != nullptr)
        {
            FGuid      ExistingOwnerGuid;
            const bool bHasOwner = WetClothingAsset.TryGetGeneratedAssetOwnerGuid(Texture, ExistingOwnerGuid);
            if (!bHasOwner)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Wet Part Data Texture path '%s' is occupied by an unowned texture. DWC will not adopt or overwrite it automatically."),
                    *ObjectPath);
                return nullptr;
            }
            if (ExistingOwnerGuid != WetClothingAsset.GetAssetGuid())
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Wet Part Data Texture path '%s' is owned by another Wet Clothing Asset. Move that asset or choose a different output location."),
                    *ObjectPath);
                return nullptr;
            }
        }
    }

    bool bCreatedAsset = false;
    if (Texture == nullptr)
    {
        UPackage* Package = CreatePackage(*PackageName);
        if (Package == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to create package '%s'."), *PackageName);
            return nullptr;
        }
        Texture = NewObject<UTexture2D>(Package, *ObjectName, RF_Public | RF_Standalone | RF_Transactional);
        bCreatedAsset = true;
    }
    else
    {
        Texture->Modify();
    }

    if (!WetClothingAsset.TagGeneratedAsset(Texture))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Texture '%s' is tagged as generated by another Wet Clothing Asset."),
            *GetNameSafe(Texture));
        return nullptr;
    }

    if (bCreatedAsset)
    {
        FAssetRegistryModule::AssetCreated(Texture);
    }

    Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
    Texture->SRGB = false;
    Texture->CompressionSettings = TC_VectorDisplacementmap; // Exact uncompressed RGBA8 IDs.
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->Filter = TF_Nearest;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->NeverStream = true;
    Texture->PostEditChange();
    Texture->UpdateResource();
    Texture->MarkPackageDirty();
    OutErrorMessage.Reset();
    return Texture;
#else
    OutErrorMessage = TEXT("Wet Part Data Texture baking requires editor-only texture source data.");
    return nullptr;
#endif
}

FString FWetClothingWetPartDataTextureBaker::MakeBuildSignature(const UWetClothingAsset* WetClothingAsset)
{
    if (WetClothingAsset == nullptr ||
        WetClothingAsset->GetRuntimeSkeletalMesh() == nullptr ||
        WetClothingAsset->GetDWCDataUVChannelIndex() == INDEX_NONE)
    {
        return FString();
    }

    const FDWCDataUVLODMetadata* DataUVMetadata = WetClothingAsset->FindDataUVMetadataForLOD(WetClothingAsset->GetSimulationLODIndex());
#if WITH_EDITORONLY_DATA
    const FDWCEditorUVTopologyData* OriginalUVTopology = WetClothingAsset->FindOriginalUVTopologyForLOD(WetClothingAsset->GetSimulationLODIndex());
#endif
    const FString RuntimeMeshContentSignature = UWetClothingAsset::BuildMeshContentSignature(
        WetClothingAsset->GetRuntimeSkeletalMesh(),
        WetClothingAsset->GetSimulationLODIndex(),
        WetClothingAsset->GetDWCDataUVChannelIndex());
    FString Canonical = FString::Printf(
        TEXT("DWC.WetPartDataTexture.v9.PathIndependent|MeshContent=%s|DataUV=%d|DataUVInput=%s|DataUVOutput=%s|OriginalTopology=%s|")
            TEXT("Resolution=%d|Padding=%d|SurfaceTextureVersion=%d|SurfaceTextureResolution=%d"),
        *RuntimeMeshContentSignature,
        WetClothingAsset->GetDWCDataUVChannelIndex(),
        DataUVMetadata != nullptr ? *DataUVMetadata->MeshInputSignature : TEXT("None"),
        DataUVMetadata != nullptr ? *DataUVMetadata->DataUVOutputSignature : TEXT("None"),
#if WITH_EDITORONLY_DATA
        OriginalUVTopology != nullptr ? *OriginalUVTopology->BuildSignature : TEXT("None"),
#else
        TEXT("EditorOnly"),
#endif
        DWCWetPartDataTextureBake::Resolution,
        DWCWetPartDataTextureBake::PaddingPixels,
        DWCSurfaceTextureNormalization::Version,
        DWCSurfaceTextureNormalization::Resolution);

    TArray<FProfileBakeEntry> Entries;
    CollectBakeEntries(*WetClothingAsset, Entries);

    for (const FProfileBakeEntry& BakeEntry : Entries)
    {
        if (BakeEntry.Entry == nullptr)
        {
            continue;
        }

        FWetnessProfileParameters Parameters;
        ResolveProfileParameters(BakeEntry.Profile, Parameters);
        TArray<int32> IslandIDs = BakeEntry.Entry->AssignedUVIslandIDs;
        IslandIDs.Sort();
        Canonical += FString::Printf(
            TEXT("|Slot=%d;OriginalUV=%d;Part=%d;Profile=%s;Key=%s;OverrideDropletStampSize=%d;DropletRadiusScale=%.9g;OverrideDropletFlowStampSize=%d;DropletFlowSizeScale=%.9g;DropletDetailSize=%.9g;DropletFlowDetailSize=%.9g;Islands="),
            BakeEntry.MaterialSlotIndex,
            WetClothingAsset->GetOriginalUVChannelIndex(),
            BakeEntry.Entry->WetPartID,
            BakeEntry.Profile != nullptr ? *BakeEntry.Profile->GetSourceProfilePath().ToString() : TEXT("None"),
            *MakeProfileStableKey(BakeEntry.Profile, Parameters),
            BakeEntry.Entry->SurfaceWater.bOverrideDropletStampSize ? 1 : 0,
            BakeEntry.Entry->SurfaceWater.DropletRadiusScale,
            BakeEntry.Entry->SurfaceWater.bOverrideDropletFlowStampSize ? 1 : 0,
            BakeEntry.Entry->SurfaceWater.DropletFlowSizeScale,
            BakeEntry.Entry->SurfaceWater.DropletDetailSize,
            BakeEntry.Entry->SurfaceWater.DropletFlowDetailSize);
        for (const int32 IslandID : IslandIDs)
        {
            Canonical += FString::Printf(TEXT("%d,"), IslandID);
        }
    }

    return FMD5::HashAnsiString(*Canonical);
}

bool FWetClothingWetPartDataTextureBaker::Bake(
    UWetClothingAsset*                        WetClothingAsset,
    FWetClothingWetPartDataTextureBakeResult& OutResult,
    FString&                                  OutErrorMessage)
{
    OutResult = FWetClothingWetPartDataTextureBakeResult();

    if (WetClothingAsset == nullptr || WetClothingAsset->GetRuntimeSkeletalMesh() == nullptr)
    {
        OutErrorMessage = TEXT("Generate the prepared DWC Skeletal Mesh before baking Wet Part Data Textures.");
        return false;
    }
    if (!WetClothingAsset->HasValidDataUVForLOD(WetClothingAsset->GetSimulationLODIndex()) || WetClothingAsset->GetDWCDataUVChannelIndex() == INDEX_NONE)
    {
        OutErrorMessage = TEXT("Wet Part Data Textures require valid sealed DWC UV Channel. Create a new WCA if the stored layout is invalid.");
        return false;
    }

    TArray<FProfileBakeEntry> Entries;
    CollectBakeEntries(*WetClothingAsset, Entries);

    if (Entries.IsEmpty())
    {
        OutErrorMessage = TEXT("No wettable Wet Part entries are available for Wet Part Data Texture baking.");
        return false;
    }

    // Build deterministic WCA-wide IDs before rasterizing any slot.
    TMap<FString, uint8>                         LocalIDByStableKey;
    TArray<FWetClothingLocalRenderProfile>       LocalProfiles;
    TMap<const FWetClothingWetPartEntry*, uint8> LocalIDByEntry;
    for (const FProfileBakeEntry& BakeEntry : Entries)
    {
        const FWetClothingWetPartEntry* Entry = BakeEntry.Entry;
        if (Entry == nullptr)
        {
            continue;
        }

        FWetnessProfileParameters Parameters;
        ResolveProfileParameters(BakeEntry.Profile, Parameters);
        const FString StableKey = MakeProfileStableKey(BakeEntry.Profile, Parameters);

        uint8 LocalProfileID = DWCWetPartDataTextureBake::NeutralProfileID;
        if (const uint8* ExistingID = LocalIDByStableKey.Find(StableKey))
        {
            LocalProfileID = *ExistingID;
        }
        else
        {
            if (LocalProfiles.Num() >= DWCWetPartDataTextureBake::MaxLocalProfileCount)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Wet Part Data Textures support at most %d WCA-local profiles."),
                    DWCWetPartDataTextureBake::MaxLocalProfileCount);
                return false;
            }

            LocalProfileID = static_cast<uint8>(LocalProfiles.Num() + 1);
            FWetClothingLocalRenderProfile& LocalProfile = LocalProfiles.AddDefaulted_GetRef();
            LocalProfile.SetSourceProfilePath(BakeEntry.Profile != nullptr ? BakeEntry.Profile->GetSourceProfilePath() : FSoftObjectPath());
            LocalProfile.Parameters = Parameters;
            LocalProfile.StableKey = StableKey;
            if (!FWetClothingSurfaceTextureNormalizer::NormalizeProfileTextures(
                    *WetClothingAsset,
                    Parameters,
                    LocalProfile,
                    OutErrorMessage))
            {
                return false;
            }

            // Runtime rows retain either the authored 512 texture or a DWC-generated
            // 512 duplicate built by Unreal's texture pipeline. The dedicated render-profile
            // texture fields remain the authoritative Texture2DArray upload sources.
            LocalProfile.Parameters.SurfaceWater.DropletNormalTexture = nullptr;
            LocalProfile.Parameters.SurfaceWater.DropletMaskTexture = nullptr;
            LocalProfile.Parameters.SurfaceWater.DropletFlowNormalTexture = nullptr;
            LocalProfile.Parameters.SurfaceWater.DropletFlowMaskTexture = nullptr;
            LocalIDByStableKey.Add(StableKey, LocalProfileID);
        }
        LocalIDByEntry.Add(Entry, LocalProfileID);
    }

    TMap<int32, TArray<const FWetClothingWetPartEntry*>> EntriesBySlot;
    for (const FProfileBakeEntry& BakeEntry : Entries)
    {
        if (BakeEntry.Entry != nullptr)
        {
            EntriesBySlot.FindOrAdd(BakeEntry.MaterialSlotIndex).Add(BakeEntry.Entry);
        }
    }

    TArray<int32> MaterialSlots;
    EntriesBySlot.GetKeys(MaterialSlots);
    MaterialSlots.Sort();

    const int32                                     Width = DWCWetPartDataTextureBake::Resolution;
    const int32                                     Height = DWCWetPartDataTextureBake::Resolution;
    TArray<FWetClothingBakedWetPartDataSlotTexture> BakedSlotTextures;

    for (const int32 MaterialSlotIndex : MaterialSlots)
    {
        const TArray<const FWetClothingWetPartEntry*>& SlotEntries = EntriesBySlot.FindChecked(MaterialSlotIndex);
        TMap<int32, FColor>                            PackedPartDataByTriangleID;

        for (const FWetClothingWetPartEntry* Entry : SlotEntries)
        {
            TArray<FWetClothingAssetUVIsland> OriginalIslands;
            FString                           BuildError;
            if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
                    WetClothingAsset->GetRuntimeSkeletalMesh(),
                    0,
                    WetClothingAsset->GetOriginalUVChannelIndex(),
                    MaterialSlotIndex,
                    OriginalIslands,
                    &BuildError))
            {
                OutErrorMessage = BuildError;
                return false;
            }

            const uint8  LocalProfileID = LocalIDByEntry.FindChecked(Entry);
            const FColor PackedPartData(
                LocalProfileID,
                DWCWetPartDataTextureBake::EncodeDetailSize(Entry->SurfaceWater.DropletDetailSize),
                DWCWetPartDataTextureBake::EncodeDetailSize(Entry->SurfaceWater.DropletFlowDetailSize),
                0);
            for (const FWetClothingAssetUVIsland& Island : OriginalIslands)
            {
                if (!Entry->AssignedUVIslandIDs.Contains(Island.UVIslandID))
                {
                    continue;
                }
                for (const int32 TriangleID : Island.TriangleIDs)
                {
                    if (const FColor* ExistingData = PackedPartDataByTriangleID.Find(TriangleID))
                    {
                        if (*ExistingData != PackedPartData)
                        {
                            OutErrorMessage = FString::Printf(
                                TEXT("Material slot %d triangle %d belongs to Wet Parts with different profile/detail data."),
                                MaterialSlotIndex,
                                TriangleID);
                            return false;
                        }
                    }
                    else
                    {
                        PackedPartDataByTriangleID.Add(TriangleID, PackedPartData);
                    }
                }
            }
        }

        TArray<FWetClothingAssetUVIsland> DataUVIslands;
        FString                           BuildError;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotDataUVIslands(
                *WetClothingAsset,
                0,
                MaterialSlotIndex,
                DataUVIslands,
                &BuildError))
        {
            OutErrorMessage = BuildError;
            return false;
        }

        TArray<FColor> Pixels;
        TArray<bool>   PaintedMask;
        Pixels.Init(FColor(DWCWetPartDataTextureBake::NeutralProfileID, DWCWetPartDataTextureBake::EncodeDetailSize(1.0f), DWCWetPartDataTextureBake::EncodeDetailSize(1.0f), 0), Width * Height);
        PaintedMask.Init(false, Width * Height);
        int32 SlotPaintedPixelCount = 0;

        for (const FWetClothingAssetUVIsland& Island : DataUVIslands)
        {
            for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
            {
                const FColor* PackedPartData = PackedPartDataByTriangleID.Find(Triangle.TriangleID);
                if (PackedPartData == nullptr)
                {
                    continue;
                }
                SlotPaintedPixelCount += PaintTriangle(
                    Pixels,
                    PaintedMask,
                    Width,
                    Height,
                    Triangle,
                    *PackedPartData);
            }
        }

        if (SlotPaintedPixelCount <= 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Wet Part Data Texture slot %d did not rasterize any DWC UV Channel pixels."),
                MaterialSlotIndex);
            return false;
        }

        DilatePaintedPixels(Pixels, PaintedMask, Width, Height, DWCWetPartDataTextureBake::PaddingPixels);

        UTexture2D* WetPartDataTexture = CreateOrUpdateTextureAsset(
            *WetClothingAsset,
            MaterialSlotIndex,
            Pixels,
            Width,
            Height,
            OutErrorMessage);
        if (WetPartDataTexture == nullptr)
        {
            return false;
        }

        FWetClothingBakedWetPartDataSlotTexture& BakedSlot = BakedSlotTextures.AddDefaulted_GetRef();
        BakedSlot.MaterialSlotIndex = MaterialSlotIndex;
        BakedSlot.WetPartDataTexture = WetPartDataTexture;
        BakedSlot.BuildSignature = MakeSlotBuildSignature(MakeBuildSignature(WetClothingAsset), MaterialSlotIndex);
        BakedSlot.BakeGuid = FGuid::NewGuid();

        FWetClothingWetPartDataSlotBakeResult& SlotResult = OutResult.SlotResults.AddDefaulted_GetRef();
        SlotResult.MaterialSlotIndex = MaterialSlotIndex;
        SlotResult.WetPartDataTexture = WetPartDataTexture;
        SlotResult.PaintedPixelCount = SlotPaintedPixelCount;
        OutResult.PaintedPixelCount += SlotPaintedPixelCount;
    }

    WetClothingAsset->Modify();
    FWetClothingBakedWetPartData& Baked = WetClothingAsset->Derived.Inline.BakedWetPartData;
    Baked.NormalizedNeutralSurfaceNormal =
        FWetClothingSurfaceTextureNormalizer::GetOrCreateNeutralNormalTexture(
            *WetClothingAsset,
            OutErrorMessage);
    if (Baked.NormalizedNeutralSurfaceNormal == nullptr)
    {
        return false;
    }
    Baked.LocalProfiles = MoveTemp(LocalProfiles);
    Baked.SlotTextures = MoveTemp(BakedSlotTextures);
    Baked.DataUVChannelIndex = WetClothingAsset->GetDWCDataUVChannelIndex();
    Baked.Resolution = Width;
    Baked.PaddingPixels = DWCWetPartDataTextureBake::PaddingPixels;
    Baked.SurfaceTextureResolution = DWCSurfaceTextureNormalization::Resolution;
    Baked.BuildSignature = MakeBuildSignature(WetClothingAsset);
    Baked.BakeGuid = FGuid::NewGuid();

    WetClothingAsset->MarkPackageDirty();

    OutResult.LocalProfileCount = Baked.LocalProfiles.Num();
    OutErrorMessage.Reset();
    return true;
}
