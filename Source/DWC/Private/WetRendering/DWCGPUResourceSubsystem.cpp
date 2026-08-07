//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetRendering/DWCGPUResourceSubsystem.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/SecureHash.h"
#include "PixelFormat.h"
#include "Rendering/Texture2DResource.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "WetRendering/DWCSurfaceTextureSharedAsset.h"
#include "WetRendering/WetMaterialParameters.h"
#include "Utility/DWCLog.h"

namespace
{
    constexpr float GlobalTexelSize = 1.0f / static_cast<float>(UDWCGPUResourceSubsystem::GlobalLUTWidth);
    constexpr int32 InitialTextureArrayCapacity = 16;
    const FName PreviewSurfaceWaterOverrideParameter(TEXT("DWC_PreviewSurfaceWaterOverride"));
    const FName PreviewSurfaceWaterAmountParameter(TEXT("DWC_PreviewSurfaceWaterAmount"));

    int32 ResolveTextureArrayCapacity(const int32 RequiredSlices)
    {
        const uint32 SafeRequired = static_cast<uint32>(FMath::Max(RequiredSlices, InitialTextureArrayCapacity));
        return static_cast<int32>(FMath::RoundUpToPowerOfTwo(SafeRequired));
    }

    UTexture2D* CreateFloatLUTTexture(
        UObject* Outer,
        const FName Name,
        const int32 Width,
        const TArray<FLinearColor>& Pixels)
    {
        if (Width <= 0 || Pixels.Num() != Width)
        {
            return nullptr;
        }

        UTexture2D* Texture = UTexture2D::CreateTransient(Width, 1, PF_A32B32G32R32F, Name);
        if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.IsEmpty())
        {
            return nullptr;
        }

        Texture->Rename(nullptr, Outer, REN_DontCreateRedirectors | REN_NonTransactional);
        Texture->SRGB = false;
        Texture->Filter = TF_Nearest;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
#if WITH_EDITORONLY_DATA
        Texture->MipGenSettings = TMGS_NoMipmaps;
#endif
        Texture->NeverStream = true;

        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FLinearColor));
        Mip.BulkData.Unlock();
        Texture->UpdateResource();
        return Texture;
    }

    UTexture2D* ResolveDirectSurfaceTexture(
        const FWetClothingLocalRenderProfile& LocalProfile,
        UTexture2D* BakedArrayTexture,
        UTexture2D* AuthoredSourceTexture,
        const FSoftObjectPath& AuthoredSourcePath,
        const TCHAR* TextureRole,
        const bool bNormalMap)
    {
        // Shipping/non-editor runtime data is self-contained. Only the prepared texture
        // stored in BakedWetPartData may be uploaded to the shared Texture2DArray.
        if (BakedArrayTexture != nullptr)
        {
            return BakedArrayTexture;
        }

        const FString ProfileIdentity = !LocalProfile.StableKey.IsEmpty()
            ? LocalProfile.StableKey
            : LocalProfile.GetSourceProfilePath().ToString();

#if WITH_EDITOR
        const auto IsCompatibleAuthoredFallback = [bNormalMap](const UTexture2D* Texture)
        {
            const TextureCompressionSettings ExpectedCompression =
                bNormalMap ? TC_Normalmap : TC_Masks;
            return Texture != nullptr &&
                   Texture->GetSizeX() == DWCSurfaceTextureSharedAsset::Resolution &&
                   Texture->GetSizeY() == DWCSurfaceTextureSharedAsset::Resolution &&
                   !Texture->SRGB &&
                   Texture->CompressionSettings == ExpectedCompression;
        };

        // Editor preview may use an authored source only when it already satisfies the
        // same fixed-resolution/format contract as the prepared array texture.
        if (IsCompatibleAuthoredFallback(AuthoredSourceTexture))
        {
            return AuthoredSourceTexture;
        }
        if (!AuthoredSourcePath.IsValid())
        {
            return nullptr;
        }

        UTexture2D* LoadedTexture = Cast<UTexture2D>(AuthoredSourcePath.ResolveObject());
        if (LoadedTexture == nullptr)
        {
            LoadedTexture = Cast<UTexture2D>(AuthoredSourcePath.TryLoad());
        }

        if (LoadedTexture != nullptr && !IsCompatibleAuthoredFallback(LoadedTexture))
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC refused authored %s texture '%s' for profile '%s' because it is %dx%d. The baked/generated %dx%d reference is missing; rebake Render Profile Lookup Texture."),
                TextureRole,
                *LoadedTexture->GetPathName(),
                *ProfileIdentity,
                LoadedTexture->GetSizeX(),
                LoadedTexture->GetSizeY(),
                DWCSurfaceTextureSharedAsset::Resolution,
                DWCSurfaceTextureSharedAsset::Resolution);
            return nullptr;
        }
        if (LoadedTexture == nullptr)
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC could not load authored %s texture '%s' for profile '%s'. Use Build for Runtime > Bake Render Profile Lookup Texture after validating the 512x512 source texture."),
                TextureRole,
                *AuthoredSourcePath.ToString(),
                *ProfileIdentity);
        }
        return LoadedTexture;
#else
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DWC prepared %s texture is missing for profile '%s'. Non-editor builds do not load authored Surface Water textures; rebake Render Profile Lookup Texture before cooking."),
            TextureRole,
            *ProfileIdentity);
        return nullptr;
