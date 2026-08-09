//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyIntermediateAssetPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyIntermediateAssetCookPolicyTest,
    "DWC.Transparency.Cook.IntermediateAssetPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyIntermediateAssetCookPolicyTest::RunTest(const FString& Parameters)
{
    const FString UniqueToken = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString TempPackageName = FString::Printf(
        TEXT("/Game/__DWC_Automation__/Generated/WCA_%s/Textures/Transparency/Temp/T_Source"),
        *UniqueToken);
    UPackage* TempPackage = CreatePackage(*TempPackageName);
    UTexture2D* TempTexture = NewObject<UTexture2D>(TempPackage, TEXT("T_Source"));

    TestTrue(TEXT("The dynamic WCA Temp path is recognized."),
        FDWCTransparencyIntermediateAssetPolicy::IsIntermediatePackagePath(TempPackageName));
    TestFalse(TEXT("A new intermediate package is not implicitly editor-only."),
        TempPackage->HasAnyPackageFlags(PKG_EditorOnly));

    bool bChanged = false;
    FString Error;
    TestTrue(TEXT("The intermediate package accepts the editor-only policy."),
        FDWCTransparencyIntermediateAssetPolicy::EnsureEditorOnlyPackage(
            *TempTexture, &bChanged, &Error));
    TestTrue(TEXT("Applying the policy changes an untagged package."), bChanged);
    TestTrue(TEXT("The intermediate package is marked editor-only."),
        TempPackage->HasAnyPackageFlags(PKG_EditorOnly));
    TestTrue(TEXT("A tagged intermediate reference is excluded from cook."),
        FDWCTransparencyIntermediateAssetPolicy::IsReferenceCookExcluded(
            FSoftObjectPath(TempTexture), &Error));

    bChanged = true;
    TestTrue(TEXT("Applying the policy is idempotent."),
        FDWCTransparencyIntermediateAssetPolicy::EnsureEditorOnlyPackage(
            *TempTexture, &bChanged, &Error));
    TestFalse(TEXT("An already tagged package is not dirtied again."), bChanged);

    const FString FinalPackageName = FString::Printf(
        TEXT("/Game/__DWC_Automation__/Generated/WCA_%s/Textures/Transparency/T_Final"),
        *UniqueToken);
    UPackage* FinalPackage = CreatePackage(*FinalPackageName);
    UTexture2D* FinalTexture = NewObject<UTexture2D>(FinalPackage, TEXT("T_Final"));
    TestFalse(TEXT("The final Transparency path is not an intermediate path."),
        FDWCTransparencyIntermediateAssetPolicy::IsIntermediatePackagePath(FinalPackageName));
    TestFalse(TEXT("The intermediate policy refuses a final runtime texture."),
        FDWCTransparencyIntermediateAssetPolicy::EnsureEditorOnlyPackage(
            *FinalTexture, &bChanged, &Error));
    TestFalse(TEXT("The refused final package remains cookable."),
        FinalPackage->HasAnyPackageFlags(PKG_EditorOnly));

    TempPackage->SetDirtyFlag(false);
    FinalPackage->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyIntermediateReferenceSerializationTest,
    "DWC.Transparency.Cook.EditorOnlyReferenceBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyIntermediateReferenceSerializationTest::RunTest(const FString& Parameters)
{
#if WITH_EDITORONLY_DATA
    const FProperty* StageCacheProperty = FindFProperty<FProperty>(
        FWetClothingTransparencyLayerData::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWetClothingTransparencyLayerData, EditorStageCache));
    TestNotNull(TEXT("Layer stage cache is reflected."), StageCacheProperty);
    if (StageCacheProperty != nullptr)
    {
        TestTrue(TEXT("Layer stage cache is stripped from runtime serialization."),
            StageCacheProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    }

    const FProperty* MaterialColorCacheProperty = FindFProperty<FProperty>(
        FWetClothingTransparencyData::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWetClothingTransparencyData, MaterialColorCache));
    TestNotNull(TEXT("Material color cache is reflected."), MaterialColorCacheProperty);
    if (MaterialColorCacheProperty != nullptr)
    {
        TestTrue(TEXT("Material color cache is stripped from runtime serialization."),
            MaterialColorCacheProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    }

    const FProperty* TransparencyLayersProperty = FindFProperty<FProperty>(
        FWetClothingTransparencyData::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWetClothingTransparencyData, TransparencyLayers));
    TestNotNull(TEXT("Runtime transparency layers are reflected."), TransparencyLayersProperty);
    if (TransparencyLayersProperty != nullptr)
    {
        TestFalse(TEXT("Authored transparency layers remain in runtime serialization."),
            TransparencyLayersProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    }

    const FProperty* BakedMapsProperty = FindFProperty<FProperty>(
        FWetClothingTransparencyLayerData::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWetClothingTransparencyLayerData, BakedMaps));
    TestNotNull(TEXT("Final baked transparency maps are reflected."), BakedMapsProperty);
    if (BakedMapsProperty != nullptr)
    {
        TestFalse(TEXT("Final baked transparency maps remain available at runtime."),
            BakedMapsProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    }
#endif
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyIntermediateArtifactKindCoverageTest,
    "DWC.Transparency.Cook.IntermediateArtifactKindCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyIntermediateArtifactKindCoverageTest::RunTest(const FString& Parameters)
{
    const EDWCTransparencyTempArtifactKind IntermediateKinds[] =
    {
        EDWCTransparencyTempArtifactKind::SourceMaterialColor,
        EDWCTransparencyTempArtifactKind::BaseRevealColor,
        EDWCTransparencyTempArtifactKind::ValidHit,
        EDWCTransparencyTempArtifactKind::HitSource,
        EDWCTransparencyTempArtifactKind::HitDistance,
        EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
        EDWCTransparencyTempArtifactKind::OuterCoverage,
        EDWCTransparencyTempArtifactKind::OuterIslandID
    };

    for (const EDWCTransparencyTempArtifactKind Kind : IntermediateKinds)
    {
        TestTrue(TEXT("Every rebuildable transparency artifact is classified as intermediate."),
            FDWCTransparencyIntermediateAssetPolicy::IsIntermediateArtifactKind(Kind));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyLoadedReferenceCookBoundaryTest,
    "DWC.Transparency.Cook.LoadedReferenceRepairAndRuntimeBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyLoadedReferenceCookBoundaryTest::RunTest(const FString& Parameters)
{
    const FString UniqueToken = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto MakeTexture = [&UniqueToken](const TCHAR* Leaf, const bool bIntermediate)
    {
        const FString PackageName = bIntermediate
            ? FString::Printf(
                TEXT("/Game/__DWC_Automation__/Generated/WCA_%s/Textures/Transparency/Temp/%s"),
                *UniqueToken,
                Leaf)
            : FString::Printf(
                TEXT("/Game/__DWC_Automation__/Generated/WCA_%s/Textures/Transparency/%s"),
                *UniqueToken,
                Leaf);
        UPackage* Package = CreatePackage(*PackageName);
        return NewObject<UTexture2D>(Package, Leaf);
    };

    UTexture2D* MaterialColorTexture = MakeTexture(TEXT("T_MaterialColor"), true);
    UTexture2D* StageArtifactTexture = MakeTexture(TEXT("T_StageArtifact"), true);
    UTexture2D* FinalTexture = MakeTexture(TEXT("T_Final"), false);
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());

    FDWCTransparencyMaterialColorCacheReference& MaterialColorReference =
        Asset->Authored.TransparencyData.MaterialColorCache.AddDefaulted_GetRef();
    MaterialColorReference.Texture = MaterialColorTexture;

    FWetClothingTransparencyLayerData& Layer =
        Asset->Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
    FDWCTransparencyTempArtifactReference& ArtifactReference =
        Layer.EditorStageCache.Artifacts.AddDefaulted_GetRef();
    ArtifactReference.Kind = EDWCTransparencyTempArtifactKind::BaseRevealColor;
    ArtifactReference.Texture = StageArtifactTexture;

    FWetClothingBakedTransparencyMap& BakedMap = Layer.BakedMaps.AddDefaulted_GetRef();
    BakedMap.TransparencyMap = FinalTexture;

    TArray<UPackage*> ChangedPackages;
    TArray<FString> Warnings;
    FDWCTransparencyIntermediateAssetPolicy::RepairLoadedReferences(
        *Asset, ChangedPackages, Warnings);

    TestEqual(TEXT("Both loaded intermediate packages are repaired."),
        ChangedPackages.Num(), 2);
    TestTrue(TEXT("Valid loaded intermediate references produce no warnings."),
        Warnings.IsEmpty());
    TestTrue(TEXT("The material color cache package is editor-only."),
        MaterialColorTexture->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly));
    TestTrue(TEXT("The Stage cache artifact package is editor-only."),
        StageArtifactTexture->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly));
    TestFalse(TEXT("Repair never marks the final runtime map editor-only."),
        FinalTexture->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly));
    TestFalse(TEXT("The final runtime map is not treated as cook-excluded intermediate data."),
        FDWCTransparencyIntermediateAssetPolicy::IsReferenceCookExcluded(
            FSoftObjectPath(FinalTexture)));

    ChangedPackages.Reset();
    FDWCTransparencyIntermediateAssetPolicy::RepairLoadedReferences(
        *Asset, ChangedPackages, Warnings);
    TestTrue(TEXT("Loaded-reference repair is idempotent."), ChangedPackages.IsEmpty());

    MaterialColorTexture->GetOutermost()->SetDirtyFlag(false);
    StageArtifactTexture->GetOutermost()->SetDirtyFlag(false);
    FinalTexture->GetOutermost()->SetDirtyFlag(false);
    return true;
}

#endif
