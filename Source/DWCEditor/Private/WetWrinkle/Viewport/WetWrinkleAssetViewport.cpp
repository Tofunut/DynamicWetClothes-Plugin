#include "WetWrinkleAssetViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetWrinkleAsset.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
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

    FVector ComputeWetWrinkleBarycentric2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        const FVector2D V0 = B - A;
        const FVector2D V1 = C - A;
        const FVector2D V2 = Point - A;
        const double D00 = FVector2D::DotProduct(V0, V0);
        const double D01 = FVector2D::DotProduct(V0, V1);
        const double D11 = FVector2D::DotProduct(V1, V1);
        const double D20 = FVector2D::DotProduct(V2, V0);
        const double D21 = FVector2D::DotProduct(V2, V1);
        const double Denom = D00 * D11 - D01 * D01;
        if (FMath::IsNearlyZero(Denom))
        {
            return FVector(-1.0, -1.0, -1.0);
        }

        const double V = (D11 * D20 - D01 * D21) / Denom;
        const double W = (D00 * D21 - D01 * D20) / Denom;
        return FVector(1.0 - V - W, V, W);
    }

    bool IsWetWrinkleBarycentricInside(const FVector& Barycentric)
    {
        constexpr double Tolerance = 0.0001;
        return Barycentric.X >= -Tolerance &&
               Barycentric.Y >= -Tolerance &&
               Barycentric.Z >= -Tolerance &&
               Barycentric.X <= 1.0 + Tolerance &&
               Barycentric.Y <= 1.0 + Tolerance &&
               Barycentric.Z <= 1.0 + Tolerance;
    }

    bool IsWetWrinkleLinkedSurface(const FVector& PrimaryWorldPosition, const FVector& CandidateWorldPosition, float Radius)
    {
        const float MinLinkedDistance = FMath::Max(Radius * 0.5f, 1.0f);
        return FVector::DistSquared(PrimaryWorldPosition, CandidateWorldPosition) > FMath::Square(MinLinkedDistance);
    }

    void AppendWetWrinkleRingMesh(
        TArray<FVector>& Vertices,
        TArray<int32>& Indices,
        TArray<FVector>& Normals,
        TArray<FVector2D>& UVs,
        TArray<FLinearColor>& VertexColors,
        TArray<FProcMeshTangent>& Tangents,
        const FVector& Center,
        const FVector& Normal,
        const FVector& Tangent,
        const FVector& Bitangent,
        float Radius,
        float InnerRadius,
        const FLinearColor& Color)
    {
        constexpr int32 SegmentCount = 32;
        const int32 BaseVertexIndex = Vertices.Num();

        for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
        {
            const float Angle = (static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount)) * UE_TWO_PI;
            const FVector Direction = Tangent * FMath::Cos(Angle) + Bitangent * FMath::Sin(Angle);

            Vertices.Add(Center + Direction * Radius);
            Vertices.Add(Center + Direction * InnerRadius);
            Normals.Add(Normal);
            Normals.Add(Normal);
            UVs.Add(FVector2D(1.0f, 0.0f));
            UVs.Add(FVector2D(0.0f, 0.0f));
            VertexColors.Add(Color);
            VertexColors.Add(Color);
            Tangents.Add(FProcMeshTangent(Tangent, false));
            Tangents.Add(FProcMeshTangent(Tangent, false));
        }

        for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
        {
            const int32 NextSegmentIndex = (SegmentIndex + 1) % SegmentCount;
            const int32 OuterA = BaseVertexIndex + SegmentIndex * 2;
            const int32 InnerA = OuterA + 1;
            const int32 OuterB = BaseVertexIndex + NextSegmentIndex * 2;
            const int32 InnerB = OuterB + 1;

            Indices.Add(OuterA);
            Indices.Add(OuterB);
            Indices.Add(InnerB);

            Indices.Add(OuterA);
            Indices.Add(InnerB);
            Indices.Add(InnerA);
        }
    }
} // namespace