#endif
    }

    FString MakeTextureContentIdentity(const UTexture2D* Texture)
    {
        if (Texture == nullptr)
        {
            return TEXT("None");
        }
#if WITH_EDITORONLY_DATA
        if (Texture->Source.IsValid())
        {
            return FString::Printf(
                TEXT("Source{%s,%d,%d,%d}"),
                *Texture->Source.GetId().ToString(EGuidFormats::Digits),
                Texture->Source.GetSizeX(),
                Texture->Source.GetSizeY(),
                static_cast<int32>(Texture->Source.GetFormat()));
        }
#endif
        return FString::Printf(
            TEXT("Built{%d,%d,%d,%d,%d}"),
            Texture->GetSizeX(),
            Texture->GetSizeY(),
            static_cast<int32>(Texture->GetPixelFormat()),
            static_cast<int32>(Texture->CompressionSettings),
            Texture->SRGB ? 1 : 0);
    }

    FString ResolveProfileTextureIdentity(
        UTexture2D* BakedNormalizedTexture,
        UTexture2D* AuthoredSourceTexture,
        const FSoftObjectPath& AuthoredSourcePath)
    {
        if (BakedNormalizedTexture != nullptr)
        {
            return MakeTextureContentIdentity(BakedNormalizedTexture);
        }
        if (AuthoredSourceTexture != nullptr)
        {
            return MakeTextureContentIdentity(AuthoredSourceTexture);
        }
#if WITH_EDITOR
        if (AuthoredSourcePath.IsValid())
        {
            const UTexture2D* LoadedTexture = Cast<UTexture2D>(AuthoredSourcePath.ResolveObject());
            if (LoadedTexture == nullptr)
            {
                LoadedTexture = Cast<UTexture2D>(AuthoredSourcePath.TryLoad());
            }
            return LoadedTexture != nullptr ? MakeTextureContentIdentity(LoadedTexture) : FString(TEXT("Missing"));
        }
#endif
        return FString(TEXT("None"));
    }

    FString DescribeSurfaceTexture(UTexture2D* Texture)
    {
        return Texture != nullptr
            ? FString::Printf(
                TEXT("%s (%dx%d format %d)"),
                *Texture->GetPathName(),
                Texture->GetSizeX(),
                Texture->GetSizeY(),
                static_cast<int32>(Texture->GetPixelFormat()))
            : FString(TEXT("None"));
    }

    FString ResolveProfileKey(const FWetClothingLocalRenderProfile& LocalProfile)
    {
        if (!LocalProfile.StableKey.IsEmpty())
        {
            return LocalProfile.StableKey;
        }

        const FSurfaceWaterProfileParameters& Surface = LocalProfile.Parameters.SurfaceWater;
        const FString ParameterState = FString::Printf(
            TEXT("AbsorbedDarkening=%.9g|AbsorbedGlossiness=%.9g|")
            TEXT("DropletsEnabled=%d|SecondaryDropletsEnabled=%d|Droplet1Normal=%s|Droplet1Mask=%s|")
            TEXT("Droplet2Normal=%s|Droplet2Mask=%s|")
            TEXT("TargetRoughness=%.9g|NormalStrength=%.9g|RoughnessBlend=%.9g|")
            TEXT("SurfaceWaterTotalStrength=%.9g|StaticColorBlend=%.9g|WaterSpecular=%.9g|")
            TEXT("FlowTargetRoughness=%.9g|FlowRoughnessBlend=%.9g|FlowTotalStrength=%.9g|")
            TEXT("FlowColorBlend=%.9g|FlowNormalStrength=%.9g|FlowSpecular=%.9g"),
            LocalProfile.Parameters.GetAbsorbedDarkeningStrength(),
            LocalProfile.Parameters.GetAbsorbedGlossinessStrength(),
            Surface.bEnabled ? 1 : 0,
            Surface.bUseSecondaryDroplets ? 1 : 0,
            *ResolveProfileTextureIdentity(
                LocalProfile.NormalizedDropletNormal,
                Surface.DropletNormalTexture,
                LocalProfile.GetSourceDropletNormalPath()),
            *ResolveProfileTextureIdentity(
                LocalProfile.NormalizedDropletMask,
                Surface.DropletMaskTexture,
                LocalProfile.GetSourceDropletMaskPath()),
            *ResolveProfileTextureIdentity(
                LocalProfile.NormalizedDropletFlowNormal,
                Surface.DropletFlowNormalTexture,
                LocalProfile.GetSourceDropletFlowNormalPath()),
            *ResolveProfileTextureIdentity(
                LocalProfile.NormalizedDropletFlowMask,
                Surface.DropletFlowMaskTexture,
                LocalProfile.GetSourceDropletFlowMaskPath()),
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
        const FString ParameterHash = FMD5::HashAnsiString(*ParameterState);
        return FString::Printf(TEXT("ProfileContent:%s"), *ParameterHash);
    }

    FString MakeRenderFallbackParameterKey(const FWetnessProfileParameters& Parameters)
    {
        const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
        return FString::Printf(
            TEXT("AbsorbedDarkening=%.9g|AbsorbedGlossiness=%.9g|DropletsEnabled=%d|SecondaryDropletsEnabled=%d|Droplet1Normal=%s|Droplet1Mask=%s|")
            TEXT("Droplet2Normal=%s|Droplet2Mask=%s|")
            TEXT("TargetRoughness=%.9g|NormalStrength=%.9g|RoughnessBlend=%.9g|")
            TEXT("SurfaceWaterTotalStrength=%.9g|StaticColorBlend=%.9g|WaterSpecular=%.9g|")
            TEXT("FlowTargetRoughness=%.9g|FlowRoughnessBlend=%.9g|FlowTotalStrength=%.9g|")
            TEXT("FlowColorBlend=%.9g|FlowNormalStrength=%.9g|FlowSpecular=%.9g"),
            Parameters.GetAbsorbedDarkeningStrength(),
            Parameters.GetAbsorbedGlossinessStrength(),
            Surface.bEnabled ? 1 : 0,
            Surface.bUseSecondaryDroplets ? 1 : 0,
            *MakeTextureContentIdentity(Surface.DropletNormalTexture),
            *MakeTextureContentIdentity(Surface.DropletMaskTexture),
            *MakeTextureContentIdentity(Surface.DropletFlowNormalTexture),
            *MakeTextureContentIdentity(Surface.DropletFlowMaskTexture),
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

    bool ResolveSourceProfileParameters(
        const FSoftObjectPath& SourceProfilePath,
        const bool bResolveUnloadedProfile,
        FWetnessProfileParameters& OutParameters)
    {
        if (!SourceProfilePath.IsValid())
        {
            return false;
        }

        UObject* SourceObject = SourceProfilePath.ResolveObject();
        if (SourceObject == nullptr && bResolveUnloadedProfile)
        {
            SourceObject = SourceProfilePath.TryLoad();
        }

        if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(SourceObject))
        {
            OutParameters = SourceProfile->GetParameters();
            return true;
        }

        return false;
    }

    void ApplyResolvedSourceProfileParameters(
        FWetClothingLocalRenderProfile& LocalProfile,
        const FWetnessProfileParameters& ResolvedParameters)
    {
        LocalProfile.Parameters = ResolvedParameters;
        LocalProfile.StableKey.Reset();

        const FSurfaceWaterProfileParameters& Surface = ResolvedParameters.SurfaceWater;
        LocalProfile.SetSourceDropletNormal(Surface.DropletNormalTexture.Get());
        LocalProfile.SetSourceDropletMask(Surface.DropletMaskTexture.Get());
        LocalProfile.SetSourceDropletFlowNormal(Surface.DropletFlowNormalTexture.Get());
        LocalProfile.SetSourceDropletFlowMask(Surface.DropletFlowMaskTexture.Get());

        // Refresh authored parameters and source identities here. Array-compatible
        // editor-prepared references are applied separately; non-editor builds retain
        // the generated/baked references already stored in LocalProfile.
    }

#if WITH_EDITOR
    void ApplyPreparedSourceProfileTextures(FWetClothingLocalRenderProfile& LocalProfile)
    {
        if (!LocalProfile.GetSourceProfilePath().IsValid())
        {
            return;
        }

        UObject* SourceObject = LocalProfile.GetSourceProfilePath().ResolveObject();
        if (SourceObject == nullptr)
        {
            SourceObject = LocalProfile.GetSourceProfilePath().TryLoad();
        }
        const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(SourceObject);
        if (SourceProfile == nullptr)
        {
            return;
        }

#if WITH_EDITORONLY_DATA
        if (!SourceProfile->HasPreparedSurfaceTextures())
        {
            return;
        }

        // A Wetness Profile may carry stale editor-only prepared-reference state
        // (for example bHasPreparedSurfaceTextures=true with null references after an
        // asset copy/migration). Never overwrite a valid WCA-baked or preview-prepared
        // texture with null or an incompatible source. The existing LocalProfile value
        // remains the authoritative fallback until a valid prepared reference exists.
        const auto ApplyPreparedTextureIfValid = [](
            TObjectPtr<UTexture2D>& InOutTexture,
            UTexture2D* Candidate,
            const bool bNormalMap)
        {
            const TextureCompressionSettings ExpectedCompression =
                bNormalMap ? TC_Normalmap : TC_Masks;
            if (Candidate != nullptr &&
                Candidate->GetSizeX() == DWCSurfaceTextureSharedAsset::Resolution &&
                Candidate->GetSizeY() == DWCSurfaceTextureSharedAsset::Resolution &&
                !Candidate->SRGB &&
                Candidate->CompressionSettings == ExpectedCompression)
            {
                InOutTexture = Candidate;
            }
        };

        const FSurfaceWaterProfileParameters& Surface = SourceProfile->GetParameters().SurfaceWater;
        ApplyPreparedTextureIfValid(
            LocalProfile.NormalizedDropletNormal,
            SourceProfile->GetPreparedDropletNormalTexture(),
            true);
        ApplyPreparedTextureIfValid(
            LocalProfile.NormalizedDropletMask,
            SourceProfile->GetPreparedDropletMaskTexture(),
            false);
        if (Surface.DropletFlowNormalTexture != nullptr)
        {
            ApplyPreparedTextureIfValid(
                LocalProfile.NormalizedDropletFlowNormal,
                SourceProfile->GetPreparedDroplet2NormalTexture(),
                true);
        }
        if (Surface.DropletFlowMaskTexture != nullptr)
        {
            ApplyPreparedTextureIfValid(
                LocalProfile.NormalizedDropletFlowMask,
                SourceProfile->GetPreparedDroplet2MaskTexture(),
                false);
        }
#endif
    }
#endif

    TArray<FWetClothingLocalRenderProfile> MakeResolvedLocalRenderProfiles(
        const UWetClothingAsset* WetClothingAsset)
    {
        TArray<FWetClothingLocalRenderProfile> Profiles;
        if (WetClothingAsset == nullptr)
        {
            return Profiles;
        }

        Profiles = WetClothingAsset->Derived.Inline.BakedWetPartData.LocalProfiles;
#if WITH_EDITOR
        // Editor builds resolve the latest WP values and its editor-prepared 512 textures.
        for (FWetClothingLocalRenderProfile& Profile : Profiles)
        {
            FWetnessProfileParameters ResolvedParameters;
            if (ResolveSourceProfileParameters(Profile.GetSourceProfilePath(), true, ResolvedParameters))
            {
                ApplyResolvedSourceProfileParameters(Profile, ResolvedParameters);
                ApplyPreparedSourceProfileTextures(Profile);
            }
        }
#endif

        return Profiles;
    }

    FString MakeResolvedProfileResourceSignature(const TArray<FWetClothingLocalRenderProfile>& Profiles)
    {
        FString Signature = FString::Printf(TEXT("DWC.ResolvedRenderProfiles.v2|Count=%d"), Profiles.Num());
        for (int32 ProfileIndex = 0; ProfileIndex < Profiles.Num(); ++ProfileIndex)
        {
            Signature += FString::Printf(
                TEXT("|%d=%s"),
                ProfileIndex + 1,
                *ResolveProfileKey(Profiles[ProfileIndex]));
        }
        return FMD5::HashAnsiString(*Signature);
    }

    const FWetClothingLocalRenderProfile* FindMatchingBakedFallbackProfile(
        const UWetClothingAsset* WetClothingAsset,
        const FWetPartProfileAssignment& SourceAssignment,
        const FWetnessProfileParameters& Parameters)
    {
        if (WetClothingAsset == nullptr ||
            !WetClothingAsset->Derived.Inline.BakedWetPartData.IsValid())
        {
            return nullptr;
        }

        const TArray<FWetClothingLocalRenderProfile>& LocalProfiles =
            WetClothingAsset->Derived.Inline.BakedWetPartData.LocalProfiles;
        const FString ParameterKey = MakeRenderFallbackParameterKey(Parameters);
        if (SourceAssignment.HasSourceProfile())
        {
            if (const FWetClothingLocalRenderProfile* ExactSourceMatch = LocalProfiles.FindByPredicate(
                    [&SourceAssignment, &ParameterKey](const FWetClothingLocalRenderProfile& Candidate)
                    {
                        return Candidate.GetSourceProfilePath() == SourceAssignment.GetSourceProfilePath() &&
                               MakeRenderFallbackParameterKey(Candidate.Parameters) == ParameterKey;
                    }))
            {
                return ExactSourceMatch;
            }
        }

        if (const FWetClothingLocalRenderProfile* ParameterMatch = LocalProfiles.FindByPredicate(
                [&ParameterKey](const FWetClothingLocalRenderProfile& Candidate)
                {
                    return MakeRenderFallbackParameterKey(Candidate.Parameters) == ParameterKey;
                }))
        {
            return ParameterMatch;
        }

        if (SourceAssignment.HasSourceProfile())
        {
            if (const FWetClothingLocalRenderProfile* SourceMatch = LocalProfiles.FindByPredicate(
                    [&SourceAssignment](const FWetClothingLocalRenderProfile& Candidate)
                    {
                        return Candidate.GetSourceProfilePath() == SourceAssignment.GetSourceProfilePath();
                    }))
            {
                return SourceMatch;
            }
        }

        return LocalProfiles.Num() == 1 ? &LocalProfiles[0] : nullptr;
    }

    bool ResolveFallbackRenderProfile(
        const UWetClothingAsset* WetClothingAsset,
        const int32 MaterialSlotIndex,
        FWetClothingLocalRenderProfile& OutProfile,
        const bool bResolveSourceProfile)
    {
        if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return false;
        }

        const FWetClothingEditableWetPartData& WetPartData =
            WetClothingAsset->Authored.PartData.EditableWetPartData;
        const FWetClothingAuthoredMaterialSlot* Slot = WetPartData.FindMaterialSlot(MaterialSlotIndex);
        if (Slot == nullptr)
        {
            return false;
        }

        const FWetClothingWetPartEntry* SourceEntry = Slot->FindPart(0);
        if (SourceEntry == nullptr && !Slot->WetPartEntries.IsEmpty())
        {
            SourceEntry = &Slot->WetPartEntries[0];
        }
        if (SourceEntry == nullptr)
        {
            return false;
        }

        const int32 ProfileIndex = WetPartData.Profiles.IsValidIndex(SourceEntry->ProfileIndex)
            ? SourceEntry->ProfileIndex
            : 0;
        const FWetPartProfileAssignment* SourceAssignment = WetPartData.FindProfile(ProfileIndex);
        if (SourceAssignment == nullptr)
        {
            return false;
        }

        FWetnessProfileParameters Parameters =
            WetClothingAsset->Derived.Inline.ResolvedWetnessProfileParameters.IsValidIndex(ProfileIndex)
                ? WetClothingAsset->Derived.Inline.ResolvedWetnessProfileParameters[ProfileIndex]
                : SourceAssignment->Parameters;
        bool bResolvedSourceProfile = false;
        if (bResolveSourceProfile && SourceAssignment->HasSourceProfile())
        {
            UObject* SourceObject = SourceAssignment->GetSourceProfilePath().ResolveObject();
            if (SourceObject == nullptr)
            {
                SourceObject = SourceAssignment->GetSourceProfilePath().TryLoad();
            }

            if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(SourceObject))
            {
                Parameters = SourceProfile->GetParameters();
                bResolvedSourceProfile = true;
            }
        }

        if (const FWetClothingLocalRenderProfile* BakedProfile =
                FindMatchingBakedFallbackProfile(WetClothingAsset, *SourceAssignment, Parameters))
        {
            OutProfile = *BakedProfile;
            OutProfile.SetSourceProfilePath(SourceAssignment->GetSourceProfilePath());
            if (bResolvedSourceProfile)
            {
                ApplyResolvedSourceProfileParameters(OutProfile, Parameters);
#if WITH_EDITOR
                ApplyPreparedSourceProfileTextures(OutProfile);
#endif
            }
            else
            {
                OutProfile.Parameters = Parameters;
            }
            return true;
        }

        OutProfile.SetSourceProfilePath(SourceAssignment->GetSourceProfilePath());
        if (bResolvedSourceProfile)
        {
            ApplyResolvedSourceProfileParameters(OutProfile, Parameters);
#if WITH_EDITOR
            ApplyPreparedSourceProfileTextures(OutProfile);
#endif
        }
        else
        {
            OutProfile.Parameters = Parameters;
        }
        return true;
    }

    struct FFallbackRenderProfileSlices
    {
        int32 DropletMask = 0;
        int32 DropletNormal = 0;
        int32 DropletFlowMask = 0;
        int32 DropletFlowNormal = 0;
    };

    FLinearColor MakeFallbackRenderProfileTexel(
        const FWetClothingLocalRenderProfile& Profile,
        const FFallbackRenderProfileSlices& Slices,
        const int32 TexelIndex)
    {
        const FWetnessProfileParameters& Parameters = Profile.Parameters;
        const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
        switch (TexelIndex)
        {
        case 0:
            return FLinearColor(
                Parameters.GetAbsorbedDarkeningStrength(),
                Parameters.GetAbsorbedGlossinessStrength(),
                static_cast<float>(Surface.bEnabled ? Slices.DropletNormal : 0),
                0.0f);

        case 1:
            return FLinearColor(
                FMath::Clamp(Surface.SurfaceWaterNormalStrength, 0.0f, 3.0f),
                FMath::Clamp(Surface.SurfaceWaterRoughnessBlend, 0.0f, 1.0f),
                0.0f,
                FMath::Clamp(Surface.SurfaceWaterSpecular, 0.0f, 1.0f));

        case 2:
            return FLinearColor(
                static_cast<float>(Surface.bEnabled ? Slices.DropletMask : 0),
                0.0f,
                FMath::Clamp(Surface.SurfaceWaterTargetRoughness, 0.0f, 1.0f),
                FMath::Clamp(Surface.SurfaceWaterTotalStrength, 0.0f, 1.0f));

        case 3:
            return FLinearColor::Black;

        case 4:
            return FLinearColor(
                static_cast<float>(Surface.SupportsSecondaryDroplets() ? Slices.DropletFlowNormal : 0),
                static_cast<float>(Surface.SupportsSecondaryDroplets() ? Slices.DropletFlowMask : 0),
                0.0f,
                0.0f);

        case 5:
            return Surface.SupportsSecondaryDroplets()
                ? FLinearColor(
                    FMath::Clamp(Surface.DropletFlowTotalStrength, 0.0f, 1.0f),
                    FMath::Clamp(Surface.DropletFlowTargetRoughness, 0.0f, 1.0f),
                    FMath::Clamp(Surface.DropletFlowRoughnessBlend, 0.0f, 1.0f),
                    FMath::Clamp(Surface.DropletFlowSpecular, 0.0f, 1.0f))
                : FLinearColor::Black;

        case 6:
            return FLinearColor(
                FMath::Clamp(Surface.SurfaceWaterColorBlend, 0.0f, 1.0f),
                Surface.SupportsSecondaryDroplets()
                    ? FMath::Clamp(Surface.DropletFlowColorBlend, 0.0f, 1.0f)
                    : 0.0f,
                Surface.SupportsSecondaryDroplets()
                    ? FMath::Clamp(Surface.DropletFlowNormalStrength, 0.0f, 3.0f)
                    : 0.0f,
                0.0f);

        default:
            return FLinearColor::Black;
        }
    }


    UTexture2D* CreateNeutralWetPartDataTexture(UObject* Outer)
    {
        // The encoded size range is 0.0..4.0, so authored size 1.0 maps to 64/255.
        constexpr uint8 DefaultDetailSizeEncoded = 64u;
        UTexture2D* Texture = UTexture2D::CreateTransient(1, 1, PF_B8G8R8A8, TEXT("DWC_NeutralWetPartData"));
        if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.IsEmpty())
        {
            return nullptr;
        }

        Texture->Rename(nullptr, Outer, REN_DontCreateRedirectors | REN_NonTransactional);
        Texture->SRGB = false;
        Texture->CompressionSettings = TC_VectorDisplacementmap;
        Texture->Filter = TF_Nearest;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
