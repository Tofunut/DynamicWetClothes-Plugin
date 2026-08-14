// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetLifetimePolicy.h"

namespace
{
    FDWCTransparencyTempArtifactReference MakeStageReference(
        const EDWCTransparencyTempArtifactKind Kind,
        const int32 Slot,
        const TCHAR* Suffix = TEXT("Artifact"))
    {
        FDWCTransparencyTempArtifactReference Reference;
        Reference.Kind = Kind;
        Reference.Texture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(
            TEXT("/Game/Test/T_Layer_S%d_%s.T_Layer_S%d_%s"),
            Slot,
            Suffix,
            Slot,
            Suffix)));
        Reference.BuildSignature = TEXT("Signature");
        Reference.ContractVersion = 1;
        Reference.CommitGeneration = FGuid::NewGuid();
        Reference.Resolution = FIntPoint(16, 16);
        return Reference;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyTempAssetGenerationLifetimeTest,
    "DWC.Editor.Transparency.TempAssetStore.GenerationLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyTempAssetGenerationLifetimeTest::RunTest(const FString&)
{
    FWetClothingTransparencyLayerData Layer;
    const EDWCTransparencyTempArtifactKind Kind =
        EDWCTransparencyTempArtifactKind::BaseRevealColor;

    TestEqual(
        TEXT("A layer without a bounded artifact starts in slot zero."),
        FDWCTransparencyTempAssetLifetimePolicy::SelectNextGenerationSlot(Layer, Kind),
        0);

    TSet<FString> PublishedPaths;
    for (int32 CommitIndex = 0; CommitIndex < 12; ++CommitIndex)
    {
        const int32 Slot =
            FDWCTransparencyTempAssetLifetimePolicy::SelectNextGenerationSlot(Layer, Kind);
        FDWCTransparencyTempArtifactReference Current = MakeStageReference(Kind, Slot);
        PublishedPaths.Add(Current.Texture.ToSoftObjectPath().ToString());
        FDWCTransparencyTempAssetLifetimePolicy::PublishCurrentReference(Layer, Current);

        TestEqual(TEXT("Only one current reference is published for an artifact kind."),
            Layer.EditorStageCache.Artifacts.Num(), 1);
        TestEqual(TEXT("The next commit targets the inactive slot."),
            FDWCTransparencyTempAssetLifetimePolicy::SelectNextGenerationSlot(Layer, Kind),
            (Slot + 1) % FDWCTransparencyTempAssetLifetimePolicy::GenerationSlotCount);
    }

    TestEqual(TEXT("Repeated commits use only two persistent package paths."),
        PublishedPaths.Num(),
        FDWCTransparencyTempAssetLifetimePolicy::GenerationSlotCount);

    Layer.EditorStageCache.Artifacts.Reset();
    FDWCTransparencyTempArtifactReference Legacy;
    Legacy.Kind = Kind;
    Legacy.Texture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(
        TEXT("/Game/Test/T_Layer_G01234567_BaseRevealColor.T_Layer_G01234567_BaseRevealColor")));
    Layer.EditorStageCache.Artifacts.Add(Legacy);
    TestEqual(TEXT("A legacy GUID generation migrates into slot zero on its next commit."),
        FDWCTransparencyTempAssetLifetimePolicy::SelectNextGenerationSlot(Layer, Kind), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyTempAssetMetadataNormalizationTest,
    "DWC.Editor.Transparency.TempAssetStore.MetadataNormalization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyTempAssetMetadataNormalizationTest::RunTest(const FString&)
{
    FWetClothingTransparencyLayerData Layer;
    Layer.EditorStageCache.Artifacts.Add(MakeStageReference(
        EDWCTransparencyTempArtifactKind::BaseRevealColor, 0, TEXT("OldA")));
    Layer.EditorStageCache.Artifacts.Add(MakeStageReference(
        EDWCTransparencyTempArtifactKind::BaseRevealColor, 1, TEXT("OldB")));
    Layer.EditorStageCache.Artifacts.Add(MakeStageReference(
        EDWCTransparencyTempArtifactKind::ValidHit, 0, TEXT("ValidHit")));

    const FDWCTransparencyTempArtifactReference Current = MakeStageReference(
        EDWCTransparencyTempArtifactKind::BaseRevealColor, 0, TEXT("Current"));
    FDWCTransparencyTempAssetLifetimePolicy::PublishCurrentReference(Layer, Current);

    TestEqual(TEXT("Publishing collapses duplicate references but preserves other kinds."),
        Layer.EditorStageCache.Artifacts.Num(), 2);
    int32 CurrentCount = 0;
    for (const FDWCTransparencyTempArtifactReference& Reference :
         Layer.EditorStageCache.Artifacts)
    {
        if (Reference.Kind == EDWCTransparencyTempArtifactKind::BaseRevealColor)
        {
            ++CurrentCount;
        }
    }
    TestEqual(TEXT("Exactly one canonical current reference remains."), CurrentCount, 1);

    FWetClothingTransparencyData Data;
    FDWCTransparencyMaterialColorCacheReference& Valid =
        Data.MaterialColorCache.AddDefaulted_GetRef();
    Valid.IdentityVersion = 1;
    Valid.CacheIdentity = TEXT("ValidIdentity");
    Valid.Texture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(
        TEXT("/Game/Test/T_Color.T_Color")));
    Valid.NormalTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(
        TEXT("/Game/Test/T_Normal.T_Normal")));
    Valid.MetallicTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(
        TEXT("/Game/Test/T_Metallic.T_Metallic")));

    FDWCTransparencyMaterialColorCacheReference& Obsolete =
        Data.MaterialColorCache.AddDefaulted_GetRef();
    Obsolete = Valid;
    Obsolete.CacheIdentity = TEXT("ObsoleteIdentity");
    Obsolete.bObsolete = true;

    FDWCTransparencyMaterialColorCacheReference& Incomplete =
        Data.MaterialColorCache.AddDefaulted_GetRef();
    Incomplete.IdentityVersion = 1;
    Incomplete.CacheIdentity = TEXT("IncompleteIdentity");

    TestEqual(TEXT("Obsolete and incomplete cache metadata are pruned."),
        FDWCTransparencyTempAssetLifetimePolicy::PruneObsoleteMaterialSurfaceReferences(Data),
        2);
    TestEqual(TEXT("A complete exact-identity cache reference remains."),
        Data.MaterialColorCache.Num(), 1);
    TestEqual(TEXT("The retained cache identity is unchanged."),
        Data.MaterialColorCache[0].CacheIdentity, FString(TEXT("ValidIdentity")));
    return true;
}

#endif
