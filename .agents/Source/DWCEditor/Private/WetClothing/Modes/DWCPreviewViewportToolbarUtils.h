#pragma once

struct FToolMenuEntry;
class FEditorViewportClient;

namespace UE::DWCEditor
{
	void ApplyDWCPreviewCameraSpeedSettings(FEditorViewportClient& ViewportClient);
	FToolMenuEntry CreateDWCViewModesSubmenu();
}