#if WITH_EDITORONLY_DATA
        Texture->MipGenSettings = TMGS_NoMipmaps;
#endif
        Texture->NeverStream = true;

        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        FColor* Data = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
        *Data = FColor(0u, DefaultDetailSizeEncoded, DefaultDetailSizeEncoded, 0u);
        Mip.BulkData.Unlock();
        Texture->UpdateResource();
        return Texture;
    }

    void AddUniqueTextureGPUBytes(
        UTexture* Texture,
        TSet<const UTexture*>& SeenTextures,
        uint64& OutBytes)
    {
        if (Texture == nullptr || SeenTextures.Contains(Texture))
        {
            return;
        }

        SeenTextures.Add(Texture);
        OutBytes += Texture->CalcTextureMemorySizeEnum(TMC_ResidentMips);
    }

}


void UDWCGPUResourceSubsystem::FTextureArrayRegistry::SetNeutral(
    UTexture2D* Texture,
    bool& bOutChanged)
{
    if (Texture == nullptr || Texture->GetSizeX() <= 0 || Texture->GetSizeY() <= 0)
    {
        return;
    }

    const int32 TextureFormat = static_cast<int32>(Texture->GetPixelFormat());
    if (SizeX == 0)
    {
        SizeX = Texture->GetSizeX();
        SizeY = Texture->GetSizeY();
        PixelFormat = TextureFormat;
    }
    else if (Texture->GetSizeX() != SizeX ||
             Texture->GetSizeY() != SizeY ||
             TextureFormat != PixelFormat)
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DWC neutral texture '%s' does not match the existing Texture2DArray registry format."),
            *Texture->GetPathName());
        return;
    }

    if (SourceTextures.IsEmpty())
    {
        SourceTextures.Add(Texture);
        SliceByPath.Add(Texture->GetPathName(), 0);
        DirtySlices.Add(0);
        bOutChanged = true;
    }
    else if (SourceTextures[0] != Texture)
    {
        if (SourceTextures[0] != nullptr)
        {
            SliceByPath.Remove(SourceTextures[0]->GetPathName());
        }
        SourceTextures[0] = Texture;
        SliceByPath.Add(Texture->GetPathName(), 0);
        DirtySlices.Add(0);
        bOutChanged = true;
    }
}

void UDWCGPUResourceSubsystem::FTextureArrayRegistry::ReserveNeutralSlice(bool& bOutChanged)
{
    if (!SourceTextures.IsEmpty())
    {
        return;
    }

    SourceTextures.Add(nullptr);
    bOutChanged = true;
}

int32 UDWCGPUResourceSubsystem::FTextureArrayRegistry::FindOrAdd(
    UTexture2D* Texture,
    bool& bOutChanged)
{
    if (Texture == nullptr || Texture->GetSizeX() <= 0 || Texture->GetSizeY() <= 0)
    {
        return 0;
    }

    const FString Path = Texture->GetPathName();
    if (const int32* Existing = SliceByPath.Find(Path))
    {
        if (SourceTextures.IsValidIndex(*Existing))
        {
            SourceTextures[*Existing] = Texture;
            DirtySlices.Add(*Existing);
            bOutChanged = true;
        }
        return *Existing;
    }

    const int32 TextureFormat = static_cast<int32>(Texture->GetPixelFormat());
    if (SizeX == 0)
    {
        SizeX = Texture->GetSizeX();
        SizeY = Texture->GetSizeY();
        PixelFormat = TextureFormat;
    }
    else if (Texture->GetSizeX() != SizeX ||
             Texture->GetSizeY() != SizeY ||
             TextureFormat != PixelFormat)
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DWC prepared render texture '%s' is %dx%d format %d, but its Texture2DArray requires %dx%d format %d. The profile uses neutral slice 0; rebake Render Profile Lookup Texture to regenerate the shared 512 texture."),
            *Path,
            Texture->GetSizeX(),
            Texture->GetSizeY(),
            TextureFormat,
            SizeX,
            SizeY,
            PixelFormat);
        return 0;
    }

    if (SourceTextures.IsEmpty())
    {
        UE_LOG(LogDWC, Warning, TEXT("DWC texture-array registry has no neutral slice; texture '%s' uses slice 0."), *Path);
        return 0;
    }

    const int32 NewSlice = SourceTextures.Add(Texture);
    SliceByPath.Add(Path, NewSlice);
    DirtySlices.Add(NewSlice);
    bOutChanged = true;
    return NewSlice;
}

void UDWCGPUResourceSubsystem::FTextureArrayRegistry::Reset()
{
    SourceTextures.Reset();
    SliceByPath.Reset();
    DirtySlices.Reset();
    SizeX = 0;
    SizeY = 0;
    PixelFormat = INDEX_NONE;
    AllocatedCapacity = 0;
}

bool UDWCGPUResourceSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game ||
           WorldType == EWorldType::PIE ||
           WorldType == EWorldType::GamePreview ||
           WorldType == EWorldType::EditorPreview;
}

void UDWCGPUResourceSubsystem::Deinitialize()
{
    AssetResources.Reset();
    StaticSlotResources.Reset();
    RuntimeProfiles.Reset();
    RuntimeProfileIndexByKey.Reset();
    DropletMaskRegistry.Reset();
    DropletNormalRegistry.Reset();
    DirtyRuntimeProfileIndices.Reset();
    RegisteredMaterialInstances.Reset();
    GPUMaterialInstances.Reset();
    NeutralWetPartDataTexture = nullptr;
    NeutralProfileRemapLUT = nullptr;
    GlobalRenderProfileLUT = nullptr;
    DropletMaskArray = nullptr;
    DropletNormalArray = nullptr;
    bTextureArraysDirty = false;
    RegistryRevision = 0;
    Super::Deinitialize();
}

