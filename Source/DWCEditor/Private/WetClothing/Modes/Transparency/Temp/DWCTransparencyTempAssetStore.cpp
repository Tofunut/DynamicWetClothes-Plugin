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
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageArtifactContract.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeUtilities.h"

namespace
{
    FString BuildSourceMaterialPropertyAssetName(
        const UWetClothingAsset& Asset,
        const FDWCTransparencyMaterialSurfaceBakeIdentity& Identity,
        const TCHAR* PropertyToken)
    {
        return FString::Printf(
            TEXT("T_%s_S%s_%s"),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(Asset.GetName()),
            *Identity.Digest.Left(16),
            PropertyToken);
    }

    UTexture2D* FindOrCreateSourceMaterialPropertyTexture(
        UWetClothingAsset& Asset,
        const FDWCTransparencyMaterialSurfaceBakeIdentity& Identity,
        const TCHAR* PropertyToken,
        FString& OutError)
    {
        const FString PackagePath = FDWCRevealBakeUtilities::GetGeneratedPackagePath(
            Asset, TEXT("Textures/Transparency/Temp"));
        const FString AssetName = BuildSourceMaterialPropertyAssetName(
            Asset, Identity, PropertyToken);
        const FString PackageName = PackagePath / AssetName;
        const FString ObjectPath = PackageName + TEXT(".") + AssetName;
        // A missing generated cache is the normal first-build case. Avoid
        // emitting a LoadErrors warning before creating its package below.
        UObject* Existing = LoadObject<UObject>(
            nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (Existing != nullptr && !Existing->IsA<UTexture2D>())
        {
            OutError = FString::Printf(
                TEXT("Transparency source-material property path '%s' is occupied by '%s'."),
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
                TEXT("Transparency source-material property artifact '%s' is not owned by this WCA."),
                    *ObjectPath);
                return nullptr;
            }
        }
        if (Texture == nullptr || !Asset.TagGeneratedAsset(Texture))
        {
            OutError = FString::Printf(TEXT("Could not create source-material property artifact '%s'."), *ObjectPath);
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

    FString BuildAssetName(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const EDWCTransparencyTempArtifactKind Kind,
        const FGuid& CommitGeneration)
    {
        return FString::Printf(
            TEXT("T_%s_L%s_G%s_%s"),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(Asset.GetName()),
            *Layer.LayerGuid.ToString(EGuidFormats::Digits).Left(8),
            *CommitGeneration.ToString(EGuidFormats::Digits).Left(8),
            *FDWCTransparencyStageArtifactContract::GetAssetToken(Kind));
    }

    UTexture2D* FindOrCreateTexture(
        UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const EDWCTransparencyTempArtifactKind Kind,
        const FGuid& CommitGeneration,
        FString& OutError)
    {
        const FString PackagePath = FDWCRevealBakeUtilities::GetGeneratedPackagePath(
            Asset, TEXT("Textures/Transparency/Temp"));
        const FString AssetName = BuildAssetName(Asset, Layer, Kind, CommitGeneration);
        const FString PackageName = PackagePath / AssetName;
        const FString ObjectPath = PackageName + TEXT(".") + AssetName;

        // Stage artifacts are created lazily, so an absent object is expected
        // and must not be reported as an editor load failure.
        UObject* Existing = LoadObject<UObject>(
            nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
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
        const FIntPoint Resolution,
        const FGuid& CommitGeneration)
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
        Reference->ContractVersion = FDWCTransparencyStageArtifactContract::ContractVersion;
        Reference->CommitGeneration = CommitGeneration;
        Reference->TextureSourceId = Texture != nullptr ? Texture->Source.GetId() : FGuid();
        Reference->Resolution = Resolution;
        Reference->bObsolete = false;
#endif
    }
}

bool FDWCTransparencyTempAssetStore::FindCurrentSourceMaterialSurface(
    const UWetClothingAsset& Asset,
    const USkeletalMesh& SourceMesh,
    const int32 MaterialSlotIndex,
    const int32 SourceUVChannel,
    const int32 LogicalResolution,
    const FDWCTransparencyMaterialSurfaceBakeIdentity& Identity,
    const bool bLoadIfNeeded,
    FDWCTransparencyMaterialColorCacheReference& OutReference,
    UTexture2D*& OutBaseColorTexture,
    UTexture2D*& OutNormalTexture,
    UTexture2D*& OutMetallicTexture)
{
    OutReference = FDWCTransparencyMaterialColorCacheReference();
    OutBaseColorTexture = nullptr;
    OutNormalTexture = nullptr;
    OutMetallicTexture = nullptr;
#if WITH_EDITORONLY_DATA
    if (!Identity.IsValid())
    {
        return false;
    }
    const FDWCTransparencyMaterialColorCacheReference* Reference =
        Asset.Authored.TransparencyData.MaterialColorCache.FindByPredicate(
            [&SourceMesh, MaterialSlotIndex, SourceUVChannel, LogicalResolution, &Identity](
                const FDWCTransparencyMaterialColorCacheReference& Candidate)
            {
                return !Candidate.bObsolete &&
                    Candidate.SourceMesh.ToSoftObjectPath() == FSoftObjectPath(&SourceMesh) &&
                    Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                    Candidate.SourceUVChannel == SourceUVChannel &&
                    Candidate.Resolution == LogicalResolution &&
                    Candidate.IdentityVersion == FDWCTransparencyMaterialSurfaceBakeIdentity::Version &&
                    Candidate.CacheIdentity == Identity.Digest &&
                    Candidate.SourceMeshContentSignature == Identity.SourceMeshContentSignature &&
                    Candidate.EffectiveMaterialSignature == Identity.EffectiveMaterialSignature &&
                    Candidate.PlacementSignature == Identity.PlacementSignature;
            });
    if (Reference == nullptr || Reference->Texture.IsNull() || Reference->NormalTexture.IsNull() ||
        Reference->MetallicTexture.IsNull())
    {
        return false;
    }

    OutBaseColorTexture = bLoadIfNeeded ? Reference->Texture.LoadSynchronous() : Reference->Texture.Get();
    OutNormalTexture = bLoadIfNeeded ? Reference->NormalTexture.LoadSynchronous() : Reference->NormalTexture.Get();
    OutMetallicTexture = bLoadIfNeeded ? Reference->MetallicTexture.LoadSynchronous() : Reference->MetallicTexture.Get();
    if (OutBaseColorTexture == nullptr || OutNormalTexture == nullptr || OutMetallicTexture == nullptr)
    {
        OutBaseColorTexture = nullptr;
        OutNormalTexture = nullptr;
        OutMetallicTexture = nullptr;
        return false;
    }

    OutReference = *Reference;
    return true;
#else
    return false;
#endif
}

bool FDWCTransparencyTempAssetStore::CommitSourceMaterialSurface(
    UWetClothingAsset& Asset,
    USkeletalMesh& SourceMesh,
    const int32 MaterialSlotIndex,
    const int32 SourceUVChannel,
    const FIntPoint LogicalResolution,
    const FIntPoint BaseColorPhysicalResolution,
    const EDWCTransparencyMaterialColorPayloadKind BaseColorPayloadKind,
    const TConstArrayView<FColor> BaseColorPixels,
    const bool bBaseColorSRGB,
    const FIntPoint NormalPhysicalResolution,
    const EDWCTransparencyMaterialColorPayloadKind NormalPayloadKind,
    const TConstArrayView<FColor> NormalPixels,
    const bool bHasBakedNormalProperty,
    const FIntPoint MetallicPhysicalResolution,
    const EDWCTransparencyMaterialColorPayloadKind MetallicPayloadKind,
    const TConstArrayView<uint8> MetallicPixels,
    const bool bHasBakedMetallicProperty,
    const FDWCTransparencyMaterialSurfaceBakeIdentity& Identity,
    UTexture2D*& OutBaseColorTexture,
    UTexture2D*& OutNormalTexture,
    UTexture2D*& OutMetallicTexture,
    FString& OutError)
{
    check(IsInGameThread());
    OutBaseColorTexture = nullptr;
    OutNormalTexture = nullptr;
    OutMetallicTexture = nullptr;
    OutError.Reset();

    const auto IsValidPayload = [LogicalResolution](
                                    const EDWCTransparencyMaterialColorPayloadKind Kind,
                                    const FIntPoint PhysicalResolution,
                                    const int32 NumPixels)
    {
        const bool bShapeValid = Kind == EDWCTransparencyMaterialColorPayloadKind::ConstantColor
            ? PhysicalResolution == FIntPoint(1, 1)
            : PhysicalResolution == LogicalResolution;
        return PhysicalResolution.X > 0 && PhysicalResolution.Y > 0 &&
            NumPixels == PhysicalResolution.X * PhysicalResolution.Y && bShapeValid;
    };
    if (LogicalResolution.X <= 0 || LogicalResolution.Y <= 0 || !Identity.IsValid() ||
        !IsValidPayload(BaseColorPayloadKind, BaseColorPhysicalResolution, BaseColorPixels.Num()) ||
        !IsValidPayload(NormalPayloadKind, NormalPhysicalResolution, NormalPixels.Num()) ||
        !IsValidPayload(MetallicPayloadKind, MetallicPhysicalResolution, MetallicPixels.Num()))
    {
        OutError = TEXT("Source material surface payload is incomplete.");
        return false;
    }

    UTexture2D* BaseColorTexture = FindOrCreateSourceMaterialPropertyTexture(
        Asset, Identity, TEXT("SourceMaterialColor"), OutError);
    if (BaseColorTexture == nullptr) return false;
    UTexture2D* NormalTexture = FindOrCreateSourceMaterialPropertyTexture(
        Asset, Identity, TEXT("SourceMaterialNormal"), OutError);
    if (NormalTexture == nullptr) return false;
    UTexture2D* MetallicTexture = FindOrCreateSourceMaterialPropertyTexture(
        Asset, Identity, TEXT("SourceMaterialMetallic"), OutError);
    if (MetallicTexture == nullptr) return false;

    BaseColorTexture->Source.Init(
        BaseColorPhysicalResolution.X, BaseColorPhysicalResolution.Y, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(BaseColorPixels.GetData()));
    BaseColorTexture->CompressionSettings = TC_Default;
    ConfigureCommon(*BaseColorTexture, bBaseColorSRGB);

    NormalTexture->Source.Init(
        NormalPhysicalResolution.X, NormalPhysicalResolution.Y, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(NormalPixels.GetData()));
    NormalTexture->CompressionSettings = TC_Default;
    ConfigureCommon(*NormalTexture, false);

    MetallicTexture->Source.Init(
        MetallicPhysicalResolution.X, MetallicPhysicalResolution.Y, 1, 1, TSF_G8,
        MetallicPixels.GetData());
    MetallicTexture->CompressionSettings = TC_Grayscale;
    ConfigureCommon(*MetallicTexture, false);

#if WITH_EDITORONLY_DATA
    TArray<FDWCTransparencyMaterialColorCacheReference>& References =
        Asset.Authored.TransparencyData.MaterialColorCache;
    // Type 2/3 can legitimately use the same mesh slot at multiple placements
    // or with different effective material overrides in one source set. Keep
    // those exact identities side by side; lookup never accepts a partial key.
    FDWCTransparencyMaterialColorCacheReference* Reference = References.FindByPredicate(
        [&Identity](
            const FDWCTransparencyMaterialColorCacheReference& Candidate)
        {
            return Candidate.IdentityVersion == FDWCTransparencyMaterialSurfaceBakeIdentity::Version &&
                Candidate.CacheIdentity == Identity.Digest;
        });
    if (Reference == nullptr)
    {
        Reference = &References.AddDefaulted_GetRef();
    }
    Reference->SourceMesh = &SourceMesh;
    Reference->MaterialSlotIndex = MaterialSlotIndex;
    Reference->SourceUVChannel = SourceUVChannel;
    Reference->Resolution = LogicalResolution.X;
    Reference->PayloadResolution = BaseColorPhysicalResolution;
    Reference->PayloadKind = BaseColorPayloadKind;
    Reference->Texture = BaseColorTexture;
    Reference->NormalPayloadResolution = NormalPhysicalResolution;
    Reference->NormalPayloadKind = NormalPayloadKind;
    Reference->NormalTexture = NormalTexture;
    Reference->MetallicPayloadResolution = MetallicPhysicalResolution;
    Reference->MetallicPayloadKind = MetallicPayloadKind;
    Reference->MetallicTexture = MetallicTexture;
    Reference->bHasBakedNormalProperty = bHasBakedNormalProperty;
    Reference->bHasBakedMetallicProperty = bHasBakedMetallicProperty;
    Reference->CacheIdentity = Identity.Digest;
    Reference->IdentityVersion = FDWCTransparencyMaterialSurfaceBakeIdentity::Version;
    Reference->SourceMeshContentSignature = Identity.SourceMeshContentSignature;
    Reference->EffectiveMaterialSignature = Identity.EffectiveMaterialSignature;
    Reference->PlacementSignature = Identity.PlacementSignature;
    Reference->MaterialBakeSignature = Identity.Digest;
    Reference->bObsolete = false;
    Asset.MarkPackageDirty();
#endif
    OutBaseColorTexture = BaseColorTexture;
    OutNormalTexture = NormalTexture;
    OutMetallicTexture = MetallicTexture;
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
        !Result.RevealSurfaceAuthoring.IsValidForResolution(Result.Resolution) ||
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

    // Each Stage 2 commit writes a new immutable artifact set. References are
    // published only after every texture has been created and configured.
    const FGuid CommitGeneration = FGuid::NewGuid();

    UTexture2D* BaseReveal = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::BaseRevealColor,
        CommitGeneration, OutError);
    if (BaseReveal == nullptr) return false;
    BaseReveal->Source.Init(Result.Resolution.X, Result.Resolution.Y, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(PackedBaseReveal.GetData()));
    BaseReveal->CompressionSettings = TC_Default;
    ConfigureCommon(*BaseReveal, true);

    UTexture2D* BaseRevealSurface = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::BaseRevealSurface,
        CommitGeneration, OutError);
    if (BaseRevealSurface == nullptr) return false;
    BaseRevealSurface->Source.Init(Result.Resolution.X, Result.Resolution.Y, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(Result.RevealSurfaceAuthoring.GetData()));
    BaseRevealSurface->CompressionSettings = TC_Default;
    ConfigureCommon(*BaseRevealSurface, false);

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
        Asset, Layer, EDWCTransparencyTempArtifactKind::ValidHit,
        CommitGeneration, OutError);
    if (ValidHitTexture == nullptr) return false;
    ValidHitTexture->Source.Init(Result.Resolution.X, Result.Resolution.Y, 1, 1, TSF_G8, ValidHit.GetData());
    ValidHitTexture->CompressionSettings = TC_Grayscale;
    ConfigureCommon(*ValidHitTexture, false);

    UTexture2D* OuterCoverageTexture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::OuterCoverage,
        CommitGeneration, OutError);
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

    UTexture2D* OuterIslandIDTexture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::OuterIslandID,
        CommitGeneration, OutError);
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

    UTexture2D* HitSourceTexture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::HitSource,
        CommitGeneration, OutError);
    if (HitSourceTexture == nullptr) return false;
    HitSourceTexture->Source.Init(Result.Resolution.X, Result.Resolution.Y, 1, 1, TSF_G16,
        reinterpret_cast<const uint8*>(HitSource.GetData()));
    HitSourceTexture->CompressionSettings = TC_Grayscale;
    ConfigureCommon(*HitSourceTexture, false);

    UTexture2D* HitDistanceTexture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::HitDistance,
        CommitGeneration, OutError);
    if (HitDistanceTexture == nullptr) return false;
    HitDistanceTexture->Source.Init(Result.Resolution.X, Result.Resolution.Y, 1, 1, TSF_R16F,
        reinterpret_cast<const uint8*>(HitDistance.GetData()));
    HitDistanceTexture->CompressionSettings = TC_HDR;
    ConfigureCommon(*HitDistanceTexture, false);

    struct FPendingArtifactReference
    {
        EDWCTransparencyTempArtifactKind Kind;
        UTexture2D* Texture;
    };
    const FPendingArtifactReference PendingReferences[] = {
        {EDWCTransparencyTempArtifactKind::BaseRevealColor, BaseReveal},
        {EDWCTransparencyTempArtifactKind::BaseRevealSurface, BaseRevealSurface},
        {EDWCTransparencyTempArtifactKind::ValidHit, ValidHitTexture},
        {EDWCTransparencyTempArtifactKind::OuterCoverage, OuterCoverageTexture},
        {EDWCTransparencyTempArtifactKind::OuterIslandID, OuterIslandIDTexture},
        {EDWCTransparencyTempArtifactKind::HitSource, HitSourceTexture},
        {EDWCTransparencyTempArtifactKind::HitDistance, HitDistanceTexture}
    };
    for (const FPendingArtifactReference& Pending : PendingReferences)
    {
        UpdateReference(
            Layer,
            Pending.Kind,
            Pending.Texture,
            FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
                Pending.Kind, Result.BuildSignature),
            Result.Resolution,
            CommitGeneration);
    }

