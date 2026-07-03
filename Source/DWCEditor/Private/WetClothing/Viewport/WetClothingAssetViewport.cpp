#include "WetClothingAssetViewport.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothingAssetViewportClient.h"
#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetViewport"

namespace
{
    struct FQuantizedLocalVertex
    {
        int32 X = 0;
        int32 Y = 0;
        int32 Z = 0;

        bool operator==(const FQuantizedLocalVertex& Other) const
        {
            return X == Other.X && Y == Other.Y && Z == Other.Z;
        }
    };

    uint32 GetTypeHash(const FQuantizedLocalVertex& Vertex)
    {
        return HashCombine(HashCombine(::GetTypeHash(Vertex.X), ::GetTypeHash(Vertex.Y)), ::GetTypeHash(Vertex.Z));
    }

    bool operator<(const FQuantizedLocalVertex& A, const FQuantizedLocalVertex& B)
    {
        if (A.X != B.X)
        {
            return A.X < B.X;
        }

        if (A.Y != B.Y)
        {
            return A.Y < B.Y;
        }

        return A.Z < B.Z;
    }

    struct FQuantizedLocalEdge
    {
        FQuantizedLocalVertex A;
        FQuantizedLocalVertex B;

        bool operator==(const FQuantizedLocalEdge& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    uint32 GetTypeHash(const FQuantizedLocalEdge& Edge)
    {
        return HashCombine(GetTypeHash(Edge.A), GetTypeHash(Edge.B));
    }

    struct FWetClothingAssetSelectionEdge
    {
        FVector LocalStart = FVector::ZeroVector;
        FVector LocalEnd = FVector::ZeroVector;
        FVector LocalNormal = FVector::UpVector;
    };

    FVector MakeWetPartOverlayNormal(const FVector& A, const FVector& B, const FVector& C)
    {
        FVector Normal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
        if (Normal.IsNearlyZero())
        {
            Normal = FVector::UpVector;
        }
        return Normal;
    }

    float CalculateWetPartOverlayOffset(const USkeletalMeshComponent* MeshComponent)
    {
        if (MeshComponent == nullptr)
        {
            return 0.02f;
        }

        return FMath::Clamp(static_cast<float>(MeshComponent->Bounds.SphereRadius) * 0.0012f, 0.02f, 0.12f);
    }

    float CalculateSelectionOverlayHalfThickness(const USkeletalMeshComponent* MeshComponent)
    {
        if (MeshComponent == nullptr)
        {
            return 0.08f;
        }

        return FMath::Clamp(static_cast<float>(MeshComponent->Bounds.SphereRadius) * 0.001f, 0.025f, 0.16f);
    }

    FQuantizedLocalVertex MakeQuantizedLocalVertex(const FVector& Position)
    {
        constexpr double QuantizeScale = 1000.0;

        return FQuantizedLocalVertex{
            static_cast<int32>(FMath::RoundToInt(Position.X * QuantizeScale)),
            static_cast<int32>(FMath::RoundToInt(Position.Y * QuantizeScale)),
            static_cast<int32>(FMath::RoundToInt(Position.Z * QuantizeScale))
        };
    }

    FQuantizedLocalEdge MakeQuantizedLocalEdge(const FVector& Start, const FVector& End)
    {
        FQuantizedLocalVertex QuantizedStart = MakeQuantizedLocalVertex(Start);
        FQuantizedLocalVertex QuantizedEnd = MakeQuantizedLocalVertex(End);

        if (QuantizedEnd < QuantizedStart)
        {
            Swap(QuantizedStart, QuantizedEnd);
        }

        return FQuantizedLocalEdge{ QuantizedStart, QuantizedEnd };
    }

    FVector MakeAnyPerpendicular(const FVector& Direction)
    {
        FVector Perpendicular = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
        if (Perpendicular.IsNearlyZero())
        {
            Perpendicular = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
        }

        return Perpendicular.IsNearlyZero() ? FVector::ForwardVector : Perpendicular;
    }

    void AddSelectionOverlayVertex(
        TArray<FVector>&      Vertices,
        TArray<FVector>&      Normals,
        TArray<FVector2D>&    UVs,
        TArray<FLinearColor>& VertexColors,
        const FVector&        Position,
        const FVector&        Normal,
        const FLinearColor&   Color)
    {
        Vertices.Add(Position);
        Normals.Add(Normal);
        UVs.Add(FVector2D::ZeroVector);
        VertexColors.Add(Color);
    }

    void AddSelectionOverlayQuad(
        TArray<int32>& Indices,
        int32          A,
        int32          B,
        int32          C,
        int32          D)
    {
        Indices.Add(A);
        Indices.Add(B);
        Indices.Add(C);
        Indices.Add(C);
        Indices.Add(B);
        Indices.Add(A);

        Indices.Add(A);
        Indices.Add(C);
        Indices.Add(D);
        Indices.Add(D);
        Indices.Add(C);
        Indices.Add(A);
    }

    void AddSelectionOverlayEdgeMesh(
        TArray<FVector>&                      Vertices,
        TArray<int32>&                        Indices,
        TArray<FVector>&                      Normals,
        TArray<FVector2D>&                    UVs,
        TArray<FLinearColor>&                 VertexColors,
        const FWetClothingAssetSelectionEdge& Edge,
        float                                 HalfThickness,
        const FLinearColor&                   Color)
    {
        const FVector EdgeDirection = (Edge.LocalEnd - Edge.LocalStart).GetSafeNormal();
        if (EdgeDirection.IsNearlyZero())
        {
            return;
        }

        FVector Normal = Edge.LocalNormal.GetSafeNormal();
        if (Normal.IsNearlyZero())
        {
            Normal = MakeAnyPerpendicular(EdgeDirection);
        }

        FVector Side = FVector::CrossProduct(EdgeDirection, Normal).GetSafeNormal();
        if (Side.IsNearlyZero())
        {
            Side = MakeAnyPerpendicular(EdgeDirection);
            Normal = FVector::CrossProduct(Side, EdgeDirection).GetSafeNormal();
        }

        const FVector CenterOffset = Normal * (HalfThickness * 1.5f);
        const FVector Start = Edge.LocalStart + CenterOffset;
        const FVector End = Edge.LocalEnd + CenterOffset;
        const int32   BaseIndex = Vertices.Num();

        const FVector Corners[8] = {
            Start + Side * HalfThickness + Normal * HalfThickness,
            Start - Side * HalfThickness + Normal * HalfThickness,
            Start - Side * HalfThickness - Normal * HalfThickness,
            Start + Side * HalfThickness - Normal * HalfThickness,
            End + Side * HalfThickness + Normal * HalfThickness,
            End - Side * HalfThickness + Normal * HalfThickness,
            End - Side * HalfThickness - Normal * HalfThickness,
            End + Side * HalfThickness - Normal * HalfThickness
        };

        for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
        {
            FVector VertexNormal = (Corners[CornerIndex] - ((CornerIndex < 4) ? Start : End)).GetSafeNormal();
            if (VertexNormal.IsNearlyZero())
            {
                VertexNormal = Normal;
            }

            AddSelectionOverlayVertex(Vertices, Normals, UVs, VertexColors, Corners[CornerIndex], VertexNormal, Color);
        }

        AddSelectionOverlayQuad(Indices, BaseIndex + 0, BaseIndex + 4, BaseIndex + 5, BaseIndex + 1);
        AddSelectionOverlayQuad(Indices, BaseIndex + 1, BaseIndex + 5, BaseIndex + 6, BaseIndex + 2);
        AddSelectionOverlayQuad(Indices, BaseIndex + 2, BaseIndex + 6, BaseIndex + 7, BaseIndex + 3);
        AddSelectionOverlayQuad(Indices, BaseIndex + 3, BaseIndex + 7, BaseIndex + 4, BaseIndex + 0);
        AddSelectionOverlayQuad(Indices, BaseIndex + 0, BaseIndex + 1, BaseIndex + 2, BaseIndex + 3);
        AddSelectionOverlayQuad(Indices, BaseIndex + 4, BaseIndex + 7, BaseIndex + 6, BaseIndex + 5);
    }
} // namespace

void SWetClothingAssetViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    OnIslandPicked = InArgs._OnIslandPicked;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