FDWCGPUResourceSubsystemStats UDWCGPUResourceSubsystem::GetStats() const
{
    constexpr uint64 Uint4GPUBytes = 16ull;
    const auto GetRegistryCPUBytes = [](const FTextureArrayRegistry& Registry)
    {
        uint64 Bytes = Registry.SourceTextures.GetAllocatedSize() +
                       Registry.SliceByPath.GetAllocatedSize();
        for (const TPair<FString, int32>& Pair : Registry.SliceByPath)
        {
            Bytes += Pair.Key.GetAllocatedSize();
        }
        return Bytes;
    };

    FDWCGPUResourceSubsystemStats Stats;
    Stats.AssetResourceCount = static_cast<uint32>(AssetResources.Num());
    Stats.StaticSlotResourceCount = static_cast<uint32>(StaticSlotResources.Num());
    Stats.RuntimeProfileCount = static_cast<uint32>(RuntimeProfiles.Num());

    Stats.CPUBytes += AssetResources.GetAllocatedSize();
    for (const TPair<TObjectPtr<UWetClothingAsset>, FDWCAssetRenderProfileResources>& Pair : AssetResources)
    {
        Stats.CPUBytes += Pair.Value.WetPartDataTexturesByMaterialSlot.GetAllocatedSize();
    }

    Stats.CPUBytes += StaticSlotResources.GetAllocatedSize();
    for (const TPair<FDWCGPUStaticResourceKey, TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe>>& Pair : StaticSlotResources)
    {
        Stats.CPUBytes += Pair.Key.BuildSignature.GetAllocatedSize();
        const FDWCGPUStaticSlotResources* Resources = Pair.Value.Get();
        if (Resources == nullptr)
        {
            continue;
        }

        Stats.CPUBytes += sizeof(*Resources) +
                          Resources->Key.BuildSignature.GetAllocatedSize() +
                          Resources->Sections.GetAllocatedSize();

        if (Resources->TriangleProfileIndices.IsValid())
        {
            Stats.StaticBufferGPUBytes += static_cast<uint64>(Resources->TriangleCount) * sizeof(uint32);
        }
        if (Resources->TriangleDataToSurfaceWaterNormalUV.IsValid())
        {
            Stats.StaticBufferGPUBytes += static_cast<uint64>(Resources->TriangleCount) * sizeof(FVector4f);
        }
        if (Resources->TexelLookup.IsValid())
        {
            Stats.StaticBufferGPUBytes += static_cast<uint64>(Resources->TexelCount) * Uint4GPUBytes;
        }
        if (Resources->SurfaceTexelLookup.IsValid())
        {
            Stats.StaticBufferGPUBytes += static_cast<uint64>(Resources->SurfaceTexelCount) * Uint4GPUBytes;
        }
        if (Resources->SeamDestinations.IsValid())
        {
            Stats.StaticBufferGPUBytes += static_cast<uint64>(Resources->SeamDestinationCount) * Uint4GPUBytes;
        }
        if (Resources->SeamIncoming.IsValid())
        {
            Stats.StaticBufferGPUBytes += static_cast<uint64>(Resources->SeamIncomingCount) * sizeof(FVector4f);
        }

        for (const FDWCGPUStaticSectionResources& Section : Resources->Sections)
        {
            Stats.CPUBytes += sizeof(Section);
            if (Section.TriangleIndices.IsValid())
            {
                Stats.StaticBufferGPUBytes += static_cast<uint64>(Section.TriangleCount) * Uint4GPUBytes;
            }
            if (Section.TriangleUV01.IsValid())
            {
                Stats.StaticBufferGPUBytes += static_cast<uint64>(Section.TriangleCount) * sizeof(FVector4f);
            }
            if (Section.TriangleUV2RestArea.IsValid())
            {
                Stats.StaticBufferGPUBytes += static_cast<uint64>(Section.TriangleCount) * sizeof(FVector4f);
            }
        }
    }

    Stats.CPUBytes += RuntimeProfiles.GetAllocatedSize() +
                      RuntimeProfileIndexByKey.GetAllocatedSize() +
                      DirtyRuntimeProfileIndices.GetAllocatedSize() +
                      RegisteredMaterialInstances.GetAllocatedSize() +
                      GPUMaterialInstances.GetAllocatedSize() +
                      GetRegistryCPUBytes(DropletMaskRegistry) +
                      GetRegistryCPUBytes(DropletNormalRegistry);
    for (const FRuntimeProfileRecord& Profile : RuntimeProfiles)
    {
        Stats.CPUBytes += Profile.StableKey.GetAllocatedSize() +
                          Profile.PackedTexels.GetAllocatedSize();
    }
    for (const TPair<FString, int32>& Pair : RuntimeProfileIndexByKey)
    {
        Stats.CPUBytes += Pair.Key.GetAllocatedSize();
    }

    TSet<const UTexture*> SeenTextures;
    AddUniqueTextureGPUBytes(GlobalRenderProfileLUT, SeenTextures, Stats.RenderProfileLUTGPUBytes);
    AddUniqueTextureGPUBytes(NeutralWetPartDataTexture, SeenTextures, Stats.WetPartDataRemapGPUBytes);
    AddUniqueTextureGPUBytes(NeutralProfileRemapLUT, SeenTextures, Stats.WetPartDataRemapGPUBytes);
    AddUniqueTextureGPUBytes(DropletMaskArray, SeenTextures, Stats.SurfaceNormalArrayGPUBytes);
    AddUniqueTextureGPUBytes(DropletNormalArray, SeenTextures, Stats.SurfaceNormalArrayGPUBytes);
    Stats.TextureArrayCount += DropletMaskArray != nullptr ? 1u : 0u;
    Stats.TextureArrayCount += DropletNormalArray != nullptr ? 1u : 0u;

    for (const TPair<TObjectPtr<UWetClothingAsset>, FDWCAssetRenderProfileResources>& Pair : AssetResources)
    {
        for (const TPair<int32, TObjectPtr<UTexture2D>>& TexturePair : Pair.Value.WetPartDataTexturesByMaterialSlot)
        {
            AddUniqueTextureGPUBytes(TexturePair.Value, SeenTextures, Stats.WetPartDataRemapGPUBytes);
        }
        AddUniqueTextureGPUBytes(Pair.Value.ProfileRemapLUT, SeenTextures, Stats.WetPartDataRemapGPUBytes);
    }

    return Stats;
}

void UDWCGPUResourceSubsystem::EnsureNeutralResources()
{
    if (RuntimeProfiles.IsEmpty())
    {
        FRuntimeProfileRecord& Neutral = RuntimeProfiles.AddDefaulted_GetRef();
        Neutral.StableKey = TEXT("DWC.Neutral");
        Neutral.PackedTexels.SetNumZeroed(TexelsPerProfile);
        RuntimeProfileIndexByKey.Add(Neutral.StableKey, 0);
    }

    if (NeutralWetPartDataTexture == nullptr)
    {
        NeutralWetPartDataTexture = CreateNeutralWetPartDataTexture(this);
    }

    if (NeutralProfileRemapLUT == nullptr)
    {
        TArray<FLinearColor> Pixels;
        Pixels.Init(FLinearColor(0.5f * GlobalTexelSize, 0.0f, 0.0f, 0.0f), LocalRemapWidth);
        NeutralProfileRemapLUT = CreateFloatLUTTexture(
            this,
            TEXT("DWC_NeutralProfileRemapLUT"),
            LocalRemapWidth,
            Pixels);
    }

    if (GlobalRenderProfileLUT == nullptr)
    {
        RebuildGlobalRenderProfileLUT();
    }

}

void UDWCGPUResourceSubsystem::EnsureMaskRegistryNeutral(
    FTextureArrayRegistry& Registry,
    UTexture2D* ReferenceTexture,
    bool& bOutChanged)
{
    if (ReferenceTexture == nullptr)
    {
        return;
    }

    Registry.ReserveNeutralSlice(bOutChanged);
}

int32 UDWCGPUResourceSubsystem::FindOrAddRuntimeProfile(
    const FWetClothingLocalRenderProfile& LocalProfile,
    const EDWCRenderResourceUsage Usage)
{
    const FString Key = ResolveProfileKey(LocalProfile);
    int32 RuntimeIndex = INDEX_NONE;
    FRuntimeProfileRecord* Record = nullptr;

    if (const int32* ExistingIndex = RuntimeProfileIndexByKey.Find(Key))
    {
        RuntimeIndex = *ExistingIndex;
        Record = RuntimeProfiles.IsValidIndex(RuntimeIndex) ? &RuntimeProfiles[RuntimeIndex] : nullptr;
        if (Record == nullptr)
        {
            return 0;
        }
        if (Usage == EDWCRenderResourceUsage::AbsorbedOnly)
        {
            return RuntimeIndex;
        }
    }
    else
    {
        if (RuntimeProfiles.Num() >= MaxRuntimeProfileCount)
        {
            UE_LOG(LogDWC, Error, TEXT("DWC Render Profile Registry exceeded %d profiles."), MaxRuntimeProfileCount);
            return 0;
        }

        Record = &RuntimeProfiles.AddDefaulted_GetRef();
        Record->StableKey = Key;
        Record->PackedTexels.SetNumZeroed(TexelsPerProfile);
        RuntimeIndex = RuntimeProfiles.Num() - 1;
        RuntimeProfileIndexByKey.Add(Key, RuntimeIndex);
        Record->PackedTexels[0] = FLinearColor(
            LocalProfile.Parameters.GetAbsorbedDarkeningStrength(),
            LocalProfile.Parameters.GetAbsorbedGlossinessStrength(),
            0.0f,
            0.0f);
        Record->PackedTexels[1] = FLinearColor::Black;
        DirtyRuntimeProfileIndices.Add(RuntimeIndex);
    }

    if (Usage == EDWCRenderResourceUsage::FullGPU && !Record->bSurfaceResourcesResolved)
    {
        const FSurfaceWaterProfileParameters& Surface = LocalProfile.Parameters.SurfaceWater;
        bool bTextureArraysChanged = false;
        const bool bDropletRequested = Surface.bEnabled;
        const bool bFlowRequested = bDropletRequested && Surface.bUseSecondaryDroplets;
        UTexture2D* ResolvedDropletNormal = bDropletRequested
            ? ResolveDirectSurfaceTexture(
                LocalProfile,
                LocalProfile.NormalizedDropletNormal,
                Surface.DropletNormalTexture,
                LocalProfile.GetSourceDropletNormalPath(),
                TEXT("DropletNormal"),
                true)
            : nullptr;
        UTexture2D* ResolvedDropletMask = bDropletRequested
            ? ResolveDirectSurfaceTexture(
                LocalProfile,
                LocalProfile.NormalizedDropletMask,
                Surface.DropletMaskTexture,
                LocalProfile.GetSourceDropletMaskPath(),
                TEXT("DropletMask"),
                false)
            : nullptr;
        // Droplet2 is independent. A missing authored Droplet2 texture must use
        // neutral slice 0 rather than silently borrowing Droplet1.
        const bool bHasAuthoredFlowNormal =
            Surface.DropletFlowNormalTexture != nullptr ||
            LocalProfile.GetSourceDropletFlowNormalPath().IsValid();
        const bool bHasAuthoredFlowMask =
            Surface.DropletFlowMaskTexture != nullptr ||
            LocalProfile.GetSourceDropletFlowMaskPath().IsValid();
        UTexture2D* ResolvedDropletFlowNormal = bFlowRequested && bHasAuthoredFlowNormal
            ? ResolveDirectSurfaceTexture(
                LocalProfile,
                LocalProfile.NormalizedDropletFlowNormal,
                Surface.DropletFlowNormalTexture,
                LocalProfile.GetSourceDropletFlowNormalPath(),
                TEXT("DropletFlowNormal"),
                true)
            : nullptr;
        UTexture2D* ResolvedDropletFlowMask = bFlowRequested && bHasAuthoredFlowMask
            ? ResolveDirectSurfaceTexture(
                LocalProfile,
                LocalProfile.NormalizedDropletFlowMask,
                Surface.DropletFlowMaskTexture,
                LocalProfile.GetSourceDropletFlowMaskPath(),
                TEXT("DropletFlowMask"),
                false)
            : nullptr;
        EnsureMaskRegistryNeutral(
            DropletMaskRegistry,
            ResolvedDropletMask,
            bTextureArraysChanged);
        const int32 DropletMaskSlice = bDropletRequested
            ? DropletMaskRegistry.FindOrAdd(ResolvedDropletMask, bTextureArraysChanged)
            : 0;
        const int32 DropletNormalSlice = bDropletRequested
            ? DropletNormalRegistry.FindOrAdd(ResolvedDropletNormal, bTextureArraysChanged)
            : 0;
        const int32 DropletFlowMaskSlice = bFlowRequested
            ? DropletMaskRegistry.FindOrAdd(ResolvedDropletFlowMask, bTextureArraysChanged)
            : 0;
        const int32 DropletFlowNormalSlice = bFlowRequested
            ? DropletNormalRegistry.FindOrAdd(ResolvedDropletFlowNormal, bTextureArraysChanged)
            : 0;

        Record->PackedTexels[0] = FLinearColor(
            LocalProfile.Parameters.GetAbsorbedDarkeningStrength(),
            LocalProfile.Parameters.GetAbsorbedGlossinessStrength(),
            static_cast<float>(DropletNormalSlice),
            0.0f);
        Record->PackedTexels[1] = FLinearColor(
            FMath::Clamp(Surface.SurfaceWaterNormalStrength, 0.0f, 3.0f),
            FMath::Clamp(Surface.SurfaceWaterRoughnessBlend, 0.0f, 1.0f),
            0.0f,
            FMath::Clamp(Surface.SurfaceWaterSpecular, 0.0f, 1.0f));
        Record->PackedTexels[2] = FLinearColor(
            static_cast<float>(DropletMaskSlice),
            0.0f,
            FMath::Clamp(Surface.SurfaceWaterTargetRoughness, 0.0f, 1.0f),
            FMath::Clamp(Surface.SurfaceWaterTotalStrength, 0.0f, 1.0f));
        Record->PackedTexels[3] = FLinearColor::Black;
        Record->PackedTexels[4] = FLinearColor(
            static_cast<float>(DropletFlowNormalSlice),
            static_cast<float>(DropletFlowMaskSlice),
            0.0f,
            0.0f);
        Record->PackedTexels[5] = Surface.SupportsSecondaryDroplets()
            ? FLinearColor(
                FMath::Clamp(Surface.DropletFlowTotalStrength, 0.0f, 1.0f),
                FMath::Clamp(Surface.DropletFlowTargetRoughness, 0.0f, 1.0f),
                FMath::Clamp(Surface.DropletFlowRoughnessBlend, 0.0f, 1.0f),
                FMath::Clamp(Surface.DropletFlowSpecular, 0.0f, 1.0f))
            : FLinearColor::Black;
        Record->PackedTexels[6] = FLinearColor(
            FMath::Clamp(Surface.SurfaceWaterColorBlend, 0.0f, 1.0f),
            Surface.SupportsSecondaryDroplets()
                ? FMath::Clamp(Surface.DropletFlowColorBlend, 0.0f, 1.0f)
                : 0.0f,
            Surface.SupportsSecondaryDroplets()
                ? FMath::Clamp(Surface.DropletFlowNormalStrength, 0.0f, 3.0f)
                : 0.0f,
            0.0f);
        Record->bSurfaceResourcesResolved = true;
        DirtyRuntimeProfileIndices.Add(RuntimeIndex);
        bTextureArraysDirty |= bTextureArraysChanged;
    }

    return RuntimeIndex;
}


