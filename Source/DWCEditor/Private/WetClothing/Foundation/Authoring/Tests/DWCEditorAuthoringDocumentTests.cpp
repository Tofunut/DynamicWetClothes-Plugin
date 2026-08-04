#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "UObject/UnrealType.h"
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
    FDWCEditorAuthoringDocumentCommandTest,
    "DWC.Editor.Authoring.Document.CommandContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAuthoringDocumentCommandTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    TSharedRef<FDWCEditorAuthoringDocument> Document =
        MakeShared<FDWCEditorAuthoringDocument>(Asset);

    int32 NotificationCount = 0;
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
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAuthoringDocumentInteractiveTest,
    "DWC.Editor.Authoring.Document.InteractiveContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAuthoringDocumentInteractiveTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
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
