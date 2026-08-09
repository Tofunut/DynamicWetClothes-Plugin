//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyIntermediateAssetPolicy.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "Math/Float16.h"
#include "Misc/SecureHash.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeUtilities.h"

namespace
{
    FString BuildSourceMaterialColorAssetName(
        const UWetClothingAsset& Asset,
        const USkeletalMesh& SourceMesh,
        const int32 MaterialSlotIndex,
        const int32 SourceUVChannel)
    {
        const FString MeshHash = FMD5::HashAnsiString(*SourceMesh.GetPathName()).Left(8);
        return FString::Printf(
            TEXT("T_%s_Src%s_Slot%d_UV%d_SourceMaterialColor"),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(Asset.GetName()),
            *MeshHash,
            MaterialSlotIndex,
            SourceUVChannel);
    }

    UTexture2D* FindOrCreateSourceMaterialColorTexture(
        UWetClothingAsset& Asset,
        const USkeletalMesh& SourceMesh,
        const int32 MaterialSlotIndex,
        const int32 SourceUVChannel,
        FString& OutError)
    {
        const FString PackagePath = FDWCRevealBakeUtilities::GetGeneratedPackagePath(
            Asset, TEXT("Textures/Transparency/Temp"));
        const FString AssetName = BuildSourceMaterialColorAssetName(
            Asset, SourceMesh, MaterialSlotIndex, SourceUVChannel);
        const FString PackageName = PackagePath / AssetName;
        const FString ObjectPath = PackageName + TEXT(".") + AssetName;
        UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath);
        if (Existing != nullptr && !Existing->IsA<UTexture2D>())
        {
            OutError = FString::Printf(
                TEXT("Transparency source-color path '%s' is occupied by '%s'."),
                *ObjectPath,
                *GetNameSafe(Existing->GetClass()));
            return nullptr;
        }

        UTexture2D* Texture = Cast<UTexture2D>(Existing);
        const bool bCreated = Texture == nullptr;
        if (bCreated)
        {
            UPackage* Package = CreatePackage(*PackageName);
            Texture = NewObject<UTexture2D>(
                Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        }
        else
        {
            FGuid OwnerGuid;
            if (!Asset.TryGetGeneratedAssetOwnerGuid(Texture, OwnerGuid) ||
                OwnerGuid != Asset.GetAssetGuid())
            {
                OutError = FString::Printf(
                    TEXT("Transparency source-color artifact '%s' is not owned by this WCA."),
                    *ObjectPath);
                return nullptr;
            }
        }
        if (Texture == nullptr || !Asset.TagGeneratedAsset(Texture))
        {
            OutError = FString::Printf(TEXT("Could not create source-color artifact '%s'."), *ObjectPath);
            return nullptr;
        }
        if (!FDWCTransparencyIntermediateAssetPolicy::EnsureEditorOnlyPackage(
                *Texture, nullptr, &OutError))
        {
            return nullptr;
        }
        Texture->Modify();
        if (bCreated)
        {
            FAssetRegistryModule::AssetCreated(Texture);
        }
        return Texture;
    }

    FString ArtifactToken(const EDWCTransparencyTempArtifactKind Kind)
    {
        switch (Kind)
        {
        case EDWCTransparencyTempArtifactKind::SourceMaterialColor: return TEXT("SourceMaterialColor");
        case EDWCTransparencyTempArtifactKind::BaseRevealColor: return TEXT("BaseRevealColor");
        case EDWCTransparencyTempArtifactKind::ValidHit: return TEXT("ValidHit");
        case EDWCTransparencyTempArtifactKind::HitSource: return TEXT("HitSource");
        case EDWCTransparencyTempArtifactKind::HitDistance: return TEXT("HitDistance");
        case EDWCTransparencyTempArtifactKind::CorrectedRevealColor: return TEXT("CorrectedRevealColor");
        case EDWCTransparencyTempArtifactKind::OuterCoverage: return TEXT("OuterCoverage");
        case EDWCTransparencyTempArtifactKind::OuterIslandID: return TEXT("OuterIslandID");
        default: return TEXT("Unknown");
        }
    }