    SEditorViewport::Construct(SEditorViewport::FArguments());

    PreviewMeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
    PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewScene->AddComponent(PreviewMeshComponent, FTransform::Identity);

    WetPartOverlayComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    WetPartOverlayComponent->SetMobility(EComponentMobility::Movable);
    WetPartOverlayComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WetPartOverlayComponent->SetCastShadow(false);
    WetPartOverlayComponent->bUseAsyncCooking = false;
    WetPartOverlayComponent->SetMaterial(0, ResolveWetPartOverlayMaterial());
    PreviewScene->AddComponent(WetPartOverlayComponent, FTransform::Identity);

    SelectionOverlayComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    SelectionOverlayComponent->SetMobility(EComponentMobility::Movable);
    SelectionOverlayComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SelectionOverlayComponent->SetCastShadow(false);
    SelectionOverlayComponent->bUseAsyncCooking = false;
    SelectionOverlayComponent->SetMaterial(0, ResolveWetPartOverlayMaterial());
    PreviewScene->AddComponent(SelectionOverlayComponent, FTransform::Identity);

    RefreshPreviewMesh();
}

SWetClothingAssetViewport::~SWetClothingAssetViewport()
{
    if (PreviewScene.IsValid() && SelectionOverlayComponent != nullptr)
    {
        PreviewScene->RemoveComponent(SelectionOverlayComponent);
    }

    if (PreviewScene.IsValid() && WetPartOverlayComponent != nullptr)
    {
        PreviewScene->RemoveComponent(WetPartOverlayComponent);
    }

    if (PreviewScene.IsValid() && PreviewMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewMeshComponent);
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->Viewport = nullptr;
    }
}

void SWetClothingAssetViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(WetPartOverlayComponent);
    Collector.AddReferencedObject(SelectionOverlayComponent);
    Collector.AddReferencedObject(WetPartOverlayMaterial);
    Collector.AddReferencedObjects(OriginalPreviewMaterials);
}

void SWetClothingAssetViewport::RefreshPreviewMesh()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    USkeletalMesh* TargetMesh = nullptr;
    if (UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        TargetMesh = WetClothingAssetPtr->TargetMesh;
    }

    PreviewMeshComponent->SetSkeletalMeshAsset(TargetMesh);
    PreviewMeshComponent->ShowAllMaterialSections(0);
    if (WetPartOverlayComponent != nullptr)
    {
        WetPartOverlayComponent->ClearAllMeshSections();
        WetPartOverlayComponent->SetMaterial(0, ResolveWetPartOverlayMaterial());
    }
    if (SelectionOverlayComponent != nullptr)
    {
        SelectionOverlayComponent->ClearAllMeshSections();
        SelectionOverlayComponent->SetMaterial(0, ResolveWetPartOverlayMaterial());
    }
    CacheOriginalMaterials();
    CurrentHighlightedMaterialSlot = INDEX_NONE;
    CurrentHighlightedUVIslandIDs.Reset();
    ClearHighlightedIsland();
    ClearWetPartIslandColors();

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

void SWetClothingAssetViewport::SetHighlightedMaterialSlot(int32 SlotIndex)
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    PreviewMeshComponent->ShowAllMaterialSections(0);

    const int32 MaterialCount = PreviewMeshComponent->GetNumMaterials();
    if (SlotIndex < 0 || SlotIndex >= MaterialCount || !OriginalPreviewMaterials.IsValidIndex(SlotIndex))
    {
        CurrentHighlightedMaterialSlot = INDEX_NONE;
        if (OverlayText.IsValid())
        {
            OverlayText->SetText(GetViewportHintText());
        }
        return;
    }

    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        PreviewMeshComponent->ShowMaterialSection(MaterialIndex, MaterialIndex, MaterialIndex == SlotIndex, 0);
    }

    CurrentHighlightedMaterialSlot = SlotIndex;

    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
    }

    Invalidate();
}

