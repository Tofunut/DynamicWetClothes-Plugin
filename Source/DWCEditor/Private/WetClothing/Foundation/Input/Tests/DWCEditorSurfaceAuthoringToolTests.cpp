//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "InputState.h"
#include "WetClothing/Foundation/Input/DWCEditorSurfaceAuthoringTool.h"

namespace
{
class FDWCTestSurfaceToolTarget final : public IDWCEditorSurfaceToolTarget
{
  public:
    virtual bool HitTestSurface(const FRay&, double& OutHitDepth) const override
    {
        OutHitDepth = 1.0;
        return bAcceptHit;
    }

    virtual bool CanBeginSurfaceInteraction(const FRay&, double& OutHitDepth) override
    {
        OutHitDepth = 1.0;
        return bAcceptCapture;
    }

    virtual void BeginSurfaceInteraction(const FRay&) override { ++BeginCount; }
    virtual void UpdateSurfaceInteraction(const FRay&) override {}
    virtual void EndSurfaceInteraction() override { ++EndCount; }
    virtual void CancelSurfaceInteraction() override { ++CancelCount; }
    virtual bool UpdateSurfaceHover(const FRay&) override
    {
        ++HoverUpdateCount;
        return bAcceptHit;
    }
    virtual void ClearSurfaceHover() override { ++HoverClearCount; }

    bool bAcceptHit = true;
    bool bAcceptCapture = true;
    int32 BeginCount = 0;
    int32 EndCount = 0;
    int32 CancelCount = 0;
    int32 HoverUpdateCount = 0;
    int32 HoverClearCount = 0;
};

FInputDeviceRay MakeTestRay()
{
    return FInputDeviceRay(FRay(FVector::ZeroVector, FVector::ForwardVector));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceToolHoverClickHandoffTest,
    "DWC.Editor.Foundation.Input.HoverClickCaptureHandoff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceToolHoverClickHandoffTest::RunTest(const FString&)
{
    FDWCTestSurfaceToolTarget Target;
    UDWCEditorSurfaceAuthoringTool* Tool =
        NewObject<UDWCEditorSurfaceAuthoringTool>(GetTransientPackage());
    Tool->SetTarget(&Target);
    const FInputDeviceRay Ray = MakeTestRay();

    Tool->OnBeginHover(Ray);
    Tool->CanBeginClickDragSequence(Ray);
    Tool->OnEndHover();

    TestEqual(
        TEXT("InputRouter's hover termination preserves the presented hover until click capture"),
        Target.HoverClearCount,
        0);

    Tool->OnClickPress(Ray);
    TestEqual(TEXT("The accepted capture begins one interaction"), Target.BeginCount, 1);
    TestEqual(
        TEXT("Beginning the click consumes the deferred clear without erasing hover state"),
        Target.HoverClearCount,
        0);

    Tool->OnClickRelease(Ray);
    TestEqual(TEXT("The click release ends the interaction"), Target.EndCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceToolRealHoverExitTest,
    "DWC.Editor.Foundation.Input.RealHoverExit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceToolRealHoverExitTest::RunTest(const FString&)
{
    FDWCTestSurfaceToolTarget Target;
    UDWCEditorSurfaceAuthoringTool* Tool =
        NewObject<UDWCEditorSurfaceAuthoringTool>(GetTransientPackage());
    Tool->SetTarget(&Target);
    const FInputDeviceRay Ray = MakeTestRay();

    Tool->OnBeginHover(Ray);
    Tool->OnEndHover();

    TestEqual(TEXT("A real hover exit clears the target immediately"), Target.HoverClearCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceToolRejectedCaptureTest,
    "DWC.Editor.Foundation.Input.RejectedCaptureClearsDeferredHover",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceToolRejectedCaptureTest::RunTest(const FString&)
{
    FDWCTestSurfaceToolTarget Target;
    UDWCEditorSurfaceAuthoringTool* Tool =
        NewObject<UDWCEditorSurfaceAuthoringTool>(GetTransientPackage());
    Tool->SetTarget(&Target);
    const FInputDeviceRay Ray = MakeTestRay();

    Tool->OnBeginHover(Ray);
    Tool->CanBeginClickDragSequence(Ray);
    Tool->OnEndHover();
    Tool->OnTick(0.0f);

    TestEqual(
        TEXT("A capture request that never begins completes its deferred hover exit"),
        Target.HoverClearCount,
        1);
    TestEqual(TEXT("A rejected capture does not begin an interaction"), Target.BeginCount, 0);
    return true;
}

#endif
