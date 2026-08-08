// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "Math/Ray.h"

#include "DWCEditorSurfaceAuthoringTool.generated.h"

/** Mode adapter used by the common ITF tool. Data changes remain in the mode controller. */
class IDWCEditorSurfaceToolTarget
{
  public:
    virtual ~IDWCEditorSurfaceToolTarget() = default;

    virtual bool HitTestSurface(const FRay& WorldRay, double& OutHitDepth) const = 0;
    virtual bool CanBeginSurfaceInteraction(const FRay& WorldRay, double& OutHitDepth) = 0;
    virtual void BeginSurfaceInteraction(const FRay& WorldRay) = 0;
    virtual void UpdateSurfaceInteraction(const FRay& WorldRay) = 0;
    virtual void EndSurfaceInteraction() = 0;
    virtual void CancelSurfaceInteraction() = 0;
    virtual bool UpdateSurfaceHover(const FRay& WorldRay) = 0;
    virtual void ClearSurfaceHover() = 0;
};

UCLASS(Transient)
class UDWCEditorSurfaceAuthoringToolBuilder final : public UInteractiveToolBuilder
{
    GENERATED_BODY()

  public:
    void                      SetTarget(IDWCEditorSurfaceToolTarget* InTarget) { Target = InTarget; }
    virtual bool              CanBuildTool(const FToolBuilderState& SceneState) const override;
    virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

  private:
    IDWCEditorSurfaceToolTarget* Target = nullptr;
};

UCLASS(Transient)
class UDWCEditorSurfaceAuthoringTool final
    : public UInteractiveTool,
      public IClickDragBehaviorTarget,
      public IHoverBehaviorTarget
{
    GENERATED_BODY()

  public:
    void SetTarget(IDWCEditorSurfaceToolTarget* InTarget) { Target = InTarget; }
    bool IsInteracting() const { return bInteracting; }

    virtual void Setup() override;
    virtual void Shutdown(EToolShutdownType ShutdownType) override;
    virtual bool HasCancel() const override { return false; }
    virtual bool HasAccept() const override { return false; }

    virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
    virtual void         OnClickPress(const FInputDeviceRay& PressPos) override;
    virtual void         OnClickDrag(const FInputDeviceRay& DragPos) override;
    virtual void         OnClickRelease(const FInputDeviceRay& ReleasePos) override;
    virtual void         OnTerminateDragSequence() override;

    virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos) override;
    virtual void         OnBeginHover(const FInputDeviceRay& DevicePos) override;
    virtual bool         OnUpdateHover(const FInputDeviceRay& DevicePos) override;
    virtual void         OnEndHover() override;

  private:
    IDWCEditorSurfaceToolTarget* Target = nullptr;
    bool                         bInteracting = false;
    bool                         bHovering = false;
};
