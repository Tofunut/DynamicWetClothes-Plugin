// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCEditorInteractiveToolsHost.h"

#include "AssetEditorModeManager.h"
#include "DWCEditorSurfaceAuthoringTool.h"
#include "InputRouter.h"
#include "InteractiveToolManager.h"
#include "Tools/EdModeInteractiveToolsContext.h"

namespace
{
    const FString SurfaceToolIdentifier(TEXT("DWCEditor.SurfaceAuthoring"));
}

FDWCEditorInteractiveToolsHost::FDWCEditorInteractiveToolsHost(
    FPreviewScene*               InPreviewScene,
    IDWCEditorSurfaceToolTarget* InTarget)
{
    ModeManager = MakeShared<FAssetEditorModeManager>();
    ModeManager->SetPreviewScene(InPreviewScene);

    ToolBuilder.Reset(NewObject<UDWCEditorSurfaceAuthoringToolBuilder>(
        GetTransientPackage(), NAME_None, RF_Transient));
    ToolBuilder->SetTarget(InTarget);

    UInteractiveToolManager* ToolManager = ModeManager->GetInteractiveToolsContext()->ToolManager;
    ToolManager->RegisterToolType(SurfaceToolIdentifier, ToolBuilder.Get());
    if (ToolManager->SelectActiveToolType(EToolSide::Left, SurfaceToolIdentifier))
    {
        ToolManager->ActivateTool(EToolSide::Left);
    }
}

FDWCEditorInteractiveToolsHost::~FDWCEditorInteractiveToolsHost()
{
    Shutdown();
}

FEditorModeTools* FDWCEditorInteractiveToolsHost::GetModeTools() const
{
    return ModeManager.Get();
}

bool FDWCEditorInteractiveToolsHost::CancelActiveInteraction()
{
    if (bShutdown || !ModeManager.IsValid())
    {
        return false;
    }

    UModeManagerInteractiveToolsContext* Context = ModeManager->GetInteractiveToolsContext();
    UDWCEditorSurfaceAuthoringTool*      Tool = Cast<UDWCEditorSurfaceAuthoringTool>(
        Context->ToolManager->GetActiveTool(EToolSide::Left));
    if (Tool == nullptr || !Tool->IsInteracting())
    {
        return false;
    }

    Context->InputRouter->ForceTerminateAll();
    return true;
}

void FDWCEditorInteractiveToolsHost::Shutdown()
{
    if (bShutdown)
    {
        return;
    }
    bShutdown = true;

    if (ModeManager.IsValid())
    {
        UModeManagerInteractiveToolsContext* Context = ModeManager->GetInteractiveToolsContext();
        Context->InputRouter->ForceTerminateAll();
        if (Context->ToolManager->HasActiveTool(EToolSide::Left))
        {
            Context->ToolManager->DeactivateTool(EToolSide::Left, EToolShutdownType::Cancel);
        }
        Context->ToolManager->UnregisterToolType(SurfaceToolIdentifier);
        ModeManager->SetPreviewScene(nullptr);
    }

    ToolBuilder.Reset();
    ModeManager.Reset();
}
