// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "DataAssets/WetnessProfile.h"
#include "Editor.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Modes/Part/Authoring/DWCPartAuthoringController.h"
#include "WetClothing/Modes/Part/Partition/WetPartEditingService.h"
#include "WetClothing/Modes/Part/Presentation/DWCPartPresentationModel.h"

namespace
{
    constexpr int32 ExpectedPartOutputs =
        DWCBakeOutput::CPURuntimeData |
        DWCBakeOutput::GPURuntimeData |
        DWCBakeOutput::GPUMaps |
        DWCBakeOutput::WrinkleMaps |
        DWCBakeOutput::TransparencyMaps |
        DWCBakeOutput::RenderProfileData;

    struct FPartAuthoringFixture
    {
        FPartAuthoringFixture()
        {
            Asset = NewObject<UWetClothingAsset>(
                GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
            Document = MakeShared<FDWCEditorAuthoringDocument>(Asset);
            Controller = MakeShared<FDWCPartAuthoringController>(Asset, Document);
            Document->OnChanged().AddLambda(
                [this](const FDWCEditorAuthoringChange& Change)
                {
                    ++NotificationCount;
                    LastChange = Change;
                });
        }

        UWetClothingAsset* Asset = nullptr;
        TSharedPtr<FDWCEditorAuthoringDocument> Document;
        TSharedPtr<FDWCPartAuthoringController> Controller;
        int32 NotificationCount = 0;
        FDWCEditorAuthoringChange LastChange;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCPartAuthoringReadOnlyRefreshTest,
    "DWC.Editor.Authoring.Part.ReadOnlyRefresh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCPartAuthoringReadOnlyRefreshTest::RunTest(const FString&)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    const FDWCPartPresentationSnapshot Snapshot =
        FDWCPartPresentationModel::Build(Asset, 4, TArray<int32>({2, 7}));

    TestTrue(TEXT("A read-only Part refresh provides a virtual default row"),
        Snapshot.Items.Num() == 1 && Snapshot.Items[0].IsValid() &&
            Snapshot.Items[0]->WetPartID == 0 && Snapshot.Items[0]->bSyntheticDefault);
    TestEqual(TEXT("Unassigned islands resolve through the same presentation snapshot"),
        Snapshot.GetEffectivePartID(7), 0);
    TestTrue(TEXT("A read-only Part refresh does not create serialized slot data"),
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.IsEmpty());
    TestTrue(TEXT("A read-only Part refresh does not create serialized profiles"),
        Asset->Authored.PartData.EditableWetPartData.Profiles.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCPartAuthoringCommandContractTest,
    "DWC.Editor.Authoring.Part.CommandContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCPartAuthoringCommandContractTest::RunTest(const FString&)
{
    FPartAuthoringFixture Fixture;
    const FDWCPartAuthoringResult EnableResult =
        Fixture.Controller->SetMaterialSlotWettable(4, true);
    TestTrue(TEXT("Wettable activation commits"), EnableResult.bChanged);
    TestEqual(TEXT("Wettable activation emits one event"), Fixture.NotificationCount, 1);
    TestEqual(TEXT("Wettable activation uses the Part domain"),
        Fixture.LastChange.Domain, EDWCEditorAuthoringDomain::Part);
    TestEqual(TEXT("Wettable activation identifies its slot"),
        Fixture.LastChange.MaterialSlotIndex, 4);
    TestEqual(TEXT("Wettable activation invalidates every Part-derived output"),
        Fixture.LastChange.InvalidatedBakeOutputMask, ExpectedPartOutputs);
    TestTrue(TEXT("Wettable activation invalidates slot presentation"),
        EnumHasAnyFlags(
            Fixture.LastChange.Impact,
            EDWCEditorAuthoringImpact::PartSlotPresentation));

    const FWetClothingAuthoredMaterialSlot* Slot =
        Fixture.Asset->Authored.PartData.EditableWetPartData.FindMaterialSlot(4);
    TestTrue(TEXT("Wettable activation persists the slot and default Part"),
        Slot != nullptr && Slot->bIsWettableSlot && Slot->FindPart(0) != nullptr);

    const FDWCPartAuthoringResult RepeatedEnable =
        Fixture.Controller->SetMaterialSlotWettable(4, true);
    TestFalse(TEXT("Repeating an identical command is a no-op"), RepeatedEnable.bChanged);
    TestEqual(TEXT("A no-op does not emit another event"), Fixture.NotificationCount, 1);
    TestEqual(TEXT("A no-op does not advance the document revision"),
        Fixture.Document->GetRevision(), uint64(1));

    const FDWCPartAuthoringResult AddResult = Fixture.Controller->AddPart(4);
    TestTrue(TEXT("Adding a Part commits"), AddResult.bChanged);
    TestEqual(TEXT("The controller returns the new Part identity"), AddResult.WetPartID, 1);
    TestEqual(TEXT("Adding a Part produces one additional revision"),
        Fixture.Document->GetRevision(), uint64(2));

    TSet<int32> Islands;
    Islands.Add(8);
    Islands.Add(3);
    const FDWCPartAuthoringResult AssignResult =
        Fixture.Controller->AssignIslands(4, AddResult.WetPartID, Islands);
    TestTrue(TEXT("Island assignment commits"), AssignResult.bChanged);
    Slot = Fixture.Asset->Authored.PartData.EditableWetPartData.FindMaterialSlot(4);
    const FWetClothingWetPartEntry* Part = Slot != nullptr ? Slot->FindPart(1) : nullptr;
    TestTrue(TEXT("Island assignment is stored deterministically"),
        Part != nullptr && Part->AssignedUVIslandIDs == TArray<int32>({3, 8}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCPartAuthoringSurfaceInvalidationTest,
    "DWC.Editor.Authoring.Part.SurfaceWaterInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCPartAuthoringSurfaceInvalidationTest::RunTest(const FString&)
{
    FPartAuthoringFixture Fixture;
    Fixture.Controller->SetMaterialSlotWettable(2, true);
    const FDWCPartAuthoringResult AddResult = Fixture.Controller->AddPart(2);

    FWetPartSurfaceWaterSettings DetailSettings;
    DetailSettings.DropletDetailSize = 1.5f;
    TestTrue(TEXT("A detail-only Surface Water edit commits"),
        Fixture.Controller->ApplySurfaceWaterSettings(2, AddResult.WetPartID, DetailSettings).bChanged);
    TestEqual(TEXT("A detail-only edit invalidates only GPU Maps"),
        Fixture.LastChange.InvalidatedBakeOutputMask, DWCBakeOutput::GPUMaps);

    FWetPartSurfaceWaterSettings StampSettings = DetailSettings;
    StampSettings.bOverrideDropletStampSize = true;
    StampSettings.DropletRadiusScale = 2.0f;
    TestTrue(TEXT("A stamp-size Surface Water edit commits"),
        Fixture.Controller->ApplySurfaceWaterSettings(2, AddResult.WetPartID, StampSettings).bChanged);
    TestEqual(TEXT("A stamp-size edit also invalidates GPU runtime data"),
        Fixture.LastChange.InvalidatedBakeOutputMask,
        DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCPartAuthoringMutationMatrixTest,
    "DWC.Editor.Authoring.Part.MutationMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCPartAuthoringMutationMatrixTest::RunTest(const FString&)
{
    FPartAuthoringFixture Fixture;
    Fixture.Controller->SetMaterialSlotWettable(5, true);
    const int32 WetPartID = Fixture.Controller->AddPart(5).WetPartID;

    TestTrue(TEXT("Renaming a Part commits"),
        Fixture.Controller->RenamePart(5, WetPartID, TEXT("Sleeve")).bChanged);
    TestEqual(TEXT("A display name edit does not stale derived outputs"),
        Fixture.LastChange.InvalidatedBakeOutputMask, 0);
    TestFalse(TEXT("A display name edit does not rebuild slot presentation"),
        EnumHasAnyFlags(
            Fixture.LastChange.Impact,
            EDWCEditorAuthoringImpact::PartSlotPresentation));
    TestTrue(TEXT("Changing a Part color commits"),
        Fixture.Controller->SetPartColor(5, WetPartID, FLinearColor::Red).bChanged);
    TestEqual(TEXT("A color edit does not stale derived outputs"),
        Fixture.LastChange.InvalidatedBakeOutputMask, 0);
    TestFalse(TEXT("A color edit does not rebuild slot presentation"),
        EnumHasAnyFlags(
            Fixture.LastChange.Impact,
            EDWCEditorAuthoringImpact::PartSlotPresentation));
    TestTrue(TEXT("Changing editor visibility commits"),
        Fixture.Controller->SetPartVisibility(5, WetPartID, false).bChanged);
    TestEqual(TEXT("An editor visibility edit does not stale derived outputs"),
        Fixture.LastChange.InvalidatedBakeOutputMask, 0);
    TestFalse(TEXT("An editor visibility edit does not rebuild slot presentation"),
        EnumHasAnyFlags(
            Fixture.LastChange.Impact,
            EDWCEditorAuthoringImpact::PartSlotPresentation));

    UWetnessProfile* Profile = NewObject<UWetnessProfile>(GetTransientPackage());
    const FDWCPartAuthoringResult ProfileResult = Fixture.Controller->SetPartProfile(
        5, WetPartID, FSoftObjectPath(Profile), &Profile->Parameters);
    TestTrue(TEXT("Assigning a Wetness Profile commits"), ProfileResult.bChanged);
    TestEqual(TEXT("A profile edit stales all Part-derived outputs"),
        Fixture.LastChange.InvalidatedBakeOutputMask, ExpectedPartOutputs);
    TestTrue(TEXT("A profile edit invalidates slot presentation"),
        EnumHasAnyFlags(
            Fixture.LastChange.Impact,
            EDWCEditorAuthoringImpact::PartSlotPresentation));
    TestFalse(TEXT("Reassigning an identical profile is a no-op"),
        Fixture.Controller->SetPartProfile(
            5, WetPartID, FSoftObjectPath(Profile), &Profile->Parameters).bChanged);

    TestTrue(TEXT("Resetting authored Part settings commits"),
        Fixture.Controller->ResetPart(5, WetPartID).bChanged);
    const FWetClothingWetPartEntry* ResetEntry = FWetPartEditingService::FindEntry(
        Fixture.Asset, FWetPartEditingService::MakeScope(5), WetPartID);
    TestTrue(TEXT("Reset restores display and profile defaults"),
        ResetEntry != nullptr &&
        ResetEntry->DisplayName == FWetPartEditingService::GetDefaultWetPartName(WetPartID) &&
        ResetEntry->Color.Equals(FWetPartEditingService::GetDefaultWetPartColor(WetPartID)) &&
        ResetEntry->bViewEnabled && ResetEntry->ProfileIndex == 0);

    FWetPartAutoPartitionCluster Cluster;
    Cluster.UVIslandIDs = {9, 2, 5};
    TestTrue(TEXT("Applying an auto partition commits through one command"),
        Fixture.Controller->ReplaceWithAutoPartition(5, {Cluster}).bChanged);
    const FWetClothingAuthoredMaterialSlot* Slot =
        Fixture.Asset->Authored.PartData.EditableWetPartData.FindMaterialSlot(5);
    const FWetClothingWetPartEntry* AutoPart = Slot != nullptr ? Slot->FindPart(1) : nullptr;
    TestTrue(TEXT("Auto partition output is stored in deterministic island order"),
        AutoPart != nullptr && AutoPart->AssignedUVIslandIDs == TArray<int32>({2, 5, 9}));
    TestTrue(TEXT("Removing the generated Part commits"),
        Fixture.Controller->RemovePart(5, 1).bChanged);
    TestNull(TEXT("Removal deletes the selected non-default Part"),
        FWetPartEditingService::FindEntry(
            Fixture.Asset, FWetPartEditingService::MakeScope(5), 1));
    TestNotNull(TEXT("Removal preserves the default Part"),
        FWetPartEditingService::FindEntry(
            Fixture.Asset, FWetPartEditingService::MakeScope(5), 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCPartAuthoringUndoRedoTest,
    "DWC.Editor.Authoring.Part.UndoRedo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCPartAuthoringUndoRedoTest::RunTest(const FString&)
{
    TestNotNull(TEXT("The editor transaction system is available"), GEditor);
    if (GEditor == nullptr)
    {
        return false;
    }
    GEditor->ResetTransaction(FText::FromString(TEXT("DWC Part authoring test")));

    FPartAuthoringFixture Fixture;
    Fixture.Controller->SetMaterialSlotWettable(1, true);
    const FDWCPartAuthoringResult AddResult = Fixture.Controller->AddPart(1);
    TestTrue(TEXT("The Part exists before undo"),
        FWetPartEditingService::FindEntry(
            Fixture.Asset,
            FWetPartEditingService::MakeScope(1),
            AddResult.WetPartID) != nullptr);
    TestTrue(TEXT("Undo succeeds"), GEditor->UndoTransaction());
    TestNull(TEXT("Undo removes the Part added by the last command"),
        FWetPartEditingService::FindEntry(
            Fixture.Asset,
            FWetPartEditingService::MakeScope(1),
            AddResult.WetPartID));
    TestTrue(TEXT("Redo succeeds"), GEditor->RedoTransaction());
    TestNotNull(TEXT("Redo restores the Part"),
        FWetPartEditingService::FindEntry(
            Fixture.Asset,
            FWetPartEditingService::MakeScope(1),
            AddResult.WetPartID));

    GEditor->ResetTransaction(FText::FromString(TEXT("DWC Part authoring test complete")));
    return true;
}

#endif