void SWetClothingAssetViewport::ClearMaterialSlotHighlight()
{
    if (PreviewMeshComponent != nullptr)
    {
        PreviewMeshComponent->ShowAllMaterialSections(0);
    }

    CurrentHighlightedMaterialSlot = INDEX_NONE;

    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
    }

    Invalidate();
}

void SWetClothingAssetViewport::SetSelectableIslands(const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& InIslands)
{
    CurrentSelectableIslands.Reset();

    for (const TSharedPtr<FWetClothingAssetUVIsland>& Island : InIslands)
    {
        if (Island.IsValid())
        {
            CurrentSelectableIslands.Add(*Island);
        }
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPickableIslands(CurrentSelectableIslands);
    }

    RefreshWetPartOverlayMesh();
    SetHighlightedUVIslandIDs(CurrentHighlightedUVIslandIDs);
}

void SWetClothingAssetViewport::SetHighlightedUVIslandIDs(const TSet<int32>& InUVIslandIDs)
{
    CurrentHighlightedUVIslandIDs = InUVIslandIDs;
    RefreshSelectionOverlayMesh();
}

void SWetClothingAssetViewport::SetSelectionOverlayThicknessScale(float InThicknessScale)
{
    const float NewThicknessScale = FMath::Clamp(InThicknessScale, 0.25f, 4.0f);
    if (!FMath::IsNearlyEqual(SelectionOverlayThicknessScale, NewThicknessScale))
    {
        SelectionOverlayThicknessScale = NewThicknessScale;
        RefreshSelectionOverlayMesh();
    }
}

void SWetClothingAssetViewport::ClearHighlightedIsland()
{
    CurrentHighlightedUVIslandIDs.Reset();
    if (SelectionOverlayComponent != nullptr)
    {
        SelectionOverlayComponent->ClearAllMeshSections();
    }
}

void SWetClothingAssetViewport::SetWetPartIslandAssignments(const TMap<int32, int32>& InUVIslandToWetPartID, const TMap<int32, FLinearColor>& InIslandColors)
{
    CurrentWetPartIslandAssignments = InUVIslandToWetPartID;
    CurrentWetPartIslandColors = InIslandColors;
    RefreshWetPartOverlayMesh();
}

void SWetClothingAssetViewport::ClearWetPartIslandColors()
{
    CurrentWetPartIslandAssignments.Reset();
    CurrentWetPartIslandColors.Reset();

    if (WetPartOverlayComponent != nullptr)
    {
        WetPartOverlayComponent->ClearAllMeshSections();
    }
}

