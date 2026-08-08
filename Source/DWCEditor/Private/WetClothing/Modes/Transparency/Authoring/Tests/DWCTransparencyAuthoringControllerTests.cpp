// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionAction.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Modes/Transparency/Authoring/DWCTransparencyAuthoringController.h"

namespace
{
    struct FTransparencyAuthoringFixture
    {
        FTransparencyAuthoringFixture()
        {
            Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
            FWetClothingTransparencyLayerData& Layer =
                Asset->Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
            Layer.LayerGuid = FGuid::NewGuid();
            Layer.TargetSurface.OuterMaterialSlotIndex = 2;
            Layer.TargetSurface.UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
            LayerGuid = Layer.LayerGuid;

            Document = MakeShared<FDWCEditorAuthoringDocument>(Asset);
            Store = MakeShared<FDWCEditorSessionStore>();
            Controller = MakeShared<FDWCTransparencyAuthoringController>(Asset, Document, Store);
        }

        void SetContext(const EDWCTransparencyPaintTarget Target)
        {
            FDWCSetTransparencyEditContextAction ContextAction;
            ContextAction.Context.LayerGuid = LayerGuid;
            ContextAction.Context.MaterialSlotIndex = 2;
            ContextAction.Context.UVChannelIndex = 1;
            ContextAction.Context.PaintTarget = Target;
            Store->Dispatch(ContextAction);

            FDWCSetTransparencyPaintAction PaintAction;
            PaintAction.bRevealPaint = Target == EDWCTransparencyPaintTarget::RevealColor;
            PaintAction.Paint.bEnabled = true;
            PaintAction.Paint.bRevealColorPaint = PaintAction.bRevealPaint;
            PaintAction.Paint.RadiusUV = 0.1f;
            PaintAction.Paint.Spacing = 0.5f;
            PaintAction.Paint.Strength = 0.75f;
            Store->Dispatch(PaintAction);
            Controller->HandleSessionStateChanged(Store->GetState());
        }

        void SetRevealPaintEnabled(const bool bEnabled)
        {
            FDWCSetTransparencyPaintAction PaintAction;
            PaintAction.bRevealPaint = true;
            PaintAction.Paint = Store->GetState().Transparency.RevealPaint;
            PaintAction.Paint.bEnabled = bEnabled;
            Store->Dispatch(PaintAction);
            Controller->HandleSessionStateChanged(Store->GetState());
        }

        FDWCEditorSurfaceHit MakeHit(const FVector2D UV) const
        {
            FDWCEditorSurfaceHit Hit;
            Hit.bHit = true;
            Hit.MaterialSlotIndex = 2;
            Hit.UVChannelIndex = 1;
            Hit.UVIslandID = 4;
            Hit.UV = UV;
            return Hit;
        }

