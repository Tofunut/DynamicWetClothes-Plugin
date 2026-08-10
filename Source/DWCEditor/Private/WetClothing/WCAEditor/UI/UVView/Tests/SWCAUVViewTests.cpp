// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Widgets/SNullWidget.h"

#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSWCAUVViewMarkerLayerTest,
    "DWC.Editor.WCA.UVView.MarkerLayers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWCAUVViewMarkerLayerTest::RunTest(const FString& Parameters)
{
    TSharedRef<SWCAUVView> UVView = SNew(SWCAUVView);

    FWCAUVViewCircleMarker PersistentMarker;
    PersistentMarker.CenterUV = FVector2D(0.25f, 0.75f);
    PersistentMarker.RadiusUV = 0.1f;
    PersistentMarker.FillColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
    PersistentMarker.OutlineColor = FLinearColor::White;
    PersistentMarker.OutlineThickness = 2.0f;
    const TArray<FWCAUVViewCircleMarker> PersistentMarkers = {PersistentMarker};

    FWCAUVViewCircleMarker HoverMarker = PersistentMarker;
    HoverMarker.CenterUV = FVector2D(0.4f, 0.6f);
    HoverMarker.RadiusUV = 0.05f;

    TestTrue(TEXT("Initial persistent marker update is applied"),
             UVView->SetPersistentCircleMarkers(PersistentMarkers));
    TestFalse(TEXT("Identical persistent markers are a no-op"),
              UVView->SetPersistentCircleMarkers(PersistentMarkers));

    TestTrue(TEXT("Initial hover marker update is applied"),
             UVView->SetHoverCircleMarker(HoverMarker));
    TestFalse(TEXT("Identical hover marker is a no-op"),
              UVView->SetHoverCircleMarker(HoverMarker));
    TestFalse(TEXT("Hover updates do not replace persistent markers"),
              UVView->SetPersistentCircleMarkers(PersistentMarkers));

    TestTrue(TEXT("Clearing an active hover marker is applied"), UVView->ClearHoverCircleMarker());
    TestFalse(TEXT("Clearing an already empty hover marker is a no-op"), UVView->ClearHoverCircleMarker());
    TestFalse(TEXT("Clearing hover retains persistent markers"),
              UVView->SetPersistentCircleMarkers(PersistentMarkers));

    TestTrue(TEXT("Hover can be restored after a clear"), UVView->SetHoverCircleMarker(HoverMarker));
    return true;
}

#endif
