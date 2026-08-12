//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageContracts.h"

class UTexture2D;
class USkeletalMesh;
class UWetClothingAsset;
struct FDWCTransparencySourcePayload;
struct FDWCTransparencyMaterialSurfaceBakeIdentity;

enum class EDWCTransparencyCorrectedRevealRestoreResult : uint8
{
    Restored,
    MissingOrStale,
    Invalid
};

/** Persistent, editor-only cache for rebuildable transparency stage artifacts. */
class FDWCTransparencyTempAssetStore
{
  public:
    /** Loads a complete evaluated source-material surface cache entry. */
    static bool FindCurrentSourceMaterialSurface(
        const UWetClothingAsset& Asset,
        const USkeletalMesh& SourceMesh,
        int32 MaterialSlotIndex,
        int32 SourceUVChannel,
        int32 SourceBakeResolution,
        const FDWCTransparencyMaterialSurfaceBakeIdentity& Identity,
        bool bLoadIfNeeded,
        FDWCTransparencyMaterialColorCacheReference& OutReference,
        UTexture2D*& OutBaseColorTexture,
        UTexture2D*& OutNormalTexture,
        UTexture2D*& OutMetallicTexture);

    /** Persists the three-property source material bake as editor-only Temp assets. */
    static bool CommitSourceMaterialSurface(
        UWetClothingAsset& Asset,
        USkeletalMesh& SourceMesh,
        int32 MaterialSlotIndex,
        int32 SourceUVChannel,
        FIntPoint SourceBakeResolution,
        FIntPoint BaseColorPhysicalResolution,
        EDWCTransparencyMaterialColorPayloadKind BaseColorPayloadKind,
        TConstArrayView<FColor> BaseColorPixels,
        bool bBaseColorSRGB,
        FIntPoint NormalPhysicalResolution,
        EDWCTransparencyMaterialColorPayloadKind NormalPayloadKind,
        TConstArrayView<FColor> NormalPixels,
        bool bHasBakedNormalProperty,
        FIntPoint MetallicPhysicalResolution,
        EDWCTransparencyMaterialColorPayloadKind MetallicPayloadKind,
        TConstArrayView<uint8> MetallicPixels,
        bool bHasBakedMetallicProperty,
        const FDWCTransparencyMaterialSurfaceBakeIdentity& Identity,
        UTexture2D*& OutBaseColorTexture,
        UTexture2D*& OutNormalTexture,
        UTexture2D*& OutMetallicTexture,
        FString& OutError);

    static bool CommitSourceArtifacts(
        UWetClothingAsset& Asset,
        FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencySourcePayload& Result,
        const FString& MaterialBakeSignature,
        FString& OutError);

    static bool CommitRevealArtifact(
        UWetClothingAsset& Asset,
        FWetClothingTransparencyLayerData& Layer,
        TConstArrayView<FColor> CorrectedRevealPixels,
        FIntPoint Resolution,
        const FString& SourceSignature,
        const FString& RevealSignature,
        FString& OutError);

    /** Commits RGB from final pixels while restoring the Stage 2 checkpoint alpha. */
    static bool CommitRevealArtifact(
        UWetClothingAsset& Asset,
        FWetClothingTransparencyLayerData& Layer,
        TConstArrayView<FColor> CorrectedRevealRgbPixels,
        TConstArrayView<uint8> CorrectedRevealAlpha,
        FIntPoint Resolution,
        const FString& SourceSignature,
        const FString& RevealSignature,
        FString& OutError);

    /**
     * Reads the current Stage 3 checkpoint. Its RGB contains the corrected
     * reveal color and its alpha contains the Stage 2 automatic alpha.
     */
    static EDWCTransparencyCorrectedRevealRestoreResult RestoreCurrentCorrectedReveal(
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencySourcePayload& SourcePayload,
        float RevealMetallicDarkeningStrength,
        TArray<FColor>& OutPixels,
        FString& OutError);

    static UTexture2D* FindCurrentArtifact(
        const FWetClothingTransparencyLayerData& Layer,
        EDWCTransparencyTempArtifactKind Kind,
        const FString& ExpectedSignature,
        bool bLoadIfNeeded);

    static bool HasCurrentArtifact(
        const FWetClothingTransparencyLayerData& Layer,
        EDWCTransparencyTempArtifactKind Kind,
        const FString& ExpectedSignature);

};
