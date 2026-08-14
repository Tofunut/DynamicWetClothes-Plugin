// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Assets/DWCEditorArtifactStore.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCEditorArtifactStore, Log, All);

namespace
{
    uint64 GetSourceBytesPerPixel(const ETextureSourceFormat Format)
    {
        switch (Format)
        {
        case TSF_G8: return 1;
        case TSF_G16:
        case TSF_R16F: return 2;
        case TSF_BGRA8: return 4;
        default: return 0;
        }
    }

    bool TryMultiply(const uint64 A, const uint64 B, uint64& OutValue)
    {
        if (A != 0 && B > MAX_uint64 / A)
        {
            return false;
        }
        OutValue = A * B;
        return true;
    }
}

FString FDWCEditorArtifactTextureRequest::GetObjectPath() const
{
    return PackageName.IsEmpty() || AssetName.IsEmpty()
        ? FString()
        : PackageName + TEXT(".") + AssetName;
}

TSharedRef<FDWCEditorArtifactStore> FDWCEditorArtifactStore::Get()
{
    static TSharedRef<FDWCEditorArtifactStore> Store =
        MakeShareable(new FDWCEditorArtifactStore(
            FDWCEditorResourceBroker::Get()->GetResourceGovernor()));
    return Store;
}

TSharedRef<FDWCEditorArtifactStore> FDWCEditorArtifactStore::CreateForTesting(
    TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor)
{
    return MakeShareable(new FDWCEditorArtifactStore(InResourceGovernor));
}

FDWCEditorArtifactStore::FDWCEditorArtifactStore(
    TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor)
    : ResourceGovernor(InResourceGovernor)
    , SessionEpoch(FGuid::NewGuid())
{
}