void UDWCGPUResourceSubsystem::RebuildGlobalRenderProfileLUT()
{
    TArray<FLinearColor> Pixels;
    Pixels.Init(FLinearColor::Black, GlobalLUTWidth);

    for (int32 ProfileIndex = 0; ProfileIndex < RuntimeProfiles.Num(); ++ProfileIndex)
    {
        const FRuntimeProfileRecord& Record = RuntimeProfiles[ProfileIndex];
        for (int32 TexelIndex = 0; TexelIndex < TexelsPerProfile; ++TexelIndex)
        {
            if (Record.PackedTexels.IsValidIndex(TexelIndex))
            {
                Pixels[ProfileIndex * TexelsPerProfile + TexelIndex] = Record.PackedTexels[TexelIndex];
            }
        }
    }

    GlobalRenderProfileLUT = CreateFloatLUTTexture(
        this,
        TEXT("DWC_GlobalRenderProfileLUT"),
        GlobalLUTWidth,
        Pixels);
    RebindGlobalRenderProfileLUT();
}

void UDWCGPUResourceSubsystem::RebindGlobalRenderProfileLUT()
{
    if (GlobalRenderProfileLUT == nullptr)
    {
        return;
    }

    for (auto It = RegisteredMaterialInstances.CreateIterator(); It; ++It)
    {
        UMaterialInstanceDynamic* MID = It->Get();
        if (MID == nullptr)
        {
            It.RemoveCurrent();
            continue;
        }
        MID->SetTextureParameterValue(
            DWCWetMaterialParameters::GlobalRenderProfileLUT(),
            GlobalRenderProfileLUT);
    }
}

void UDWCGPUResourceSubsystem::UpdateGlobalRenderProfileLUT(const int32 RuntimeProfileIndex)
{
    if (GlobalRenderProfileLUT == nullptr || !RuntimeProfiles.IsValidIndex(RuntimeProfileIndex))
    {
        RebuildGlobalRenderProfileLUT();
        return;
    }

    FTexture2DResource* DestinationResource =
        static_cast<FTexture2DResource*>(GlobalRenderProfileLUT->GetResource());
    const FTextureRHIRef DestinationTexture =
        DestinationResource != nullptr
            ? DestinationResource->TextureRHI
            : FTextureRHIRef();
    if (!DestinationTexture.IsValid())
    {
        RebuildGlobalRenderProfileLUT();
        return;
    }

    TArray<FLinearColor> Texels;
    Texels.Init(FLinearColor::Black, TexelsPerProfile);
    const TArray<FLinearColor>& PackedTexels = RuntimeProfiles[RuntimeProfileIndex].PackedTexels;
    for (int32 TexelIndex = 0; TexelIndex < TexelsPerProfile && PackedTexels.IsValidIndex(TexelIndex); ++TexelIndex)
    {
        Texels[TexelIndex] = PackedTexels[TexelIndex];
    }
    const int32 DestinationX = RuntimeProfileIndex * TexelsPerProfile;
    ENQUEUE_RENDER_COMMAND(DWCUpdateRenderProfileLUT)(
        [DestinationTexture, DestinationX, Texels = MoveTemp(Texels)](FRHICommandListImmediate& RHICmdList)
        {
            const FUpdateTextureRegion2D Region(
                DestinationX,
                0,
                0,
                0,
                UDWCGPUResourceSubsystem::TexelsPerProfile,
                1);
            RHICmdList.UpdateTexture2D(
                DestinationTexture.GetReference(),
                0,
                Region,
                UDWCGPUResourceSubsystem::TexelsPerProfile * sizeof(FLinearColor),
                reinterpret_cast<const uint8*>(Texels.GetData()));
        });
}

void UDWCGPUResourceSubsystem::FlushDirtyRuntimeProfiles()
{
    if (DirtyRuntimeProfileIndices.IsEmpty())
    {
        return;
    }

    if (GlobalRenderProfileLUT == nullptr)
    {
        RebuildGlobalRenderProfileLUT();
    }
    else
    {
        for (const int32 RuntimeProfileIndex : DirtyRuntimeProfileIndices)
        {
            UpdateGlobalRenderProfileLUT(RuntimeProfileIndex);
        }
    }
    DirtyRuntimeProfileIndices.Reset();
}

UTexture2DArray* UDWCGPUResourceSubsystem::BuildTextureArray(
    const TCHAR* DebugName,
    const TArray<TObjectPtr<UTexture2D>>& SourceTextures,
    const int32 SliceCapacity,
    const bool bNormalArray)
{
    UTexture2D* FirstValid = nullptr;
    for (UTexture2D* Source : SourceTextures)
    {
        if (Source != nullptr && Source->GetResource() != nullptr)
        {
            FirstValid = Source;
            break;
        }
    }

    const int32 SizeX = FirstValid != nullptr ? FirstValid->GetSizeX() : 1;
    const int32 SizeY = FirstValid != nullptr ? FirstValid->GetSizeY() : 1;
    const EPixelFormat Format = FirstValid != nullptr ? FirstValid->GetPixelFormat() : PF_B8G8R8A8;
    const int32 SafeCapacity = FMath::Max(SliceCapacity, 1);

    UTexture2DArray* Array = UTexture2DArray::CreateTransient(SizeX, SizeY, SafeCapacity, Format, FName(DebugName));
    if (Array == nullptr)
    {
        return nullptr;
    }

    Array->Rename(nullptr, this, REN_DontCreateRedirectors | REN_NonTransactional);
    Array->SRGB = false;
    Array->Filter = TF_Bilinear;
    Array->AddressX = TA_Wrap;
    Array->AddressY = TA_Wrap;
#if WITH_EDITORONLY_DATA
    Array->MipGenSettings = TMGS_NoMipmaps;
#endif
    Array->CompressionSettings = bNormalArray ? TC_VectorDisplacementmap : TC_Masks;
    Array->NeverStream = true;
    Array->UpdateResource();

    TSet<int32> InitialSlices;
    for (int32 SliceIndex = 0; SliceIndex < SourceTextures.Num(); ++SliceIndex)
    {
        InitialSlices.Add(SliceIndex);
    }
    FTextureArrayRegistry TemporaryRegistry;
    TemporaryRegistry.SourceTextures = SourceTextures;
    TemporaryRegistry.SizeX = SizeX;
    TemporaryRegistry.SizeY = SizeY;
    TemporaryRegistry.PixelFormat = static_cast<int32>(Format);
    UploadTextureArraySlices(Array, TemporaryRegistry, InitialSlices);
    return Array;
}