void SWetClothingAssetViewport::RefreshWetPartOverlayMesh()
{
    if (WetPartOverlayComponent == nullptr)
    {
        return;
    }

    WetPartOverlayComponent->ClearAllMeshSections();
    WetPartOverlayComponent->SetMaterial(0, ResolveWetPartOverlayMaterial());

    TArray<FVector>          Vertices;
    TArray<int32>            Indices;
    TArray<FVector>          Normals;
    TArray<FVector2D>        UVs;
    TArray<FLinearColor>     VertexColors;
    TArray<FProcMeshTangent> Tangents;

    const float NormalOffset = CalculateWetPartOverlayOffset(PreviewMeshComponent);

    for (const FWetClothingAssetUVIsland& Island : CurrentSelectableIslands)
    {
        const int32*        WetPartID = CurrentWetPartIslandAssignments.Find(Island.UVIslandID);
        const FLinearColor* IslandColor = CurrentWetPartIslandColors.Find(Island.UVIslandID);
        if (WetPartID == nullptr || *WetPartID == 0 || IslandColor == nullptr)
        {
            continue;
        }

        for (const FWetClothingAssetUVTriangle& UVTriangle : Island.UVTriangles)
        {
            const FVector Normal = MakeWetPartOverlayNormal(UVTriangle.LocalPositions[0], UVTriangle.LocalPositions[1], UVTriangle.LocalPositions[2]);

            for (float OffsetSign : { 1.0f, -1.0f })
            {
                const FVector OffsetNormal = Normal * OffsetSign;
                const int32   BaseVertexIndex = Vertices.Num();

                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    Vertices.Add(UVTriangle.LocalPositions[CornerIndex] + OffsetNormal * NormalOffset);
                    Normals.Add(OffsetNormal);
                    UVs.Add(UVTriangle.UVs[CornerIndex]);
                    VertexColors.Add(*IslandColor);
                }

                Indices.Add(BaseVertexIndex);
                Indices.Add(BaseVertexIndex + 1);
                Indices.Add(BaseVertexIndex + 2);

                Indices.Add(BaseVertexIndex + 2);
                Indices.Add(BaseVertexIndex + 1);
                Indices.Add(BaseVertexIndex);
            }
        }
    }

    if (Vertices.Num() > 0)
    {
        WetPartOverlayComponent->CreateMeshSection_LinearColor(
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

void SWetClothingAssetViewport::RefreshSelectionOverlayMesh()
{
    if (SelectionOverlayComponent == nullptr)
    {
        return;
    }

    SelectionOverlayComponent->ClearAllMeshSections();
    SelectionOverlayComponent->SetMaterial(0, ResolveWetPartOverlayMaterial());

    if (CurrentHighlightedUVIslandIDs.Num() == 0)
    {
        return;
    }

    struct FEdgeAccumulatorWithNormal
    {
        int32   Count = 0;
        FVector Start = FVector::ZeroVector;
        FVector End = FVector::ZeroVector;
        FVector NormalSum = FVector::ZeroVector;
    };

    TMap<FQuantizedLocalEdge, FEdgeAccumulatorWithNormal> EdgeMap;
    auto                                                  AccumulateEdge = [&EdgeMap](const FVector& Start, const FVector& End, const FVector& TriangleNormal)
    {
        const FQuantizedLocalEdge   EdgeKey = MakeQuantizedLocalEdge(Start, End);
        FEdgeAccumulatorWithNormal& Accumulator = EdgeMap.FindOrAdd(EdgeKey);
        if (Accumulator.Count == 0)
        {
            Accumulator.Start = Start;
            Accumulator.End = End;
        }
        ++Accumulator.Count;
        Accumulator.NormalSum += TriangleNormal;
    };

    for (const FWetClothingAssetUVIsland& Island : CurrentSelectableIslands)
    {
        if (!CurrentHighlightedUVIslandIDs.Contains(Island.UVIslandID))
        {
            continue;
        }

        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            const FVector TriangleNormal = MakeWetPartOverlayNormal(Triangle.LocalPositions[0], Triangle.LocalPositions[1], Triangle.LocalPositions[2]);
            AccumulateEdge(Triangle.LocalPositions[0], Triangle.LocalPositions[1], TriangleNormal);
            AccumulateEdge(Triangle.LocalPositions[1], Triangle.LocalPositions[2], TriangleNormal);
            AccumulateEdge(Triangle.LocalPositions[2], Triangle.LocalPositions[0], TriangleNormal);
        }
    }

    TArray<FVector>          Vertices;
    TArray<int32>            Indices;
    TArray<FVector>          Normals;
    TArray<FVector2D>        UVs;
    TArray<FLinearColor>     VertexColors;
    TArray<FProcMeshTangent> Tangents;

    const float        HalfThickness = CalculateSelectionOverlayHalfThickness(PreviewMeshComponent) * SelectionOverlayThicknessScale;
    const FLinearColor SelectionColor(1.0f, 0.58f, 0.02f, 1.0f);

    for (const TPair<FQuantizedLocalEdge, FEdgeAccumulatorWithNormal>& Pair : EdgeMap)
    {
        FWetClothingAssetSelectionEdge SelectionEdge;
        SelectionEdge.LocalStart = Pair.Value.Start;
        SelectionEdge.LocalEnd = Pair.Value.End;
        SelectionEdge.LocalNormal = Pair.Value.NormalSum.GetSafeNormal();
        if (SelectionEdge.LocalNormal.IsNearlyZero())
        {
            SelectionEdge.LocalNormal = FVector::UpVector;
        }

        AddSelectionOverlayEdgeMesh(
            Vertices,
            Indices,
            Normals,
            UVs,
            VertexColors,
            SelectionEdge,
            HalfThickness,
            SelectionColor);
    }

    if (Vertices.Num() > 0)
    {
        SelectionOverlayComponent->CreateMeshSection_LinearColor(
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

void SWetClothingAssetViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

TSharedRef<FEditorViewportClient> SWetClothingAssetViewport::MakeEditorViewportClient()
{
    check(PreviewScene.IsValid());
    ViewportClient = MakeShared<FWetClothingAssetViewportClient>(PreviewScene.Get(), SharedThis(this));

    if (PreviewMeshComponent != nullptr)
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
    }

    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SWetClothingAssetViewport::BuildViewportToolbar()
{
    const FName ViewportToolbarName = TEXT("WetClothingAssetEditor.ViewportToolbar");

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

void SWetClothingAssetViewport::HandleIslandPickedFromClient(int32 UVIslandID, bool bAppendSelection)
{
    if (OnIslandPicked.IsBound())
    {
        OnIslandPicked.Execute(UVIslandID, bAppendSelection);
    }
}

void SWetClothingAssetViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
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

void SWetClothingAssetViewport::OnFocusViewportToSelection()
{
    FocusOnPreviewMesh(false);
}

void SWetClothingAssetViewport::CacheOriginalMaterials()
{
    OriginalPreviewMaterials.Reset();

    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    const int32 MaterialCount = PreviewMeshComponent->GetNumMaterials();
    OriginalPreviewMaterials.Reserve(MaterialCount);

    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        OriginalPreviewMaterials.Add(PreviewMeshComponent->GetMaterial(MaterialIndex));
    }
}

void SWetClothingAssetViewport::RestoreOriginalMaterials()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    for (int32 MaterialIndex = 0; MaterialIndex < OriginalPreviewMaterials.Num(); ++MaterialIndex)
    {
        PreviewMeshComponent->SetMaterial(MaterialIndex, OriginalPreviewMaterials[MaterialIndex]);
    }
}

UMaterialInterface* SWetClothingAssetViewport::ResolveWetPartOverlayMaterial()
{
    if (WetPartOverlayMaterial != nullptr)
    {
        return WetPartOverlayMaterial;
    }

    if (GEngine != nullptr)
    {
        if (GEngine->VertexColorMaterial != nullptr)
        {
            WetPartOverlayMaterial = GEngine->VertexColorMaterial;
            return WetPartOverlayMaterial;
        }

        if (GEngine->VertexColorViewModeMaterial_ColorOnly != nullptr)
        {
            WetPartOverlayMaterial = GEngine->VertexColorViewModeMaterial_ColorOnly;
            return WetPartOverlayMaterial;
        }
    }

    WetPartOverlayMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    return WetPartOverlayMaterial;
}

FText SWetClothingAssetViewport::GetViewportHintText() const
{
    FString Hint = TEXT("Left click islands in the preview to select them. Hold Shift to add to the current island selection.");

    if (CurrentHighlightedMaterialSlot != INDEX_NONE)
    {
        Hint += FString::Printf(TEXT("\nShowing only material slot %d."), CurrentHighlightedMaterialSlot);
    }
    else
    {
        Hint += TEXT("\nSelect a material slot from the list to isolate it.");
    }

    return FText::FromString(Hint);
}

#undef LOCTEXT_NAMESPACE