bool FDWCEditorArtifactStore::CommitTextureBatch(
    const TConstArrayView<FDWCEditorArtifactTextureRequest> Requests,
    TArray<FDWCEditorArtifactCommitReceipt>& OutReceipts,
    FString& OutError)
{
    check(IsInGameThread());
    PruneExpiredTracking();
    OutReceipts.Reset();
    OutError.Reset();
    if (Requests.IsEmpty())
    {
        OutError = TEXT("An artifact commit batch must contain at least one texture.");
        return false;
    }

    struct FPreflightTarget
    {
        UTexture2D* Texture = nullptr;
        uint64 SourceBytes = 0;
    };
    TArray<FPreflightTarget> Targets;
    Targets.Reserve(Requests.Num());
    TSet<FString> ObjectPaths;
    uint64 PeakCommitBytes = 1;

    for (const FDWCEditorArtifactTextureRequest& Request : Requests)
    {
        const FString ObjectPath = Request.GetObjectPath();
        const uint64 BytesPerPixel = GetSourceBytesPerPixel(Request.SourceFormat);
        uint64 PixelCount = 0;
        uint64 ExpectedBytes = 0;
        if (Request.OwnerAsset == nullptr || Request.Resolution.X <= 0 ||
            Request.Resolution.Y <= 0 || BytesPerPixel == 0 ||
            (Request.PixelData == nullptr && !Request.SourceWriter) || ObjectPath.IsEmpty() ||
            !TryMultiply(static_cast<uint64>(Request.Resolution.X),
                static_cast<uint64>(Request.Resolution.Y), PixelCount) ||
            !TryMultiply(PixelCount, BytesPerPixel, ExpectedBytes) ||
            ExpectedBytes != Request.PixelBytes)
        {
            OutError = FString::Printf(
                TEXT("Artifact '%s' has an invalid texture payload contract."),
                *ObjectPath);
            ++Diagnostics.FailedCommitCount;
            return false;
        }
        if (ObjectPaths.Contains(ObjectPath))
        {
            OutError = FString::Printf(
                TEXT("Artifact commit batch contains duplicate path '%s'."), *ObjectPath);
            ++Diagnostics.FailedCommitCount;
            return false;
        }
        ObjectPaths.Add(ObjectPath);

        UTexture2D* Texture = Request.ExistingTexture;
        if (Texture != nullptr && Texture->GetPathName() != ObjectPath)
        {
            Texture = nullptr;
        }
        if (Texture == nullptr)
        {
            UObject* Existing = LoadObject<UObject>(
                nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (Existing != nullptr && !Existing->IsA<UTexture2D>())
            {
                OutError = FString::Printf(
                    TEXT("Generated artifact path '%s' is occupied by '%s'."),
                    *ObjectPath, *GetNameSafe(Existing->GetClass()));
                ++Diagnostics.FailedCommitCount;
                return false;
            }
            Texture = Cast<UTexture2D>(Existing);
        }
        if (Texture != nullptr)
        {
            FGuid OwnerGuid;
            const bool bHasOwner = Request.OwnerAsset->TryGetGeneratedAssetOwnerGuid(
                Texture, OwnerGuid);
            if ((bHasOwner && OwnerGuid != Request.OwnerAsset->GetAssetGuid()) ||
                (!bHasOwner && !Request.bExistingReferenceIsTrusted))
            {
                OutError = FString::Printf(
                    TEXT("Generated artifact '%s' is not owned by this WCA."),
                    *ObjectPath);
                ++Diagnostics.FailedCommitCount;
                return false;
            }
        }

        Targets.Add({Texture, ExpectedBytes});
        const uint64 CommitBytes = ExpectedBytes <= MAX_uint64 / 2
            ? ExpectedBytes * 2
            : MAX_uint64;
        PeakCommitBytes = FMath::Max(PeakCommitBytes, CommitBytes);
    }

    FDWCEditorResourceReservationRequest Reservation;
    Reservation.Pool = EDWCEditorResourcePool::AssetCommitCPU;
    Reservation.Bytes = PeakCommitBytes;
    Reservation.Owner.Key.Namespace = TEXT("DWC.AssetCommit");
    Reservation.Owner.SessionEpoch = SessionEpoch;
    Reservation.Owner.OperationId = NextOperationId++;
    Reservation.Owner.Generation = 1;
    Reservation.DebugName = Requests.Num() == 1
        ? Requests[0].DebugName
        : FString::Printf(TEXT("Artifact batch (%d textures)"), Requests.Num());
    FString ReservationError;
    FDWCEditorMemoryLease CommitLease = ResourceGovernor->TryAcquire(
        Reservation, &ReservationError);
    if (!CommitLease.IsValid())
    {
        OutError = FString::Printf(
            TEXT("The editor could not reserve %.2f MiB for asset commit. %s"),
            static_cast<double>(PeakCommitBytes) /
                FDWCEditorResourceBudgetConfig::MiB,
            *ReservationError);
        ++Diagnostics.FailedCommitCount;
        return false;
    }
    Diagnostics.PeakCommitReservationBytes = FMath::Max(
        Diagnostics.PeakCommitReservationBytes, PeakCommitBytes);

    OutReceipts.Reserve(Requests.Num());
    for (int32 Index = 0; Index < Requests.Num(); ++Index)
    {
        const FDWCEditorArtifactTextureRequest& Request = Requests[Index];
        UTexture2D* Texture = Targets[Index].Texture;
        const bool bCreated = Texture == nullptr;
        UPackage* Package = nullptr;
        if (bCreated)
        {
            Package = CreatePackage(*Request.PackageName);
            Texture = Package != nullptr
                ? NewObject<UTexture2D>(
                    Package, *Request.AssetName,
                    RF_Public | RF_Standalone | RF_Transactional)
                : nullptr;
        }
        else
        {
            Package = Texture->GetOutermost();
        }
        if (Texture == nullptr || Package == nullptr ||
            !Request.OwnerAsset->TagGeneratedAsset(Texture))
        {
            OutError = FString::Printf(
                TEXT("Could not create or claim generated artifact '%s'."),
                *Request.GetObjectPath());
            ++Diagnostics.FailedCommitCount;
            OutReceipts.Reset();
            return false;
        }
        if (Request.Lifetime == EDWCEditorArtifactLifetime::EditorIntermediate)
        {
            Package->SetPackageFlags(PKG_EditorOnly);
        }
        else
        {
            Package->ClearPackageFlags(PKG_EditorOnly);
        }
        Texture->Modify();

        if (Request.SourceWriter)
        {
            Texture->Source.Init(
                Request.Resolution.X, Request.Resolution.Y, 1, 1,
                Request.SourceFormat);
            uint8* Destination = Texture->Source.LockMip(0);
            if (Destination == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("Could not lock generated artifact source '%s'."),
                    *Request.GetObjectPath());
                ++Diagnostics.FailedCommitCount;
                OutReceipts.Reset();
                return false;
            }
            Request.SourceWriter(Destination, Targets[Index].SourceBytes);
            Texture->Source.UnlockMip(0);
        }
        else
        {
            Texture->Source.Init(
                Request.Resolution.X, Request.Resolution.Y, 1, 1,
                Request.SourceFormat, Request.PixelData);
        }
        Texture->CompressionSettings = Request.Settings.CompressionSettings;
        Texture->MipGenSettings = Request.Settings.MipGenSettings;
        Texture->LODGroup = Request.Settings.LODGroup;
        Texture->Filter = Request.Settings.Filter;
        Texture->AddressX = Request.Settings.AddressX;
        Texture->AddressY = Request.Settings.AddressY;
        Texture->SRGB = Request.Settings.bSRGB;
        Texture->NeverStream = Request.Settings.bNeverStream;
        Texture->VirtualTextureStreaming = Request.Settings.bVirtualTextureStreaming;
        Texture->CompressionNoAlpha = Request.Settings.bCompressionNoAlpha;
        Texture->bFlipGreenChannel = Request.Settings.bFlipGreenChannel;
        Texture->PostEditChange();
        Texture->MarkPackageDirty();
        Package->MarkPackageDirty();
        if (bCreated)
        {
            FAssetRegistryModule::AssetCreated(Texture);
        }

        FDWCEditorArtifactCommitReceipt& Receipt = OutReceipts.AddDefaulted_GetRef();
        Receipt.Texture = Texture;
        Receipt.TextureSourceId = Texture->Source.GetId();
        Receipt.ObjectPath = Request.GetObjectPath();
        Receipt.SourceBytes = Targets[Index].SourceBytes;
        Receipt.bCreated = bCreated;

        FTrackedArtifact& Tracked = Artifacts.FindOrAdd(Receipt.ObjectPath);
        Tracked.Texture = Texture;
        Tracked.OwnerGuid = Request.OwnerAsset->GetAssetGuid();
        Tracked.Lifetime = Request.Lifetime;
        Tracked.SourceBytes = Receipt.SourceBytes;
        Tracked.LastUseSerial = NextUseSerial++;
        Tracked.bDirty = true;
    }

    ++Diagnostics.CommitCount;
    RefreshTrackingDiagnostics();
    return true;
}