void UDWCGPUResourceSubsystem::UploadTextureArraySlices(
    UTexture2DArray* Array,
    const FTextureArrayRegistry& Registry,
    const TSet<int32>& SliceIndices)
{
    if (Array == nullptr || Array->GetResource() == nullptr || SliceIndices.IsEmpty())
    {
        return;
    }

    struct FSliceUpload
    {
        TArray<uint8> Bytes;
        FString SourceName;
        int32 DestinationSliceIndex = 0;
        uint32 SourceRowPitch = 0;
        uint32 SourceRowCount = 0;
    };

    TArray<FSliceUpload> SliceUploads;
    SliceUploads.Reserve(SliceIndices.Num());
    for (const int32 SliceIndex : SliceIndices)
    {
        if (!Registry.SourceTextures.IsValidIndex(SliceIndex))
        {
            continue;
        }
        UTexture2D* Source = Registry.SourceTextures[SliceIndex];
        if (Source == nullptr || Source->GetResource() == nullptr ||
            Source->GetSizeX() != Registry.SizeX || Source->GetSizeY() != Registry.SizeY ||
            static_cast<int32>(Source->GetPixelFormat()) != Registry.PixelFormat)
        {
            continue;
        }

        FTexturePlatformData* PlatformData = Source->GetPlatformData();
        if (PlatformData == nullptr || PlatformData->Mips.IsEmpty())
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC could not upload texture-array slice %d from '%s' because it has no CPU mip data."),
                SliceIndex,
                *Source->GetPathName());
            continue;
        }

        const EPixelFormat SourceFormat = Source->GetPixelFormat();
        const FPixelFormatInfo& FormatInfo = GPixelFormats[SourceFormat];
        const uint32 BlockSizeX = FMath::Max<uint32>(FormatInfo.BlockSizeX, 1u);
        const uint32 BlockSizeY = FMath::Max<uint32>(FormatInfo.BlockSizeY, 1u);
        const uint32 BlockBytes = FMath::Max<uint32>(FormatInfo.BlockBytes, 1u);
        const uint32 SourceBlockCountX = FMath::DivideAndRoundUp<uint32>(
            static_cast<uint32>(Source->GetSizeX()),
            BlockSizeX);
        const uint32 SourceBlockCountY = FMath::DivideAndRoundUp<uint32>(
            static_cast<uint32>(Source->GetSizeY()),
            BlockSizeY);
        const uint32 SourceRowPitch = SourceBlockCountX * BlockBytes;
        const uint64 ExpectedByteCount = static_cast<uint64>(SourceRowPitch) * SourceBlockCountY;

        FTexture2DMipMap& Mip = PlatformData->Mips[0];
        const int64 BulkByteCount = Mip.BulkData.GetBulkDataSize();
        if (BulkByteCount < static_cast<int64>(ExpectedByteCount))
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC could not upload texture-array slice %d from '%s' because mip bulk is %lld bytes but %llu bytes are required."),
                SliceIndex,
                *Source->GetPathName(),
                BulkByteCount,
                ExpectedByteCount);
            continue;
        }

        const void* MipBytes = Mip.BulkData.LockReadOnly();
        if (MipBytes == nullptr)
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC could not lock CPU mip data while uploading texture-array slice %d from '%s'."),
                SliceIndex,
                *Source->GetPathName());
            continue;
        }

        FSliceUpload& Upload = SliceUploads.AddDefaulted_GetRef();
        Upload.Bytes.SetNumUninitialized(static_cast<int32>(ExpectedByteCount));
        FMemory::Memcpy(Upload.Bytes.GetData(), MipBytes, static_cast<SIZE_T>(ExpectedByteCount));
        Upload.SourceName = Source->GetPathName();
        Upload.DestinationSliceIndex = SliceIndex;
        Upload.SourceRowPitch = SourceRowPitch;
        Upload.SourceRowCount = SourceBlockCountY;
        Mip.BulkData.Unlock();
    }

    FTextureResource* DestinationResource = Array->GetResource();
    ENQUEUE_RENDER_COMMAND(DWCUploadTextureArraySlices)(
        [DestinationResource, SliceUploads = MoveTemp(SliceUploads)](FRHICommandListImmediate& RHICmdList)
        {
            if (DestinationResource == nullptr || DestinationResource->TextureRHI == nullptr)
            {
                return;
            }
            for (const FSliceUpload& Upload : SliceUploads)
            {
                if (Upload.Bytes.IsEmpty() || Upload.SourceRowPitch == 0 || Upload.SourceRowCount == 0)
                {
                    continue;
                }

                uint32 DestinationStride = 0;
                void* DestinationBytes = RHICmdList.LockTexture2DArray(
                    DestinationResource->TextureRHI,
                    Upload.DestinationSliceIndex,
                    0,
                    RLM_WriteOnly,
                    DestinationStride,
                    false);
                if (DestinationBytes == nullptr)
                {
                    continue;
                }

                if (DestinationStride >= Upload.SourceRowPitch)
                {
                    const uint8* SourceRow = Upload.Bytes.GetData();
                    uint8* DestinationRow = static_cast<uint8*>(DestinationBytes);
                    for (uint32 RowIndex = 0; RowIndex < Upload.SourceRowCount; ++RowIndex)
                    {
                        FMemory::Memcpy(DestinationRow, SourceRow, Upload.SourceRowPitch);
                        SourceRow += Upload.SourceRowPitch;
                        DestinationRow += DestinationStride;
                    }
                }

                RHICmdList.UnlockTexture2DArray(
                    DestinationResource->TextureRHI,
                    Upload.DestinationSliceIndex,
                    0,
                    false);
            }
        });
}

bool UDWCGPUResourceSubsystem::EnsureTextureArray(
    const TCHAR* DebugName,
    FTextureArrayRegistry& Registry,
    TObjectPtr<UTexture2DArray>& Array,
    const bool bNormalArray)
{
    const int32 RequiredSlices = FMath::Max(Registry.SourceTextures.Num(), 1);
    const bool bArrayFormatMismatch =
        Array != nullptr &&
        Registry.SizeX > 0 &&
        Registry.SizeY > 0 &&
        Registry.PixelFormat != INDEX_NONE &&
        (Array->GetSizeX() != Registry.SizeX ||
         Array->GetSizeY() != Registry.SizeY ||
         static_cast<int32>(Array->GetPixelFormat()) != Registry.PixelFormat);
    if (Array == nullptr || Registry.AllocatedCapacity < RequiredSlices || bArrayFormatMismatch)
    {
        const int32 NewCapacity = ResolveTextureArrayCapacity(RequiredSlices);
        Array = BuildTextureArray(DebugName, Registry.SourceTextures, NewCapacity, bNormalArray);
        Registry.AllocatedCapacity = Array != nullptr ? NewCapacity : 0;
        Registry.DirtySlices.Reset();
        return Array != nullptr;
    }

    if (!Registry.DirtySlices.IsEmpty())
    {
        UploadTextureArraySlices(Array, Registry, Registry.DirtySlices);
        Registry.DirtySlices.Reset();
    }
    return false;
}

bool UDWCGPUResourceSubsystem::EnsureTextureArraysUpToDate()
{
    const bool bDropletMaskArrayReplaced = EnsureTextureArray(
        TEXT("DWC_DropletMaskArray"),
        DropletMaskRegistry,
        DropletMaskArray,
        false);
    const bool bDropletArrayReplaced = EnsureTextureArray(
        TEXT("DWC_DropletNormalArray"),
        DropletNormalRegistry,
        DropletNormalArray,
        true);
    return bDropletMaskArrayReplaced ||
           bDropletArrayReplaced;
}

void UDWCGPUResourceSubsystem::RebindGPUTextureArrays()
{
    for (auto It = GPUMaterialInstances.CreateIterator(); It; ++It)
    {
        UMaterialInstanceDynamic* MID = It->Get();
        if (MID == nullptr)
        {
            It.RemoveCurrent();
            continue;
        }
        BindGlobalResources(*MID, EDWCRenderResourceUsage::FullGPU);
    }
}


UTexture2D* UDWCGPUResourceSubsystem::BuildAssetRemapLUT(
    UWetClothingAsset* Asset,
    const TArray<int32>& LocalToRuntimeProfileIndices)
{
    TArray<FLinearColor> Pixels;
    Pixels.Init(FLinearColor((0.5f) * GlobalTexelSize, 0.0f, 0.0f, 0.0f), LocalRemapWidth);
    for (int32 LocalID = 1; LocalID < LocalToRuntimeProfileIndices.Num() && LocalID < LocalRemapWidth; ++LocalID)
    {
        const int32 RuntimeIndex = FMath::Clamp(LocalToRuntimeProfileIndices[LocalID], 0, MaxRuntimeProfileCount - 1);
        const float BaseU = (static_cast<float>(RuntimeIndex * TexelsPerProfile) + 0.5f) * GlobalTexelSize;
        Pixels[LocalID] = FLinearColor(BaseU, 0.0f, 0.0f, 0.0f);
    }

    return CreateFloatLUTTexture(
        this,
        FName(*FString::Printf(TEXT("DWC_%s_ProfileRemapLUT"), *GetNameSafe(Asset))),
        LocalRemapWidth,
        Pixels);
}

const FDWCAssetRenderProfileResources* UDWCGPUResourceSubsystem::AcquireAssetResources(
    UWetClothingAsset* Asset,
    const EDWCRenderResourceUsage Usage)
{
    EnsureNeutralResources();
    if (Asset == nullptr || !Asset->Derived.Inline.BakedWetPartData.IsValid())
    {
        return nullptr;
    }

    const TArray<FWetClothingLocalRenderProfile> ResolvedLocalProfiles =
        MakeResolvedLocalRenderProfiles(Asset);
    const FString ResolvedProfileSignature =
        MakeResolvedProfileResourceSignature(ResolvedLocalProfiles);

    FDWCAssetRenderProfileResources* Existing = AssetResources.Find(Asset);
    const bool bExistingProfileSignatureValid =
        Existing != nullptr &&
        Existing->ResolvedProfileSignature == ResolvedProfileSignature;
    const bool bExistingMappingValid =
        Existing != nullptr &&
        Existing->RegistryRevision == RegistryRevision &&
        Existing->SourceBakeGuid == Asset->Derived.Inline.BakedWetPartData.BakeGuid &&
        bExistingProfileSignatureValid &&
        Existing->IsValid();
    if (bExistingMappingValid &&
        (Usage == EDWCRenderResourceUsage::AbsorbedOnly || Existing->bSurfaceResourcesResolved))
    {
        return Existing;
    }
    if (Usage == EDWCRenderResourceUsage::FullGPU)
    {
        bool bNeutralRegistryChanged = false;
        UTexture2D* NeutralNormal = Asset->Derived.Inline.BakedWetPartData.NormalizedNeutralSurfaceNormal;
        DropletNormalRegistry.SetNeutral(NeutralNormal, bNeutralRegistryChanged);
        bTextureArraysDirty |= bNeutralRegistryChanged;
    }

    TArray<int32> LocalToRuntime;
    LocalToRuntime.Init(0, ResolvedLocalProfiles.Num() + 1);
    for (int32 LocalProfileIndex = 0; LocalProfileIndex < ResolvedLocalProfiles.Num(); ++LocalProfileIndex)
    {
        LocalToRuntime[LocalProfileIndex + 1] = FindOrAddRuntimeProfile(
            ResolvedLocalProfiles[LocalProfileIndex],
            Usage);
    }

    if (Usage == EDWCRenderResourceUsage::FullGPU &&
        (bTextureArraysDirty ||
         DropletMaskArray == nullptr ||
         DropletNormalArray == nullptr))
    {
        const bool bArrayResourceReplaced = EnsureTextureArraysUpToDate();
        bTextureArraysDirty = false;
        if (bArrayResourceReplaced)
        {
            RebindGPUTextureArrays();
        }
    }
    if (!DirtyRuntimeProfileIndices.IsEmpty())
    {
        RebuildGlobalRenderProfileLUT();
        DirtyRuntimeProfileIndices.Reset();
    }

    if (bExistingMappingValid)
    {
        Existing->bSurfaceResourcesResolved = Usage == EDWCRenderResourceUsage::FullGPU;
        return Existing;
    }

    FDWCAssetRenderProfileResources& Resources = AssetResources.FindOrAdd(Asset);
    Resources.WetPartDataTexturesByMaterialSlot.Reset();
    Resources.FallbackRenderProfilesByMaterialSlot.Reset();
    for (const FWetClothingBakedWetPartDataSlotTexture& SlotTexture :
         Asset->Derived.Inline.BakedWetPartData.SlotTextures)
    {
        if (SlotTexture.IsValid())
        {
            Resources.WetPartDataTexturesByMaterialSlot.Add(
                SlotTexture.MaterialSlotIndex,
                SlotTexture.WetPartDataTexture);
        }
    }
    for (const FWetClothingAuthoredMaterialSlot& Slot :
         Asset->Authored.PartData.EditableWetPartData.MaterialSlots)
    {
        if (Slot.MaterialSlotIndex == INDEX_NONE)
        {
            continue;
        }

        FWetClothingLocalRenderProfile FallbackProfile;
#if WITH_EDITOR
        constexpr bool bResolveFallbackSourceProfile = true;
#else
        constexpr bool bResolveFallbackSourceProfile = false;
#endif
        if (ResolveFallbackRenderProfile(
                Asset,
                Slot.MaterialSlotIndex,
                FallbackProfile,
                bResolveFallbackSourceProfile))
        {
            Resources.FallbackRenderProfilesByMaterialSlot.Add(
                Slot.MaterialSlotIndex,
                FallbackProfile);
        }
    }
    Resources.ProfileRemapLUT = BuildAssetRemapLUT(Asset, LocalToRuntime);
    Resources.SourceBakeGuid = Asset->Derived.Inline.BakedWetPartData.BakeGuid;
    Resources.ResolvedProfileSignature = ResolvedProfileSignature;
    Resources.RegistryRevision = RegistryRevision;
    Resources.bSurfaceResourcesResolved = Usage == EDWCRenderResourceUsage::FullGPU;
    return Resources.IsValid() ? &Resources : nullptr;
}