void SWetWrinkleAssetViewport::Construct(const FArguments& InArgs)
{
    WetWrinkleAsset = InArgs._WetWrinkleAsset;
    OnSurfaceHitChanged = InArgs._OnSurfaceHitChanged;
    OnPaintStrokeStarted = InArgs._OnPaintStrokeStarted;
    OnPaintStampRequested = InArgs._OnPaintStampRequested;
    OnPaintStrokeEnded = InArgs._OnPaintStrokeEnded;
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
    BrushCursorComponent->SetVisibility(false, true);
    BrushCursorComponent->bUseAsyncCooking = false;
    BrushCursorComponent->SetMaterial(0, ResolveCursorMaterial());
    PreviewScene->AddComponent(BrushCursorComponent, FTransform::Identity);

    StoredStampOverlayComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    StoredStampOverlayComponent->SetMobility(EComponentMobility::Movable);
    StoredStampOverlayComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StoredStampOverlayComponent->SetCastShadow(false);
    StoredStampOverlayComponent->bUseAsyncCooking = false;
    StoredStampOverlayComponent->SetMaterial(0, ResolveCursorMaterial());
    PreviewScene->AddComponent(StoredStampOverlayComponent, FTransform::Identity);

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

    if (PreviewScene.IsValid() && StoredStampOverlayComponent != nullptr)
    {
        PreviewScene->RemoveComponent(StoredStampOverlayComponent);
    }
}

void SWetWrinkleAssetViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(BrushCursorComponent);
    Collector.AddReferencedObject(StoredStampOverlayComponent);
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
    ApplyMaterialSlotVisibility();
    RebuildHitTriangles();
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    ClearBrushCursor();
    RefreshStoredStampOverlay();

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
    ApplyMaterialSlotVisibility();

    if (bNeedsTriangleRebuild)
    {
        RebuildHitTriangles();
        RefreshStoredStampOverlay();
    }

    RefreshBrushCursor();
    Invalidate();
}