#if WITH_EDITORONLY_DATA
    Layer.EditorStageCache.MaterialBakeSignature = MaterialBakeSignature;
    Layer.EditorStageCache.SourceSignature = Result.BuildSignature;
    Layer.EditorStageCache.RevealSignature =
        FDWCTransparencySignatureService::BuildRevealSignature(
            Result.BuildSignature,
            Layer,
            Asset.Authored.TransparencyData.RevealMetallicDarkeningStrength);
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

    const FGuid CommitGeneration = FGuid::NewGuid();
    // Rename-free generated assets are immutable by generation, so create the
    // texture with the same generation that is published in its reference.
    UTexture2D* Texture = FindOrCreateTexture(
        Asset, Layer, EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
        CommitGeneration, OutError);
    if (Texture == nullptr)
    {
        return false;
    }
    Texture->Source.Init(Resolution.X, Resolution.Y, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(CorrectedRevealPixels.GetData()));
    Texture->CompressionSettings = TC_Default;
    ConfigureCommon(*Texture, true);
    UpdateReference(
        Layer,
        EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
        Texture,
        FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
            EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
            SourceSignature,
            RevealSignature),
        Resolution,
        CommitGeneration);
#if WITH_EDITORONLY_DATA
    Layer.EditorStageCache.SourceSignature = SourceSignature;
    Layer.EditorStageCache.RevealSignature = RevealSignature;
    Layer.EditorStageCache.bSourceGenerated = true;
    Layer.EditorStageCache.bRevealReviewed = true;