    FString BuildAssetName(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const EDWCTransparencyTempArtifactKind Kind)
    {
        return FString::Printf(
            TEXT("T_%s_L%s_Slot%d_%s"),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(Asset.GetName()),
            *Layer.LayerGuid.ToString(EGuidFormats::Digits).Left(8),
            Layer.TargetSurface.OuterMaterialSlotIndex,
            *ArtifactToken(Kind));
    }

    UTexture2D* FindOrCreateTexture(
        UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const EDWCTransparencyTempArtifactKind Kind,
        FString& OutError)
    {
        const FString PackagePath = FDWCRevealBakeUtilities::GetGeneratedPackagePath(
            Asset, TEXT("Textures/Transparency/Temp"));
        const FString AssetName = BuildAssetName(Asset, Layer, Kind);
        const FString PackageName = PackagePath / AssetName;
        const FString ObjectPath = PackageName + TEXT(".") + AssetName;

        UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath);
        if (Existing != nullptr && !Existing->IsA<UTexture2D>())
        {
            OutError = FString::Printf(
                TEXT("Transparency Temp path '%s' is occupied by '%s'."),
                *ObjectPath, *GetNameSafe(Existing->GetClass()));
            return nullptr;
        }

        UTexture2D* Texture = Cast<UTexture2D>(Existing);
        bool bCreated = false;
        if (Texture == nullptr)
        {
            UPackage* Package = CreatePackage(*PackageName);
            Texture = NewObject<UTexture2D>(
                Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
            bCreated = Texture != nullptr;
        }
        if (Texture != nullptr && !bCreated)
        {
            FGuid OwnerGuid;
            if (!Asset.TryGetGeneratedAssetOwnerGuid(Texture, OwnerGuid) ||
                OwnerGuid != Asset.GetAssetGuid())
            {
                OutError = FString::Printf(
                    TEXT("Transparency Temp artifact '%s' is not owned by this WCA and will not be overwritten."),
                    *ObjectPath);
                return nullptr;
            }
        }
        if (Texture == nullptr || !Asset.TagGeneratedAsset(Texture))
        {
            OutError = FString::Printf(TEXT("Could not create Temp artifact '%s'."), *ObjectPath);
            return nullptr;
        }
        if (!FDWCTransparencyIntermediateAssetPolicy::EnsureEditorOnlyPackage(
                *Texture, nullptr, &OutError))
        {
            return nullptr;
        }

        Texture->Modify();
        if (bCreated)
        {
            FAssetRegistryModule::AssetCreated(Texture);
        }
        return Texture;
    }

    void ConfigureCommon(UTexture2D& Texture, const bool bSRGB)
    {
        Texture.SRGB = bSRGB;
        Texture.MipGenSettings = TMGS_NoMipmaps;
        Texture.AddressX = TA_Clamp;
        Texture.AddressY = TA_Clamp;
        Texture.LODGroup = TEXTUREGROUP_Pixels2D;
        Texture.PostEditChange();
        Texture.MarkPackageDirty();
        Texture.GetOutermost()->MarkPackageDirty();
    }

    void UpdateReference(
        FWetClothingTransparencyLayerData& Layer,
        const EDWCTransparencyTempArtifactKind Kind,
        UTexture2D* Texture,
        const FString& Signature,
        const FIntPoint Resolution)
    {
#if WITH_EDITORONLY_DATA
        FDWCTransparencyTempArtifactReference* Reference =
            Layer.EditorStageCache.Artifacts.FindByPredicate(
                [Kind](const FDWCTransparencyTempArtifactReference& Candidate)
                {
                    return Candidate.Kind == Kind;
                });
        if (Reference == nullptr)
        {
            Reference = &Layer.EditorStageCache.Artifacts.AddDefaulted_GetRef();
            Reference->Kind = Kind;
        }
        Reference->Texture = Texture;
        Reference->BuildSignature = Signature;
        Reference->Resolution = Resolution;
        Reference->bObsolete = false;
#endif
    }
}

