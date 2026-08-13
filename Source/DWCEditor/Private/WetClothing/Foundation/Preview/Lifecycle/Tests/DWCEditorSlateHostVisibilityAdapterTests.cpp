// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorSlateHostVisibilityAdapter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorHostVisibilityPublisherLifetimeTest,
    "DWC.Editor.Lifecycle.Visibility.AdapterPublisherLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorHostVisibilityPublisherLifetimeTest::RunTest(const FString&)
{
    int32 PublishCount = 0;
    FDWCEditorHostVisibilitySnapshot LastSnapshot;
    FDWCEditorHostVisibilityPublisher Publisher;
    Publisher.Initialize(
        [&PublishCount, &LastSnapshot](const FDWCEditorHostVisibilitySnapshot& Snapshot)
        {
            ++PublishCount;
            LastSnapshot = Snapshot;
        });

    FDWCEditorHostVisibilitySnapshot Foreground;
    Foreground.bHostAvailable = true;
    Foreground.bTabForeground = true;
    Foreground.bWindowVisible = true;
    Foreground.bWindowActive = true;
    Foreground.bApplicationActive = true;

    TestTrue(TEXT("The initial adapter snapshot is published"), Publisher.Publish(Foreground));
    TestEqual(TEXT("The initial callback runs once"), PublishCount, 1);
    TestFalse(TEXT("An identical poll snapshot is suppressed"), Publisher.Publish(Foreground));
    TestEqual(TEXT("A no-op poll does not run the callback"), PublishCount, 1);
    TestTrue(TEXT("A forced host rebind republishes the snapshot"),
        Publisher.Publish(Foreground, true));
    TestEqual(TEXT("Forced publication runs exactly once"), PublishCount, 2);

    FDWCEditorHostVisibilitySnapshot Background = Foreground;
    Background.bTabForeground = false;
    TestTrue(TEXT("A meaningful tab transition is published"), Publisher.Publish(Background));
    TestEqual(TEXT("The changed snapshot reaches the callback"), PublishCount, 3);
    TestFalse(TEXT("The last published tab state is retained"), LastSnapshot.bTabForeground);

    Publisher.Shutdown();
    TestFalse(TEXT("Shutdown prevents late Slate events from publishing"),
        Publisher.Publish(Foreground, true));
    TestEqual(TEXT("No callback survives shutdown"), PublishCount, 3);

    Publisher.Initialize(
        [&PublishCount](const FDWCEditorHostVisibilitySnapshot&)
        {
            ++PublishCount;
        });
    TestTrue(TEXT("Reinitialization starts with an empty deduplication snapshot"),
        Publisher.Publish(Foreground));
    TestEqual(TEXT("A respawned tab publishes once"), PublishCount, 4);
    Publisher.Shutdown();
    return true;
}

#endif
