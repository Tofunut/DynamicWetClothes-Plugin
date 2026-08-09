//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialColorBakeCache.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCTransparencyMaterialBake, Log, All);

namespace
{
    struct FCacheEntry
    {
        TSharedPtr<const FDWCTransparencyMaterialColorBakeResult> Result;
        uint64 LastUsedSerial = 0;
    };

    TMap<FDWCTransparencyMaterialColorBakeKey, FCacheEntry> GMaterialColorCache;
    uint64 GUseSerial = 0;
    constexpr uint64 DefaultCacheBudgetBytes = 128ull * 1024ull * 1024ull;

    struct FSessionBudgetContext
    {
        TWeakPtr<FDWCEditorResourceGovernor> ResourceGovernor;
        FDWCEditorAsyncOperationIdentity MemoryOwner;
        uint64 CacheBudgetBytes = DefaultCacheBudgetBytes;
    };

    TMap<FObjectKey, FSessionBudgetContext> GSessionBudgetContexts;

    uint64 CalculateResidentBytes()
    {
        uint64 Total = 0;
        for (const TPair<FDWCTransparencyMaterialColorBakeKey, FCacheEntry>& Pair : GMaterialColorCache)
        {
            if (Pair.Value.Result.IsValid())
            {
                Total += Pair.Value.Result->AllocatedBytes;
            }
        }
        return Total;
    }

    uint64 CalculateResidentBytes(const FSoftObjectPath& OwnerAssetPath)
    {
        uint64 Total = 0;
        for (const TPair<FDWCTransparencyMaterialColorBakeKey, FCacheEntry>& Pair : GMaterialColorCache)
        {
            if (Pair.Key.OwnerAssetPath == OwnerAssetPath && Pair.Value.Result.IsValid())
            {
                Total += Pair.Value.Result->AllocatedBytes;
            }
        }
        return Total;
    }

    void TrimCache(
        const FDWCTransparencyMaterialColorBakeKey& ProtectedKey,
        const uint64 CacheBudgetBytes)
    {
        while (CalculateResidentBytes(ProtectedKey.OwnerAssetPath) > CacheBudgetBytes)
        {
            const FDWCTransparencyMaterialColorBakeKey* OldestKey = nullptr;
            uint64 OldestSerial = MAX_uint64;
            for (const TPair<FDWCTransparencyMaterialColorBakeKey, FCacheEntry>& Pair : GMaterialColorCache)
            {
                if (Pair.Key.OwnerAssetPath == ProtectedKey.OwnerAssetPath &&
                    !(Pair.Key == ProtectedKey) && Pair.Value.LastUsedSerial < OldestSerial)
                {
                    OldestKey = &Pair.Key;
                    OldestSerial = Pair.Value.LastUsedSerial;
                }
            }
            if (OldestKey == nullptr)
            {
                break;
            }
            GMaterialColorCache.Remove(*OldestKey);
        }
    }

    bool MakeReadback(
        TArray<FColor>&& Pixels,
        const FIntPoint Resolution,
        const bool bSRGB,
        FWetClothingTextureReadback& OutReadback)
    {
        if (Resolution.X <= 0 || Resolution.Y <= 0 || Pixels.Num() != Resolution.X * Resolution.Y)
        {
            return false;
        }
        TSharedPtr<TArray64<uint8>> RawData = MakeShared<TArray64<uint8>>();
        RawData->SetNumUninitialized(static_cast<int64>(Pixels.Num()) * sizeof(FColor));
        FMemory::Memcpy(RawData->GetData(), Pixels.GetData(), RawData->Num());
        OutReadback.Width = Resolution.X;
        OutReadback.Height = Resolution.Y;
        OutReadback.BytesPerPixel = sizeof(FColor);
        OutReadback.bSRGB = bSRGB;
        OutReadback.Format = TSF_BGRA8;
        OutReadback.AddressX = TA_Clamp;
        OutReadback.AddressY = TA_Clamp;
        OutReadback.RawData = MoveTemp(RawData);
        return OutReadback.IsValid();
    }

