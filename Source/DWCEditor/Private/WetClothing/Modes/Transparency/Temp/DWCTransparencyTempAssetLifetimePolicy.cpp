// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetLifetimePolicy.h"

#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageArtifactContract.h"

namespace
{
    constexpr TCHAR GenerationSlot0Marker[] = TEXT("_S0_");
    constexpr TCHAR GenerationSlot1Marker[] = TEXT("_S1_");
}

int32 FDWCTransparencyTempAssetLifetimePolicy::SelectNextGenerationSlot(
    const FWetClothingTransparencyLayerData& Layer,
    const EDWCTransparencyTempArtifactKind AnchorKind)
{
#if WITH_EDITORONLY_DATA
    const FDWCTransparencyTempArtifactReference* Current =
        FDWCTransparencyStageArtifactContract::FindReference(Layer, AnchorKind);
    if (Current != nullptr)
    {
        const int32 CurrentSlot = ResolveGenerationSlot(Current->Texture.ToSoftObjectPath());
        if (CurrentSlot != INDEX_NONE)
        {
            return (CurrentSlot + 1) % GenerationSlotCount;
        }
    }
#endif
    return 0;
}

FString FDWCTransparencyTempAssetLifetimePolicy::GetGenerationSlotToken(
    const int32 GenerationSlot)
{
    check(GenerationSlot >= 0 && GenerationSlot < GenerationSlotCount);
    return FString::Printf(TEXT("S%d"), GenerationSlot);
}

void FDWCTransparencyTempAssetLifetimePolicy::PublishCurrentReference(
    FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencyTempArtifactReference& CurrentReference)
{
#if WITH_EDITORONLY_DATA
    TArray<FDWCTransparencyTempArtifactReference>& References =
        Layer.EditorStageCache.Artifacts;
    References.RemoveAll(
        [&CurrentReference](const FDWCTransparencyTempArtifactReference& Candidate)
        {
            return Candidate.Kind == CurrentReference.Kind;
        });
    References.Add(CurrentReference);
#endif
}

int32 FDWCTransparencyTempAssetLifetimePolicy::PruneObsoleteMaterialSurfaceReferences(
    FWetClothingTransparencyData& TransparencyData)
{
#if WITH_EDITORONLY_DATA
    return TransparencyData.MaterialColorCache.RemoveAll(
        [](const FDWCTransparencyMaterialColorCacheReference& Reference)
        {
            return Reference.bObsolete || Reference.IdentityVersion <= 0 ||
                Reference.CacheIdentity.IsEmpty() || Reference.Texture.IsNull() ||
                Reference.NormalTexture.IsNull() || Reference.MetallicTexture.IsNull();
        });
#else
    return 0;
#endif
}

int32 FDWCTransparencyTempAssetLifetimePolicy::ResolveGenerationSlot(
    const FSoftObjectPath& ArtifactPath)
{
    if (ArtifactPath.IsNull())
    {
        return INDEX_NONE;
    }

    const FString AssetName = ArtifactPath.GetAssetName();
    if (AssetName.Contains(GenerationSlot0Marker, ESearchCase::CaseSensitive))
    {
        return 0;
    }
    if (AssetName.Contains(GenerationSlot1Marker, ESearchCase::CaseSensitive))
    {
        return 1;
    }
    return INDEX_NONE;
}
