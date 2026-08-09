//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "DWCEditorSurfaceAuthoringTool.h"

#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "InputState.h"
#include "InteractiveToolManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DWCEditorSurfaceAuthoringTool)

bool UDWCEditorSurfaceAuthoringToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
    return Target != nullptr;
}

UInteractiveTool* UDWCEditorSurfaceAuthoringToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
    UDWCEditorSurfaceAuthoringTool* Tool =
        NewObject<UDWCEditorSurfaceAuthoringTool>(SceneState.ToolManager);
    Tool->SetTarget(Target);
    return Tool;
}

void UDWCEditorSurfaceAuthoringTool::Setup()
{
    UInteractiveTool::Setup();

    UClickDragInputBehavior* DragBehavior = NewObject<UClickDragInputBehavior>(this);
    DragBehavior->Initialize(this);
    DragBehavior->SetUseLeftMouseButton();
    DragBehavior->SetDefaultPriority(FInputCapturePriority(90));
    DragBehavior->ModifierCheckFunc = [](const FInputDeviceState& Input)
    {
        return !Input.bAltKeyDown;
    };
    AddInputBehavior(DragBehavior);

    UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
    HoverBehavior->Initialize(this);
    HoverBehavior->HoverModifierCheckFunc = [](const FInputDeviceState& Input)
    {
        return !Input.bAltKeyDown;
    };
    AddInputBehavior(HoverBehavior);
}

void UDWCEditorSurfaceAuthoringTool::Shutdown(const EToolShutdownType ShutdownType)
{
    if (bInteracting && Target != nullptr)
    {
        Target->CancelSurfaceInteraction();
    }
    if ((bHovering || bDeferredHoverClear || bInteracting) && Target != nullptr)
    {
        Target->ClearSurfaceHover();
    }
    bInteracting = false;
    bHovering = false;
    bClickCapturePending = false;
    bDeferredHoverClear = false;
    Target = nullptr;
    UInteractiveTool::Shutdown(ShutdownType);
}

void UDWCEditorSurfaceAuthoringTool::OnTick(const float DeltaTime)
{
    UInteractiveTool::OnTick(DeltaTime);

    // InputRouter ends hover before it grants a click capture. If another
    // behavior wins that capture, finish the deferred real hover exit here.
    if (bDeferredHoverClear && !bInteracting)
    {
        if (Target != nullptr)
        {
            Target->ClearSurfaceHover();
        }
        bDeferredHoverClear = false;
        bClickCapturePending = false;
    }
}

FInputRayHit UDWCEditorSurfaceAuthoringTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
    double HitDepth = 0.0;
    bClickCapturePending =
        Target != nullptr && Target->CanBeginSurfaceInteraction(PressPos.WorldRay, HitDepth);
    return bClickCapturePending ? FInputRayHit(HitDepth) : FInputRayHit();
}

void UDWCEditorSurfaceAuthoringTool::OnClickPress(const FInputDeviceRay& PressPos)
{
    if (Target != nullptr)
    {
        bClickCapturePending = false;
        bDeferredHoverClear = false;
        bInteracting = true;
        Target->BeginSurfaceInteraction(PressPos.WorldRay);
    }
}

void UDWCEditorSurfaceAuthoringTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
    if (bInteracting && Target != nullptr)
    {
        Target->UpdateSurfaceInteraction(DragPos.WorldRay);
    }
}

void UDWCEditorSurfaceAuthoringTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
    if (bInteracting && Target != nullptr)
    {
        Target->EndSurfaceInteraction();
    }
    bInteracting = false;
    bClickCapturePending = false;
    bDeferredHoverClear = false;
}

void UDWCEditorSurfaceAuthoringTool::OnTerminateDragSequence()
{
    if (bInteracting && Target != nullptr)
    {
        Target->CancelSurfaceInteraction();
    }
    bInteracting = false;
    bClickCapturePending = false;
    bDeferredHoverClear = false;
}

FInputRayHit UDWCEditorSurfaceAuthoringTool::BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos)
{
    double HitDepth = 0.0;
    return Target != nullptr && Target->HitTestSurface(DevicePos.WorldRay, HitDepth)
        ? FInputRayHit(HitDepth)
        : FInputRayHit();
}

void UDWCEditorSurfaceAuthoringTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{
    bClickCapturePending = false;
    bDeferredHoverClear = false;
    bHovering = Target != nullptr && Target->UpdateSurfaceHover(DevicePos.WorldRay);
}

bool UDWCEditorSurfaceAuthoringTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
    bClickCapturePending = false;
    bDeferredHoverClear = false;
    bHovering = Target != nullptr && Target->UpdateSurfaceHover(DevicePos.WorldRay);
    return bHovering;
}

void UDWCEditorSurfaceAuthoringTool::OnEndHover()
{
    if (bHovering && Target != nullptr)
    {
        if (bClickCapturePending)
        {
            bDeferredHoverClear = true;
        }
        else
        {
            Target->ClearSurfaceHover();
        }
    }
    bHovering = false;
}