void UDWCGPUResourceSubsystem::InvalidateAssetResources(const UWetClothingAsset* Asset)
{
    if (Asset == nullptr)
    {
        return;
    }

    for (auto It = AssetResources.CreateIterator(); It; ++It)
    {
        if (It.Key().Get() == Asset)
        {
            It.RemoveCurrent();
        }
    }
}


TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe>
UDWCGPUResourceSubsystem::AcquireStaticSlotResources(
    const UWetClothingAsset* Asset,
    const int32 MaterialSlotIndex,
    const FString& BuildSignature,
    const FIntPoint LookupExtent,
    const uint32 TexelCount,
    const FIntPoint SurfaceLookupExtent,
    const uint32 SurfaceTexelCount,
    const uint32 TriangleCount,
    const int32 SectionCount)
{
    const bool bSurfaceMetadataValid =
        (SurfaceTexelCount == 0 && SurfaceLookupExtent == FIntPoint::ZeroValue) ||
        (SurfaceTexelCount > 0 && SurfaceLookupExtent.X > 0 && SurfaceLookupExtent.Y > 0);
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE || BuildSignature.IsEmpty() ||
        LookupExtent.X <= 0 || LookupExtent.Y <= 0 || TexelCount == 0 ||
        !bSurfaceMetadataValid || SectionCount < 0)
    {
        return nullptr;
    }

    const FDWCGPUStaticResourceKey Key(Asset, MaterialSlotIndex, BuildSignature);
    if (TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe>* Existing = StaticSlotResources.Find(Key))
    {
        if (Existing->IsValid() &&
            ((*Existing)->LookupExtent != LookupExtent ||
             (*Existing)->TexelCount != TexelCount ||
             (*Existing)->SurfaceLookupExtent != SurfaceLookupExtent ||
             (*Existing)->SurfaceTexelCount != SurfaceTexelCount ||
             (*Existing)->TriangleCount != TriangleCount ||
             (*Existing)->Sections.Num() != SectionCount))
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC shared GPU resource metadata mismatch for '%s' slot %d signature '%s'. CachedWet=%dx%d/%u CachedSurface=%dx%d/%u RequestedWet=%dx%d/%u RequestedSurface=%dx%d/%u. Replacing the stale cache entry."),
                *GetNameSafe(Asset),
                MaterialSlotIndex,
                *BuildSignature,
                (*Existing)->LookupExtent.X,
                (*Existing)->LookupExtent.Y,
                (*Existing)->TexelCount,
                (*Existing)->SurfaceLookupExtent.X,
                (*Existing)->SurfaceLookupExtent.Y,
                (*Existing)->SurfaceTexelCount,
                LookupExtent.X,
                LookupExtent.Y,
                TexelCount,
                SurfaceLookupExtent.X,
                SurfaceLookupExtent.Y,
                SurfaceTexelCount);
            StaticSlotResources.Remove(Key);
        }
        else
        {
            return *Existing;
        }
    }

    TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe> Resources =
        MakeShared<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe>();
    Resources->Key = Key;
    Resources->LookupExtent = LookupExtent;
    Resources->TexelCount = TexelCount;
    Resources->SurfaceLookupExtent = SurfaceLookupExtent;
    Resources->SurfaceTexelCount = SurfaceTexelCount;
    Resources->TriangleCount = TriangleCount;
    Resources->Sections.SetNum(SectionCount);
    StaticSlotResources.Add(Key, Resources);
    return Resources;
}

void UDWCGPUResourceSubsystem::InvalidateStaticResources(const UWetClothingAsset* Asset)
{
    if (Asset == nullptr)
    {
        return;
    }

    const FObjectKey AssetKey(const_cast<UWetClothingAsset*>(Asset));
    for (auto It = StaticSlotResources.CreateIterator(); It; ++It)
    {
        if (It.Key().AssetKey == AssetKey)
        {
            It.RemoveCurrent();
        }
    }
}

void UDWCGPUResourceSubsystem::BindGlobalResources(
    UMaterialInstanceDynamic& MID,
    const EDWCRenderResourceUsage Usage) const
{
    if (GlobalRenderProfileLUT != nullptr)
    {
        MID.SetTextureParameterValue(DWCWetMaterialParameters::GlobalRenderProfileLUT(), GlobalRenderProfileLUT);
    }
    MID.SetScalarParameterValue(DWCWetMaterialParameters::GlobalRenderProfileTexelSize(), GlobalTexelSize);
    if (Usage == EDWCRenderResourceUsage::FullGPU)
    {
        if (DropletMaskArray != nullptr)
        {
            MID.SetTextureParameterValue(DWCWetMaterialParameters::DropletMaskTextureArray(), DropletMaskArray);
        }
        if (DropletNormalArray != nullptr)
        {
            MID.SetTextureParameterValue(DWCWetMaterialParameters::DropletNormalTextureArray(), DropletNormalArray);
        }
    }
}

