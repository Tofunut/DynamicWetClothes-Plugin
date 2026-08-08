// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionAction.h"

class FDWCEditorSessionReducer
{
  public:
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCActivateEditorModeAction& Action);
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCReconcileAuthoringAction& Action);
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCSetWrinkleBrushAction& Action);
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCSelectWrinkleElementAction& Action);
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCSetWrinkleCrossPreviewAction& Action);
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCSelectTransparencyLayerAction& Action);
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCSetTransparencyStageAction& Action);
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCInitializeTransparencyPreviewSettingsAction& Action);
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCSetTransparencyPreviewAction& Action);
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCSetTransparencyPaintAction& Action);
    static EDWCEditorSessionEffect Reduce(FDWCEditorSessionState& State, const FDWCSetTransparencyEditContextAction& Action);
};
