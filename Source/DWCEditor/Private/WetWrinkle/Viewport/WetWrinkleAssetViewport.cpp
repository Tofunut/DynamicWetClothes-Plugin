#include "WetWrinkleAssetViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetWrinkleAsset.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetWrinkleAssetViewportClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "WetWrinkleAssetViewport"

namespace
{
    FVector MakeWetWrinkleAnyPerpendicular(const FVector& Direction)
    {
        FVector Perpendicular = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
        if (Perpendicular.IsNearlyZero())
        {
            Perpendicular = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
        }

        return Perpendicular.IsNearlyZero() ? FVector::ForwardVector : Perpendicular;
    }

    FVector ComputeWetWrinkleBarycentric(const FVector& Point, const FVector& A, const FVector& B, const FVector& C)
    {
        const FVector V0 = B - A;
        const FVector V1 = C - A;
        const FVector V2 = Point - A;
        const double D00 = FVector::DotProduct(V0, V0);
        const double D01 = FVector::DotProduct(V0, V1);
        const double D11 = FVector::DotProduct(V1, V1);
        const double D20 = FVector::DotProduct(V2, V0);
        const double D21 = FVector::DotProduct(V2, V1);
        const double Denom = D00 * D11 - D01 * D01;
        if (FMath::IsNearlyZero(Denom))
        {
            return FVector(1.0, 0.0, 0.0);
        }

        const double V = (D11 * D20 - D01 * D21) / Denom;
        const double W = (D00 * D21 - D01 * D20) / Denom;
        return FVector(1.0 - V - W, V, W);
    }
} // namespace

void SWetWrinkleAssetViewport::Construct(const FArguments& InArgs)
{
    WetWrinkleAsset = InArgs._WetWrinkleAsset;
    OnSurfaceHitChanged = InArgs._OnSurfaceHitChanged;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

    SEditorViewport::Construct(SEditorViewport::FArguments());

    PreviewMeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
    PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewScene->AddComponent(PreviewMeshComponent, FTransform::Identity);

    BrushCursorComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    BrushCursorComponent->SetMobility(EComponentMobility::Movable);
    BrushCursorComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BrushCursorComponent->SetCastShadow(false);
    BrushCursorComponent->bUseAsyncCooking = false;
    BrushCursorComponent->SetMaterial(0, ResolveCursorMaterial());
    PreviewScene->AddComponent(BrushCursorComponent, FTransform::Identity);

    RefreshPreviewMesh();
}

SWetWrinkleAssetViewport::~SWetWrinkleAssetViewport()
{
    if (PreviewScene.IsValid() && PreviewMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewMeshComponent);
    }

    if (PreviewScene.IsValid() && BrushCursorComponent != nullptr)
    {
        PreviewScene->RemoveComponent(BrushCursorComponent);
    }
}

void SWetWrinkleAssetViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(BrushCursorComponent);
    Collector.AddReferencedObject(CursorMaterial);
}

void SWetWrinkleAssetViewport::RefreshPreviewMesh()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    PreviewMeshComponent->SetSkeletalMeshAsset(TargetMesh);
    PreviewMeshComponent->ShowAllMaterialSections(0);
    RebuildHitTriangles();
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    ClearBrushCursor();

    if (TargetMesh != nullptr)
    {
        const FBoxSphereBounds Bounds = PreviewMeshComponent->CalcBounds(FTransform::Identity);
        PreviewScene->SetFloorOffset(-Bounds.Origin.Z + Bounds.BoxExtent.Z);
    }
    else
    {
        PreviewScene->SetFloorOffset(0.0f);
    }

    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, true);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
        ViewportClient->Invalidate();
    }
    else
    {
        Invalidate();
    }
}

void SWetWrinkleAssetViewport::SetBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings)
{
    const bool bNeedsTriangleRebuild =
        BrushSettings.UVChannelIndex != InBrushSettings.UVChannelIndex ||
        BrushSettings.MaterialSlotIndex != InBrushSettings.MaterialSlotIndex;

    BrushSettings = InBrushSettings;

    if (bNeedsTriangleRebuild)
    {
        RebuildHitTriangles();
    }

    RefreshBrushCursor();
    Invalidate();
}