bool FDWCTransparencyTempAssetStore::FindCurrentSourceMaterialColor(
    const UWetClothingAsset& Asset,
    const USkeletalMesh& SourceMesh,
    const int32 MaterialSlotIndex,
    const int32 SourceUVChannel,
    const int32 LogicalResolution,
    const FString& MaterialBakeSignature,
    const bool bLoadIfNeeded,
    FDWCTransparencyMaterialColorCacheReference& OutReference,
    UTexture2D*& OutTexture)
{
    OutReference = FDWCTransparencyMaterialColorCacheReference();
    OutTexture = nullptr;
#if WITH_EDITORONLY_DATA
    const FDWCTransparencyMaterialColorCacheReference* Reference =
        Asset.Authored.TransparencyData.MaterialColorCache.FindByPredicate(
            [&SourceMesh, MaterialSlotIndex, SourceUVChannel, LogicalResolution, &MaterialBakeSignature](
                const FDWCTransparencyMaterialColorCacheReference& Candidate)
            {
                return !Candidate.bObsolete &&
                    Candidate.SourceMesh.ToSoftObjectPath() == FSoftObjectPath(&SourceMesh) &&
                    Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                    Candidate.SourceUVChannel == SourceUVChannel &&
                    Candidate.Resolution == LogicalResolution &&
                    Candidate.MaterialBakeSignature == MaterialBakeSignature;
            });
    if (Reference == nullptr || Reference->Texture.IsNull())
    {
        return false;
    }
    UTexture2D* Texture = bLoadIfNeeded ? Reference->Texture.LoadSynchronous() : Reference->Texture.Get();
    FString PolicyError;
    if (Texture != nullptr)
    {
        // Existing caches remain readable even if they predate the canonical
        // Temp path. New writes are strict; validation reports legacy paths.
        FDWCTransparencyIntermediateAssetPolicy::EnsureEditorOnlyPackage(
            *Texture, nullptr, &PolicyError);
    }
    if (Texture == nullptr)
    {
        return false;
    }
    OutReference = *Reference;
    OutTexture = Texture;
    return true;
#else
    return false;
#endif
}
bool FDWCTransparencyTempAssetStore::CommitSourceMaterialColor(
    UWetClothingAsset& Asset,
    USkeletalMesh& SourceMesh,
    const int32 MaterialSlotIndex,
    const int32 SourceUVChannel,
    const FIntPoint LogicalResolution,
    const FIntPoint PhysicalResolution,
    const EDWCTransparencyMaterialColorPayloadKind PayloadKind,
    const FString& MaterialBakeSignature,
    const TConstArrayView<FColor> Pixels,
    const bool bSRGB,
    UTexture2D*& OutTexture,
    FString& OutError)
{
    check(IsInGameThread());
    OutTexture = nullptr;
    OutError.Reset();
    const bool bValidPayloadShape =
        PayloadKind == EDWCTransparencyMaterialColorPayloadKind::ConstantColor
            ? PhysicalResolution == FIntPoint(1, 1)
            : PhysicalResolution == LogicalResolution;
    if (LogicalResolution.X <= 0 || LogicalResolution.Y <= 0 ||
        PhysicalResolution.X <= 0 || PhysicalResolution.Y <= 0 ||
        Pixels.Num() != PhysicalResolution.X * PhysicalResolution.Y ||
        !bValidPayloadShape || MaterialBakeSignature.IsEmpty())
    {
        OutError = TEXT("Source material color payload is incomplete.");
        return false;
    }

    UTexture2D* Texture = FindOrCreateSourceMaterialColorTexture(
        Asset, SourceMesh, MaterialSlotIndex, SourceUVChannel, OutError);
    if (Texture == nullptr)
    {
        return false;
    }
    Texture->Source.Init(
        PhysicalResolution.X, PhysicalResolution.Y, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(Pixels.GetData()));
    Texture->CompressionSettings = TC_Default;
    ConfigureCommon(*Texture, bSRGB);

#if WITH_EDITORONLY_DATA
    TArray<FDWCTransparencyMaterialColorCacheReference>& References =
        Asset.Authored.TransparencyData.MaterialColorCache;
    const FSoftObjectPath SourceMeshPath(&SourceMesh);
    for (FDWCTransparencyMaterialColorCacheReference& Candidate : References)
    {
        if (Candidate.SourceMesh.ToSoftObjectPath() == SourceMeshPath &&
            Candidate.MaterialSlotIndex == MaterialSlotIndex &&
            Candidate.SourceUVChannel == SourceUVChannel)
        {
            Candidate.bObsolete = true;
        }
    }
    FDWCTransparencyMaterialColorCacheReference* Reference = References.FindByPredicate(
        [&SourceMeshPath, MaterialSlotIndex, SourceUVChannel, &MaterialBakeSignature](
            const FDWCTransparencyMaterialColorCacheReference& Candidate)
        {
            return Candidate.SourceMesh.ToSoftObjectPath() == SourceMeshPath &&
                Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                Candidate.SourceUVChannel == SourceUVChannel &&
                Candidate.MaterialBakeSignature == MaterialBakeSignature;
        });
    if (Reference == nullptr)
    {
        Reference = &References.AddDefaulted_GetRef();
    }
    Reference->SourceMesh = &SourceMesh;
    Reference->MaterialSlotIndex = MaterialSlotIndex;
    Reference->SourceUVChannel = SourceUVChannel;
    Reference->Resolution = LogicalResolution.X;
    Reference->PayloadResolution = PhysicalResolution;
    Reference->PayloadKind = PayloadKind;
    Reference->MaterialBakeSignature = MaterialBakeSignature;
    Reference->Texture = Texture;
    Reference->bObsolete = false;
    // Cache metadata is rebuildable editor state, so it is persisted without
    // placing the full WCA payload into an undo transaction.
    Asset.MarkPackageDirty();
#endif
    OutTexture = Texture;
    return true;
}