void SWetWrinkleAssetViewport::RefreshStoredStampOverlay()
{
    if (StoredStampOverlayComponent == nullptr)
    {
        return;
    }

    StoredStampOverlayComponent->ClearAllMeshSections();
    StoredStampOverlayComponent->SetMaterial(0, ResolveCursorMaterial());

    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    if (Asset == nullptr || HitTriangles.Num() == 0)
    {
        return;
    }

    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    const FBoxSphereBounds Bounds = PreviewMeshComponent != nullptr
                                        ? PreviewMeshComponent->CalcBounds(PreviewMeshComponent->GetComponentTransform())
                                        : FBoxSphereBounds(FSphere(FVector::ZeroVector, 1.0f));
    const float MeshRadius = FMath::Max(1.0f, static_cast<float>(Bounds.SphereRadius));
    const FTransform ComponentTransform = PreviewMeshComponent != nullptr
                                              ? PreviewMeshComponent->GetComponentTransform()
                                              : FTransform::Identity;
    const FVector ViewLocation = ViewportClient.IsValid() ? ViewportClient->GetViewLocation() : FVector::ZeroVector;
    const bool bHasViewLocation = ViewportClient.IsValid();

    for (const FWetWrinkleStroke& Stroke : Asset->Strokes)
    {
        const bool bSelectedStroke = SelectedStrokeGuid.IsValid() && Stroke.StrokeGuid == SelectedStrokeGuid;
        const FLinearColor StrokeColor = bSelectedStroke
                                             ? FLinearColor(1.0f, 0.78f, 0.1f, 1.0f)
                                             : Stroke.bEnabled ? FLinearColor(0.12f, 0.82f, 1.0f, 1.0f)
                                                               : FLinearColor(0.25f, 0.25f, 0.25f, 0.85f);

        for (const FWetWrinkleStamp& Stamp : Stroke.Stamps)
        {
            if (Stamp.UVChannelIndex != BrushSettings.UVChannelIndex)
            {
                continue;
            }

            if (BrushSettings.MaterialSlotIndex != INDEX_NONE && Stamp.MaterialSlotIndex != BrushSettings.MaterialSlotIndex)
            {
                continue;
            }

            TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
            FindProjectedSurfacesAtUV(Stamp.MaterialSlotIndex, Stamp.UVChannelIndex, Stamp.PositionUV, ProjectedSurfaces);
            if (ProjectedSurfaces.Num() == 0)
            {
                continue;
            }

            int32 PrimarySurfaceIndex = 0;
#if WITH_EDITORONLY_DATA
            if (Stamp.bHasEditorSurface)
            {
                const FVector PrimaryWorldPosition = ComponentTransform.TransformPosition(Stamp.EditorSurfaceLocalPosition);
                double ClosestDistanceSq = TNumericLimits<double>::Max();
                for (int32 SurfaceIndex = 0; SurfaceIndex < ProjectedSurfaces.Num(); ++SurfaceIndex)
                {
                    const double DistanceSq = FVector::DistSquared(PrimaryWorldPosition, ProjectedSurfaces[SurfaceIndex].WorldPosition);
                    if (DistanceSq < ClosestDistanceSq)
                    {
                        ClosestDistanceSq = DistanceSq;
                        PrimarySurfaceIndex = SurfaceIndex;
                    }
                }
            }
#endif

            const float Radius = FMath::Clamp(MeshRadius * Stamp.BrushRadiusUV, 0.25f, MeshRadius * 0.35f);
            const float InnerRadius = Radius * 0.9f;
            const float NormalOffset = FMath::Max(Radius * 0.18f, 2.0f);
            const FVector PrimarySurfacePosition = ProjectedSurfaces[PrimarySurfaceIndex].WorldPosition;

            for (int32 SurfaceIndex = 0; SurfaceIndex < ProjectedSurfaces.Num(); ++SurfaceIndex)
            {
                const FWetWrinkleProjectedSurface& Surface = ProjectedSurfaces[SurfaceIndex];
                const bool bPrimarySurface = SurfaceIndex == PrimarySurfaceIndex;
                if (!bPrimarySurface && !IsWetWrinkleLinkedSurface(PrimarySurfacePosition, Surface.WorldPosition, Radius))
                {
                    continue;
                }

                FVector SurfaceNormal = Surface.WorldNormal.GetSafeNormal();
                if (SurfaceNormal.IsNearlyZero())
                {
                    SurfaceNormal = FVector::UpVector;
                }
                if (bHasViewLocation && FVector::DotProduct(SurfaceNormal, ViewLocation - Surface.WorldPosition) < 0.0)
                {
                    SurfaceNormal *= -1.0;
                }

                FVector SurfaceTangent = Surface.WorldTangent.GetSafeNormal();
                SurfaceTangent = (SurfaceTangent - SurfaceNormal * FVector::DotProduct(SurfaceTangent, SurfaceNormal)).GetSafeNormal();
                if (SurfaceTangent.IsNearlyZero())
                {
                    SurfaceTangent = MakeWetWrinkleAnyPerpendicular(SurfaceNormal);
                }

                FVector SurfaceBitangent = Surface.WorldBitangent.GetSafeNormal();
                SurfaceBitangent = (SurfaceBitangent - SurfaceNormal * FVector::DotProduct(SurfaceBitangent, SurfaceNormal)).GetSafeNormal();
                if (SurfaceBitangent.IsNearlyZero())
                {
                    SurfaceBitangent = FVector::CrossProduct(SurfaceNormal, SurfaceTangent).GetSafeNormal();
                }
                if (SurfaceBitangent.IsNearlyZero())
                {
                    SurfaceBitangent = MakeWetWrinkleAnyPerpendicular(SurfaceNormal);
                }

                FLinearColor SurfaceColor = StrokeColor;
                if (!bPrimarySurface)
                {
                    SurfaceColor = FLinearColor(1.0f, 0.55f, 0.08f, 1.0f);
                }
                else if (!Stroke.bEnabled)
                {
                    SurfaceColor = FLinearColor(0.45f, 0.45f, 0.45f, 0.9f);
                }

                AppendWetWrinkleRingMesh(
                    Vertices,
                    Indices,
                    Normals,
                    UVs,
                    VertexColors,
                    Tangents,
                    Surface.WorldPosition + SurfaceNormal * NormalOffset,
                    SurfaceNormal,
                    SurfaceTangent,
                    SurfaceBitangent,
                    Radius,
                    InnerRadius,
                    SurfaceColor);
            }
        }
    }

    if (Vertices.Num() > 0)
    {
        StoredStampOverlayComponent->CreateMeshSection_LinearColor(
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
}

void SWetWrinkleAssetViewport::SetSelectedStrokeGuid(const FGuid& InStrokeGuid)
{
    SelectedStrokeGuid = InStrokeGuid;
    RefreshStoredStampOverlay();
    Invalidate();
}

void SWetWrinkleAssetViewport::PreviewBrushAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV)
{
    TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
    FindProjectedSurfacesAtUV(MaterialSlotIndex, UVChannelIndex, UV, ProjectedSurfaces);
    if (ProjectedSurfaces.Num() == 0)
    {
        ClearBrushCursor();
        Invalidate();
        return;
    }

    const FWetWrinkleProjectedSurface& Surface = ProjectedSurfaces[0];
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    CurrentSurfaceHit.bHit = true;
    CurrentSurfaceHit.MaterialSlotIndex = Surface.MaterialSlotIndex;
    CurrentSurfaceHit.TriangleID = Surface.TriangleID;
    CurrentSurfaceHit.UVChannelIndex = UVChannelIndex;
    CurrentSurfaceHit.WorldPosition = Surface.WorldPosition;
    CurrentSurfaceHit.WorldNormal = Surface.WorldNormal;
    CurrentSurfaceHit.WorldTangent = Surface.WorldTangent;
    CurrentSurfaceHit.WorldBitangent = Surface.WorldBitangent;
    CurrentSurfaceHit.UV = UV;
    RefreshBrushCursor();
    Invalidate();
}

void SWetWrinkleAssetViewport::ClearExternalBrushPreview()
{
    ClearBrushCursor();
    Invalidate();
}

bool SWetWrinkleAssetViewport::TryBuildSurfaceHitAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    OutHit.UVChannelIndex = UVChannelIndex;
    OutHit.UV = UV;

    TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
    FindProjectedSurfacesAtUV(MaterialSlotIndex, UVChannelIndex, UV, ProjectedSurfaces);
    if (ProjectedSurfaces.Num() == 0 || PreviewMeshComponent == nullptr)
    {
        return false;
    }

    const FWetWrinkleProjectedSurface& Surface = ProjectedSurfaces[0];
    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();

    OutHit.bHit = true;
    OutHit.MaterialSlotIndex = Surface.MaterialSlotIndex;
    OutHit.TriangleID = Surface.TriangleID;
    OutHit.UVChannelIndex = UVChannelIndex;
    OutHit.WorldPosition = Surface.WorldPosition;
    OutHit.WorldNormal = Surface.WorldNormal;
    OutHit.WorldTangent = Surface.WorldTangent;
    OutHit.WorldBitangent = Surface.WorldBitangent;
    OutHit.LocalPosition = ComponentTransform.InverseTransformPosition(Surface.WorldPosition);
    OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Surface.WorldNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    OutHit.LocalTangent = ComponentTransform.InverseTransformVectorNoScale(Surface.WorldTangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    OutHit.LocalBitangent = ComponentTransform.InverseTransformVectorNoScale(Surface.WorldBitangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
    OutHit.UV = UV;
    OutHit.DistanceSq = 0.0;
    return true;
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
        const FVector LocalPosition =
            Triangle.LocalPositions[0] * Barycentric.X +
            Triangle.LocalPositions[1] * Barycentric.Y +
            Triangle.LocalPositions[2] * Barycentric.Z;
        const FVector2D UV = Triangle.UVs[0] * Barycentric.X + Triangle.UVs[1] * Barycentric.Y + Triangle.UVs[2] * Barycentric.Z;

        OutHit.bHit = true;
        OutHit.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        OutHit.TriangleID = Triangle.TriangleID;
        OutHit.UVChannelIndex = BrushSettings.UVChannelIndex;
        OutHit.WorldPosition = IntersectionPoint;
        OutHit.WorldNormal = Normal;
        OutHit.WorldTangent = Tangent;
        OutHit.WorldBitangent = Bitangent;
        OutHit.LocalPosition = LocalPosition;
        OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Normal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        OutHit.LocalTangent = ComponentTransform.InverseTransformVectorNoScale(Tangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        OutHit.LocalBitangent = ComponentTransform.InverseTransformVectorNoScale(Bitangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
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

void SWetWrinkleAssetViewport::ApplyMaterialSlotVisibility()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    const USkeletalMesh* SkeletalMesh = PreviewMeshComponent->GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr)
    {
        return;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr)
    {
        return;
    }

    for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
    {
        if (BrushSettings.MaterialSlotIndex == INDEX_NONE)
        {
            PreviewMeshComponent->ShowAllMaterialSections(LODIndex);
            continue;
        }

        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
        for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
        {
            const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
            const bool bShowSection = Section.MaterialIndex == BrushSettings.MaterialSlotIndex;
            PreviewMeshComponent->ShowMaterialSection(Section.MaterialIndex, SectionIndex, bShowSection, LODIndex);
        }
    }
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

void SWetWrinkleAssetViewport::BeginPaintStrokeFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (OnPaintStrokeStarted.IsBound())
    {
        OnPaintStrokeStarted.Execute(SurfaceHit);
    }
}

void SWetWrinkleAssetViewport::RequestPaintStampFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (OnPaintStampRequested.IsBound())
    {
        OnPaintStampRequested.Execute(SurfaceHit);
    }
}

void SWetWrinkleAssetViewport::EndPaintStrokeFromClient()
{
    if (OnPaintStrokeEnded.IsBound())
    {
        OnPaintStrokeEnded.Execute();
    }
}

void SWetWrinkleAssetViewport::RefreshBrushCursor()
{
    if (BrushCursorComponent == nullptr)
    {
        return;
    }

    BrushCursorComponent->SetVisibility(false, true);
    BrushCursorComponent->ClearAllMeshSections();
    BrushCursorComponent->SetMaterial(0, ResolveCursorMaterial());
    BrushCursorComponent->MarkRenderStateDirty();

    if (!BrushSettings.bShowPreview || !CurrentSurfaceHit.bHit)
    {
        return;
    }

    const float Radius = CalculateBrushCursorWorldRadius();
    const float InnerRadius = Radius * 0.92f;
    const float NormalOffset = FMath::Max(Radius * 0.12f, 1.0f);
    const FVector ViewLocation = ViewportClient.IsValid() ? ViewportClient->GetViewLocation() : FVector::ZeroVector;
    const bool bHasViewLocation = ViewportClient.IsValid();

    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    const FLinearColor CursorColor(0.12f, 0.82f, 1.0f, 1.0f);
    TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
    FindProjectedSurfacesAtUV(CurrentSurfaceHit.MaterialSlotIndex, CurrentSurfaceHit.UVChannelIndex, CurrentSurfaceHit.UV, ProjectedSurfaces);
    if (ProjectedSurfaces.Num() == 0)
    {
        FWetWrinkleProjectedSurface FallbackSurface;
        FallbackSurface.MaterialSlotIndex = CurrentSurfaceHit.MaterialSlotIndex;
        FallbackSurface.TriangleID = CurrentSurfaceHit.TriangleID;
        FallbackSurface.WorldPosition = CurrentSurfaceHit.WorldPosition;
        FallbackSurface.WorldNormal = CurrentSurfaceHit.WorldNormal;
        FallbackSurface.WorldTangent = CurrentSurfaceHit.WorldTangent;
        FallbackSurface.WorldBitangent = CurrentSurfaceHit.WorldBitangent;
        ProjectedSurfaces.Add(FallbackSurface);
    }

    int32 PrimarySurfaceIndex = 0;
    double ClosestPrimaryDistanceSq = TNumericLimits<double>::Max();
    for (int32 SurfaceIndex = 0; SurfaceIndex < ProjectedSurfaces.Num(); ++SurfaceIndex)
    {
        const double DistanceSq = FVector::DistSquared(CurrentSurfaceHit.WorldPosition, ProjectedSurfaces[SurfaceIndex].WorldPosition);
        if (DistanceSq < ClosestPrimaryDistanceSq)
        {
            ClosestPrimaryDistanceSq = DistanceSq;
            PrimarySurfaceIndex = SurfaceIndex;
        }
    }

    const FVector PrimarySurfacePosition = ProjectedSurfaces[PrimarySurfaceIndex].WorldPosition;
    for (int32 SurfaceIndex = 0; SurfaceIndex < ProjectedSurfaces.Num(); ++SurfaceIndex)
    {
        const FWetWrinkleProjectedSurface& Surface = ProjectedSurfaces[SurfaceIndex];
        const bool bPrimarySurface = SurfaceIndex == PrimarySurfaceIndex;
        if (!bPrimarySurface && !IsWetWrinkleLinkedSurface(PrimarySurfacePosition, Surface.WorldPosition, Radius))
        {
            continue;
        }

        const FVector SurfacePosition = bPrimarySurface ? CurrentSurfaceHit.WorldPosition : Surface.WorldPosition;
        FVector SurfaceNormal = (bPrimarySurface ? CurrentSurfaceHit.WorldNormal : Surface.WorldNormal).GetSafeNormal();
        if (SurfaceNormal.IsNearlyZero())
        {
            SurfaceNormal = FVector::UpVector;
        }
        if (!bPrimarySurface && bHasViewLocation && FVector::DotProduct(SurfaceNormal, ViewLocation - SurfacePosition) < 0.0)
        {
            SurfaceNormal *= -1.0;
        }

        FVector SurfaceTangent = (bPrimarySurface ? CurrentSurfaceHit.WorldTangent : Surface.WorldTangent).GetSafeNormal();
        FVector SurfaceBitangent = (bPrimarySurface ? CurrentSurfaceHit.WorldBitangent : Surface.WorldBitangent).GetSafeNormal();
        SurfaceTangent = (SurfaceTangent - SurfaceNormal * FVector::DotProduct(SurfaceTangent, SurfaceNormal)).GetSafeNormal();
        if (SurfaceTangent.IsNearlyZero())
        {
            SurfaceTangent = MakeWetWrinkleAnyPerpendicular(SurfaceNormal);
        }
        SurfaceBitangent = (SurfaceBitangent - SurfaceNormal * FVector::DotProduct(SurfaceBitangent, SurfaceNormal)).GetSafeNormal();
        if (SurfaceBitangent.IsNearlyZero())
        {
            SurfaceBitangent = FVector::CrossProduct(SurfaceNormal, SurfaceTangent).GetSafeNormal();
        }

        if (!FMath::IsNearlyZero(BrushSettings.RotationRadians))
        {
            const FQuat Rotation(SurfaceNormal, BrushSettings.RotationRadians);
            SurfaceTangent = Rotation.RotateVector(SurfaceTangent).GetSafeNormal();
            SurfaceBitangent = Rotation.RotateVector(SurfaceBitangent).GetSafeNormal();
        }

        FLinearColor SurfaceColor = CursorColor;
        if (!bPrimarySurface)
        {
            SurfaceColor = FLinearColor(1.0f, 0.55f, 0.08f, 1.0f);
        }

        AppendWetWrinkleRingMesh(
            Vertices,
            Indices,
            Normals,
            UVs,
            VertexColors,
            Tangents,
            SurfacePosition + SurfaceNormal * NormalOffset,
            SurfaceNormal,
            SurfaceTangent,
            SurfaceBitangent,
            Radius,
            InnerRadius,
            SurfaceColor);
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
    BrushCursorComponent->SetVisibility(true, true);
    BrushCursorComponent->MarkRenderStateDirty();
}

void SWetWrinkleAssetViewport::ClearBrushCursor()
{
    if (BrushCursorComponent != nullptr)
    {
        BrushCursorComponent->SetVisibility(false, true);
        BrushCursorComponent->ClearAllMeshSections();
        BrushCursorComponent->MarkRenderStateDirty();
    }
}

void SWetWrinkleAssetViewport::ClearStoredStampOverlay()
{
    if (StoredStampOverlayComponent != nullptr)
    {
        StoredStampOverlayComponent->ClearAllMeshSections();
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

void SWetWrinkleAssetViewport::FindProjectedSurfacesAtUV(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex,
    const FVector2D& UV,
    TArray<FWetWrinkleProjectedSurface>& OutSurfaces) const
{
    OutSurfaces.Reset();

    if (PreviewMeshComponent == nullptr || UVChannelIndex != BrushSettings.UVChannelIndex)
    {
        return;
    }

    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();

    for (const FWetClothingAssetUVTriangle& Triangle : HitTriangles)
    {
        if (MaterialSlotIndex != INDEX_NONE && Triangle.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        const FVector Barycentric = ComputeWetWrinkleBarycentric2D(UV, Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]);
        if (!IsWetWrinkleBarycentricInside(Barycentric))
        {
            continue;
        }

        const FVector LocalPosition =
            Triangle.LocalPositions[0] * Barycentric.X +
            Triangle.LocalPositions[1] * Barycentric.Y +
            Triangle.LocalPositions[2] * Barycentric.Z;

        const FVector WorldA = ComponentTransform.TransformPosition(Triangle.LocalPositions[0]);
        const FVector WorldB = ComponentTransform.TransformPosition(Triangle.LocalPositions[1]);
        const FVector WorldC = ComponentTransform.TransformPosition(Triangle.LocalPositions[2]);

        FVector WorldNormal = FVector::CrossProduct(WorldB - WorldA, WorldC - WorldA).GetSafeNormal();
        if (WorldNormal.IsNearlyZero())
        {
            WorldNormal = FVector::UpVector;
        }

        FVector WorldTangent = (WorldB - WorldA).GetSafeNormal();
        WorldTangent = (WorldTangent - WorldNormal * FVector::DotProduct(WorldTangent, WorldNormal)).GetSafeNormal();
        if (WorldTangent.IsNearlyZero())
        {
            WorldTangent = MakeWetWrinkleAnyPerpendicular(WorldNormal);
        }

        FVector WorldBitangent = FVector::CrossProduct(WorldNormal, WorldTangent).GetSafeNormal();
        if (WorldBitangent.IsNearlyZero())
        {
            WorldBitangent = MakeWetWrinkleAnyPerpendicular(WorldNormal);
        }

        FWetWrinkleProjectedSurface ProjectedSurface;
        ProjectedSurface.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        ProjectedSurface.TriangleID = Triangle.TriangleID;
        ProjectedSurface.WorldPosition = ComponentTransform.TransformPosition(LocalPosition);
        ProjectedSurface.WorldNormal = WorldNormal;
        ProjectedSurface.WorldTangent = WorldTangent;
        ProjectedSurface.WorldBitangent = WorldBitangent;
        OutSurfaces.Add(ProjectedSurface);
    }
}

bool SWetWrinkleAssetViewport::TryProjectUVToWorld(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex,
    const FVector2D& UV,
    FVector& OutWorldPosition,
    FVector& OutWorldNormal,
    FVector& OutWorldTangent,
    FVector& OutWorldBitangent) const
{
    TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
    FindProjectedSurfacesAtUV(MaterialSlotIndex, UVChannelIndex, UV, ProjectedSurfaces);
    if (ProjectedSurfaces.Num() == 0)
    {
        return false;
    }

    OutWorldPosition = ProjectedSurfaces[0].WorldPosition;
    OutWorldNormal = ProjectedSurfaces[0].WorldNormal;
    OutWorldTangent = ProjectedSurfaces[0].WorldTangent;
    OutWorldBitangent = ProjectedSurfaces[0].WorldBitangent;
    return true;
}

#undef LOCTEXT_NAMESPACE
