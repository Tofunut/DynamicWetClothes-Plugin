//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class FAssetEditorModeManager;
class FEditorModeTools;
class FPreviewScene;
class IDWCEditorSurfaceToolTarget;
class UDWCEditorSurfaceAuthoringToolBuilder;

/** Owns an asset-viewport-local ITF context and one long-lived surface tool. */
class FDWCEditorInteractiveToolsHost
{
  public:
    FDWCEditorInteractiveToolsHost(
        FPreviewScene* InPreviewScene,
        IDWCEditorSurfaceToolTarget* InTarget);
    ~FDWCEditorInteractiveToolsHost();

    FEditorModeTools* GetModeTools() const;
    bool CancelActiveInteraction();
    void Shutdown();

  private:
    TSharedPtr<FAssetEditorModeManager> ModeManager;
    TStrongObjectPtr<UDWCEditorSurfaceAuthoringToolBuilder> ToolBuilder;
    bool bShutdown = false;
};