    bool BakeMaterialColor(
        USkeletalMesh& SourceMesh,
        UMaterialInterface& Material,
        const FTransform& BakeTransform,
        const int32 MaterialSlotIndex,
        const int32 SourceUVChannel,
        const int32 Resolution,
        TArray<FColor>& OutPixels,
        bool& bOutSRGB,
        FString& OutError)
    {
        check(IsInGameThread());
        FMeshDescription* MeshDescription = SourceMesh.GetMeshDescription(0);
        if (MeshDescription == nullptr)
        {
            OutError = FString::Printf(
                TEXT("Source mesh '%s' has no editable LOD 0 MeshDescription for material color baking."),
                *SourceMesh.GetName());
            return false;
        }

        FMaterialData MaterialData;
        MaterialData.Material = &Material;
        MaterialData.PropertySizes.Add(MP_BaseColor, FIntPoint(Resolution, Resolution));
        MaterialData.bPerformBorderSmear = true;
        MaterialData.bPerformShrinking = false;
        MaterialData.BlendMode = BLEND_Opaque;
        MaterialData.BackgroundColor = FColor::Black;

        FMeshData MeshData;
        MeshData.MeshDescription = MeshDescription;
        MeshData.MaterialIndices.Add(MaterialSlotIndex);
        MeshData.TextureCoordinateIndex = SourceUVChannel;
        MeshData.TextureCoordinateBox = FBox2D(FVector2D::ZeroVector, FVector2D(1.0, 1.0));
        FPrimitiveData PrimitiveData(&SourceMesh);
        PrimitiveData.LocalToWorld = BakeTransform.ToMatrixWithScale();
        PrimitiveData.ActorPosition = BakeTransform.GetTranslation();
        PrimitiveData.WorldBounds = PrimitiveData.LocalBounds.TransformBy(BakeTransform);
        MeshData.PrimitiveData = PrimitiveData;

        TArray<FMaterialData*> MaterialSettings{&MaterialData};
        TArray<FMeshData*> MeshSettings{&MeshData};
        TArray<FBakeOutput> Outputs;
        IMaterialBakingModule& Module =
            FModuleManager::LoadModuleChecked<IMaterialBakingModule>(TEXT("MaterialBaking"));
        Module.BakeMaterials(MaterialSettings, MeshSettings, Outputs);
        if (Outputs.Num() != 1)
        {
            OutError = TEXT("The engine MaterialBaking module returned no Base Color output.");
            return false;
        }

        FBakeOutput& Output = Outputs[0];
        TArray<FColor>* PropertyPixels = Output.PropertyData.Find(MP_BaseColor);
        const FIntPoint* PropertySize = Output.PropertySizes.Find(MP_BaseColor);
        if (PropertyPixels == nullptr || PropertySize == nullptr ||
            *PropertySize != FIntPoint(Resolution, Resolution) ||
            PropertyPixels->Num() != Resolution * Resolution)
        {
            OutError = TEXT("The engine MaterialBaking module returned an incomplete Base Color texture.");
            return false;
        }
        for (FColor& Pixel : *PropertyPixels)
        {
            Pixel.A = 255;
        }
        const bool* bLinear = Output.PropertyIsLinearColor.Find(MP_BaseColor);
        bOutSRGB = bLinear == nullptr || !*bLinear;
        OutPixels = MoveTemp(*PropertyPixels);
        return true;
    }
}

void FDWCTransparencyMaterialColorBakeCache::ConfigureResourceGovernor(
    UWetClothingAsset& Asset,
    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor,
    const FGuid& SessionEpoch,
    const uint64 InCacheBudgetBytes)
{
    check(IsInGameThread());
    Clear(&Asset);
    FSessionBudgetContext& Context = GSessionBudgetContexts.Add(FObjectKey(&Asset));
    Context.ResourceGovernor = ResourceGovernor;
    Context.CacheBudgetBytes = FMath::Max<uint64>(InCacheBudgetBytes, 1);
    Context.MemoryOwner.Key.Namespace = TEXT("DWC.TransparencyMaterialColorCache");
    Context.MemoryOwner.SessionEpoch = SessionEpoch.IsValid() ? SessionEpoch : FGuid::NewGuid();
    Context.MemoryOwner.OperationId = 1;
    Context.MemoryOwner.Generation = 1;
}

bool FDWCTransparencyMaterialColorBakeKey::IsValid() const
{
    return SourceMeshPath.IsValid() && MaterialSlotIndex != INDEX_NONE &&
        SourceUVChannel >= 0 && Resolution > 0 && !MaterialBakeSignature.IsEmpty();
}

bool FDWCTransparencyMaterialColorBakeKey::operator==(
    const FDWCTransparencyMaterialColorBakeKey& Other) const
{
    return SourceMeshPath == Other.SourceMeshPath &&
        OwnerAssetPath == Other.OwnerAssetPath &&
        MaterialSlotIndex == Other.MaterialSlotIndex &&
        SourceUVChannel == Other.SourceUVChannel && Resolution == Other.Resolution &&
        MaterialBakeSignature == Other.MaterialBakeSignature;
}

uint32 GetTypeHash(const FDWCTransparencyMaterialColorBakeKey& Key)
{
    uint32 Hash = GetTypeHash(Key.OwnerAssetPath);
    Hash = HashCombineFast(Hash, GetTypeHash(Key.SourceMeshPath));
    Hash = HashCombineFast(Hash, GetTypeHash(Key.MaterialSlotIndex));
    Hash = HashCombineFast(Hash, GetTypeHash(Key.SourceUVChannel));
    Hash = HashCombineFast(Hash, GetTypeHash(Key.Resolution));
    return HashCombineFast(Hash, GetTypeHash(Key.MaterialBakeSignature));
}

