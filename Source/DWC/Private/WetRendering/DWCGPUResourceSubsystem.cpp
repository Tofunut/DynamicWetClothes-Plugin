#include "WetRendering/DWCGPUResourceSubsystem.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/SecureHash.h"
#include "PixelFormat.h"
#include "Rendering/Texture2DResource.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "UObject/Package.h"
#include "WetRendering/WetMaterialParameters.h"
#include "Utility/DWCLog.h"

namespace
{
    constexpr float GlobalTexelSize = 1.0f / static_cast<float>(UDWCGPUResourceSubsystem::GlobalLUTWidth);
    constexpr int32 InitialTextureArrayCapacity = 16;

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
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->NeverStream = true;

        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FLinearColor));
        Mip.BulkData.Unlock();
        Texture->UpdateResource();
        return Texture;
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
            TEXT("DropletsEnabled=%d|DropletNormal=%s|DropletMask=%s|")
            TEXT("RivuletsEnabled=%d|RivuletNormal=%s|RivuletMask=%s|")
            TEXT("NormalStrength=%.9g|RoughnessStrength=%.9g|")
            TEXT("VisibilityThreshold=%.9g|RivuletScrollSpeed=%.9g"),
            LocalProfile.Parameters.GetAbsorbedDarkeningStrength(),
            LocalProfile.Parameters.GetAbsorbedGlossinessStrength(),
            Surface.bEnabled && Surface.bEnableDroplets ? 1 : 0,
            LocalProfile.NormalizedDropletNormal != nullptr
                ? *LocalProfile.NormalizedDropletNormal->GetPathName()
                : TEXT("None"),
            LocalProfile.NormalizedDropletMask != nullptr
                ? *LocalProfile.NormalizedDropletMask->GetPathName()
                : TEXT("None"),
            Surface.bEnabled && Surface.bEnableRivulets ? 1 : 0,
            LocalProfile.NormalizedRivuletNormal != nullptr
                ? *LocalProfile.NormalizedRivuletNormal->GetPathName()
                : TEXT("None"),
            LocalProfile.NormalizedRivuletMask != nullptr
                ? *LocalProfile.NormalizedRivuletMask->GetPathName()
                : TEXT("None"),
            Surface.SurfaceWaterNormalStrength,
            Surface.SurfaceWaterRoughnessStrength,
            Surface.SurfaceVisibilityThreshold,
            Surface.RivuletUVScrollSpeed);
        const FString ParameterHash = FMD5::HashAnsiString(*ParameterState);
        if (LocalProfile.SourceProfile.IsValid())
        {
            return FString::Printf(TEXT("Asset:%s|Parameters:%s"), *LocalProfile.SourceProfile.ToString(), *ParameterHash);
        }
        return FString::Printf(TEXT("Inline:%s"), *ParameterHash);
    }

    FString MakeRenderFallbackParameterKey(const FWetnessProfileParameters& Parameters)
    {
        const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
        return FString::Printf(
            TEXT("AbsorbedDarkening=%.9g|AbsorbedGlossiness=%.9g|DropletsEnabled=%d|DropletNormal=%s|DropletMask=%s|")
            TEXT("RivuletsEnabled=%d|RivuletNormal=%s|RivuletMask=%s|")
            TEXT("NormalStrength=%.9g|RoughnessStrength=%.9g|")
            TEXT("VisibilityThreshold=%.9g|RivuletScrollSpeed=%.9g"),
            Parameters.GetAbsorbedDarkeningStrength(),
            Parameters.GetAbsorbedGlossinessStrength(),
            Surface.bEnabled && Surface.bEnableDroplets ? 1 : 0,
            *GetPathNameSafe(Surface.DropletNormalTexture),
            *GetPathNameSafe(Surface.DropletMaskTexture),
            Surface.bEnabled && Surface.bEnableRivulets ? 1 : 0,
            *GetPathNameSafe(Surface.RivuletNormalTexture),
            *GetPathNameSafe(Surface.RivuletMaskTexture),
            Surface.SurfaceWaterNormalStrength,
            Surface.SurfaceWaterRoughnessStrength,
            Surface.SurfaceVisibilityThreshold,
            Surface.RivuletUVScrollSpeed);
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
            if (const UPackage* SourcePackage = SourceProfile->GetOutermost();
                SourcePackage != nullptr && SourcePackage->IsDirty())
            {
                return false;
            }

            OutParameters = SourceProfile->GetParameters();
            return true;
        }

        return false;
    }

    TArray<FWetClothingLocalRenderProfile> MakeResolvedLocalRenderProfiles(
        const UWetClothingAsset* WetClothingAsset,
        const bool bResolveUnloadedProfiles)
    {
        TArray<FWetClothingLocalRenderProfile> Profiles;
        if (WetClothingAsset == nullptr)
        {
            return Profiles;
        }

        Profiles = WetClothingAsset->Derived.Inline.BakedWetPartData.LocalProfiles;
        for (FWetClothingLocalRenderProfile& Profile : Profiles)
        {
            FWetnessProfileParameters ResolvedParameters;
            if (ResolveSourceProfileParameters(
                    Profile.SourceProfile,
                    bResolveUnloadedProfiles,
                    ResolvedParameters))
            {
                Profile.Parameters = ResolvedParameters;
                Profile.StableKey.Reset();
            }
        }

        return Profiles;
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
        if (SourceAssignment.SourceProfile.IsValid())
        {
            if (const FWetClothingLocalRenderProfile* ExactSourceMatch = LocalProfiles.FindByPredicate(
                    [&SourceAssignment, &ParameterKey](const FWetClothingLocalRenderProfile& Candidate)
                    {
                        return Candidate.SourceProfile == SourceAssignment.SourceProfile &&
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

        if (SourceAssignment.SourceProfile.IsValid())
        {
            if (const FWetClothingLocalRenderProfile* SourceMatch = LocalProfiles.FindByPredicate(
                    [&SourceAssignment](const FWetClothingLocalRenderProfile& Candidate)
                    {
                        return Candidate.SourceProfile == SourceAssignment.SourceProfile;
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

        const FWetPartProfileAssignment* SourceAssignment = WetPartData.FindProfile(*SourceEntry);
        if (SourceAssignment == nullptr)
        {
            return false;
        }

        FWetnessProfileParameters Parameters = SourceAssignment->Parameters;
        if (bResolveSourceProfile && SourceAssignment->SourceProfile.IsValid())
        {
            UObject* SourceObject = SourceAssignment->SourceProfile.ResolveObject();
            if (SourceObject == nullptr)
            {
                SourceObject = SourceAssignment->SourceProfile.TryLoad();
            }

            if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(SourceObject))
            {
                if (const UPackage* SourcePackage = SourceProfile->GetOutermost();
                    SourcePackage == nullptr || !SourcePackage->IsDirty())
                {
                    Parameters = SourceProfile->GetParameters();
                }
            }
        }

        if (const FWetClothingLocalRenderProfile* BakedProfile =
                FindMatchingBakedFallbackProfile(WetClothingAsset, *SourceAssignment, Parameters))
        {
            OutProfile = *BakedProfile;
            OutProfile.SourceProfile = SourceAssignment->SourceProfile;
            OutProfile.Parameters = Parameters;
            return true;
        }

        OutProfile.SourceProfile = SourceAssignment->SourceProfile;
        OutProfile.Parameters = Parameters;
        return true;
    }

    struct FFallbackRenderProfileSlices
    {
        int32 DropletMask = 0;
        int32 DropletNormal = 0;
        int32 RivuletMask = 0;
        int32 RivuletNormal = 0;
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
                static_cast<float>(Surface.bEnabled && Surface.bEnableDroplets ? Slices.DropletNormal : 0),
                static_cast<float>(Surface.bEnabled && Surface.bEnableRivulets ? Slices.RivuletNormal : 0));

        case 1:
            return FLinearColor(
                FMath::Max(0.0f, Surface.SurfaceWaterNormalStrength),
                FMath::Clamp(Surface.SurfaceWaterRoughnessStrength, 0.0f, 1.0f),
                FMath::Clamp(Surface.SurfaceVisibilityThreshold, 0.0f, 1.0f),
                Surface.RivuletUVScrollSpeed);

        case 2:
            return FLinearColor(
                static_cast<float>(Surface.bEnabled && Surface.bEnableDroplets ? Slices.DropletMask : 0),
                static_cast<float>(Surface.bEnabled && Surface.bEnableRivulets ? Slices.RivuletMask : 0),
                0.0f,
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
        Texture->MipGenSettings = TMGS_NoMipmaps;
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
            TEXT("DWC render texture '%s' is %dx%d format %d, but its Texture2DArray requires %dx%d format %d. The profile uses neutral slice 0 until the texture is normalized."),
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
    RivuletMaskRegistry.Reset();
    RivuletNormalRegistry.Reset();
    DirtyRuntimeProfileIndices.Reset();
    RegisteredMaterialInstances.Reset();
    GPUMaterialInstances.Reset();
    NeutralWetPartDataTexture = nullptr;
    NeutralProfileRemapLUT = nullptr;
    GlobalRenderProfileLUT = nullptr;
    DropletMaskArray = nullptr;
    DropletNormalArray = nullptr;
    RivuletMaskArray = nullptr;
    RivuletNormalArray = nullptr;
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
                      GetRegistryCPUBytes(DropletNormalRegistry) +
                      GetRegistryCPUBytes(RivuletMaskRegistry) +
                      GetRegistryCPUBytes(RivuletNormalRegistry);
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
    AddUniqueTextureGPUBytes(RivuletMaskArray, SeenTextures, Stats.SurfaceNormalArrayGPUBytes);
    AddUniqueTextureGPUBytes(RivuletNormalArray, SeenTextures, Stats.SurfaceNormalArrayGPUBytes);
    Stats.TextureArrayCount += DropletMaskArray != nullptr ? 1u : 0u;
    Stats.TextureArrayCount += DropletNormalArray != nullptr ? 1u : 0u;
    Stats.TextureArrayCount += RivuletMaskArray != nullptr ? 1u : 0u;
    Stats.TextureArrayCount += RivuletNormalArray != nullptr ? 1u : 0u;

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
        if (Usage == EDWCRenderResourceUsage::AbsorbedOnly || Record->bSurfaceResourcesResolved)
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
        const bool bDropletRequested = Surface.bEnabled && Surface.bEnableDroplets;
        const bool bRivuletRequested = Surface.bEnabled && Surface.bEnableRivulets;
        EnsureMaskRegistryNeutral(
            DropletMaskRegistry,
            bDropletRequested ? LocalProfile.NormalizedDropletMask : nullptr,
            bTextureArraysChanged);
        EnsureMaskRegistryNeutral(
            RivuletMaskRegistry,
            bRivuletRequested ? LocalProfile.NormalizedRivuletMask : nullptr,
            bTextureArraysChanged);
        const int32 DropletMaskSlice =
            bDropletRequested
                ? DropletMaskRegistry.FindOrAdd(LocalProfile.NormalizedDropletMask, bTextureArraysChanged)
                : 0;
        const int32 DropletNormalSlice =
            bDropletRequested
                ? DropletNormalRegistry.FindOrAdd(LocalProfile.NormalizedDropletNormal, bTextureArraysChanged)
                : 0;
        const int32 RivuletMaskSlice =
            bRivuletRequested
                ? RivuletMaskRegistry.FindOrAdd(LocalProfile.NormalizedRivuletMask, bTextureArraysChanged)
                : 0;
        const int32 RivuletNormalSlice =
            bRivuletRequested
                ? RivuletNormalRegistry.FindOrAdd(LocalProfile.NormalizedRivuletNormal, bTextureArraysChanged)
                : 0;

        Record->PackedTexels[0] = FLinearColor(
            LocalProfile.Parameters.GetAbsorbedDarkeningStrength(),
            LocalProfile.Parameters.GetAbsorbedGlossinessStrength(),
            static_cast<float>(DropletNormalSlice),
            static_cast<float>(RivuletNormalSlice));
        Record->PackedTexels[1] = FLinearColor(
            FMath::Max(0.0f, Surface.SurfaceWaterNormalStrength),
            FMath::Clamp(Surface.SurfaceWaterRoughnessStrength, 0.0f, 1.0f),
            FMath::Clamp(Surface.SurfaceVisibilityThreshold, 0.0f, 1.0f),
            Surface.RivuletUVScrollSpeed);
        Record->PackedTexels[2] = FLinearColor(
            static_cast<float>(DropletMaskSlice),
            static_cast<float>(RivuletMaskSlice),
            0.0f,
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
    Array->MipGenSettings = TMGS_NoMipmaps;
    Array->CompressionSettings = bNormalArray ? TC_Normalmap : TC_Masks;
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
    if (Array == nullptr || Registry.AllocatedCapacity < RequiredSlices)
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
    const bool bRivuletMaskArrayReplaced = EnsureTextureArray(
        TEXT("DWC_RivuletMaskArray"),
        RivuletMaskRegistry,
        RivuletMaskArray,
        false);
    const bool bRivuletArrayReplaced = EnsureTextureArray(
        TEXT("DWC_RivuletNormalArray"),
        RivuletNormalRegistry,
        RivuletNormalArray,
        true);
    return bDropletMaskArrayReplaced || bDropletArrayReplaced || bRivuletMaskArrayReplaced || bRivuletArrayReplaced;
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

    FDWCAssetRenderProfileResources* Existing = AssetResources.Find(Asset);
    const bool bExistingMappingValid =
        Existing != nullptr &&
        Existing->RegistryRevision == RegistryRevision &&
        Existing->SourceBakeGuid == Asset->Derived.Inline.BakedWetPartData.BakeGuid &&
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
        RivuletNormalRegistry.SetNeutral(NeutralNormal, bNeutralRegistryChanged);
        bTextureArraysDirty |= bNeutralRegistryChanged;
    }

    const TArray<FWetClothingLocalRenderProfile> ResolvedLocalProfiles =
        MakeResolvedLocalRenderProfiles(Asset, true);
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
         DropletNormalArray == nullptr ||
         RivuletMaskArray == nullptr ||
         RivuletNormalArray == nullptr))
    {
        const bool bArrayResourceReplaced = EnsureTextureArraysUpToDate();
        bTextureArraysDirty = false;
        if (bArrayResourceReplaced)
        {
            RebindGPUTextureArrays();
        }
    }
    FlushDirtyRuntimeProfiles();

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
        if (ResolveFallbackRenderProfile(Asset, Slot.MaterialSlotIndex, FallbackProfile, true))
        {
            Resources.FallbackRenderProfilesByMaterialSlot.Add(
                Slot.MaterialSlotIndex,
                FallbackProfile);
        }
    }
    Resources.ProfileRemapLUT = BuildAssetRemapLUT(Asset, LocalToRuntime);
    Resources.SourceBakeGuid = Asset->Derived.Inline.BakedWetPartData.BakeGuid;
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
        if (RivuletMaskArray != nullptr)
        {
            MID.SetTextureParameterValue(DWCWetMaterialParameters::RivuletMaskTextureArray(), RivuletMaskArray);
        }
        if (RivuletNormalArray != nullptr)
        {
            MID.SetTextureParameterValue(DWCWetMaterialParameters::RivuletNormalTextureArray(), RivuletNormalArray);
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
    }
    else
    {
        ResolveFallbackRenderProfile(WetClothingAsset, MaterialSlotIndex, Profile, false);
    }

    FFallbackRenderProfileSlices Slices;
    if (Usage == EDWCRenderResourceUsage::FullGPU)
    {
        bool bTextureArraysChanged = false;
        if (WetClothingAsset != nullptr)
        {
            UTexture2D* NeutralNormal =
                WetClothingAsset->Derived.Inline.BakedWetPartData.NormalizedNeutralSurfaceNormal;
            DropletNormalRegistry.SetNeutral(NeutralNormal, bTextureArraysChanged);
            RivuletNormalRegistry.SetNeutral(NeutralNormal, bTextureArraysChanged);
        }
        const FSurfaceWaterProfileParameters& Surface = Profile.Parameters.SurfaceWater;
        const bool bDropletRequested = Surface.bEnabled && Surface.bEnableDroplets;
        const bool bRivuletRequested = Surface.bEnabled && Surface.bEnableRivulets;
        EnsureMaskRegistryNeutral(
            DropletMaskRegistry,
            bDropletRequested ? Profile.NormalizedDropletMask : nullptr,
            bTextureArraysChanged);
        EnsureMaskRegistryNeutral(
            RivuletMaskRegistry,
            bRivuletRequested ? Profile.NormalizedRivuletMask : nullptr,
            bTextureArraysChanged);
        Slices.DropletMask = bDropletRequested
            ? DropletMaskRegistry.FindOrAdd(Profile.NormalizedDropletMask, bTextureArraysChanged)
            : 0;
        Slices.DropletNormal = bDropletRequested
            ? DropletNormalRegistry.FindOrAdd(Profile.NormalizedDropletNormal, bTextureArraysChanged)
            : 0;
        Slices.RivuletMask = bRivuletRequested
            ? RivuletMaskRegistry.FindOrAdd(Profile.NormalizedRivuletMask, bTextureArraysChanged)
            : 0;
        Slices.RivuletNormal = bRivuletRequested
            ? RivuletNormalRegistry.FindOrAdd(Profile.NormalizedRivuletNormal, bTextureArraysChanged)
            : 0;

        const auto DescribeNormalTexture =
            [](UTexture2D* SourceTexture)
            {
                return SourceTexture != nullptr
                    ? FString::Printf(
                        TEXT("%s (%dx%d format %d)"),
                        *SourceTexture->GetPathName(),
                        SourceTexture->GetSizeX(),
                        SourceTexture->GetSizeY(),
                        static_cast<int32>(SourceTexture->GetPixelFormat()))
                    : FString(TEXT("None"));
            };
        const FString ProfileKey = ResolveProfileKey(Profile);
        UE_LOG(
            LogDWC,
            Display,
            TEXT("DWC Surface Water normal registry result for asset '%s' slot %d profile '%s': surfaceEnabled=%d normalStrength=%.6g droplet(requested=%d slice=%d source=%s) rivulet(requested=%d slice=%d source=%s)."),
            *GetPathNameSafe(WetClothingAsset),
            MaterialSlotIndex,
            *ProfileKey,
            Surface.bEnabled ? 1 : 0,
            Surface.SurfaceWaterNormalStrength,
            bDropletRequested ? 1 : 0,
            Slices.DropletNormal,
            *DescribeNormalTexture(Profile.NormalizedDropletNormal),
            bRivuletRequested ? 1 : 0,
            Slices.RivuletNormal,
            *DescribeNormalTexture(Profile.NormalizedRivuletNormal));

        if (Surface.bEnabled && Surface.SurfaceWaterNormalStrength <= UE_KINDA_SMALL_NUMBER &&
            (bDropletRequested || bRivuletRequested))
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC Surface Water normal strength is %.6g for asset '%s' slot %d profile '%s'; detail normal weight resolves to zero even if coverage is visible."),
                Surface.SurfaceWaterNormalStrength,
                *GetPathNameSafe(WetClothingAsset),
                MaterialSlotIndex,
                *ProfileKey);
        }

        if (Surface.bEnabled && !bDropletRequested && !bRivuletRequested)
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC Surface Water is enabled but neither Droplet nor Rivulet normals are enabled for asset '%s' slot %d profile '%s'; World Normal remains the source material normal."),
                *GetPathNameSafe(WetClothingAsset),
                MaterialSlotIndex,
                *ProfileKey);
        }

        const auto LogSliceZeroFallback =
            [WetClothingAsset, MaterialSlotIndex, &Profile, &DescribeNormalTexture](
                const TCHAR* NormalKind,
                UTexture2D* SourceTexture,
                const int32 ResolvedSlice,
                const bool bRequested)
            {
                if (!bRequested || ResolvedSlice != 0)
                {
                    return;
                }

                UE_LOG(
                    LogDWC,
                    Warning,
                    TEXT("DWC %s normal resolved to Texture2DArray slice 0 flat-normal fallback for asset '%s' slot %d profile '%s'. Source texture: %s."),
                    NormalKind,
                    *GetPathNameSafe(WetClothingAsset),
                    MaterialSlotIndex,
                    *ResolveProfileKey(Profile),
                    *DescribeNormalTexture(SourceTexture));
            };
        LogSliceZeroFallback(
            TEXT("Droplet"),
            Profile.NormalizedDropletNormal,
            Slices.DropletNormal,
            bDropletRequested);
        LogSliceZeroFallback(
            TEXT("Rivulet"),
            Profile.NormalizedRivuletNormal,
            Slices.RivuletNormal,
            bRivuletRequested);

        const auto LogMaskSliceZero =
            [WetClothingAsset, MaterialSlotIndex, &Profile, &DescribeNormalTexture](
                const TCHAR* MaskKind,
                UTexture2D* SourceTexture,
                const int32 ResolvedSlice,
                const bool bRequested)
            {
                if (!bRequested || ResolvedSlice != 0)
                {
                    return;
                }

                UE_LOG(
                    LogDWC,
                    Warning,
                    TEXT("DWC %s mask resolved to Texture2DArray slice 0 for asset '%s' slot %d profile '%s'. Reference-style Surface Water is mask-gated, so this contribution resolves to zero. Source texture: %s."),
                    MaskKind,
                    *GetPathNameSafe(WetClothingAsset),
                    MaterialSlotIndex,
                    *ResolveProfileKey(Profile),
                    *DescribeNormalTexture(SourceTexture));
            };
        LogMaskSliceZero(
            TEXT("Droplet"),
            Profile.NormalizedDropletMask,
            Slices.DropletMask,
            bDropletRequested);
        LogMaskSliceZero(
            TEXT("Rivulet"),
            Profile.NormalizedRivuletMask,
            Slices.RivuletMask,
            bRivuletRequested);

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

        ApplyFallbackRenderProfileParameters(
            *MID,
            Asset,
            MaterialSlotIndex,
            Resources != nullptr
                ? Resources->FallbackRenderProfilesByMaterialSlot.Find(MaterialSlotIndex)
                : nullptr,
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
        MakeResolvedLocalRenderProfiles(Asset, true);
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
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE)
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