bool FDWCTransparencyTempAssetStore::CommitSourceArtifacts(
    UWetClothingAsset& Asset,
    FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencySourcePayload& Result,
    const FString& MaterialBakeSignature,
    FString& OutError)
{
    check(IsInGameThread());
    OutError.Reset();
    const int32 PixelCount = Result.Resolution.X * Result.Resolution.Y;
    if (PixelCount <= 0 || Result.InnerColorBuffer.Num() != PixelCount ||
        Result.AutoAlphaBuffer.Num() != PixelCount ||
        Result.OuterCoverageBuffer.Num() != PixelCount ||
        Result.OuterIslandIDBuffer.Num() != PixelCount ||
        Result.ValidHitBuffer.Num() != PixelCount || Result.HitDistanceBuffer.Num() != PixelCount ||
        Result.SourcePriorityBuffer.Num() != PixelCount)
    {
        OutError = TEXT("Transparency source result does not contain a complete Temp artifact payload.");
        return false;
    }

    TArray<FColor> PackedBaseReveal = Result.InnerColorBuffer;
    for (int32 Index = 0; Index < PixelCount; ++Index)
    {
        // Stage 2 -> 3 uses one canonical payload: reveal RGB plus generated alpha.
        PackedBaseReveal[Index].A = Result.AutoAlphaBuffer[Index];
    }

    UTexture2D* BaseReveal = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::BaseRevealColor, OutError);
    if (BaseReveal == nullptr) return false;
    BaseReveal->Source.Init(Result.Resolution.X, Result.Resolution.Y, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(PackedBaseReveal.GetData()));
    BaseReveal->CompressionSettings = TC_Default;
    ConfigureCommon(*BaseReveal, true);
    UpdateReference(Layer, EDWCTransparencyTempArtifactKind::BaseRevealColor,
        BaseReveal, Result.BuildSignature, Result.Resolution);

    TArray<uint8> ValidHit;
    ValidHit.SetNumUninitialized(PixelCount);
    TArray<uint16> HitSource;
    HitSource.SetNumUninitialized(PixelCount);
    TArray<FFloat16> HitDistance;
    HitDistance.SetNumUninitialized(PixelCount);
    for (int32 Index = 0; Index < PixelCount; ++Index)
    {
        ValidHit[Index] = Result.ValidHitBuffer[Index] ? 255 : 0;
        HitSource[Index] = Result.SourcePriorityBuffer[Index] >= 0
            ? static_cast<uint16>(Result.SourcePriorityBuffer[Index] + 1)
            : 0;
        HitDistance[Index] = FFloat16(FMath::Max(Result.HitDistanceBuffer[Index], 0.0f));
    }

    UTexture2D* ValidHitTexture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::ValidHit, OutError);
    if (ValidHitTexture == nullptr) return false;
    ValidHitTexture->Source.Init(Result.Resolution.X, Result.Resolution.Y, 1, 1, TSF_G8, ValidHit.GetData());
    ValidHitTexture->CompressionSettings = TC_Grayscale;
    ConfigureCommon(*ValidHitTexture, false);
    UpdateReference(Layer, EDWCTransparencyTempArtifactKind::ValidHit,
        ValidHitTexture, Result.BuildSignature, Result.Resolution);

    UTexture2D* OuterCoverageTexture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::OuterCoverage, OutError);
    if (OuterCoverageTexture == nullptr) return false;
    OuterCoverageTexture->Source.Init(
        Result.Resolution.X,
        Result.Resolution.Y,
        1,
        1,
        TSF_G8,
        Result.OuterCoverageBuffer.GetData());
    OuterCoverageTexture->CompressionSettings = TC_Grayscale;
    ConfigureCommon(*OuterCoverageTexture, false);
    UpdateReference(Layer, EDWCTransparencyTempArtifactKind::OuterCoverage,
        OuterCoverageTexture, Result.BuildSignature, Result.Resolution);

    UTexture2D* OuterIslandIDTexture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::OuterIslandID, OutError);
    if (OuterIslandIDTexture == nullptr) return false;
    OuterIslandIDTexture->Source.Init(
        Result.Resolution.X,
        Result.Resolution.Y,
        1,
        1,
        TSF_G16,
        reinterpret_cast<const uint8*>(Result.OuterIslandIDBuffer.GetData()));
    OuterIslandIDTexture->CompressionSettings = TC_Grayscale;
    ConfigureCommon(*OuterIslandIDTexture, false);
    UpdateReference(Layer, EDWCTransparencyTempArtifactKind::OuterIslandID,
        OuterIslandIDTexture, Result.BuildSignature, Result.Resolution);

    UTexture2D* HitSourceTexture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::HitSource, OutError);
    if (HitSourceTexture == nullptr) return false;
    HitSourceTexture->Source.Init(Result.Resolution.X, Result.Resolution.Y, 1, 1, TSF_G16,
        reinterpret_cast<const uint8*>(HitSource.GetData()));
    HitSourceTexture->CompressionSettings = TC_Grayscale;
    ConfigureCommon(*HitSourceTexture, false);
    UpdateReference(Layer, EDWCTransparencyTempArtifactKind::HitSource,
        HitSourceTexture, Result.BuildSignature, Result.Resolution);

    UTexture2D* HitDistanceTexture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::HitDistance, OutError);
    if (HitDistanceTexture == nullptr) return false;
    HitDistanceTexture->Source.Init(Result.Resolution.X, Result.Resolution.Y, 1, 1, TSF_R16F,
        reinterpret_cast<const uint8*>(HitDistance.GetData()));
    HitDistanceTexture->CompressionSettings = TC_HDR;
    ConfigureCommon(*HitDistanceTexture, false);
    UpdateReference(Layer, EDWCTransparencyTempArtifactKind::HitDistance,
        HitDistanceTexture, Result.BuildSignature, Result.Resolution);

