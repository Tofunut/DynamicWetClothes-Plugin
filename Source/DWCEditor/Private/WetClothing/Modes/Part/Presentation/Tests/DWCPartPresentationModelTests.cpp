// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Modes/Part/Presentation/DWCPartPresentationModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCPartPresentationSnapshotTest,
    "DWC.Editor.Authoring.Part.PresentationSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCPartPresentationSnapshotTest::RunTest(const FString&)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    FWetClothingEditableWetPartData& Editable =
        Asset->Authored.PartData.EditableWetPartData;
    FWetClothingAuthoredMaterialSlot& Slot = Editable.FindOrAddMaterialSlot(5);
    Slot.bIsWettableSlot = true;

    FWetClothingWetPartEntry& VisiblePart = Slot.WetPartEntries.AddDefaulted_GetRef();
    VisiblePart.WetPartID = 1;
    VisiblePart.DisplayName = TEXT("Visible");
    VisiblePart.Color = FLinearColor::Red;
    VisiblePart.AssignedUVIslandIDs = {11};

    FWetClothingWetPartEntry& HiddenPart = Slot.WetPartEntries.AddDefaulted_GetRef();
    HiddenPart.WetPartID = 2;
    HiddenPart.DisplayName = TEXT("Hidden");
    HiddenPart.Color = FLinearColor::Green;
    HiddenPart.bViewEnabled = false;
    HiddenPart.AssignedUVIslandIDs = {12};

    const int32 OriginalEntryCount = Slot.WetPartEntries.Num();
    const int32 OriginalProfileCount = Editable.Profiles.Num();
    const TArray<int32> IslandIDs({10, 11, 12});
    const FDWCPartPresentationSnapshot Snapshot =
        FDWCPartPresentationModel::Build(Asset, 5, IslandIDs);

    const FDWCPartPresentationItemPtr DefaultItem = Snapshot.FindItem(0);
    TestTrue(TEXT("A missing serialized default is represented virtually"),
        DefaultItem.IsValid() && DefaultItem->bSyntheticDefault);
    TestEqual(TEXT("Unassigned islands use Part 0"), Snapshot.GetEffectivePartID(10), 0);
    TestEqual(TEXT("Explicit islands keep their Part"), Snapshot.GetEffectivePartID(11), 1);
    TestTrue(TEXT("Visible Part colors are supplied to the preview"),
        Snapshot.PreviewIslandColors.Contains(11));
    TestTrue(TEXT("Hidden Part islands are hidden consistently"),
        Snapshot.HiddenIslandIDs.Contains(12) &&
            !Snapshot.UVIslandColors.Contains(12) &&
            !Snapshot.PreviewIslandColors.Contains(12));
    TestEqual(TEXT("Building the view model does not materialize a default Part"),
        Slot.WetPartEntries.Num(), OriginalEntryCount);
    TestEqual(TEXT("Building the view model does not create profiles"),
        Editable.Profiles.Num(), OriginalProfileCount);

    const FDWCPartPresentationSnapshot Rebuilt =
        FDWCPartPresentationModel::Build(Asset, 5, IslandIDs);
    TestTrue(TEXT("Identical presentation inputs produce an equivalent snapshot"),
        Snapshot.IsEquivalentTo(Rebuilt));

    const FDWCPartSlotPresentationItem SlotPresentation =
        FDWCPartSlotPresentationModel::BuildSlot(Asset, 5, TSet<int32>());
    TestTrue(TEXT("The slot snapshot captures authored Wettable state"),
        SlotPresentation.bIsWettableSlot);
    TestFalse(TEXT("A slot without generated topology is not Data UV ready"),
        SlotPresentation.bDataUVReady);
    TestTrue(TEXT("An incomplete Wettable slot requests Part Map attention"),
        SlotPresentation.bNeedsPartMapAttention);

    FDWCPartSlotPresentationSnapshot SlotSnapshot;
    TestTrue(TEXT("The first slot snapshot update changes presentation state"),
        SlotSnapshot.Update(SlotPresentation));
    const uint32 InitialSlotHash = SlotSnapshot.SemanticHash;
    TestFalse(TEXT("An identical slot snapshot is a no-op"),
        SlotSnapshot.Update(SlotPresentation));
    TestEqual(TEXT("A no-op update preserves the aggregate hash"),
        SlotSnapshot.SemanticHash, InitialSlotHash);

    FDWCPartSlotPresentationItem ChangedSlotPresentation = SlotPresentation;
    ChangedSlotPresentation.bIsWettableSlot = false;
    ChangedSlotPresentation.SemanticHash = HashCombine(
        ChangedSlotPresentation.SemanticHash,
        GetTypeHash(false));
    TestTrue(TEXT("A changed slot snapshot invalidates presentation state"),
        SlotSnapshot.Update(MoveTemp(ChangedSlotPresentation)));
    TestNotEqual(TEXT("A changed slot snapshot updates the aggregate hash"),
        SlotSnapshot.SemanticHash, InitialSlotHash);
    return true;
}

#endif
