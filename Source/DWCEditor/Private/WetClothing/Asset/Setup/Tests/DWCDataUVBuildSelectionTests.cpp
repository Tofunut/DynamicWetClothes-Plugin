// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Asset/Setup/DWCDataUVBuildService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCDataUVBuildSelectionContractTest,
    "DWC.Editor.DataUV.ImmutableBuildSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCDataUVBuildSelectionContractTest::RunTest(const FString&)
{
    const TArray<int32> RequestedSlots({3, 1, 3});
    const TArray<int32> ExistingSlots({4, 1});
    FString             Error;

    const TOptional<FDWCDataUVBuildSelection> Selection =
        FDWCDataUVBuildSelection::Create(RequestedSlots, ExistingSlots, 6, &Error);
    TestTrue(TEXT("A valid selection is captured"), Selection.IsSet());
    TestTrue(TEXT("A valid selection has no error"), Error.IsEmpty());
    if (!Selection.IsSet())
    {
        return false;
    }

    TestEqual(
        TEXT("Requested slots are sorted and deduplicated"),
        Selection->GetRequestedMaterialSlotIndices(),
        TArray<int32>({1, 3}));
    TestEqual(
        TEXT("Existing layout slots are sorted and deduplicated"),
        Selection->GetExistingLayoutMaterialSlotIndices(),
        TArray<int32>({1, 4}));
    TestEqual(
        TEXT("Build slots are the stable union of requested and existing slots"),
        Selection->GetBuildMaterialSlotIndices(),
        TArray<int32>({1, 3, 4}));

    const uint32 CapturedHash = Selection->GetSemanticHash();
    TArray<int32> MutableRequestedSlots = RequestedSlots;
    MutableRequestedSlots.Reset();
    TestEqual(
        TEXT("Mutating the caller array cannot change the captured selection"),
        Selection->GetSemanticHash(),
        CapturedHash);
    TestTrue(TEXT("Requested membership is explicit"), Selection->IsRequestedMaterialSlot(3));
    TestTrue(TEXT("Existing layout membership is explicit"), Selection->IsExistingLayoutMaterialSlot(4));
    TestFalse(TEXT("An unrelated slot is not a build target"), Selection->IsBuildMaterialSlot(2));

    const TArray<int32> InvalidSlots({6});
    TestFalse(
        TEXT("Out-of-range slots are rejected before the build starts"),
        FDWCDataUVBuildSelection::Create(InvalidSlots, TArray<int32>(), 6, &Error).IsSet());
    TestFalse(
        TEXT("An empty requested selection is rejected"),
        FDWCDataUVBuildSelection::Create(TArray<int32>(), ExistingSlots, 6, &Error).IsSet());
    return true;
}

#endif
