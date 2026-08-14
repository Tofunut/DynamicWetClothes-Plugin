// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "Editor.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAuthoringDerivedBulkTransactionPolicyTest,
    "DWC.Editor.Authoring.DerivedBulkIsNonTransactional",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAuthoringDerivedBulkTransactionPolicyTest::RunTest(const FString& Parameters)
{
    const FProperty* BulkProperty = FindFProperty<FProperty>(
        FWCADerivedData::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWCADerivedData, Bulk));
    TestNotNull(TEXT("FWCADerivedData exposes the runtime bulk property"), BulkProperty);
    if (BulkProperty != nullptr)
    {
        TestTrue(
            TEXT("Rebuildable runtime bulk data is excluded from editor undo snapshots"),
            BulkProperty->HasAnyPropertyFlags(CPF_NonTransactional));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyStrokeHistoryCookBoundaryTest,
    "DWC.Editor.Authoring.TransparencyStrokeHistoryCookBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyStrokeHistoryCookBoundaryTest::RunTest(const FString&)
{
    const FProperty* HistoryProperty = FindFProperty<FProperty>(
        FWetClothingTransparencyLayerData::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWetClothingTransparencyLayerData, EditorStrokeHistory));
    const FProperty* LegacyAlphaProperty = FindFProperty<FProperty>(
        FWetClothingTransparencyLayerData::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWetClothingTransparencyLayerData, EditableStrokes));
    const FProperty* LegacyRevealProperty = FindFProperty<FProperty>(
        FWetClothingTransparencyLayerData::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWetClothingTransparencyLayerData, RevealColorPaintStrokes));

    TestNotNull(TEXT("The canonical stroke history property exists in editor builds."), HistoryProperty);
    TestNotNull(TEXT("The legacy alpha migration property exists in editor builds."), LegacyAlphaProperty);
    TestNotNull(TEXT("The legacy reveal migration property exists in editor builds."), LegacyRevealProperty);
    if (HistoryProperty != nullptr)
    {
        TestTrue(TEXT("Canonical stroke history is excluded from cooked data."),
            HistoryProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    }
    if (LegacyAlphaProperty != nullptr)
    {
        TestTrue(TEXT("Legacy alpha migration data is excluded from cooked data."),
            LegacyAlphaProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    }
    if (LegacyRevealProperty != nullptr)
    {
        TestTrue(TEXT("Legacy reveal migration data is excluded from cooked data."),
            LegacyRevealProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyStrokeHistoryMigrationTest,
    "DWC.Editor.Authoring.TransparencyStrokeHistoryMigration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyStrokeHistoryMigrationTest::RunTest(const FString&)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(
        GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
    FWetClothingTransparencyLayerData& Layer =
        Asset->Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
    Layer.LayerGuid = FGuid::NewGuid();

    FDWCTransparencyBrushSample AlphaSample;
    AlphaSample.PositionUV = FVector2D(0.125, 0.75);
    AlphaSample.UVIslandID = 4;
    AlphaSample.RadiusUV = 0.05f;
    AlphaSample.Strength = 0.8f;
    Layer.EditableStrokes.AddDefaulted_GetRef().Samples.Add(AlphaSample);

    FDWCTransparencyBrushSample RevealSample = AlphaSample;
    RevealSample.PositionUV = FVector2D(0.625, 0.25);
    Layer.RevealColorPaintStrokes.AddDefaulted_GetRef().Samples.Add(RevealSample);

    UDWCTransparencyLayerStrokeHistory* History =
        Asset->EnsureTransparencyLayerStrokeHistory(Layer.LayerGuid);
    TestNotNull(TEXT("Legacy stroke arrays migrate into a layer history object."), History);
    if (History == nullptr)
    {
        return false;
    }

    TestTrue(TEXT("The migrated history is transactional."), History->HasAnyFlags(RF_Transactional));
    TestTrue(TEXT("The migrated history is owned by its WCA."), History->GetOuter() == Asset);
    TestTrue(TEXT("Legacy inline alpha storage is released."), Layer.EditableStrokes.IsEmpty());
    TestTrue(TEXT("Legacy inline reveal storage is released."), Layer.RevealColorPaintStrokes.IsEmpty());
    TestEqual(TEXT("Alpha stroke count is preserved."), Layer.GetEditableStrokes().Num(), 1);
    TestEqual(TEXT("Reveal stroke count is preserved."), Layer.GetRevealColorPaintStrokes().Num(), 1);
    TestTrue(TEXT("Migrated alpha samples use compact storage."),
        Layer.GetEditableStrokes()[0].Samples.IsEmpty() &&
        Layer.GetEditableStrokes()[0].CompactSamples.Num() == 1);
    TestTrue(TEXT("Migrated reveal samples use compact storage."),
        Layer.GetRevealColorPaintStrokes()[0].Samples.IsEmpty() &&
        Layer.GetRevealColorPaintStrokes()[0].CompactSamples.Num() == 1);
    TestFalse(TEXT("A second compaction pass is idempotent."), History->CompactLegacySamples());
    TestEqual(TEXT("Repeated history resolution keeps the same object."),
        Asset->EnsureTransparencyLayerStrokeHistory(Layer.LayerGuid), History);

    UWetClothingAsset* Duplicate = DuplicateObject<UWetClothingAsset>(Asset, GetTransientPackage());
    TestNotNull(TEXT("A WCA containing compact stroke history duplicates."), Duplicate);
    if (Duplicate != nullptr)
    {
        const FWetClothingTransparencyLayerData& DuplicateLayer =
            Duplicate->Authored.TransparencyData.TransparencyLayers[0];
        TestNotNull(TEXT("The duplicated WCA retains its stroke history."),
            DuplicateLayer.GetEditorStrokeHistory());
        TestTrue(TEXT("Instanced stroke history is not shared by duplicated WCAs."),
            DuplicateLayer.GetEditorStrokeHistory() != History);
        TestEqual(TEXT("Duplicated alpha sample count is preserved."),
            DuplicateLayer.GetEditableStrokes()[0].GetSampleCount(), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyStrokeHistoryTransactionLifetimeTest,
    "DWC.Editor.Authoring.TransparencyStrokeHistoryTransactionLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyStrokeHistoryTransactionLifetimeTest::RunTest(const FString&)
{
    TestNotNull(TEXT("The editor transaction system is available."), GEditor);
    if (GEditor == nullptr)
    {
        return false;
    }
    GEditor->ResetTransaction(FText::FromString(TEXT("DWC stroke history lifetime test")));

    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(
        GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
    FWetClothingTransparencyLayerData& Layer =
        Asset->Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
    Layer.LayerGuid = FGuid::NewGuid();
    const FGuid LayerGuid = Layer.LayerGuid;
    UDWCTransparencyLayerStrokeHistory* History =
        Asset->EnsureTransparencyLayerStrokeHistory(LayerGuid);
    TestNotNull(TEXT("The transaction target history exists."), History);
    if (History == nullptr)
    {
        return false;
    }

    TSharedRef<FDWCEditorAuthoringDocument> Document =
        MakeShared<FDWCEditorAuthoringDocument>(Asset);
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
                    EDWCEditorAuthoringImpact::ElementList |
                    EDWCEditorAuthoringImpact::TransparencyFinalBake;
    Change.LayerGuid = LayerGuid;

    const FDWCEditorAuthoringResult Result = Document->Edit(
        FText::FromString(TEXT("Add compact transparency stroke")),
        Change,
        History,
        [LayerGuid](UWetClothingAsset& MutableAsset)
        {
            FWetClothingTransparencyLayerData* MutableLayer =
                MutableAsset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            if (MutableLayer == nullptr)
            {
                return false;
            }
            FDWCTransparencyBrushStroke& Stroke =
                MutableLayer->GetMutableEditableStrokes().AddDefaulted_GetRef();
            Stroke.StrokeGuid = FGuid::NewGuid();
            Stroke.AddSample(FDWCTransparencyBrushSample());
            return true;
        });

    TestTrue(TEXT("A scoped history edit commits."), Result.bChanged);
    const auto FindLayer = [Asset, LayerGuid]() -> const FWetClothingTransparencyLayerData*
    {
        return Asset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
            [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
            {
                return Candidate.LayerGuid == LayerGuid;
            });
    };
    const FWetClothingTransparencyLayerData* CurrentLayer = FindLayer();
    TestTrue(TEXT("The committed stroke is visible."),
        CurrentLayer != nullptr && CurrentLayer->GetEditableStrokes().Num() == 1);
    TestTrue(TEXT("The history object participates in the transaction buffer."),
        GEditor->IsObjectInTransactionBuffer(History));
    TestTrue(TEXT("Undo succeeds for the scoped history edit."), GEditor->UndoTransaction());
    CurrentLayer = FindLayer();
    TestTrue(TEXT("Undo removes only the committed stroke payload."),
        CurrentLayer != nullptr && CurrentLayer->GetEditableStrokes().IsEmpty());
    TestTrue(TEXT("Redo succeeds for the scoped history edit."), GEditor->RedoTransaction());
    CurrentLayer = FindLayer();
    TestTrue(TEXT("Redo restores the compact stroke."),
        CurrentLayer != nullptr && CurrentLayer->GetEditableStrokes().Num() == 1);
    if (CurrentLayer != nullptr && !CurrentLayer->GetEditableStrokes().IsEmpty())
    {
        TestTrue(TEXT("Redo preserves compact sample storage."),
            CurrentLayer->GetEditableStrokes()[0].Samples.IsEmpty() &&
            CurrentLayer->GetEditableStrokes()[0].CompactSamples.Num() == 1);
    }

    GEditor->ResetTransaction(FText::FromString(TEXT("DWC stroke history lifetime test complete")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAuthoringDocumentCommandTest,
    "DWC.Editor.Authoring.Document.CommandContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAuthoringDocumentCommandTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset*                      Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    TSharedRef<FDWCEditorAuthoringDocument> Document =
        MakeShared<FDWCEditorAuthoringDocument>(Asset);

    int32                     NotificationCount = 0;
    FDWCEditorAuthoringChange LastChange;
    Document->OnChanged().AddLambda(
        [&NotificationCount, &LastChange](const FDWCEditorAuthoringChange& Change)
        {
            ++NotificationCount;
            LastChange = Change;
        });

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
                    EDWCEditorAuthoringImpact::ElementList |
                    EDWCEditorAuthoringImpact::WrinkleBake;
    Change.MaterialSlotIndex = 3;

    const FDWCEditorAuthoringResult Result = Document->Edit(
        FText::FromString(TEXT("Add test wrinkle patch")),
        Change,
        [](UWetClothingAsset& MutableAsset)
        {
            MutableAsset.Authored.WrinkleData.EditablePatches.AddDefaulted();
            return true;
        });

    TestTrue(TEXT("A valid command changes the asset"), Result.bChanged);
    TestEqual(TEXT("A committed command increments the revision"), Document->GetRevision(), uint64(1));
    TestEqual(TEXT("A committed command broadcasts once"), NotificationCount, 1);
    TestEqual(TEXT("The event preserves the material slot"), LastChange.MaterialSlotIndex, 3);
    TestEqual(
        TEXT("The event reports committed phase"),
        LastChange.Phase,
        EDWCEditorAuthoringChangePhase::Committed);
    TestEqual(
        TEXT("The mutation reached the serialized authored data"),
        Asset->Authored.WrinkleData.EditablePatches.Num(),
        1);

    FDWCEditorAuthoringChange InvalidChange = Change;
    InvalidChange.Domain = EDWCEditorAuthoringDomain::None;
    const FDWCEditorAuthoringResult InvalidResult = Document->Edit(
        FText::FromString(TEXT("Invalid command")),
        InvalidChange,
        [](UWetClothingAsset&)
        {
            return true;
        });
    TestFalse(TEXT("A command without a domain is rejected"), InvalidResult.bChanged);
    TestEqual(TEXT("A rejected command does not advance revision"), Document->GetRevision(), uint64(1));

    FDWCEditorAuthoringChange InvalidCrossOutputChange = Change;
    InvalidCrossOutputChange.InvalidatedBakeOutputMask = DWCBakeOutput::GPUMaps;
    const FDWCEditorAuthoringResult InvalidCrossOutputResult = Document->Edit(
        FText::FromString(TEXT("Invalid cross-output command")),
        InvalidCrossOutputChange,
        [](UWetClothingAsset&)
        {
            return true;
        });
    TestFalse(TEXT("A non-Part command cannot use the cross-output mask"),
        InvalidCrossOutputResult.bChanged);
    TestEqual(TEXT("A rejected cross-output command does not advance revision"),
        Document->GetRevision(), uint64(1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAuthoringDocumentInteractiveTest,
    "DWC.Editor.Authoring.Document.InteractiveContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAuthoringDocumentInteractiveTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset*                      Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    TSharedRef<FDWCEditorAuthoringDocument> Document =
        MakeShared<FDWCEditorAuthoringDocument>(Asset);

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
                    EDWCEditorAuthoringImpact::ElementList |
                    EDWCEditorAuthoringImpact::Preview |
                    EDWCEditorAuthoringImpact::TransparencyFinalBake;
    Change.LayerGuid = FGuid::NewGuid();
    Change.ElementGuid = FGuid::NewGuid();

    FString Error;
    TestTrue(
        TEXT("A valid continuous command begins"),
        Document->BeginInteractiveEdit(
            FText::FromString(TEXT("Interactive transparency edit")),
            Change,
            &Error));
    TestTrue(TEXT("The document reports an active interactive edit"), Document->HasInteractiveEdit());

    const FDWCEditorAuthoringResult UpdateResult = Document->UpdateInteractiveEdit(
        Change,
        [](UWetClothingAsset& MutableAsset)
        {
            MutableAsset.Authored.TransparencyData.TransparencyPreviewStrength = 0.75f;
            return true;
        });
    TestTrue(TEXT("The continuous update changes authored data"), UpdateResult.bChanged);
    TestEqual(TEXT("Interactive updates do not commit a revision"), Document->GetRevision(), uint64(0));

    const FDWCEditorAuthoringResult CommitResult = Document->CommitInteractiveEdit(Change);
    TestTrue(TEXT("The continuous command commits"), CommitResult.bChanged);
    TestFalse(TEXT("The document releases the interactive edit"), Document->HasInteractiveEdit());
    TestEqual(TEXT("The continuous command commits one revision"), Document->GetRevision(), uint64(1));
    TestTrue(
        TEXT("The continuous mutation reached authored data"),
        FMath::IsNearlyEqual(Asset->Authored.TransparencyData.TransparencyPreviewStrength, 0.75f));
    return true;
}

#endif
