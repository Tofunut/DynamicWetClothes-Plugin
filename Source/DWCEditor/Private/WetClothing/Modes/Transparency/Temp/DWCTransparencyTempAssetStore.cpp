//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "Math/Float16.h"
#include "Misc/SecureHash.h"
#include "WetClothing/Foundation/Assets/DWCEditorArtifactStore.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageArtifactContract.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeUtilities.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetLifetimePolicy.h"

namespace
{
    FDWCEditorArtifactTextureRequest MakeTempTextureRequest(
        UWetClothingAsset& Asset,
        const FString& AssetName,
        const FIntPoint Resolution,
        const ETextureSourceFormat SourceFormat,
        const void* PixelData,
        const uint64 PixelBytes,
        const TextureCompressionSettings CompressionSettings,
        const bool bSRGB,
        const FString& DebugName)
    {
        FDWCEditorArtifactTextureRequest Request;
        Request.OwnerAsset = &Asset;
        Request.AssetName = AssetName;
        Request.PackageName = FDWCRevealBakeUtilities::GetGeneratedPackagePath(
            Asset, TEXT("Textures/Transparency/Temp")) / AssetName;
        Request.Lifetime = EDWCEditorArtifactLifetime::EditorIntermediate;
        Request.Resolution = Resolution;
        Request.SourceFormat = SourceFormat;
        Request.PixelData = static_cast<const uint8*>(PixelData);
        Request.PixelBytes = PixelBytes;
        Request.Settings.CompressionSettings = CompressionSettings;
        Request.Settings.bSRGB = bSRGB;
        Request.DebugName = DebugName;
        return Request;
    }

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

    FString BuildAssetName(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const EDWCTransparencyTempArtifactKind Kind,
        const int32 GenerationSlot)
    {
        return FString::Printf(
            TEXT("T_%s_L%s_%s_%s"),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(Asset.GetName()),
            *Layer.LayerGuid.ToString(EGuidFormats::Digits).Left(8),
            *FDWCTransparencyTempAssetLifetimePolicy::GetGenerationSlotToken(GenerationSlot),
            *FDWCTransparencyStageArtifactContract::GetAssetToken(Kind));
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
        FDWCTransparencyTempArtifactReference Reference;
        Reference.Kind = Kind;
        Reference.Texture = Texture;
        Reference.BuildSignature = Signature;
        Reference.ContractVersion = FDWCTransparencyStageArtifactContract::ContractVersion;
        Reference.CommitGeneration = CommitGeneration;
        Reference.TextureSourceId = Texture != nullptr ? Texture->Source.GetId() : FGuid();
        Reference.Resolution = Resolution;
        Reference.bObsolete = false;
        FDWCTransparencyTempAssetLifetimePolicy::PublishCurrentReference(Layer, Reference);
#endif
    }
}