void UDWCGPUResourceSubsystem::ApplyFallbackRenderProfileParameters(
    UMaterialInstanceDynamic& MID,
    const UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex,
    const FWetClothingLocalRenderProfile* CachedProfile,
    const EDWCRenderResourceUsage Usage)
{
    FWetClothingLocalRenderProfile Profile;
    if (CachedProfile != nullptr)
    {
        Profile = *CachedProfile;
#if WITH_EDITOR
        FWetnessProfileParameters ResolvedParameters;
        if (ResolveSourceProfileParameters(Profile.GetSourceProfilePath(), true, ResolvedParameters))
        {
            ApplyResolvedSourceProfileParameters(Profile, ResolvedParameters);
            ApplyPreparedSourceProfileTextures(Profile);
        }
#endif
    }
    else
    {
#if WITH_EDITOR
        constexpr bool bResolveFallbackSourceProfile = true;
#else
        constexpr bool bResolveFallbackSourceProfile = false;
#endif
        ResolveFallbackRenderProfile(
            WetClothingAsset,
            MaterialSlotIndex,
            Profile,
            bResolveFallbackSourceProfile);
    }

    FFallbackRenderProfileSlices Slices;
    if (Usage == EDWCRenderResourceUsage::FullGPU)
    {
        bool bTextureArraysChanged = false;
        const FSurfaceWaterProfileParameters& Surface = Profile.Parameters.SurfaceWater;
        const bool bDropletRequested = Surface.bEnabled;
        const bool bFlowRequested = bDropletRequested && Surface.bUseSecondaryDroplets;
        UTexture2D* ResolvedDropletNormal = bDropletRequested
            ? ResolveDirectSurfaceTexture(
                Profile,
                Profile.NormalizedDropletNormal,
                Surface.DropletNormalTexture,
                Profile.GetSourceDropletNormalPath(),
                TEXT("DropletNormal"),
                true)
            : nullptr;
        UTexture2D* ResolvedDropletMask = bDropletRequested
            ? ResolveDirectSurfaceTexture(
                Profile,
                Profile.NormalizedDropletMask,
                Surface.DropletMaskTexture,
                Profile.GetSourceDropletMaskPath(),
                TEXT("DropletMask"),
                false)
            : nullptr;
        // Droplet2 is independent. A missing authored Droplet2 texture must use
        // neutral slice 0 rather than silently borrowing Droplet1.
        const bool bHasAuthoredFlowNormal =
            Surface.DropletFlowNormalTexture != nullptr ||
            Profile.GetSourceDropletFlowNormalPath().IsValid();
        const bool bHasAuthoredFlowMask =
            Surface.DropletFlowMaskTexture != nullptr ||
            Profile.GetSourceDropletFlowMaskPath().IsValid();
        UTexture2D* ResolvedDropletFlowNormal = bFlowRequested && bHasAuthoredFlowNormal
            ? ResolveDirectSurfaceTexture(
                Profile,
                Profile.NormalizedDropletFlowNormal,
                Surface.DropletFlowNormalTexture,
                Profile.GetSourceDropletFlowNormalPath(),
                TEXT("DropletFlowNormal"),
                true)
            : nullptr;
        UTexture2D* ResolvedDropletFlowMask = bFlowRequested && bHasAuthoredFlowMask
            ? ResolveDirectSurfaceTexture(
                Profile,
                Profile.NormalizedDropletFlowMask,
                Surface.DropletFlowMaskTexture,
                Profile.GetSourceDropletFlowMaskPath(),
                TEXT("DropletFlowMask"),
                false)
            : nullptr;
        const bool bEditorPreviewWorld =
            GetWorld() != nullptr && GetWorld()->WorldType == EWorldType::EditorPreview;
        const auto ResetMismatchedPreviewRegistry = [
            bEditorPreviewWorld,
            &bTextureArraysChanged](
                FTextureArrayRegistry& Registry,
                UTexture2D* Texture,
                TObjectPtr<UTexture2DArray>& Array)
        {
            if (!bEditorPreviewWorld || Texture == nullptr || Registry.SizeX == 0)
            {
                return;
            }

            const int32 TextureFormat = static_cast<int32>(Texture->GetPixelFormat());
            if (Registry.SizeX != Texture->GetSizeX() ||
                Registry.SizeY != Texture->GetSizeY() ||
                Registry.PixelFormat != TextureFormat)
            {
                Registry.Reset();
                Array = nullptr;
                bTextureArraysChanged = true;
            }
        };
        ResetMismatchedPreviewRegistry(
            DropletNormalRegistry,
            ResolvedDropletNormal,
            DropletNormalArray);
        ResetMismatchedPreviewRegistry(
            DropletMaskRegistry,
            ResolvedDropletMask,
            DropletMaskArray);
        ResetMismatchedPreviewRegistry(
            DropletNormalRegistry,
            ResolvedDropletFlowNormal,
            DropletNormalArray);
        ResetMismatchedPreviewRegistry(
            DropletMaskRegistry,
            ResolvedDropletFlowMask,
            DropletMaskArray);

        // Establish slice 0 after any preview-registry reset. Otherwise the reset
        // would remove the reserved/neutral slice and FindOrAdd would reject the
        // first prepared texture.
        if (WetClothingAsset != nullptr)
        {
            UTexture2D* NeutralNormal =
                WetClothingAsset->Derived.Inline.BakedWetPartData.NormalizedNeutralSurfaceNormal;
            DropletNormalRegistry.SetNeutral(NeutralNormal, bTextureArraysChanged);
        }
        else
        {
            DropletNormalRegistry.ReserveNeutralSlice(bTextureArraysChanged);
        }

        EnsureMaskRegistryNeutral(
            DropletMaskRegistry,
            ResolvedDropletMask,
            bTextureArraysChanged);
        EnsureMaskRegistryNeutral(
            DropletMaskRegistry,
            ResolvedDropletFlowMask,
            bTextureArraysChanged);
        Slices.DropletMask = bDropletRequested
            ? DropletMaskRegistry.FindOrAdd(ResolvedDropletMask, bTextureArraysChanged)
            : 0;
        Slices.DropletNormal = bDropletRequested
            ? DropletNormalRegistry.FindOrAdd(ResolvedDropletNormal, bTextureArraysChanged)
            : 0;
        Slices.DropletFlowMask = bFlowRequested
            ? DropletMaskRegistry.FindOrAdd(ResolvedDropletFlowMask, bTextureArraysChanged)
            : 0;
        Slices.DropletFlowNormal = bFlowRequested
            ? DropletNormalRegistry.FindOrAdd(ResolvedDropletFlowNormal, bTextureArraysChanged)
            : 0;

        // Editor preview worlds may retain a registry created by a previous transient
        // profile. If a valid prepared primary texture still resolves to neutral slice 0,
        // rebuild that preview-only registry once instead of silently rendering flat.
        if (bEditorPreviewWorld && bDropletRequested &&
            ResolvedDropletNormal != nullptr && Slices.DropletNormal == 0)
        {
            DropletNormalRegistry.Reset();
            DropletNormalArray = nullptr;
            DropletNormalRegistry.ReserveNeutralSlice(bTextureArraysChanged);
            Slices.DropletNormal = DropletNormalRegistry.FindOrAdd(
                ResolvedDropletNormal,
                bTextureArraysChanged);
            Slices.DropletFlowNormal = bFlowRequested
                ? DropletNormalRegistry.FindOrAdd(ResolvedDropletFlowNormal, bTextureArraysChanged)
                : 0;
        }
        if (bEditorPreviewWorld && bDropletRequested &&
            ResolvedDropletMask != nullptr && Slices.DropletMask == 0)
        {
            DropletMaskRegistry.Reset();
            DropletMaskArray = nullptr;
            DropletMaskRegistry.ReserveNeutralSlice(bTextureArraysChanged);
            Slices.DropletMask = DropletMaskRegistry.FindOrAdd(
                ResolvedDropletMask,
                bTextureArraysChanged);
            Slices.DropletFlowMask = bFlowRequested
                ? DropletMaskRegistry.FindOrAdd(ResolvedDropletFlowMask, bTextureArraysChanged)
                : 0;
        }

        const FString ProfileKey = ResolveProfileKey(Profile);
        if (Surface.bEnabled && Surface.SurfaceWaterNormalStrength <= UE_KINDA_SMALL_NUMBER && bDropletRequested)
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC Surface Water normal strength is %.6g for asset '%s' slot %d profile '%s'; droplet detail normal weight resolves to zero even if coverage is visible."),
                Surface.SurfaceWaterNormalStrength,
                *GetPathNameSafe(WetClothingAsset),
                MaterialSlotIndex,
                *ProfileKey);
        }

        if (bDropletRequested && Slices.DropletNormal == 0)
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC Droplet normal resolved to Texture2DArray slice 0 flat-normal fallback for asset '%s' slot %d profile '%s'. Source texture: %s."),
                *GetPathNameSafe(WetClothingAsset),
                MaterialSlotIndex,
                *ProfileKey,
                *DescribeSurfaceTexture(ResolvedDropletNormal));
        }
        if (bDropletRequested && Slices.DropletMask == 0 && ResolvedDropletMask != nullptr)
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC Droplet mask resolved to Texture2DArray slice 0 for asset '%s' slot %d profile '%s'. Surface Water falls back to unmasked coverage. Source texture: %s."),
                *GetPathNameSafe(WetClothingAsset),
                MaterialSlotIndex,
                *ProfileKey,
                *DescribeSurfaceTexture(ResolvedDropletMask));
        }

        if (bTextureArraysChanged)
        {
            bTextureArraysDirty = true;
            const bool bArrayResourceReplaced = EnsureTextureArraysUpToDate();
            bTextureArraysDirty = false;
            if (bArrayResourceReplaced)
            {
                RebindGPUTextureArrays();
            }
        }
    }

    for (int32 TexelIndex = 0; TexelIndex < UDWCGPUResourceSubsystem::TexelsPerProfile; ++TexelIndex)
    {
        const FLinearColor Texel = Usage == EDWCRenderResourceUsage::FullGPU
            ? MakeFallbackRenderProfileTexel(Profile, Slices, TexelIndex)
            : (TexelIndex == 0
                ? FLinearColor(
                    Profile.Parameters.GetAbsorbedDarkeningStrength(),
                    Profile.Parameters.GetAbsorbedGlossinessStrength(),
                    0.0f,
                    0.0f)
                : FLinearColor::Black);
        MID.SetVectorParameterValue(
            DWCWetMaterialParameters::FallbackRenderProfileTexel(TexelIndex),
            Texel);
    }
}

void UDWCGPUResourceSubsystem::ApplyResourcesToMaterials(
    UWetClothingAsset* Asset,
    const TArray<TObjectPtr<UMaterialInstanceDynamic>>& MaterialInstances,
    const EDWCRenderResourceUsage Usage)
{
    EnsureNeutralResources();
    for (UMaterialInstanceDynamic* MID : MaterialInstances)
    {
        if (MID != nullptr)
        {
            RegisteredMaterialInstances.Add(MID);
            if (Usage == EDWCRenderResourceUsage::FullGPU)
            {
                GPUMaterialInstances.Add(MID);
            }
        }
    }

    const FDWCAssetRenderProfileResources* Resources = AcquireAssetResources(Asset, Usage);
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialInstances.Num(); ++MaterialSlotIndex)
    {
        UMaterialInstanceDynamic* MID = MaterialInstances[MaterialSlotIndex];
        if (MID == nullptr)
        {
            continue;
        }

        const FWetClothingLocalRenderProfile* FallbackProfile =
            Resources != nullptr
                ? Resources->FallbackRenderProfilesByMaterialSlot.Find(MaterialSlotIndex)
                : nullptr;
        ApplyFallbackRenderProfileParameters(
            *MID,
            Asset,
            MaterialSlotIndex,
            FallbackProfile,
            Usage);
        const bool bUseRenderProfileLUT =
            Resources != nullptr &&
            Resources->ProfileRemapLUT != nullptr &&
            Resources->FindWetPartDataTexture(MaterialSlotIndex) != nullptr &&
            GlobalRenderProfileLUT != nullptr;
        MID->SetScalarParameterValue(
            DWCWetMaterialParameters::UseRenderProfileLUT(),
            bUseRenderProfileLUT ? 1.0f : 0.0f);
        MID->SetTextureParameterValue(
            DWCWetMaterialParameters::WetPartDataTexture(),
            Resources != nullptr && Resources->FindWetPartDataTexture(MaterialSlotIndex) != nullptr
                ? Resources->FindWetPartDataTexture(MaterialSlotIndex)
                : NeutralWetPartDataTexture.Get());
        MID->SetTextureParameterValue(
            DWCWetMaterialParameters::ProfileRemapLUT(),
            Resources != nullptr && Resources->ProfileRemapLUT != nullptr
                ? Resources->ProfileRemapLUT.Get()
                : NeutralProfileRemapLUT.Get());
        MID->SetScalarParameterValue(PreviewSurfaceWaterOverrideParameter, 0.0f);
        MID->SetScalarParameterValue(PreviewSurfaceWaterAmountParameter, 0.0f);
        BindGlobalResources(*MID, Usage);
    }
}

bool UDWCGPUResourceSubsystem::ApplyPreviewRenderProfileFallback(
    UWetClothingAsset* Asset,
    const int32 MaterialSlotIndex,
    const int32 LocalProfileID,
    UMaterialInstanceDynamic& MID)
{
    EnsureNeutralResources();
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE || LocalProfileID <= 0)
    {
        return false;
    }

    const TArray<FWetClothingLocalRenderProfile> ResolvedLocalProfiles =
        MakeResolvedLocalRenderProfiles(Asset);
    const int32 LocalProfileIndex = LocalProfileID - 1;
    if (!ResolvedLocalProfiles.IsValidIndex(LocalProfileIndex))
    {
        return false;
    }

    ApplyFallbackRenderProfileParameters(
        MID,
        Asset,
        MaterialSlotIndex,
        &ResolvedLocalProfiles[LocalProfileIndex],
        EDWCRenderResourceUsage::FullGPU);
    BindGlobalResources(MID, EDWCRenderResourceUsage::FullGPU);
    MID.SetScalarParameterValue(DWCWetMaterialParameters::UseRenderProfileLUT(), 0.0f);
    return true;
}

bool UDWCGPUResourceSubsystem::ApplyPreviewRenderProfileFallbackProfile(
    const UWetClothingAsset* Asset,
    const int32 MaterialSlotIndex,
    const FWetClothingLocalRenderProfile& LocalProfile,
    UMaterialInstanceDynamic& MID)
{
    EnsureNeutralResources();
    if (MaterialSlotIndex == INDEX_NONE)
    {
        return false;
    }

    ApplyFallbackRenderProfileParameters(
        MID,
        Asset,
        MaterialSlotIndex,
        &LocalProfile,
        EDWCRenderResourceUsage::FullGPU);
    BindGlobalResources(MID, EDWCRenderResourceUsage::FullGPU);
    MID.SetScalarParameterValue(DWCWetMaterialParameters::UseRenderProfileLUT(), 0.0f);
    return true;
}