bool SWetWrinkleAssetViewport::TraceSurface(const FVector& RayOrigin, const FVector& RayDirection, FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    OutHit.UVChannelIndex = BrushSettings.UVChannelIndex;

    if (PreviewMeshComponent == nullptr || PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr || HitTriangles.Num() == 0)
    {
        return false;
    }

    const FVector SafeRayDirection = RayDirection.GetSafeNormal();
    const FVector RayEnd = RayOrigin + SafeRayDirection * 1000000.0;
    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();

    for (const FWetClothingAssetUVTriangle& Triangle : HitTriangles)
    {
        const FVector WorldA = ComponentTransform.TransformPosition(Triangle.LocalPositions[0]);
        const FVector WorldB = ComponentTransform.TransformPosition(Triangle.LocalPositions[1]);
        const FVector WorldC = ComponentTransform.TransformPosition(Triangle.LocalPositions[2]);

        FVector IntersectionPoint = FVector::ZeroVector;
        FVector TriangleNormal = FVector::ZeroVector;
        if (!FMath::SegmentTriangleIntersection(RayOrigin, RayEnd, WorldA, WorldB, WorldC, IntersectionPoint, TriangleNormal))
        {
            continue;
        }

        const double DistanceSq = FVector::DistSquared(RayOrigin, IntersectionPoint);
        if (DistanceSq >= OutHit.DistanceSq)
        {
            continue;
        }

        FVector Normal = FVector::CrossProduct(WorldB - WorldA, WorldC - WorldA).GetSafeNormal();
        if (Normal.IsNearlyZero())
        {
            Normal = TriangleNormal.GetSafeNormal();
        }
        if (Normal.IsNearlyZero())
        {
            Normal = FVector::UpVector;
        }
        if (FVector::DotProduct(Normal, SafeRayDirection) > 0.0)
        {
            Normal *= -1.0;
        }

        FVector Tangent = (WorldB - WorldA).GetSafeNormal();
        Tangent = (Tangent - Normal * FVector::DotProduct(Tangent, Normal)).GetSafeNormal();
        if (Tangent.IsNearlyZero())
        {
            Tangent = MakeWetWrinkleAnyPerpendicular(Normal);
        }

        FVector Bitangent = FVector::CrossProduct(Normal, Tangent).GetSafeNormal();
        if (Bitangent.IsNearlyZero())
        {
            Bitangent = MakeWetWrinkleAnyPerpendicular(Normal);
        }

        const FVector Barycentric = ComputeWetWrinkleBarycentric(IntersectionPoint, WorldA, WorldB, WorldC);
        const FVector2D UV = Triangle.UVs[0] * Barycentric.X + Triangle.UVs[1] * Barycentric.Y + Triangle.UVs[2] * Barycentric.Z;

        OutHit.bHit = true;
        OutHit.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        OutHit.TriangleID = Triangle.TriangleID;
        OutHit.UVChannelIndex = BrushSettings.UVChannelIndex;
        OutHit.WorldPosition = IntersectionPoint;
        OutHit.WorldNormal = Normal;
        OutHit.WorldTangent = Tangent;
        OutHit.WorldBitangent = Bitangent;
        OutHit.UV = UV;
        OutHit.Barycentric = Barycentric;
        OutHit.DistanceSq = DistanceSq;
    }

    return OutHit.bHit;
}

void SWetWrinkleAssetViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

TSharedRef<FEditorViewportClient> SWetWrinkleAssetViewport::MakeEditorViewportClient()
{
    check(PreviewScene.IsValid());
    ViewportClient = MakeShared<FWetWrinkleAssetViewportClient>(PreviewScene.Get(), SharedThis(this));

    if (PreviewMeshComponent != nullptr)
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
    }

    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SWetWrinkleAssetViewport::BuildViewportToolbar()
{
    const FName ViewportToolbarName = TEXT("WetWrinkleAssetEditor.ViewportToolbar");

    if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
    {
        UToolMenu* const ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(ViewportToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
        ViewportToolbarMenu->StyleName = TEXT("ViewportToolbar");

        ViewportToolbarMenu->AddSection(TEXT("Left"));

        FToolMenuSection& RightSection = ViewportToolbarMenu->AddSection(TEXT("Right"));
        RightSection.Alignment = EToolMenuSectionAlign::Last;
        RightSection.AddEntry(UE::UnrealEd::CreateCameraSubmenu(UE::UnrealEd::FViewportCameraMenuOptions().ShowAll()));
        RightSection.AddEntry(UE::UnrealEd::CreateViewModesSubmenu());
    }

    FToolMenuContext ViewportToolbarContext;
    ViewportToolbarContext.AppendCommandList(GetCommandList());
    ViewportToolbarContext.AddObject(UE::UnrealEd::CreateViewportToolbarDefaultContext(SharedThis(this)));

    return UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext);
}

void SWetWrinkleAssetViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
    SEditorViewport::PopulateViewportOverlays(Overlay);

    Overlay->AddSlot()
        .VAlign(VAlign_Top)
        .HAlign(HAlign_Left)
        .Padding(8.0f)
            [SNew(SBorder)
                 .BorderImage(FAppStyle::Get().GetBrush("FloatingBorder"))
                 .Padding(6.0f)
                     [SAssignNew(OverlayText, SRichTextBlock)
                          .Text(GetViewportHintText())]];
}

void SWetWrinkleAssetViewport::OnFocusViewportToSelection()
{
    FocusOnPreviewMesh(false);
}

USkeletalMesh* SWetWrinkleAssetViewport::ResolveTargetMesh() const
{
    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    if (Asset == nullptr)
    {
        return nullptr;
    }

    if (Asset->TargetMesh != nullptr)
    {
        return Asset->TargetMesh;
    }

    if (Asset->SourceWetClothingAsset != nullptr)
    {
        return Asset->SourceWetClothingAsset->TargetMesh;
    }

    return nullptr;
}

void SWetWrinkleAssetViewport::RebuildHitTriangles()
{
    HitTriangles.Reset();

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    if (TargetMesh == nullptr)
    {
        return;
    }

    const int32 MaterialCount = TargetMesh->GetMaterials().Num();
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialCount; ++MaterialSlotIndex)
    {
        if (BrushSettings.MaterialSlotIndex != INDEX_NONE && BrushSettings.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        TArray<FWetClothingAssetUVIsland> Islands;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(TargetMesh, 0, BrushSettings.UVChannelIndex, MaterialSlotIndex, Islands, nullptr))
        {
            continue;
        }

        for (const FWetClothingAssetUVIsland& Island : Islands)
        {
            HitTriangles.Append(Island.UVTriangles);
        }
    }
}

void SWetWrinkleAssetViewport::HandleSurfaceHitFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    CurrentSurfaceHit = SurfaceHit;
    RefreshBrushCursor();

    if (OnSurfaceHitChanged.IsBound())
    {
        OnSurfaceHitChanged.Execute(CurrentSurfaceHit);
    }
}

