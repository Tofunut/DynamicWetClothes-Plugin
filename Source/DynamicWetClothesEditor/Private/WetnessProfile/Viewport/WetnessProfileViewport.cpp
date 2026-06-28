#include "WetnessProfileViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "WetnessProfile.h"
#include "WetnessProfileViewportClient.h"

#define LOCTEXT_NAMESPACE "WetnessProfileViewport"

void SWetnessProfileViewport::Construct(const FArguments& InArgs)
{
    WetnessProfile = InArgs._WetnessProfile;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    SEditorViewport::Construct(SEditorViewport::FArguments());
    PreviewMeshComponent = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
    PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewScene->AddComponent(PreviewMeshComponent, FTransform::Identity);
    RefreshPreviewScene();
}

SWetnessProfileViewport::~SWetnessProfileViewport()
{
    if (PreviewScene.IsValid() && PreviewMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewMeshComponent);
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->Viewport = nullptr;
    }
}

void SWetnessProfileViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
}

void SWetnessProfileViewport::RefreshPreviewScene()
{
    if (!PreviewScene.IsValid())
    {
        return;
    }

    if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
    {
        PreviewMeshComponent->SetStaticMesh(SphereMesh);
        PreviewMeshComponent->SetRelativeScale3D(FVector(1.35f));
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, true);
    }
}

void SWetnessProfileViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

TSharedRef<FEditorViewportClient> SWetnessProfileViewport::MakeEditorViewportClient()
{
    ViewportClient = MakeShared<FWetnessProfileViewportClient>(PreviewScene.Get(), SharedThis(this));
    ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
    return ViewportClient.ToSharedRef();
}

void SWetnessProfileViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
    SEditorViewport::PopulateViewportOverlays(Overlay);

    Overlay->AddSlot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Top)
        .Padding(10.0f)
            [SAssignNew(OverlayText, SRichTextBlock)
                 .Text(LOCTEXT("PreviewHint", "Preview placeholder\nSphere only for now"))
                 .DecoratorStyleSet(&FAppStyle::Get())];
}

void SWetnessProfileViewport::OnFocusViewportToSelection()
{
    FocusOnPreviewMesh();
}

#undef LOCTEXT_NAMESPACE