#endif
    return true;
}

EDWCTransparencyCorrectedRevealRestoreResult
FDWCTransparencyTempAssetStore::RestoreCurrentCorrectedReveal(
    const FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencySourcePayload& SourcePayload,
    const float RevealMetallicDarkeningStrength,
    TArray<FColor>& OutPixels,
    FString& OutError)
{
    check(IsInGameThread());
    OutPixels.Reset();
    OutError.Reset();

    const int32 PixelCount = SourcePayload.Resolution.X * SourcePayload.Resolution.Y;
    if (SourcePayload.BuildSignature.IsEmpty() || SourcePayload.Resolution.X <= 0 ||
        SourcePayload.Resolution.Y <= 0 || PixelCount <= 0)
    {
        OutError = TEXT("The Stage 2 source identity is invalid for Corrected Reveal Color restoration.");
        return EDWCTransparencyCorrectedRevealRestoreResult::Invalid;
    }

    const FString RevealSignature =
        FDWCTransparencySignatureService::BuildRevealSignature(
            SourcePayload.BuildSignature,
            Layer,
            RevealMetallicDarkeningStrength);
    const FString ExpectedRevealSignature =
        FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
            EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
            SourcePayload.BuildSignature,
            RevealSignature);
    if (!HasCurrentArtifact(
            Layer,
            EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
            ExpectedRevealSignature))
    {
        return EDWCTransparencyCorrectedRevealRestoreResult::MissingOrStale;
    }

    UTexture2D* Texture = FindCurrentArtifact(
        Layer,
        EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
        ExpectedRevealSignature,
        true);
    if (Texture == nullptr || !Texture->Source.IsValid())
    {
        OutError = TEXT("The current Corrected Reveal Color artifact could not be loaded.");
        return EDWCTransparencyCorrectedRevealRestoreResult::Invalid;
    }
    if (Texture->Source.GetFormat() != TSF_BGRA8 ||
        Texture->Source.GetSizeX() != SourcePayload.Resolution.X ||
        Texture->Source.GetSizeY() != SourcePayload.Resolution.Y)
    {
        OutError = FString::Printf(
            TEXT("Corrected Reveal Color '%s' does not match the current Stage 2 BGRA8 resolution."),
            *GetNameSafe(Texture));
        return EDWCTransparencyCorrectedRevealRestoreResult::Invalid;
    }

    TArray64<uint8> RawMipData;
    if (!Texture->Source.GetMipData(RawMipData, 0) ||
        RawMipData.Num() != static_cast<int64>(PixelCount) * sizeof(FColor))
    {
        OutError = FString::Printf(
            TEXT("Corrected Reveal Color '%s' has incomplete source pixels."),
            *GetNameSafe(Texture));
        return EDWCTransparencyCorrectedRevealRestoreResult::Invalid;
    }

    OutPixels.SetNumUninitialized(PixelCount);
    FMemory::Memcpy(
        OutPixels.GetData(),
        RawMipData.GetData(),
        static_cast<SIZE_T>(PixelCount) * sizeof(FColor));
    return EDWCTransparencyCorrectedRevealRestoreResult::Restored;
}

