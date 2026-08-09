//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageContracts.h"

class UTexture2D;
class USkeletalMesh;
class UWetClothingAsset;
struct FDWCTransparencySourcePayload;

/** Persistent, editor-only cache for rebuildable transparency stage artifacts. */
class FDWCTransparencyTempAssetStore
{
  public:
    static bool FindCurrentSourceMaterialColor(
        const UWetClothingAsset& Asset,
        const USkeletalMesh& SourceMesh,
        int32 MaterialSlotIndex,
        int32 SourceUVChannel,
        int32 LogicalResolution,
        const FString& MaterialBakeSignature,
        bool bLoadIfNeeded,
        FDWCTransparencyMaterialColorCacheReference& OutReference,
        UTexture2D*& OutTexture);

    static bool CommitSourceMaterialColor(
        UWetClothingAsset& Asset,
        USkeletalMesh& SourceMesh,
        int32 MaterialSlotIndex,
        int32 SourceUVChannel,
        FIntPoint LogicalResolution,
        FIntPoint PhysicalResolution,
        EDWCTransparencyMaterialColorPayloadKind PayloadKind,
        const FString& MaterialBakeSignature,
        TConstArrayView<FColor> Pixels,
        bool bSRGB,
        UTexture2D*& OutTexture,
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