void FDWCEditorArtifactStore::CollectDirtyPackages(
    const UWetClothingAsset& OwnerAsset,
    TArray<UPackage*>& InOutPackages) const
{
    check(IsInGameThread());
    for (const TPair<FString, FTrackedArtifact>& Pair : Artifacts)
    {
        const FTrackedArtifact& Artifact = Pair.Value;
        UTexture2D* Texture = Artifact.Texture.Get();
        if (Artifact.bDirty && Artifact.OwnerGuid == OwnerAsset.GetAssetGuid() &&
            Texture != nullptr && Texture->GetOutermost() != nullptr &&
            Texture->GetOutermost()->IsDirty())
        {
            InOutPackages.AddUnique(Texture->GetOutermost());
        }
    }
}

void FDWCEditorArtifactStore::NotifyPackagesSaved(
    const TConstArrayView<UPackage*> Packages)
{
    check(IsInGameThread());
    TSet<UPackage*> SavedPackages;
    SavedPackages.Reserve(Packages.Num());
    for (UPackage* Package : Packages)
    {
        if (Package != nullptr)
        {
            SavedPackages.Add(Package);
        }
    }
    for (TPair<FString, FTrackedArtifact>& Pair : Artifacts)
    {
        UTexture2D* Texture = Pair.Value.Texture.Get();
        if (Texture != nullptr && SavedPackages.Contains(Texture->GetOutermost()))
        {
            Pair.Value.bDirty = false;
        }
    }
    PruneExpiredTracking();
}

FDWCEditorArtifactStoreDiagnostics FDWCEditorArtifactStore::GetDiagnostics() const
{
    check(IsInGameThread());
    return Diagnostics;
}

void FDWCEditorArtifactStore::PruneExpiredTracking()
{
    check(IsInGameThread());
    int32 Removed = 0;
    for (auto It = Artifacts.CreateIterator(); It; ++It)
    {
        if (!It.Value().Texture.IsValid())
        {
            It.RemoveCurrent();
            ++Removed;
        }
    }
    Diagnostics.ExpiredTrackingPruneCount += static_cast<uint64>(Removed);
    RefreshTrackingDiagnostics();
}

void FDWCEditorArtifactStore::RefreshTrackingDiagnostics()
{
    Diagnostics.TrackedArtifactCount = Artifacts.Num();
    Diagnostics.DirtyArtifactCount = 0;
    Diagnostics.TrackedSourceBytes = 0;
    for (const TPair<FString, FTrackedArtifact>& Pair : Artifacts)
    {
        Diagnostics.DirtyArtifactCount += Pair.Value.bDirty ? 1 : 0;
        Diagnostics.TrackedSourceBytes += Pair.Value.SourceBytes;
    }
}
