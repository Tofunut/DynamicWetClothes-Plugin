// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureDefines.h"

class FDWCEditorResourceGovernor;
class UPackage;
class UTexture2D;
class UWetClothingAsset;

enum class EDWCEditorArtifactLifetime : uint8
{
    RuntimeFinal,
    EditorIntermediate
};

struct FDWCEditorArtifactTextureSettings
{
    TextureCompressionSettings CompressionSettings = TC_Default;
    TextureMipGenSettings MipGenSettings = TMGS_NoMipmaps;
    TextureGroup LODGroup = TEXTUREGROUP_Pixels2D;
    TextureFilter Filter = TF_Default;
    TextureAddress AddressX = TA_Clamp;
    TextureAddress AddressY = TA_Clamp;
    bool bSRGB = false;
    bool bNeverStream = false;
    bool bVirtualTextureStreaming = false;
    bool bCompressionNoAlpha = false;
    bool bFlipGreenChannel = false;
};

/** A synchronous, non-owning payload used only for the duration of CommitTextureBatch. */
struct FDWCEditorArtifactTextureRequest
{
    UWetClothingAsset* OwnerAsset = nullptr;
    FString PackageName;
    FString AssetName;
    UTexture2D* ExistingTexture = nullptr;
    bool bExistingReferenceIsTrusted = false;
    EDWCEditorArtifactLifetime Lifetime = EDWCEditorArtifactLifetime::RuntimeFinal;
    FIntPoint Resolution = FIntPoint::ZeroValue;
    ETextureSourceFormat SourceFormat = TSF_Invalid;
    const uint8* PixelData = nullptr;
    uint64 PixelBytes = 0;
    /** Optional direct encoder used to avoid a second full-resolution staging array. */
    TFunction<void(uint8* Destination, uint64 DestinationBytes)> SourceWriter;
    FDWCEditorArtifactTextureSettings Settings;
    FString DebugName;

    FString GetObjectPath() const;
};

struct FDWCEditorArtifactCommitReceipt
{
    UTexture2D* Texture = nullptr;
    FGuid TextureSourceId;
    FString ObjectPath;
    uint64 SourceBytes = 0;
    bool bCreated = false;
};

struct FDWCEditorArtifactStoreDiagnostics
{
    int32 TrackedArtifactCount = 0;
    int32 DirtyArtifactCount = 0;
    uint64 TrackedSourceBytes = 0;
    uint64 ExpiredTrackingPruneCount = 0;
    uint64 CommitCount = 0;
    uint64 FailedCommitCount = 0;
    uint64 PeakCommitReservationBytes = 0;
};

/**
 * Process-wide generated-texture commit authority.
 *
 * Pixel payloads and writer captures remain owned by the producing operation. The store preflights
 * a complete batch, reserves only the largest single-artifact commit peak,
 * and publishes no domain metadata; callers publish WCA references only after
 * the complete batch succeeds.
 */
class FDWCEditorArtifactStore final
{
public:
    static TSharedRef<FDWCEditorArtifactStore> Get();
    static TSharedRef<FDWCEditorArtifactStore> CreateForTesting(
        TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor);

    bool CommitTextureBatch(
        TConstArrayView<FDWCEditorArtifactTextureRequest> Requests,
        TArray<FDWCEditorArtifactCommitReceipt>& OutReceipts,
        FString& OutError);

    void CollectDirtyPackages(
        const UWetClothingAsset& OwnerAsset,
        TArray<UPackage*>& InOutPackages) const;
    void NotifyPackagesSaved(TConstArrayView<UPackage*> Packages);
    FDWCEditorArtifactStoreDiagnostics GetDiagnostics() const;

private:
    explicit FDWCEditorArtifactStore(
        TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor);

    void PruneExpiredTracking();
    void RefreshTrackingDiagnostics();

    struct FTrackedArtifact
    {
        TWeakObjectPtr<UTexture2D> Texture;
        FGuid OwnerGuid;
        EDWCEditorArtifactLifetime Lifetime = EDWCEditorArtifactLifetime::RuntimeFinal;
        uint64 SourceBytes = 0;
        uint64 LastUseSerial = 0;
        bool bDirty = false;
    };

    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
    FGuid SessionEpoch;
    uint64 NextOperationId = 1;
    uint64 NextUseSerial = 1;
    TMap<FString, FTrackedArtifact> Artifacts;
    FDWCEditorArtifactStoreDiagnostics Diagnostics;
};
