//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Preview/Layers/DWCEditorPreviewLayerResolver.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewStaleTransparencyLayerTest,
    "DWC.Editor.Preview.Layers.StaleTransparencyRemainsVisible",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewStaleTransparencyLayerTest::RunTest(const FString& Parameters)
{
    constexpr int32 MaterialSlotIndex = 4;
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
    UTexture2D* RevealNormalTexture = NewObject<UTexture2D>(GetTransientPackage());

    FWetClothingTransparencyLayerData& Layer =
        Asset->Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
    Layer.TargetSurface.OuterMaterialSlotIndex = MaterialSlotIndex;
    Layer.SourceType = EDWCTransparencySourceType::ManualColorOrTexture;
    FWetClothingAuthoredMaterialSlot& WettableSlot =
        Asset->Authored.PartData.EditableWetPartData.FindOrAddMaterialSlot(MaterialSlotIndex);
    WettableSlot.bIsWettableSlot = true;
    FWetClothingBakedTransparencyMap& BakedMap = Layer.BakedMaps.AddDefaulted_GetRef();
    BakedMap.MaterialSlotIndex = MaterialSlotIndex;
    BakedMap.TransparencyMap = Texture;

    FDWCEditorPreviewSavedLayers Saved =
        FDWCEditorPreviewLayerResolver::Resolve(Asset, MaterialSlotIndex);
    TestEqual(TEXT("A texture-only editor result is classified as stale"),
        Saved.TransparencyState, EDWCEditorSavedLayerState::Stale);
    TestEqual(TEXT("A stale editor result keeps its texture"), Saved.TransparencyMap.Get(), Texture);
    TestNull(TEXT("An incomplete transparency result does not bind a Reveal Normal"),
        Saved.RevealNormalMap.Get());
    TestNull(TEXT("Runtime rejects the stale result"),
        Asset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(MaterialSlotIndex));

    BakedMap.RevealNormalMap = RevealNormalTexture;
    BakedMap.RevealNormalBuildSignature = TEXT("RevealNormalBuild");
    BakedMap.bSourceCoverageBakedIntoRevealNormal = true;
    Saved = FDWCEditorPreviewLayerResolver::Resolve(Asset, MaterialSlotIndex);
    TestEqual(TEXT("A complete Reveal Normal remains visible with a stale transparency layer"),
        Saved.RevealNormalMap.Get(), RevealNormalTexture);

    BakedMap.BakeGuid = FGuid::NewGuid();
    BakedMap.BuildSignature = TEXT("CurrentTransparencyBuild");
    Saved = FDWCEditorPreviewLayerResolver::Resolve(Asset, MaterialSlotIndex);
    TestEqual(TEXT("A runtime-usable result is classified as current"),
        Saved.TransparencyState, EDWCEditorSavedLayerState::Current);
    TestNotNull(TEXT("Runtime accepts the current result"),
        Asset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(MaterialSlotIndex));

    Layer.SourceType = EDWCTransparencySourceType::SameMeshMaterialSlots;
    BakedMap.bMetallicDarkeningBakedIntoColor = true;
    TestNotNull(TEXT("A raycast layer accepts a complete Reveal Normal payload"),
        Asset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(MaterialSlotIndex));
    BakedMap.RevealNormalMap = nullptr;
    TestNull(TEXT("A raycast layer rejects an incomplete Reveal Normal payload"),
        Asset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(MaterialSlotIndex));
    BakedMap.RevealNormalMap = RevealNormalTexture;

    Layer.MarkFinalBakeStale();
    Saved = FDWCEditorPreviewLayerResolver::Resolve(Asset, MaterialSlotIndex);
    TestEqual(TEXT("Invalidation changes the editor result to stale"),
        Saved.TransparencyState, EDWCEditorSavedLayerState::Stale);
    TestEqual(TEXT("Invalidation does not hide the editor texture"), Saved.TransparencyMap.Get(), Texture);
    TestEqual(TEXT("Invalidation does not hide the complete Reveal Normal payload"),
        Saved.RevealNormalMap.Get(), RevealNormalTexture);
    TestNull(TEXT("Runtime still rejects the invalidated result"),
        Asset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(MaterialSlotIndex));

    Asset->SetTransparencyBakeStatus(EDWCBakeStatus::OutOfDate);
    const FWCAValidationReport Validation =
        BuildWCAValidationReport(*Asset, EWCAValidationMode::Fast, true);
    const FWCAValidationIssue* TransparencyIssue = Validation.Issues.FindByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return Issue.Section == EWCAValidationSection::TransparencyMaps &&
                   Issue.Status.ToString() == TEXT("Out of Date");
        });
    TestNotNull(TEXT("Fast validation reports the stale transparency output"), TransparencyIssue);
    return true;
}

#endif