#if WITH_EDITORONLY_DATA
    Layer.EditorStageCache.MaterialBakeSignature = MaterialBakeSignature;
    Layer.EditorStageCache.SourceSignature = Result.BuildSignature;
    Layer.EditorStageCache.RevealSignature =
        FDWCTransparencySignatureService::BuildRevealSignature(Result.BuildSignature, Layer);
    Layer.EditorStageCache.bSourceGenerated = true;
    Layer.EditorStageCache.bRevealReviewed = false;
#endif
    return true;
}

bool FDWCTransparencyTempAssetStore::CommitRevealArtifact(
    UWetClothingAsset& Asset,
    FWetClothingTransparencyLayerData& Layer,
    const TConstArrayView<FColor> CorrectedRevealPixels,
    const FIntPoint Resolution,
    const FString& SourceSignature,
    const FString& RevealSignature,
    FString& OutError)
{
    check(IsInGameThread());
    OutError.Reset();
    if (Resolution.X <= 0 || Resolution.Y <= 0 ||
        CorrectedRevealPixels.Num() != Resolution.X * Resolution.Y ||
        SourceSignature.IsEmpty() || RevealSignature.IsEmpty())
    {
        OutError = TEXT("Corrected Reveal Color checkpoint inputs are incomplete.");
        return false;
    }

    UTexture2D* Texture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::CorrectedRevealColor, OutError);
    if (Texture == nullptr)
    {
        return false;
    }
    Texture->Source.Init(Resolution.X, Resolution.Y, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(CorrectedRevealPixels.GetData()));
    Texture->CompressionSettings = TC_Default;
    ConfigureCommon(*Texture, true);
    UpdateReference(Layer, EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
        Texture, RevealSignature, Resolution);
