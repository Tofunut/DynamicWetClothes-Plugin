//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyPlacementSession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyPlacementSessionCanonicalSyncTest,
    "DWC.Editor.Transparency.Placement.CanonicalSync",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyPlacementSessionCanonicalSyncTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyPlacementSession Session;
    const FGuid SourceA = FGuid::NewGuid();
    const FGuid SourceB = FGuid::NewGuid();
    TMap<FGuid, FTransform> CanonicalTransforms;
    CanonicalTransforms.Add(SourceA, FTransform(FVector(10.0, 20.0, 30.0)));
    CanonicalTransforms.Add(SourceB, FTransform(FVector(-5.0, 0.0, 8.0)));

    Session.SynchronizeSources(CanonicalTransforms);
    Session.SetSelection(FDWCTransparencyPlacementSelection::Source(SourceA));
    Session.SetSourceHidden(SourceA, true);
    Session.SetSourceLocked(SourceA, true);
    TestEqual(TEXT("Canonical source transform is loaded"),
        Session.GetSourceTransform(SourceA).GetTranslation(), FVector(10.0, 20.0, 30.0));

    CanonicalTransforms[SourceA] = FTransform(FVector(40.0, 50.0, 60.0));
    CanonicalTransforms.Remove(SourceB);
    Session.SynchronizeSources(CanonicalTransforms);
    TestEqual(TEXT("Undo or external refresh overwrites transient source transform"),
        Session.GetSourceTransform(SourceA).GetTranslation(), FVector(40.0, 50.0, 60.0));
    TestEqual(TEXT("Missing sources are removed"),
        Session.GetSourceTransform(SourceB), FTransform::Identity);

    CanonicalTransforms.Reset();
    Session.SynchronizeSources(CanonicalTransforms);
    TestEqual(TEXT("Selection is cleared when its source is removed"),
        Session.GetSelection().Type, EDWCTransparencyPlacementSelectionType::None);
    TestFalse(TEXT("Hidden state is removed with the source"), Session.IsSourceHidden(SourceA));
    TestFalse(TEXT("Locked state is removed with the source"), Session.IsSourceLocked(SourceA));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyPlacementSessionPresentationStateTest,
    "DWC.Editor.Transparency.Placement.PresentationState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyPlacementSessionPresentationStateTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyPlacementSession Session;
    const FGuid Source = FGuid::NewGuid();
    Session.SynchronizeSources({{Source, FTransform::Identity}});
    Session.SetAssemblyTransform(FTransform(FRotator(0.0, 25.0, 0.0), FVector(0.0, 0.0, 100.0)));
    Session.SetSelection(FDWCTransparencyPlacementSelection::Target());
    Session.SetSourceHidden(Source, true);

    TestEqual(TEXT("Target assembly movement is transient session state"),
        Session.GetAssemblyTransform().GetTranslation(), FVector(0.0, 0.0, 100.0));
    TestFalse(TEXT("Hidden source is excluded only from presentation"),
        Session.ShouldShowSource(Source));
    TestEqual(TEXT("Visibility does not modify the canonical source transform"),
        Session.GetSourceTransform(Source), FTransform::Identity);
    return true;
}

#endif
