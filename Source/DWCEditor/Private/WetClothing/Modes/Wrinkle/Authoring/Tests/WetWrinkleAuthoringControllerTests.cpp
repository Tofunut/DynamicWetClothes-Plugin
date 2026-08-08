// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionAction.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinkleAuthoringController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWetWrinkleAuthoringControllerCommitTest,
    "DWC.Editor.Wrinkle.Authoring.ControllerCommit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWetWrinkleAuthoringControllerCommitTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset*                      Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    UTexture2D*                             NormalTexture = NewObject<UTexture2D>(GetTransientPackage());
    TSharedRef<FDWCEditorAuthoringDocument> Document =
        MakeShared<FDWCEditorAuthoringDocument>(Asset);
    TSharedRef<FDWCEditorSessionStore>         Store = MakeShared<FDWCEditorSessionStore>();
    TSharedRef<FWetWrinkleAuthoringController> Controller =
        MakeShared<FWetWrinkleAuthoringController>(Asset, Document, Store);

    FDWCSetWrinkleBrushAction BrushAction;
    BrushAction.Brush.MaterialSlotIndex = 0;
    BrushAction.Brush.UVChannelIndex = 0;
    BrushAction.Brush.WrinkleNormalTexture = NormalTexture;
    BrushAction.Brush.ToolMode = EWetWrinkleToolMode::Patch;
    BrushAction.BrushSizeCm = 8.0f;
    BrushAction.BrushSizeUV = 0.05f;
    Store->Dispatch(BrushAction);

    FWetWrinkleSurfaceHit Hit;
    Hit.bHit = true;
    Hit.MaterialSlotIndex = 0;
    Hit.UVChannelIndex = 0;
    Hit.UVIslandID = 3;
    Hit.TriangleID = 7;
    Hit.UV = FVector2D(0.25, 0.5);
    Hit.Barycentric = FVector(0.2, 0.3, 0.5);
    Controller->BeginSurfaceInteraction(Hit);

    TestEqual(
        TEXT("A Patch click commits one authored patch"),
        Asset->Authored.WrinkleData.EditablePatches.Num(),
        1);
    TestTrue(
        TEXT("The committed Patch becomes the session selection"),
        Store->GetState().Wrinkle.SelectedElementGuid ==
            Asset->Authored.WrinkleData.EditablePatches[0].PatchGuid);

    BrushAction.Brush.ToolMode = EWetWrinkleToolMode::ProceduralRidgeStroke;
    BrushAction.Brush.RidgeEditMode = EWetProceduralRidgeEditMode::Draw;
    Store->Dispatch(BrushAction);
    Controller->BeginSurfaceInteraction(Hit);
    FWetWrinkleSurfaceHit EndHit = Hit;
    EndHit.WorldPosition = FVector(20.0, 0.0, 0.0);
    EndHit.UV = FVector2D(0.45, 0.5);
    Controller->UpdateSurfaceInteraction(EndHit);
    Controller->CancelSurfaceInteraction();
    TestEqual(
        TEXT("Canceling a Ridge interaction does not mutate the asset"),
        Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.Num(),
        0);

    Controller->BeginSurfaceInteraction(Hit);
    Controller->UpdateSurfaceInteraction(EndHit);
    Controller->EndSurfaceInteraction();
    TestEqual(
        TEXT("Mouse-up commits one Ridge stroke"),
        Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.Num(),
        1);
    TestFalse(TEXT("The committed interaction is no longer active"), Controller->IsInteracting());
    return true;
}

#endif
