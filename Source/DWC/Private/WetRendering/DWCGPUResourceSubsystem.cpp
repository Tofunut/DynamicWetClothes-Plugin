#include "WetRendering/DWCGPUResourceSubsystem.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/SecureHash.h"
#include "Rendering/Texture2DResource.h"
#include "RHICommandList.h"
#include "WetRendering/WetMaterialParameters.h"
#include "Utility/DWCLog.h"

namespace
{
    constexpr float GlobalTexelSize = 1.0f / static_cast<float>(UDWCGPUResourceSubsystem::GlobalLUTWidth);

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
            TEXT("WetVisual=%.9g|")
            TEXT("DropletsEnabled=%d|DropletNormal=%s|")
            TEXT("RivuletsEnabled=%d|RivuletNormal=%s|")
            TEXT("NormalStrength=%.9g|RoughnessStrength=%.9g|")
            TEXT("VisibilityThreshold=%.9g|RivuletScrollSpeed=%.9g"),
            LocalProfile.Parameters.GetWetVisualStrength(),
            Surface.bEnabled && Surface.bEnableDroplets ? 1 : 0,
            LocalProfile.NormalizedDropletNormal != nullptr
                ? *LocalProfile.NormalizedDropletNormal->GetPathName()
                : TEXT("None"),
            Surface.bEnabled && Surface.bEnableRivulets ? 1 : 0,
            LocalProfile.NormalizedRivuletNormal != nullptr
                ? *LocalProfile.NormalizedRivuletNormal->GetPathName()
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
            TEXT("WetVisual=%.9g|DropletsEnabled=%d|DropletNormal=%s|")
            TEXT("RivuletsEnabled=%d|RivuletNormal=%s|")
            TEXT("NormalStrength=%.9g|RoughnessStrength=%.9g|")
            TEXT("VisibilityThreshold=%.9g|RivuletScrollSpeed=%.9g"),
            Parameters.GetWetVisualStrength(),
            Surface.bEnabled && Surface.bEnableDroplets ? 1 : 0,
            *GetPathNameSafe(Surface.DropletNormalTexture),
            Surface.bEnabled && Surface.bEnableRivulets ? 1 : 0,
            *GetPathNameSafe(Surface.RivuletNormalTexture),
            Surface.SurfaceWaterNormalStrength,
            Surface.SurfaceWaterRoughnessStrength,
            Surface.SurfaceVisibilityThreshold,
            Surface.RivuletUVScrollSpeed);
    }

    const FWetClothingLocalRenderProfile* FindMatchingBakedFallbackProfile(
        const UWetClothingAsset* WetClothingAsset,
        const FWetClothingWetPartEntry& SourceEntry,
        const FWetnessProfileParameters& Parameters)
    {
        if (WetClothingAsset == nullptr ||
            !WetClothingAsset->Derived.Inline.BakedProfileIDData.IsValid())
        {
            return nullptr;
        }

        const TArray<FWetClothingLocalRenderProfile>& LocalProfiles =
            WetClothingAsset->Derived.Inline.BakedProfileIDData.LocalProfiles;
        const FString ParameterKey = MakeRenderFallbackParameterKey(Parameters);
        if (SourceEntry.ProfileAssignment.SourceProfile.IsValid())
        {
            if (const FWetClothingLocalRenderProfile* ExactSourceMatch = LocalProfiles.FindByPredicate(
                    [&SourceEntry, &ParameterKey](const FWetClothingLocalRenderProfile& Candidate)
                    {
                        return Candidate.SourceProfile == SourceEntry.ProfileAssignment.SourceProfile &&
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

        if (SourceEntry.ProfileAssignment.SourceProfile.IsValid())
        {
            if (const FWetClothingLocalRenderProfile* SourceMatch = LocalProfiles.FindByPredicate(
                    [&SourceEntry](const FWetClothingLocalRenderProfile& Candidate)
                    {
                        return Candidate.SourceProfile == SourceEntry.ProfileAssignment.SourceProfile;
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
        FWetClothingLocalRenderProfile& OutProfile)
    {
        if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return false;
        }

        const FWetClothingWetPartEntry* FirstSlotEntry = nullptr;
        const FWetClothingWetPartEntry* DefaultSlotEntry = nullptr;
        for (const FWetClothingWetPartEntry& Entry :
             WetClothingAsset->Authored.PartData.EditableWetPartData.WetPartEntries)
        {
            if (Entry.MaterialSlotIndex != MaterialSlotIndex)
            {
                continue;
            }

            if (FirstSlotEntry == nullptr)
            {
                FirstSlotEntry = &Entry;
            }
            if (Entry.WetPartID == 0)
            {
                DefaultSlotEntry = &Entry;
                break;
            }
        }

        const FWetClothingWetPartEntry* SourceEntry =
            DefaultSlotEntry != nullptr ? DefaultSlotEntry : FirstSlotEntry;
        if (SourceEntry == nullptr)
        {
            return false;
        }

        FWetnessProfileParameters Parameters;
        if (SourceEntry->ProfileAssignment.SourceProfile.IsValid())
        {
            if (const UWetnessProfile* SourceProfile =
                    Cast<UWetnessProfile>(SourceEntry->ProfileAssignment.SourceProfile.TryLoad()))
            {
                Parameters = SourceProfile->GetParameters();
                if (const FWetClothingLocalRenderProfile* BakedProfile =
                        FindMatchingBakedFallbackProfile(WetClothingAsset, *SourceEntry, Parameters))
                {
                    OutProfile = *BakedProfile;
                    return true;
                }

                OutProfile.SourceProfile = SourceEntry->ProfileAssignment.SourceProfile;
                OutProfile.Parameters = Parameters;
                return true;
            }
        }

        Parameters = SourceEntry->ProfileAssignment.Parameters;
        if (const FWetClothingLocalRenderProfile* BakedProfile =
                FindMatchingBakedFallbackProfile(WetClothingAsset, *SourceEntry, Parameters))
        {
            OutProfile = *BakedProfile;
            return true;
        }

        OutProfile.SourceProfile = SourceEntry->ProfileAssignment.SourceProfile;
        OutProfile.Parameters = Parameters;
        return true;
    }

    struct FFallbackRenderProfileSlices
    {
        int32 DropletNormal = 0;
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
                Parameters.GetWetVisualStrength(),
                static_cast<float>(Surface.bEnabled && Surface.bEnableDroplets ? Slices.DropletNormal : 0),
                static_cast<float>(Surface.bEnabled && Surface.bEnableRivulets ? Slices.RivuletNormal : 0),
                FMath::Max(0.0f, Surface.SurfaceWaterNormalStrength));

        case 1:
            return FLinearColor(
                FMath::Clamp(Surface.SurfaceWaterRoughnessStrength, 0.0f, 1.0f),
                FMath::Clamp(Surface.SurfaceVisibilityThreshold, 0.0f, 1.0f),
                Surface.RivuletUVScrollSpeed,
                0.0f);

        default:
            return FLinearColor::Black;
        }
    }


    UTexture2D* CreateNeutralProfileIDTexture(UObject* Outer)
    {
        UTexture2D* Texture = UTexture2D::CreateTransient(1, 1, PF_G8, TEXT("DWC_NeutralProfileID"));
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
        uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
        *Data = 0u;
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
            TEXT("DWC neutral normal '%s' does not match the existing Texture2DArray registry format."),
            *Texture->GetPathName());
        return;
    }

    if (SourceTextures.IsEmpty())
    {
        SourceTextures.Add(Texture);
        SliceByPath.Add(Texture->GetPathName(), 0);
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
        bOutChanged = true;
    }
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

    // Slice 0 must be initialized by SetNeutral before real slices are registered.
    if (SourceTextures.IsEmpty())
    {
        UE_LOG(LogDWC, Warning, TEXT("DWC normal registry has no neutral slice; texture '%s' uses slice 0."), *Path);
        return 0;
    }
    const int32 NewSlice = SourceTextures.Add(Texture);
    SliceByPath.Add(Path, NewSlice);
    bOutChanged = true;
    return NewSlice;
}

void UDWCGPUResourceSubsystem::FTextureArrayRegistry::Reset()
{
    SourceTextures.Reset();
    SliceByPath.Reset();
    SizeX = 0;
    SizeY = 0;
    PixelFormat = INDEX_NONE;
}

void UDWCGPUResourceSubsystem::Deinitialize()
{
    AssetResources.Reset();
    StaticSlotResources.Reset();
    RuntimeProfiles.Reset();
    RuntimeProfileIndexByKey.Reset();
    DropletNormalRegistry.Reset();
    RivuletNormalRegistry.Reset();
    NeutralProfileIDTexture = nullptr;
    NeutralProfileRemapLUT = nullptr;
    GlobalRenderProfileLUT = nullptr;
    DropletNormalArray = nullptr;
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
        Stats.CPUBytes += Pair.Value.ProfileIDTexturesByMaterialSlot.GetAllocatedSize();
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
                      GetRegistryCPUBytes(DropletNormalRegistry) +
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
    AddUniqueTextureGPUBytes(NeutralProfileIDTexture, SeenTextures, Stats.ProfileIDRemapGPUBytes);
    AddUniqueTextureGPUBytes(NeutralProfileRemapLUT, SeenTextures, Stats.ProfileIDRemapGPUBytes);
    AddUniqueTextureGPUBytes(DropletNormalArray, SeenTextures, Stats.SurfaceNormalArrayGPUBytes);
    AddUniqueTextureGPUBytes(RivuletNormalArray, SeenTextures, Stats.SurfaceNormalArrayGPUBytes);
    Stats.TextureArrayCount += DropletNormalArray != nullptr ? 1u : 0u;
    Stats.TextureArrayCount += RivuletNormalArray != nullptr ? 1u : 0u;

    for (const TPair<TObjectPtr<UWetClothingAsset>, FDWCAssetRenderProfileResources>& Pair : AssetResources)
    {
        for (const TPair<int32, TObjectPtr<UTexture2D>>& TexturePair : Pair.Value.ProfileIDTexturesByMaterialSlot)
        {
            AddUniqueTextureGPUBytes(TexturePair.Value, SeenTextures, Stats.ProfileIDRemapGPUBytes);
        }
        AddUniqueTextureGPUBytes(Pair.Value.ProfileRemapLUT, SeenTextures, Stats.ProfileIDRemapGPUBytes);
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

    if (NeutralProfileIDTexture == nullptr)
    {
        NeutralProfileIDTexture = CreateNeutralProfileIDTexture(this);
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

int32 UDWCGPUResourceSubsystem::FindOrAddRuntimeProfile(
    const FWetClothingLocalRenderProfile& LocalProfile,
    bool& bOutChanged)
{
    const FString Key = ResolveProfileKey(LocalProfile);
    if (const int32* ExistingIndex = RuntimeProfileIndexByKey.Find(Key))
    {
        return *ExistingIndex;
    }

    if (RuntimeProfiles.Num() >= MaxRuntimeProfileCount)
    {
        UE_LOG(LogDWC, Error, TEXT("DWC Render Profile Registry exceeded %d profiles."), MaxRuntimeProfileCount);
        return 0;
    }

    FRuntimeProfileRecord& Record = RuntimeProfiles.AddDefaulted_GetRef();
    Record.StableKey = Key;
    Record.PackedTexels.SetNumZeroed(TexelsPerProfile);
    const int32 NewIndex = RuntimeProfiles.Num() - 1;
    RuntimeProfileIndexByKey.Add(Key, NewIndex);

    const FSurfaceWaterProfileParameters& Surface = LocalProfile.Parameters.SurfaceWater;
    bool bTextureArraysChanged = false;
    const int32 DropletNormalSlice =
        Surface.bEnabled && Surface.bEnableDroplets
            ? DropletNormalRegistry.FindOrAdd(LocalProfile.NormalizedDropletNormal, bTextureArraysChanged)
            : 0;
    const int32 RivuletNormalSlice =
        Surface.bEnabled && Surface.bEnableRivulets
            ? RivuletNormalRegistry.FindOrAdd(LocalProfile.NormalizedRivuletNormal, bTextureArraysChanged)
            : 0;

    Record.PackedTexels[0] = FLinearColor(
        LocalProfile.Parameters.GetWetVisualStrength(),
        static_cast<float>(DropletNormalSlice),
        static_cast<float>(RivuletNormalSlice),
        FMath::Max(0.0f, Surface.SurfaceWaterNormalStrength));
    Record.PackedTexels[1] = FLinearColor(
        FMath::Clamp(Surface.SurfaceWaterRoughnessStrength, 0.0f, 1.0f),
        FMath::Clamp(Surface.SurfaceVisibilityThreshold, 0.0f, 1.0f),
        Surface.RivuletUVScrollSpeed,
        0.0f);

    bOutChanged = true;
    bTextureArraysDirty |= bTextureArraysChanged;
    return NewIndex;
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
}

UTexture2DArray* UDWCGPUResourceSubsystem::BuildTextureArray(
    const TCHAR* DebugName,
    const TArray<TObjectPtr<UTexture2D>>& SourceTextures,
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
    const int32 SliceCount = FMath::Max(SourceTextures.Num(), 1);

    UTexture2DArray* Array = UTexture2DArray::CreateTransient(SizeX, SizeY, SliceCount, Format, FName(DebugName));
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

    FTextureResource* DestinationResource = Array->GetResource();
    if (DestinationResource == nullptr)
    {
        return Array;
    }

    struct FCopySource
    {
        FTextureResource* Resource = nullptr;
        int32 SourceSliceIndex = 0;
        int32 DestinationSliceIndex = 0;
    };
    TArray<FCopySource> CopySources;
    for (int32 SliceIndex = 0; SliceIndex < SourceTextures.Num(); ++SliceIndex)
    {
        UTexture2D* Source = SourceTextures[SliceIndex];
        if (Source == nullptr ||
            Source->GetResource() == nullptr ||
            Source->GetSizeX() != SizeX ||
            Source->GetSizeY() != SizeY ||
            Source->GetPixelFormat() != Format)
        {
            if (Source != nullptr)
            {
                UE_LOG(
                    LogDWC,
                    Warning,
                    TEXT("DWC normalized texture '%s' does not match array '%s' (%dx%d format %d). Slice %d remains neutral."),
                    *Source->GetPathName(),
                    DebugName,
                    SizeX,
                    SizeY,
                    static_cast<int32>(Format),
                    SliceIndex);
            }
            continue;
        }
        CopySources.Add({Source->GetResource(), 0, SliceIndex});
    }

    ENQUEUE_RENDER_COMMAND(DWCCopyTextureArraySlices)(
        [DestinationResource, CopySources = MoveTemp(CopySources)](FRHICommandListImmediate& RHICmdList)
        {
            if (DestinationResource == nullptr || DestinationResource->TextureRHI == nullptr)
            {
                return;
            }
            for (const FCopySource& Source : CopySources)
            {
                if (Source.Resource == nullptr || Source.Resource->TextureRHI == nullptr)
                {
                    continue;
                }
                FRHICopyTextureInfo CopyInfo;
                CopyInfo.SourceSliceIndex = Source.SourceSliceIndex;
                CopyInfo.DestSliceIndex = Source.DestinationSliceIndex;
                CopyInfo.NumSlices = 1;
                CopyInfo.NumMips = 1;
                RHICmdList.CopyTexture(Source.Resource->TextureRHI, DestinationResource->TextureRHI, CopyInfo);
            }
        });

    return Array;
}

void UDWCGPUResourceSubsystem::RebuildTextureArrays()
{
    DropletNormalArray = BuildTextureArray(
        TEXT("DWC_DropletNormalArray"),
        DropletNormalRegistry.SourceTextures,
        true);
    RivuletNormalArray = BuildTextureArray(
        TEXT("DWC_RivuletNormalArray"),
        RivuletNormalRegistry.SourceTextures,
        true);
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

const FDWCAssetRenderProfileResources* UDWCGPUResourceSubsystem::AcquireAssetResources(UWetClothingAsset* Asset)
{
    EnsureNeutralResources();
    if (Asset == nullptr || !Asset->Derived.Inline.BakedProfileIDData.IsValid())
    {
        return nullptr;
    }

    if (FDWCAssetRenderProfileResources* Existing = AssetResources.Find(Asset))
    {
        if (Existing->RegistryRevision == RegistryRevision &&
            Existing->SourceBakeGuid == Asset->Derived.Inline.BakedProfileIDData.BakeGuid &&
            Existing->IsValid())
        {
            return Existing;
        }
    }


    bool bRegistryChanged = false;
    bool bNeutralRegistryChanged = false;
    UTexture2D* NeutralNormal = Asset->Derived.Inline.BakedProfileIDData.NormalizedNeutralSurfaceNormal;
    DropletNormalRegistry.SetNeutral(NeutralNormal, bNeutralRegistryChanged);
    RivuletNormalRegistry.SetNeutral(NeutralNormal, bNeutralRegistryChanged);
    bTextureArraysDirty |= bNeutralRegistryChanged;

    TArray<int32> LocalToRuntime;
    LocalToRuntime.Init(0, Asset->Derived.Inline.BakedProfileIDData.LocalProfiles.Num() + 1);
    for (int32 LocalProfileIndex = 0;
         LocalProfileIndex < Asset->Derived.Inline.BakedProfileIDData.LocalProfiles.Num();
         ++LocalProfileIndex)
    {
        LocalToRuntime[LocalProfileIndex + 1] = FindOrAddRuntimeProfile(
            Asset->Derived.Inline.BakedProfileIDData.LocalProfiles[LocalProfileIndex],
            bRegistryChanged);
    }

    if (bTextureArraysDirty || DropletNormalArray == nullptr || RivuletNormalArray == nullptr)
    {
        RebuildTextureArrays();
        bTextureArraysDirty = false;
    }

    if (bRegistryChanged || GlobalRenderProfileLUT == nullptr)
    {
        RebuildGlobalRenderProfileLUT();
        ++RegistryRevision;
        // Existing remap rows remain valid because runtime indices and texture
        // slices are append-only. They only need their revision updated.
        for (TPair<TObjectPtr<UWetClothingAsset>, FDWCAssetRenderProfileResources>& Pair : AssetResources)
        {
            Pair.Value.RegistryRevision = RegistryRevision;
        }
    }

    FDWCAssetRenderProfileResources& Resources = AssetResources.FindOrAdd(Asset);
    Resources.ProfileIDTexturesByMaterialSlot.Reset();
    for (const FWetClothingBakedProfileIDSlotTexture& SlotTexture :
         Asset->Derived.Inline.BakedProfileIDData.SlotTextures)
    {
        if (SlotTexture.IsValid())
        {
            Resources.ProfileIDTexturesByMaterialSlot.Add(
                SlotTexture.MaterialSlotIndex,
                SlotTexture.ProfileIDTexture);
        }
    }
    Resources.ProfileRemapLUT = BuildAssetRemapLUT(Asset, LocalToRuntime);
    Resources.SourceBakeGuid = Asset->Derived.Inline.BakedProfileIDData.BakeGuid;
    Resources.RegistryRevision = RegistryRevision;
    return Resources.IsValid() ? &Resources : nullptr;
}


TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe>
UDWCGPUResourceSubsystem::AcquireStaticSlotResources(
    const UWetClothingAsset* Asset,
    const int32 MaterialSlotIndex,
    const FString& BuildSignature,
    const FIntPoint LookupExtent,
    const uint32 TexelCount,
    const uint32 TriangleCount,
    const int32 SectionCount)
{
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE || BuildSignature.IsEmpty() ||
        LookupExtent.X <= 0 || LookupExtent.Y <= 0 || TexelCount == 0 || SectionCount < 0)
    {
        return nullptr;
    }

    const FDWCGPUStaticResourceKey Key(Asset, MaterialSlotIndex, BuildSignature);
    if (TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe>* Existing = StaticSlotResources.Find(Key))
    {
        if (Existing->IsValid() &&
            ((*Existing)->LookupExtent != LookupExtent ||
             (*Existing)->TexelCount != TexelCount ||
             (*Existing)->TriangleCount != TriangleCount ||
             (*Existing)->Sections.Num() != SectionCount))
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC shared GPU resource metadata mismatch for '%s' slot %d signature '%s'. Cached=%dx%d/%u/%u Requested=%dx%d/%u/%u. Replacing the stale cache entry."),
                *GetNameSafe(Asset),
                MaterialSlotIndex,
                *BuildSignature,
                (*Existing)->LookupExtent.X,
                (*Existing)->LookupExtent.Y,
                (*Existing)->TexelCount,
                (*Existing)->TriangleCount,
                LookupExtent.X,
                LookupExtent.Y,
                TexelCount,
                TriangleCount);
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

void UDWCGPUResourceSubsystem::BindGlobalResources(UMaterialInstanceDynamic& MID) const
{
    if (GlobalRenderProfileLUT != nullptr)
    {
        MID.SetTextureParameterValue(DWCWetMaterialParameters::GlobalRenderProfileLUT(), GlobalRenderProfileLUT);
    }
    MID.SetScalarParameterValue(DWCWetMaterialParameters::GlobalRenderProfileTexelSize(), GlobalTexelSize);
    if (DropletNormalArray != nullptr)
    {
        MID.SetTextureParameterValue(DWCWetMaterialParameters::DropletNormalTextureArray(), DropletNormalArray);
    }
    if (RivuletNormalArray != nullptr)
    {
        MID.SetTextureParameterValue(DWCWetMaterialParameters::RivuletNormalTextureArray(), RivuletNormalArray);
    }
}

void UDWCGPUResourceSubsystem::ApplyFallbackRenderProfileParameters(
    UMaterialInstanceDynamic& MID,
    const UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex)
{
    FWetClothingLocalRenderProfile Profile;
    ResolveFallbackRenderProfile(WetClothingAsset, MaterialSlotIndex, Profile);

    bool bTextureArraysChanged = false;
    if (WetClothingAsset != nullptr)
    {
        UTexture2D* NeutralNormal =
            WetClothingAsset->Derived.Inline.BakedProfileIDData.NormalizedNeutralSurfaceNormal;
        DropletNormalRegistry.SetNeutral(NeutralNormal, bTextureArraysChanged);
        RivuletNormalRegistry.SetNeutral(NeutralNormal, bTextureArraysChanged);
    }
    const FSurfaceWaterProfileParameters& Surface = Profile.Parameters.SurfaceWater;
    const FFallbackRenderProfileSlices Slices{
        Surface.bEnabled && Surface.bEnableDroplets
            ? DropletNormalRegistry.FindOrAdd(Profile.NormalizedDropletNormal, bTextureArraysChanged)
            : 0,
        Surface.bEnabled && Surface.bEnableRivulets
            ? RivuletNormalRegistry.FindOrAdd(Profile.NormalizedRivuletNormal, bTextureArraysChanged)
            : 0};

    if (bTextureArraysChanged)
    {
        bTextureArraysDirty = true;
        RebuildTextureArrays();
        bTextureArraysDirty = false;
    }

    for (int32 TexelIndex = 0; TexelIndex < UDWCGPUResourceSubsystem::TexelsPerProfile; ++TexelIndex)
    {
        MID.SetVectorParameterValue(
            DWCWetMaterialParameters::FallbackRenderProfileTexel(TexelIndex),
            MakeFallbackRenderProfileTexel(Profile, Slices, TexelIndex));
    }
}

void UDWCGPUResourceSubsystem::ApplyResourcesToMaterials(
    UWetClothingAsset* Asset,
    const TArray<TObjectPtr<UMaterialInstanceDynamic>>& MaterialInstances)
{
    EnsureNeutralResources();
    const FDWCAssetRenderProfileResources* Resources = AcquireAssetResources(Asset);
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialInstances.Num(); ++MaterialSlotIndex)
    {
        UMaterialInstanceDynamic* MID = MaterialInstances[MaterialSlotIndex];
        if (MID == nullptr)
        {
            continue;
        }

        ApplyFallbackRenderProfileParameters(*MID, Asset, MaterialSlotIndex);
        const bool bUseRenderProfileLUT =
            Resources != nullptr &&
            Resources->ProfileRemapLUT != nullptr &&
            Resources->FindProfileIDTexture(MaterialSlotIndex) != nullptr &&
            GlobalRenderProfileLUT != nullptr;
        MID->SetScalarParameterValue(
            DWCWetMaterialParameters::UseRenderProfileLUT(),
            bUseRenderProfileLUT ? 1.0f : 0.0f);
        MID->SetTextureParameterValue(
            DWCWetMaterialParameters::ProfileIDTexture(),
            Resources != nullptr && Resources->FindProfileIDTexture(MaterialSlotIndex) != nullptr
                ? Resources->FindProfileIDTexture(MaterialSlotIndex)
                : NeutralProfileIDTexture.Get());
        MID->SetTextureParameterValue(
            DWCWetMaterialParameters::ProfileRemapLUT(),
            Resources != nullptr && Resources->ProfileRemapLUT != nullptr
                ? Resources->ProfileRemapLUT.Get()
                : NeutralProfileRemapLUT.Get());
        BindGlobalResources(*MID);
    }
}