#if WITH_EDITORONLY_DATA
    Layer.EditorStageCache.SourceSignature = SourceSignature;
    Layer.EditorStageCache.RevealSignature = RevealSignature;
    Layer.EditorStageCache.bSourceGenerated = true;
    Layer.EditorStageCache.bRevealReviewed = true;
#endif
    return true;
}

UTexture2D* FDWCTransparencyTempAssetStore::FindCurrentArtifact(
    const FWetClothingTransparencyLayerData& Layer,
    const EDWCTransparencyTempArtifactKind Kind,
    const FString& ExpectedSignature,
    const bool bLoadIfNeeded)
{
#if WITH_EDITORONLY_DATA
    const FDWCTransparencyTempArtifactReference* Reference =
        Layer.EditorStageCache.Artifacts.FindByPredicate(
            [Kind](const FDWCTransparencyTempArtifactReference& Candidate)
            {
                return Candidate.Kind == Kind;
            });
    if (Reference == nullptr || Reference->bObsolete ||
        Reference->BuildSignature != ExpectedSignature || Reference->Texture.IsNull())
    {
        return nullptr;
    }
    UTexture2D* Texture = bLoadIfNeeded ? Reference->Texture.LoadSynchronous() : Reference->Texture.Get();
    FString PolicyError;
    if (Texture != nullptr)
    {
        // Do not make legacy editor caches unreadable. They are kept out of
        // runtime serialization and reported for rebuild by validation.
        FDWCTransparencyIntermediateAssetPolicy::EnsureEditorOnlyPackage(
            *Texture, nullptr, &PolicyError);
    }
    return Texture;
#else
    return nullptr;
#endif
}
bool FDWCTransparencyTempAssetStore::HasCurrentArtifact(
    const FWetClothingTransparencyLayerData& Layer,
    const EDWCTransparencyTempArtifactKind Kind,
    const FString& ExpectedSignature)
{
#if WITH_EDITORONLY_DATA
    const FDWCTransparencyTempArtifactReference* Reference =
        Layer.EditorStageCache.Artifacts.FindByPredicate(
            [Kind](const FDWCTransparencyTempArtifactReference& Candidate)
            {
                return Candidate.Kind == Kind;
            });
    return Reference != nullptr && !Reference->bObsolete &&
        Reference->BuildSignature == ExpectedSignature &&
        Reference->Resolution.X > 0 && Reference->Resolution.Y > 0 &&
        !Reference->Texture.IsNull();
#else
    return false;
#endif
}