UTexture2D* FDWCTransparencyTempAssetStore::FindCurrentArtifact(
    const FWetClothingTransparencyLayerData& Layer,
    const EDWCTransparencyTempArtifactKind Kind,
    const FString& ExpectedSignature,
    const bool bLoadIfNeeded)
{
#if WITH_EDITORONLY_DATA
    const FDWCTransparencyStageArtifactSpec* Spec =
        FDWCTransparencyStageArtifactContract::FindSpec(Kind);
    const FDWCTransparencyTempArtifactReference* Reference =
        FDWCTransparencyStageArtifactContract::FindReference(Layer, Kind);
    FString ValidationError;
    if (Spec == nullptr || Reference == nullptr ||
        !FDWCTransparencyStageArtifactContract::ValidateReference(
            *Reference,
            *Spec,
            ExpectedSignature,
            Reference->Resolution,
            nullptr,
            bLoadIfNeeded,
            ValidationError))
    {
        return nullptr;
    }
    return bLoadIfNeeded ? Reference->Texture.LoadSynchronous() : Reference->Texture.Get();
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
    const FDWCTransparencyStageArtifactSpec* Spec =
        FDWCTransparencyStageArtifactContract::FindSpec(Kind);
    const FDWCTransparencyTempArtifactReference* Reference =
        FDWCTransparencyStageArtifactContract::FindReference(Layer, Kind);
    FString ValidationError;
    return Spec != nullptr && Reference != nullptr &&
        FDWCTransparencyStageArtifactContract::ValidateReference(
            *Reference,
            *Spec,
            ExpectedSignature,
            Reference->Resolution,
            nullptr,
            false,
            ValidationError);
#else
    return false;
#endif
}