void SWetWrinkleAssetViewport::RefreshBrushCursor()
{
    if (BrushCursorComponent == nullptr)
    {
        return;
    }

    BrushCursorComponent->ClearAllMeshSections();
    BrushCursorComponent->SetMaterial(0, ResolveCursorMaterial());

    if (!BrushSettings.bShowPreview || !CurrentSurfaceHit.bHit)
    {
        return;
    }

    constexpr int32 SegmentCount = 64;
    const float Radius = CalculateBrushCursorWorldRadius();
    const float InnerRadius = Radius * 0.92f;
    const float NormalOffset = FMath::Max(Radius * 0.08f, 0.5f);
    const FVector Center = CurrentSurfaceHit.WorldPosition + CurrentSurfaceHit.WorldNormal * NormalOffset;

    FVector Tangent = CurrentSurfaceHit.WorldTangent.GetSafeNormal();
    FVector Bitangent = CurrentSurfaceHit.WorldBitangent.GetSafeNormal();
    if (Tangent.IsNearlyZero())
    {
        Tangent = MakeWetWrinkleAnyPerpendicular(CurrentSurfaceHit.WorldNormal);
    }
    if (Bitangent.IsNearlyZero())
    {
        Bitangent = FVector::CrossProduct(CurrentSurfaceHit.WorldNormal, Tangent).GetSafeNormal();
    }

    if (!FMath::IsNearlyZero(BrushSettings.RotationRadians))
    {
        const FQuat Rotation(CurrentSurfaceHit.WorldNormal.GetSafeNormal(), BrushSettings.RotationRadians);
        Tangent = Rotation.RotateVector(Tangent).GetSafeNormal();
        Bitangent = Rotation.RotateVector(Bitangent).GetSafeNormal();
    }

    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    Vertices.Reserve(SegmentCount * 2);
    Normals.Reserve(SegmentCount * 2);
    UVs.Reserve(SegmentCount * 2);
    VertexColors.Reserve(SegmentCount * 2);
    Tangents.Reserve(SegmentCount * 2);

    const FLinearColor CursorColor(0.12f, 0.82f, 1.0f, 1.0f);
    for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
    {
        const float Angle = (static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount)) * UE_TWO_PI;
        const FVector Direction = Tangent * FMath::Cos(Angle) + Bitangent * FMath::Sin(Angle);

        Vertices.Add(Center + Direction * Radius);
        Vertices.Add(Center + Direction * InnerRadius);
        Normals.Add(CurrentSurfaceHit.WorldNormal);
        Normals.Add(CurrentSurfaceHit.WorldNormal);
        UVs.Add(FVector2D(1.0f, 0.0f));
        UVs.Add(FVector2D(0.0f, 0.0f));
        VertexColors.Add(CursorColor);
        VertexColors.Add(CursorColor);
        Tangents.Add(FProcMeshTangent(Tangent, false));
        Tangents.Add(FProcMeshTangent(Tangent, false));
    }

    for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
    {
        const int32 NextSegmentIndex = (SegmentIndex + 1) % SegmentCount;
        const int32 OuterA = SegmentIndex * 2;
        const int32 InnerA = OuterA + 1;
        const int32 OuterB = NextSegmentIndex * 2;
        const int32 InnerB = OuterB + 1;

        Indices.Add(OuterA);
        Indices.Add(OuterB);
        Indices.Add(InnerB);

        Indices.Add(OuterA);
        Indices.Add(InnerB);
        Indices.Add(InnerA);
    }

    BrushCursorComponent->CreateMeshSection_LinearColor(
        0,
        Vertices,
        Indices,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        false,
        false);
}

void SWetWrinkleAssetViewport::ClearBrushCursor()
{
    if (BrushCursorComponent != nullptr)
    {
        BrushCursorComponent->ClearAllMeshSections();
    }
}

UMaterialInterface* SWetWrinkleAssetViewport::ResolveCursorMaterial()
{
    if (CursorMaterial != nullptr)
    {
        return CursorMaterial;
    }

    if (GEngine != nullptr)
    {
        if (GEngine->VertexColorMaterial != nullptr)
        {
            CursorMaterial = GEngine->VertexColorMaterial;
            return CursorMaterial;
        }

        if (GEngine->VertexColorViewModeMaterial_ColorOnly != nullptr)
        {
            CursorMaterial = GEngine->VertexColorViewModeMaterial_ColorOnly;
            return CursorMaterial;
        }
    }

    CursorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    return CursorMaterial;
}

FText SWetWrinkleAssetViewport::GetViewportHintText() const
{
    if (ResolveTargetMesh() == nullptr)
    {
        return LOCTEXT("NoTargetMeshHint", "Assign a Target Mesh or Source Wet Clothing Asset.");
    }

    if (HitTriangles.Num() == 0)
    {
        return LOCTEXT("NoHitTrianglesHint", "No triangles available for the selected UV channel/material slot.");
    }

    return LOCTEXT("ViewportHint", "Move the cursor over the mesh to inspect wrinkle brush UV hits.");
}

float SWetWrinkleAssetViewport::CalculateBrushCursorWorldRadius() const
{
    if (PreviewMeshComponent == nullptr || PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return 5.0f;
    }

    const FBoxSphereBounds Bounds = PreviewMeshComponent->CalcBounds(PreviewMeshComponent->GetComponentTransform());
    const float MeshRadius = FMath::Max(1.0f, static_cast<float>(Bounds.SphereRadius));
    return FMath::Clamp(MeshRadius * BrushSettings.BrushRadiusUV, 0.25f, MeshRadius * 0.35f);
}

#undef LOCTEXT_NAMESPACE