        UWetClothingAsset*                              Asset = nullptr;
        FGuid                                           LayerGuid;
        TSharedPtr<FDWCEditorAuthoringDocument>         Document;
        TSharedPtr<FDWCEditorSessionStore>              Store;
        TSharedPtr<FDWCTransparencyAuthoringController> Controller;
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAuthoringControllerStrokeTest,
    "DWC.Editor.Transparency.Authoring.ControllerStroke",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAuthoringControllerStrokeTest::RunTest(const FString& Parameters)
{
    FTransparencyAuthoringFixture Fixture;
    Fixture.SetContext(EDWCTransparencyPaintTarget::FinalAlpha);
    Fixture.Controller->BeginSurfaceInteraction(Fixture.MakeHit(FVector2D(0.1, 0.25)));
    Fixture.Controller->UpdateSurfaceInteraction(Fixture.MakeHit(FVector2D(0.35, 0.25)));

    const FWetClothingTransparencyLayerData& LiveLayer =
        Fixture.Asset->Authored.TransparencyData.TransparencyLayers[0];
    TestEqual(TEXT("Live drag does not mutate the asset stroke list"), LiveLayer.EditableStrokes.Num(), 0);
    TestFalse(TEXT("Live drag does not open an asset transaction"), Fixture.Document->HasInteractiveEdit());
    Fixture.Controller->EndSurfaceInteraction();

    const FWetClothingTransparencyLayerData& Layer =
        Fixture.Asset->Authored.TransparencyData.TransparencyLayers[0];
    TestEqual(TEXT("One alpha stroke is committed"), Layer.EditableStrokes.Num(), 1);
    TestTrue(TEXT("Drag spacing creates continuous samples"), Layer.EditableStrokes[0].Samples.Num() > 1);
    TestFalse(TEXT("Mouse-up ends the transaction"), Fixture.Controller->IsInteracting());
    TestTrue(TEXT("The authoring document advances after commit"), Fixture.Document->GetRevision() > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAuthoringControllerRevealCancelTest,
    "DWC.Editor.Transparency.Authoring.RevealCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAuthoringControllerRevealCancelTest::RunTest(const FString& Parameters)
{
    FTransparencyAuthoringFixture Fixture;
    Fixture.SetContext(EDWCTransparencyPaintTarget::RevealColor);
    Fixture.Controller->BeginSurfaceInteraction(Fixture.MakeHit(FVector2D(0.2, 0.2)));
    Fixture.Controller->UpdateSurfaceInteraction(Fixture.MakeHit(FVector2D(0.5, 0.2)));
    TestTrue(TEXT("Reveal paint begins an interaction"), Fixture.Controller->IsInteracting());

    const FWetClothingTransparencyLayerData& LiveLayer =
        Fixture.Asset->Authored.TransparencyData.TransparencyLayers[0];
    TestEqual(TEXT("Live reveal drag does not mutate the asset stroke list"), LiveLayer.RevealColorPaintStrokes.Num(), 0);
    TestFalse(TEXT("Live reveal drag does not open an asset transaction"), Fixture.Document->HasInteractiveEdit());

    FDWCSetTransparencyEditContextAction DisableAction;
    Fixture.Store->Dispatch(DisableAction);
    Fixture.Controller->HandleSessionStateChanged(Fixture.Store->GetState());

    const FWetClothingTransparencyLayerData& Layer =
        Fixture.Asset->Authored.TransparencyData.TransparencyLayers[0];
    TestFalse(TEXT("Changing context cancels reveal paint"), Fixture.Controller->IsInteracting());
    TestEqual(TEXT("Canceled reveal stroke is removed"), Layer.RevealColorPaintStrokes.Num(), 0);
    TestFalse(TEXT("Cancel leaves no asset transaction"), Fixture.Document->HasInteractiveEdit());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAuthoringControllerRevealToggleTest,
    "DWC.Editor.Transparency.Authoring.RevealToggleLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAuthoringControllerRevealToggleTest::RunTest(const FString& Parameters)
{
    FTransparencyAuthoringFixture Fixture;
    Fixture.SetContext(EDWCTransparencyPaintTarget::RevealColor);

    const FDWCTransparencyEditContext OriginalContext =
        Fixture.Store->GetState().Transparency.EditContext;
    Fixture.SetRevealPaintEnabled(false);

    TestEqual(TEXT("Disabling Reveal Paint keeps the reveal target context"),
              Fixture.Store->GetState().Transparency.EditContext.PaintTarget,
              EDWCTransparencyPaintTarget::RevealColor);
    TestFalse(TEXT("Disabled Reveal Paint blocks stroke begin"),
              Fixture.Controller->CanBeginSurfaceInteraction(Fixture.MakeHit(FVector2D(0.2, 0.2))));
    TestFalse(TEXT("Disabling Reveal Paint does not create an interaction"),
              Fixture.Controller->IsInteracting());

    Fixture.SetRevealPaintEnabled(true);
    TestEqual(TEXT("Re-enabling Reveal Paint preserves the context"),
              Fixture.Store->GetState().Transparency.EditContext.PaintTarget,
              OriginalContext.PaintTarget);
    TestTrue(TEXT("Re-enabling Reveal Paint restores stroke input"),
             Fixture.Controller->CanBeginSurfaceInteraction(Fixture.MakeHit(FVector2D(0.2, 0.2))));

    Fixture.Controller->BeginSurfaceInteraction(Fixture.MakeHit(FVector2D(0.2, 0.2)));
    TestTrue(TEXT("The first click begins a reveal interaction immediately"),
             Fixture.Controller->IsInteracting());
    Fixture.Controller->CancelActiveInteraction(false);
    return true;
}

#endif