TSharedPtr<const FDWCTransparencyMaterialColorBakeResult>
FDWCTransparencyMaterialColorBakeCache::ResolveOrBake(
    UWetClothingAsset& Asset,
    USkeletalMesh& SourceMesh,
    const int32 MaterialSlotIndex,
    const int32 SourceUVChannel,
    const int32 Resolution,
    FString& OutError)
{
    check(IsInGameThread());
    OutError.Reset();
    if (!SourceMesh.GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        OutError = FString::Printf(TEXT("Source material slot %d is unavailable."), MaterialSlotIndex);
        return nullptr;
    }
    UMaterialInterface* Material = SourceMesh.GetMaterials()[MaterialSlotIndex].MaterialInterface;
    if (Material == nullptr)
    {
        OutError = FString::Printf(TEXT("Source material slot %d has no material."), MaterialSlotIndex);
        return nullptr;
    }

    return ResolveOrBake(
        Asset, SourceMesh, *Material, FTransform::Identity, MaterialSlotIndex,
        SourceUVChannel, Resolution, OutError);
}

TSharedPtr<const FDWCTransparencyMaterialColorBakeResult>
FDWCTransparencyMaterialColorBakeCache::ResolveOrBake(
    UWetClothingAsset& Asset,
    USkeletalMesh& SourceMesh,
    UMaterialInterface& EffectiveMaterial,
    const FTransform& BakeTransform,
    const int32 MaterialSlotIndex,
    const int32 SourceUVChannel,
    const int32 Resolution,
    FString& OutError)
{
    check(IsInGameThread());
    OutError.Reset();
    if (MaterialSlotIndex == INDEX_NONE)
    {
        OutError = TEXT("A valid source material slot is required.");
        return nullptr;
    }

    FDWCTransparencyMaterialColorBakeKey Key;
    Key.SourceMeshPath = FSoftObjectPath(&SourceMesh);
    Key.OwnerAssetPath = FSoftObjectPath(&Asset);
    Key.MaterialSlotIndex = MaterialSlotIndex;
    Key.SourceUVChannel = SourceUVChannel;
    Key.Resolution = Resolution;
    Key.MaterialBakeSignature = FDWCTransparencySignatureService::BuildMaterialBakeSignature(
        &EffectiveMaterial, SourceUVChannel, Resolution) + FString::Printf(
            TEXT("|Placement=%s"), *BakeTransform.ToHumanReadableString());
    if (!Key.IsValid())
    {
        OutError = TEXT("Could not build a valid source material color cache key.");
        return nullptr;
    }

    if (FCacheEntry* Existing = GMaterialColorCache.Find(Key))
    {
        Existing->LastUsedSerial = ++GUseSerial;
        UE_LOG(
            LogDWCTransparencyMaterialBake, VeryVerbose,
            TEXT("Reused resident source material color for '%s' slot %d UV%d at %d."),
            *SourceMesh.GetName(), MaterialSlotIndex, SourceUVChannel, Resolution);
        return Existing->Result;
    }

    TSharedPtr<FDWCTransparencyMaterialColorBakeResult> Result =
        MakeShared<FDWCTransparencyMaterialColorBakeResult>();
    Result->Key = Key;
    if (UTexture2D* PersistentTexture =
            FDWCTransparencyTempAssetStore::FindCurrentSourceMaterialColor(
                Asset, SourceMesh, MaterialSlotIndex, SourceUVChannel, Resolution,
                Key.MaterialBakeSignature, true))
    {
        if (FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
                PersistentTexture, Result->TextureData, OutError))
        {
            if (Result->TextureData.Width == Resolution && Result->TextureData.Height == Resolution)
            {
                Result->AllocatedBytes = Result->TextureData.RawData->GetAllocatedSize();
                Result->bLoadedFromPersistentCache = true;
                UE_LOG(
                    LogDWCTransparencyMaterialBake, Verbose,
                    TEXT("Loaded persistent source material color for '%s' slot %d UV%d at %d."),
                    *SourceMesh.GetName(), MaterialSlotIndex, SourceUVChannel, Resolution);
            }
            else
            {
                Result->TextureData = FWetClothingTextureReadback();
            }
        }
    }

    if (!Result->TextureData.IsValid())
    {
        const double BakeStartSeconds = FPlatformTime::Seconds();
        TArray<FColor> Pixels;
        bool bSRGB = true;
        if (!BakeMaterialColor(
                SourceMesh, EffectiveMaterial, BakeTransform, MaterialSlotIndex, SourceUVChannel,
                Resolution, Pixels, bSRGB, OutError))
        {
            return nullptr;
        }
        UTexture2D* PersistentTexture = nullptr;
        if (!FDWCTransparencyTempAssetStore::CommitSourceMaterialColor(
                Asset, SourceMesh, MaterialSlotIndex, SourceUVChannel,
                FIntPoint(Resolution, Resolution), Key.MaterialBakeSignature,
                Pixels, bSRGB, PersistentTexture, OutError))
        {
            return nullptr;
        }
        if (!MakeReadback(MoveTemp(Pixels), FIntPoint(Resolution, Resolution), bSRGB, Result->TextureData))
        {
            OutError = TEXT("Could not create an immutable readback for the baked source material color.");
            return nullptr;
        }
        Result->AllocatedBytes = Result->TextureData.RawData->GetAllocatedSize();
        UE_LOG(
            LogDWCTransparencyMaterialBake, Display,
            TEXT("Baked source material color for '%s' slot %d UV%d at %d in %.1f ms (%llu bytes)."),
            *SourceMesh.GetName(), MaterialSlotIndex, SourceUVChannel, Resolution,
            (FPlatformTime::Seconds() - BakeStartSeconds) * 1000.0,
            Result->AllocatedBytes);
    }

    const FSessionBudgetContext* SessionContext = GSessionBudgetContexts.Find(FObjectKey(&Asset));
    if (const TSharedPtr<FDWCEditorResourceGovernor> Governor =
            SessionContext != nullptr ? SessionContext->ResourceGovernor.Pin() : nullptr)
    {
        FDWCEditorResourceReservationRequest Request;
        Request.Pool = EDWCEditorResourcePool::SharedCacheCPU;
        Request.Bytes = FMath::Max<uint64>(Result->AllocatedBytes, 1);
        Request.Owner = SessionContext->MemoryOwner;
        Request.DebugName = TEXT("Transparency material color cache");
        FDWCEditorMemoryLease Lease = Governor->TryAcquire(Request);
        while (!Lease.IsValid() && !GMaterialColorCache.IsEmpty())
        {
            const FDWCTransparencyMaterialColorBakeKey* OldestKey = nullptr;
            uint64 OldestSerial = MAX_uint64;
            for (const TPair<FDWCTransparencyMaterialColorBakeKey, FCacheEntry>& Pair : GMaterialColorCache)
            {
                if (Pair.Key.OwnerAssetPath == Key.OwnerAssetPath &&
                    Pair.Value.LastUsedSerial < OldestSerial)
                {
                    OldestKey = &Pair.Key;
                    OldestSerial = Pair.Value.LastUsedSerial;
                }
            }
            if (OldestKey == nullptr)
            {
                break;
            }
            const FDWCTransparencyMaterialColorBakeKey KeyToRemove = *OldestKey;
            GMaterialColorCache.Remove(KeyToRemove);
            Lease = Governor->TryAcquire(Request);
        }
        if (!Lease.IsValid())
        {
            OutError = TEXT("The shared editor cache budget cannot retain the material color result.");
            return nullptr;
        }
        Result->MemoryLease = MakeShared<FDWCEditorMemoryLease, ESPMode::ThreadSafe>(MoveTemp(Lease));
    }

    FCacheEntry& Entry = GMaterialColorCache.Add(Key);
    Entry.Result = Result;
    Entry.LastUsedSerial = ++GUseSerial;
    TrimCache(Key, SessionContext != nullptr ? SessionContext->CacheBudgetBytes : DefaultCacheBudgetBytes);
    OutError.Reset();
    return Result;
}

void FDWCTransparencyMaterialColorBakeCache::InvalidateMesh(const USkeletalMesh* SourceMesh)
{
    if (SourceMesh == nullptr)
    {
        return;
    }
    const FSoftObjectPath Path(SourceMesh);
    for (auto It = GMaterialColorCache.CreateIterator(); It; ++It)
    {
        if (It.Key().SourceMeshPath == Path)
        {
            It.RemoveCurrent();
        }
    }
}

void FDWCTransparencyMaterialColorBakeCache::Clear()
{
    GMaterialColorCache.Reset();
    GSessionBudgetContexts.Reset();
}

void FDWCTransparencyMaterialColorBakeCache::Clear(const UWetClothingAsset* Asset)
{
    if (Asset == nullptr)
    {
        return;
    }
    const FSoftObjectPath AssetPath(Asset);
    for (auto It = GMaterialColorCache.CreateIterator(); It; ++It)
    {
        if (It.Key().OwnerAssetPath == AssetPath)
        {
            It.RemoveCurrent();
        }
    }
    GSessionBudgetContexts.Remove(FObjectKey(Asset));
}