bool FDWCTransparencyTempAssetStore::FindCurrentSourceMaterialSurface(
    const UWetClothingAsset& Asset,
    const USkeletalMesh& SourceMesh,
    const int32 MaterialSlotIndex,
    const int32 SourceUVChannel,
    const int32 SourceBakeResolution,
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
            [&SourceMesh, MaterialSlotIndex, SourceUVChannel, SourceBakeResolution, &Identity](
                const FDWCTransparencyMaterialColorCacheReference& Candidate)
            {
                return !Candidate.bObsolete &&
                    Candidate.SourceMesh.ToSoftObjectPath() == FSoftObjectPath(&SourceMesh) &&
                    Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                    Candidate.SourceUVChannel == SourceUVChannel &&
                    Candidate.SourceBakeResolution == SourceBakeResolution &&
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
    const FIntPoint SourceBakeResolution,
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

    const auto IsValidPayload = [SourceBakeResolution](
                                    const EDWCTransparencyMaterialColorPayloadKind Kind,
                                    const FIntPoint PhysicalResolution,
                                    const int32 NumPixels)
    {
        const bool bShapeValid = Kind == EDWCTransparencyMaterialColorPayloadKind::ConstantColor
            ? PhysicalResolution == FIntPoint(1, 1)
            : PhysicalResolution == SourceBakeResolution;
        return PhysicalResolution.X > 0 && PhysicalResolution.Y > 0 &&
            NumPixels == PhysicalResolution.X * PhysicalResolution.Y && bShapeValid;
    };
    if (SourceBakeResolution.X <= 0 || SourceBakeResolution.Y <= 0 || !Identity.IsValid() ||
        !IsValidPayload(BaseColorPayloadKind, BaseColorPhysicalResolution, BaseColorPixels.Num()) ||
        !IsValidPayload(NormalPayloadKind, NormalPhysicalResolution, NormalPixels.Num()) ||
        !IsValidPayload(MetallicPayloadKind, MetallicPhysicalResolution, MetallicPixels.Num()))
    {
        OutError = TEXT("Source material surface payload is incomplete.");
        return false;
    }

    TArray<FDWCEditorArtifactTextureRequest> Requests;
    Requests.Reserve(3);
    Requests.Add(MakeTempTextureRequest(
        Asset,
        BuildSourceMaterialPropertyAssetName(Asset, Identity, TEXT("SourceMaterialColor")),
        BaseColorPhysicalResolution,
        TSF_BGRA8,
        BaseColorPixels.GetData(),
        static_cast<uint64>(BaseColorPixels.Num()) * sizeof(FColor),
        TC_Default,
        bBaseColorSRGB,
        TEXT("Transparency source material Base Color")));
    Requests.Add(MakeTempTextureRequest(
        Asset,
        BuildSourceMaterialPropertyAssetName(Asset, Identity, TEXT("SourceMaterialNormal")),
        NormalPhysicalResolution,
        TSF_BGRA8,
        NormalPixels.GetData(),
        static_cast<uint64>(NormalPixels.Num()) * sizeof(FColor),
        TC_Default,
        false,
        TEXT("Transparency source material Normal")));
    Requests.Add(MakeTempTextureRequest(
        Asset,
        BuildSourceMaterialPropertyAssetName(Asset, Identity, TEXT("SourceMaterialMetallic")),
        MetallicPhysicalResolution,
        TSF_G8,
        MetallicPixels.GetData(),
        static_cast<uint64>(MetallicPixels.Num()),
        TC_Grayscale,
        false,
        TEXT("Transparency source material Metallic")));

    TArray<FDWCEditorArtifactCommitReceipt> Receipts;
    if (!FDWCEditorArtifactStore::Get()->CommitTextureBatch(Requests, Receipts, OutError) ||
        Receipts.Num() != Requests.Num())
    {
        return false;
    }
    UTexture2D* BaseColorTexture = Receipts[0].Texture;
    UTexture2D* NormalTexture = Receipts[1].Texture;
    UTexture2D* MetallicTexture = Receipts[2].Texture;

#if WITH_EDITORONLY_DATA
    TArray<FDWCTransparencyMaterialColorCacheReference>& References =
        Asset.Authored.TransparencyData.MaterialColorCache;
    for (FDWCTransparencyMaterialColorCacheReference& ExistingReference : References)
    {
        if (ExistingReference.IdentityVersion !=
            FDWCTransparencyMaterialSurfaceBakeIdentity::Version)
        {
            ExistingReference.bObsolete = true;
        }
    }
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
    Reference->SourceBakeResolution = SourceBakeResolution.X;
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
    FDWCTransparencyTempAssetLifetimePolicy::PruneObsoleteMaterialSurfaceReferences(
        Asset.Authored.TransparencyData);
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

    // Each Stage 2 commit writes the inactive stable artifact slot. References
    // are published only after every texture has been created and configured.
    const FGuid CommitGeneration = FGuid::NewGuid();
    const int32 GenerationSlot =
        FDWCTransparencyTempAssetLifetimePolicy::SelectNextGenerationSlot(
            Layer,
            EDWCTransparencyTempArtifactKind::BaseRevealColor);
    TArray<FDWCEditorArtifactTextureRequest> Requests;
    TArray<EDWCTransparencyTempArtifactKind> ArtifactKinds;
    Requests.Reserve(7);
    ArtifactKinds.Reserve(7);
    const auto AddRequest = [&Asset, &Layer, &Result, GenerationSlot, &Requests, &ArtifactKinds](
        const EDWCTransparencyTempArtifactKind Kind,
        const ETextureSourceFormat Format,
        const void* Data,
        const uint64 Bytes,
        const TextureCompressionSettings Compression,
        const bool bSRGB)
    {
        Requests.Add(MakeTempTextureRequest(
            Asset,
            BuildAssetName(Asset, Layer, Kind, GenerationSlot),
            Result.Resolution,
            Format,
            Data,
            Bytes,
            Compression,
            bSRGB,
            FString::Printf(TEXT("Transparency Stage 2 %s"),
                *FDWCTransparencyStageArtifactContract::GetAssetToken(Kind))));
        ArtifactKinds.Add(Kind);
        return Requests.Num() - 1;
    };

    const int32 BaseRevealIndex = AddRequest(
        EDWCTransparencyTempArtifactKind::BaseRevealColor,
        TSF_BGRA8, nullptr, static_cast<uint64>(PixelCount) * sizeof(FColor), TC_Default, true);
    Requests[BaseRevealIndex].SourceWriter = [&Result, PixelCount](uint8* Destination, uint64)
    {
        FColor* Colors = reinterpret_cast<FColor*>(Destination);
        for (int32 Index = 0; Index < PixelCount; ++Index)
        {
            Colors[Index] = Result.InnerColorBuffer[Index];
            Colors[Index].A = Result.AutoAlphaBuffer[Index];
        }
    };
    AddRequest(
        EDWCTransparencyTempArtifactKind::BaseRevealSurface,
        TSF_BGRA8,
        Result.RevealSurfaceAuthoring.GetData(),
        static_cast<uint64>(PixelCount) * sizeof(FColor),
        TC_Default,
        false);
    const int32 ValidHitIndex = AddRequest(
        EDWCTransparencyTempArtifactKind::ValidHit,
        TSF_G8, nullptr, static_cast<uint64>(PixelCount), TC_Grayscale, false);
    Requests[ValidHitIndex].SourceWriter = [&Result, PixelCount](uint8* Destination, uint64)
    {
        for (int32 Index = 0; Index < PixelCount; ++Index)
        {
            Destination[Index] = Result.ValidHitBuffer[Index] ? 255 : 0;
        }
    };
    AddRequest(
        EDWCTransparencyTempArtifactKind::OuterCoverage,
        TSF_G8,
        Result.OuterCoverageBuffer.GetData(),
        static_cast<uint64>(PixelCount),
        TC_Grayscale,
        false);
    AddRequest(
        EDWCTransparencyTempArtifactKind::OuterIslandID,
        TSF_G16,
        Result.OuterIslandIDBuffer.GetData(),
        static_cast<uint64>(PixelCount) * sizeof(uint16),
        TC_Grayscale,
        false);
    const int32 HitSourceIndex = AddRequest(
        EDWCTransparencyTempArtifactKind::HitSource,
        TSF_G16, nullptr, static_cast<uint64>(PixelCount) * sizeof(uint16), TC_Grayscale, false);
    Requests[HitSourceIndex].SourceWriter = [&Result, PixelCount](uint8* Destination, uint64)
    {
        uint16* Values = reinterpret_cast<uint16*>(Destination);
        for (int32 Index = 0; Index < PixelCount; ++Index)
        {
            Values[Index] = Result.SourcePriorityBuffer[Index] >= 0
                ? static_cast<uint16>(Result.SourcePriorityBuffer[Index] + 1)
                : 0;
        }
    };
    const int32 HitDistanceIndex = AddRequest(
        EDWCTransparencyTempArtifactKind::HitDistance,
        TSF_R16F, nullptr, static_cast<uint64>(PixelCount) * sizeof(FFloat16), TC_HDR, false);
    Requests[HitDistanceIndex].SourceWriter = [&Result, PixelCount](uint8* Destination, uint64)
    {
        FFloat16* Values = reinterpret_cast<FFloat16*>(Destination);
        for (int32 Index = 0; Index < PixelCount; ++Index)
        {
            Values[Index] = FFloat16(FMath::Max(Result.HitDistanceBuffer[Index], 0.0f));
        }
    };
    TArray<FDWCEditorArtifactCommitReceipt> Receipts;
    if (!FDWCEditorArtifactStore::Get()->CommitTextureBatch(Requests, Receipts, OutError) ||
        Receipts.Num() != Requests.Num())
    {
        return false;
    }

    for (int32 Index = 0; Index < ArtifactKinds.Num(); ++Index)
    {
        UpdateReference(
            Layer,
            ArtifactKinds[Index],
            Receipts[Index].Texture,
            FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
                ArtifactKinds[Index], Result.BuildSignature),
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
    const EDWCTransparencyTempArtifactKind Kind =
        EDWCTransparencyTempArtifactKind::CorrectedRevealColor;
    const int32 GenerationSlot =
        FDWCTransparencyTempAssetLifetimePolicy::SelectNextGenerationSlot(Layer, Kind);
    TArray<FDWCEditorArtifactTextureRequest> Requests;
    Requests.Add(MakeTempTextureRequest(
        Asset,
        BuildAssetName(Asset, Layer, Kind, GenerationSlot),
        Resolution,
        TSF_BGRA8,
        CorrectedRevealPixels.GetData(),
        static_cast<uint64>(CorrectedRevealPixels.Num()) * sizeof(FColor),
        TC_Default,
        true,
        TEXT("Transparency corrected reveal color")));
    TArray<FDWCEditorArtifactCommitReceipt> Receipts;
    if (!FDWCEditorArtifactStore::Get()->CommitTextureBatch(Requests, Receipts, OutError) ||
        Receipts.Num() != 1)
    {
        return false;
    }
    UTexture2D* Texture = Receipts[0].Texture;
    UpdateReference(
        Layer,
        Kind,
        Texture,
        FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
            Kind,
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

bool FDWCTransparencyTempAssetStore::CommitRevealArtifact(
    UWetClothingAsset& Asset,
    FWetClothingTransparencyLayerData& Layer,
    const TConstArrayView<FColor> CorrectedRevealRgbPixels,
    const TConstArrayView<uint8> CorrectedRevealAlpha,
    const FIntPoint Resolution,
    const FString& SourceSignature,
    const FString& RevealSignature,
    FString& OutError)
{
    const int32 PixelCount = Resolution.X * Resolution.Y;
    if (Resolution.X <= 0 || Resolution.Y <= 0 ||
        CorrectedRevealRgbPixels.Num() != PixelCount ||
        CorrectedRevealAlpha.Num() != PixelCount)
    {
        OutError = TEXT("Corrected Reveal Color split checkpoint inputs are incomplete.");
        return false;
    }

    TArray<FColor> CheckpointPixels;
    CheckpointPixels.SetNumUninitialized(PixelCount);
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        CheckpointPixels[PixelIndex] = CorrectedRevealRgbPixels[PixelIndex];
        CheckpointPixels[PixelIndex].A = CorrectedRevealAlpha[PixelIndex];
    }
    return CommitRevealArtifact(
        Asset,
        Layer,
        CheckpointPixels,
        Resolution,
        SourceSignature,
        RevealSignature,
        OutError);
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

