#include "SDWCPartViewport.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "DWCPartViewportClient.h"
#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WetRendering/DWCGPUResourceSubsystem.h"
#include "WetRendering/WetMaterialParameters.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ProceduralMeshComponent.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetPartDataTextureBaker.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetViewport"

namespace
{
    constexpr int32 PartViewportForceRenderLOD0 = 1; // USkinnedMeshComponent forced LOD is 1-based; 0 means automatic.
    const FName PreviewSurfaceWaterOverrideParameter(TEXT("DWC_PreviewSurfaceWaterOverride"));
    const FName PreviewSurfaceWaterAmountParameter(TEXT("DWC_PreviewSurfaceWaterAmount"));
    const FName PreviewDebugModeParameter(TEXT("DWCPreview_DebugMode"));

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


    constexpr float SurfacePreviewMinDetailSize = 0.0f;
    constexpr float SurfacePreviewMaxDetailSize = 4.0f;

    uint8 EncodeSurfacePreviewUNorm(const float Value)
    {
        return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
    }

    uint8 EncodeSurfacePreviewDetailSize(const float Value)
    {
        const float Normalized = FMath::GetRangePct(
            SurfacePreviewMinDetailSize,
            SurfacePreviewMaxDetailSize,
            FMath::Clamp(Value, SurfacePreviewMinDetailSize, SurfacePreviewMaxDetailSize));
        return EncodeSurfacePreviewUNorm(Normalized);
    }

    bool IsSurfacePreviewUVPointInsideTriangle(
        const FVector2D& Point,
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C)
    {
        const auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3)
        {
            return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y);
        };

        const double D1 = Sign(Point, A, B);
        const double D2 = Sign(Point, B, C);
        const double D3 = Sign(Point, C, A);
        const bool bHasNegative = D1 < 0.0 || D2 < 0.0 || D3 < 0.0;
        const bool bHasPositive = D1 > 0.0 || D2 > 0.0 || D3 > 0.0;
        return !(bHasNegative && bHasPositive);
    }

    void RasterizeSurfacePreviewTriangleMask(
        TArray<uint8>& Mask,
        const int32 Width,
        const int32 Height,
        const FWetClothingAssetUVTriangle& Triangle)
    {
        const FVector2D& A = Triangle.UVs[0];
        const FVector2D& B = Triangle.UVs[1];
        const FVector2D& C = Triangle.UVs[2];

        const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X) * Width), 0, Width - 1);
        const int32 MaxX = FMath::Clamp(FMath::FloorToInt(FMath::Max3(A.X, B.X, C.X) * Width), 0, Width - 1);
        const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y) * Height), 0, Height - 1);
        const int32 MaxY = FMath::Clamp(FMath::FloorToInt(FMath::Max3(A.Y, B.Y, C.Y) * Height), 0, Height - 1);

        bool bPainted = false;
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 X = MinX; X <= MaxX; ++X)
            {
                const FVector2D SampleUV(
                    (static_cast<double>(X) + 0.5) / Width,
                    (static_cast<double>(Y) + 0.5) / Height);
                if (!IsSurfacePreviewUVPointInsideTriangle(SampleUV, A, B, C))
                {
                    continue;
                }

                Mask[Y * Width + X] = 1;
                bPainted = true;
            }
        }

        if (!bPainted)
        {
            const FVector2D Center = (A + B + C) / 3.0;
            const int32 X = FMath::Clamp(FMath::FloorToInt(Center.X * Width), 0, Width - 1);
            const int32 Y = FMath::Clamp(FMath::FloorToInt(Center.Y * Height), 0, Height - 1);
            Mask[Y * Width + X] = 1;
        }
    }

    void DilateSurfacePreviewMask(TArray<uint8>& Mask, const int32 Width, const int32 Height, const int32 PaddingPixels)
    {
        for (int32 Step = 0; Step < FMath::Clamp(PaddingPixels, 0, 32); ++Step)
        {
            const TArray<uint8> PreviousMask = Mask;
            bool bChanged = false;
            for (int32 Y = 0; Y < Height; ++Y)
            {
                for (int32 X = 0; X < Width; ++X)
                {
                    const int32 Index = Y * Width + X;
                    if (PreviousMask[Index] != 0)
                    {
                        continue;
                    }

                    for (int32 DY = -1; DY <= 1 && Mask[Index] == 0; ++DY)
                    {
                        for (int32 DX = -1; DX <= 1 && Mask[Index] == 0; ++DX)
                        {
                            if (DX == 0 && DY == 0)
                            {
                                continue;
                            }
                            const int32 NX = X + DX;
                            const int32 NY = Y + DY;
                            if (NX < 0 || NY < 0 || NX >= Width || NY >= Height)
                            {
                                continue;
                            }
                            if (PreviousMask[NY * Width + NX] != 0)
                            {
                                Mask[Index] = 1;
                                bChanged = true;
                            }
                        }
                    }
                }
            }
            if (!bChanged)
            {
                break;
            }
        }
    }

    bool ReadSurfacePreviewSourcePixels(
        UTexture2D* Texture,
        TArray<FColor>& OutPixels,
        int32& OutWidth,
        int32& OutHeight,
        FString& OutErrorMessage)
    {
#if WITH_EDITORONLY_DATA
        if (Texture == nullptr || !Texture->Source.IsValid())
        {
            OutErrorMessage = TEXT("The baked Wet Part Data Texture does not contain readable editor source data.");
            return false;
        }
        if (Texture->Source.GetFormat() != TSF_BGRA8)
        {
            OutErrorMessage = TEXT("The baked Wet Part Data Texture must use BGRA8 source data.");
            return false;
        }

        TArray64<uint8> RawData;
        if (!Texture->Source.GetMipData(RawData, 0))
        {
            OutErrorMessage = TEXT("Could not read the baked Wet Part Data Texture source mip.");
            return false;
        }

        OutWidth = Texture->Source.GetSizeX();
        OutHeight = Texture->Source.GetSizeY();
        const int64 PixelCount = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight);
        if (OutWidth <= 0 || OutHeight <= 0 || RawData.Num() < PixelCount * static_cast<int64>(sizeof(FColor)))
        {
            OutErrorMessage = TEXT("The baked Wet Part Data Texture has invalid source dimensions.");
            return false;
        }

        OutPixels.SetNumUninitialized(static_cast<int32>(PixelCount));
        FMemory::Memcpy(OutPixels.GetData(), RawData.GetData(), PixelCount * static_cast<int64>(sizeof(FColor)));
        return true;
#else
        OutErrorMessage = TEXT("Surface Water Tiling preview requires editor texture source data.");
        return false;
#endif
    }

    template <typename PixelType>
    bool UploadSurfacePreviewPixels(
        UTexture2D* Texture,
        const TArray<PixelType>& Pixels)
    {
        if (Texture == nullptr || Texture->GetPlatformData() == nullptr ||
            Texture->GetPlatformData()->Mips.IsEmpty())
        {
            return false;
        }

        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        const int64 ByteCount = static_cast<int64>(Pixels.Num()) * static_cast<int64>(sizeof(PixelType));
        void* Destination = Mip.BulkData.Lock(LOCK_READ_WRITE);
        if (Destination == nullptr || Mip.BulkData.GetBulkDataSize() < ByteCount)
        {
            Mip.BulkData.Unlock();
            return false;
        }

        FMemory::Memcpy(Destination, Pixels.GetData(), ByteCount);
        Mip.BulkData.Unlock();
        Texture->UpdateResource();
        return true;
    }

    bool CreateOrUpdateSurfacePreviewByteTexture(
        TObjectPtr<UTexture2D>& Texture,
        const TArray<FColor>& Pixels,
        const int32 Width,
        const int32 Height)
    {
        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            return false;
        }

        if (Texture == nullptr || Texture->GetSizeX() != Width || Texture->GetSizeY() != Height ||
            Texture->GetPixelFormat() != PF_B8G8R8A8)
        {
            Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None);
            if (Texture == nullptr)
            {
                return false;
            }
            Texture->SRGB = false;
            Texture->CompressionSettings = TC_VectorDisplacementmap;
            Texture->MipGenSettings = TMGS_NoMipmaps;
            Texture->Filter = TF_Nearest;
            Texture->AddressX = TA_Clamp;
            Texture->AddressY = TA_Clamp;
            Texture->NeverStream = true;
        }

        return UploadSurfacePreviewPixels(Texture, Pixels);
    }

    bool CreateOrUpdateSurfacePreviewWetnessTexture(
        TObjectPtr<UTexture2D>& Texture,
        const TArray<float>& Pixels,
        const int32 Width,
        const int32 Height,
        const TextureFilter Filter)
    {
        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            return false;
        }

        if (Texture == nullptr || Texture->GetSizeX() != Width || Texture->GetSizeY() != Height ||
            Texture->GetPixelFormat() != PF_R32_FLOAT)
        {
            Texture = UTexture2D::CreateTransient(Width, Height, PF_R32_FLOAT, NAME_None);
            if (Texture == nullptr)
            {
                return false;
            }
            Texture->SRGB = false;
            Texture->CompressionSettings = TC_VectorDisplacementmap;
            Texture->MipGenSettings = TMGS_NoMipmaps;
            Texture->Filter = Filter;
            Texture->AddressX = TA_Clamp;
            Texture->AddressY = TA_Clamp;
            Texture->NeverStream = true;
        }
        else
        {
            Texture->Filter = Filter;
        }

        return UploadSurfacePreviewPixels(Texture, Pixels);
    }

} // namespace

void SDWCPartViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    bSurfaceWaterTilingPreview = InArgs._SurfaceWaterTilingPreview;
    OnIslandPicked = InArgs._OnIslandPicked;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

    SEditorViewport::Construct(SEditorViewport::FArguments());

    PreviewMeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
    PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewMeshComponent->SetForcedLOD(PartViewportForceRenderLOD0);
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

SDWCPartViewport::~SDWCPartViewport()
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

void SDWCPartViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(WetPartOverlayComponent);
    Collector.AddReferencedObject(SelectionOverlayComponent);
    Collector.AddReferencedObject(WetPartOverlayMaterial);
    Collector.AddReferencedObject(SurfaceWaterPreviewMaterialParent);
    Collector.AddReferencedObject(SurfaceWaterPreviewBaseMaterial);
    Collector.AddReferencedObject(SurfaceWaterPreviewStaticMaterial);
    Collector.AddReferencedObject(SurfaceWaterPreviewMaterial);
    Collector.AddReferencedObject(SurfacePreviewWetnessMap);
    Collector.AddReferencedObject(SurfacePreviewWetPartDataTexture);
    Collector.AddReferencedObject(SurfacePreviewDropletRT);
    Collector.AddReferencedObject(SurfacePreviewFlowDropletRT);
    Collector.AddReferencedObjects(OriginalPreviewMaterials);
}

void SDWCPartViewport::RefreshPreviewMesh()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    USkeletalMesh* TargetMesh = nullptr;
    if (UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        TargetMesh = WetClothingAssetPtr->GetRuntimeSkeletalMesh();
    }

    if (PreviewMeshComponent->GetSkeletalMeshAsset() == TargetMesh && TargetMesh != nullptr)
    {
        if (bSurfaceWaterTilingPreview)
        {
            RefreshSurfaceWaterPreviewMaterial();
        }
        RefreshMaterialSectionVisibility();
        if (OverlayText.IsValid())
        {
            OverlayText->SetText(GetViewportHintText());
        }

        RequestViewportRedraw();
        return;
    }

    PreviewMeshComponent->SetSkeletalMeshAsset(TargetMesh);
    PreviewMeshComponent->SetForcedLOD(PartViewportForceRenderLOD0);
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
    SurfaceWaterPreviewMaterial = nullptr;
    SurfaceWaterPreviewBaseMaterial = nullptr;
    SurfaceWaterPreviewStaticMaterial = nullptr;
    SurfaceWaterPreviewMaterialParent = nullptr;
    SurfaceWaterPreviewMaterialSlotIndex = INDEX_NONE;
    SurfaceWaterPreviewDataUVChannel = INDEX_NONE;
    SurfaceWaterPreviewNormalUVChannel = INDEX_NONE;
    bSurfaceWaterPreviewFallbackProfileCacheValid = false;
    SurfaceWaterPreviewStatus.Reset();
    InvalidateSurfaceWaterPreviewLayoutCache();
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

    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
    RefreshMaterialSectionVisibility();

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

void SDWCPartViewport::SetHighlightedMaterialSlot(const int32 SlotIndex)
{
    const int32 MaterialCount = PreviewMeshComponent != nullptr ? PreviewMeshComponent->GetNumMaterials() : 0;
    CurrentHighlightedMaterialSlot = SlotIndex >= 0 && SlotIndex < MaterialCount ? SlotIndex : INDEX_NONE;
    RefreshMaterialSectionVisibility();

    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
    }
    RequestViewportRedraw();
}

void SDWCPartViewport::ClearMaterialSlotHighlight()
{
    CurrentHighlightedMaterialSlot = INDEX_NONE;
    RefreshMaterialSectionVisibility();
    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
    }
    RequestViewportRedraw();
}

void SDWCPartViewport::SetSelectableIslands(const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& InIslands)
{
    InvalidateSurfaceWaterPreviewLayoutCache();
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
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
}

void SDWCPartViewport::SetHighlightedUVIslandIDs(const TSet<int32>& InUVIslandIDs)
{
    CurrentHighlightedUVIslandIDs = InUVIslandIDs;
    RefreshSelectionOverlayMesh();
}

void SDWCPartViewport::SetSelectionOverlayThicknessScale(float InThicknessScale)
{
    const float NewThicknessScale = FMath::Clamp(InThicknessScale, 0.25f, 4.0f);
    if (!FMath::IsNearlyEqual(SelectionOverlayThicknessScale, NewThicknessScale))
    {
        SelectionOverlayThicknessScale = NewThicknessScale;
        RefreshSelectionOverlayMesh();
    }
}

void SDWCPartViewport::ClearHighlightedIsland()
{
    CurrentHighlightedUVIslandIDs.Reset();
    if (SelectionOverlayComponent != nullptr)
    {
        SelectionOverlayComponent->ClearAllMeshSections();
    }
    RequestViewportRedraw();
}

void SDWCPartViewport::SetWetPartIslandAssignments(const TMap<int32, int32>& InUVIslandToWetPartID, const TMap<int32, FLinearColor>& InIslandColors)
{
    InvalidateSurfaceWaterPreviewLayoutCache();
    CurrentWetPartIslandAssignments = InUVIslandToWetPartID;
    CurrentWetPartIslandColors = InIslandColors;
    RefreshWetPartOverlayMesh();
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
}

void SDWCPartViewport::ClearWetPartIslandColors()
{
    InvalidateSurfaceWaterPreviewLayoutCache();
    CurrentWetPartIslandAssignments.Reset();
    CurrentWetPartIslandColors.Reset();

    if (WetPartOverlayComponent != nullptr)
    {
        WetPartOverlayComponent->ClearAllMeshSections();
    }
    RequestViewportRedraw();
}

void SDWCPartViewport::SetShowWetPartColors(const bool bInShowWetPartColors)
{
    if (bShowWetPartColors == bInShowWetPartColors)
    {
        return;
    }

    bShowWetPartColors = bInShowWetPartColors;
    RefreshWetPartOverlayMesh();
}

void SDWCPartViewport::SetPreviewWetPart(const int32 MaterialSlotIndex, const int32 WetPartID)
{
    if (PreviewMaterialSlotIndex == MaterialSlotIndex && PreviewWetPartID == WetPartID)
    {
        return;
    }

    InvalidateSurfaceWaterPreviewLayoutCache();
    PreviewMaterialSlotIndex = MaterialSlotIndex;
    PreviewWetPartID = WetPartID;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
}

void SDWCPartViewport::SetPreviewWetness(const float AbsorbedWetness, const float SurfaceWater)
{
    const float NewAbsorbedWetness = FMath::Clamp(AbsorbedWetness, 0.0f, 1.0f);
    const float NewSurfaceWater = FMath::Clamp(SurfaceWater, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(PreviewAbsorbedWetness, NewAbsorbedWetness) &&
        FMath::IsNearlyEqual(PreviewSurfaceWater, NewSurfaceWater))
    {
        return;
    }

    PreviewAbsorbedWetness = NewAbsorbedWetness;
    PreviewSurfaceWater = NewSurfaceWater;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewDynamicTextures();
    }
}

void SDWCPartViewport::SetSurfaceWaterPreviewDropletsEnabled(const bool bInDropletsEnabled)
{
    if (bSurfaceWaterPreviewDropletsEnabled == bInDropletsEnabled)
    {
        return;
    }

    bSurfaceWaterPreviewDropletsEnabled = bInDropletsEnabled;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
    else
    {
        ApplySurfaceWaterPreviewRenderOverrides();
        RequestViewportRedraw();
    }
}

void SDWCPartViewport::SetSurfaceWaterPreviewNormalFlip(
    const bool bInFlipX,
    const bool bInFlipY)
{
    if (bSurfaceWaterPreviewFlipNormalX == bInFlipX &&
        bSurfaceWaterPreviewFlipNormalY == bInFlipY)
    {
        return;
    }

    bSurfaceWaterPreviewFlipNormalX = bInFlipX;
    bSurfaceWaterPreviewFlipNormalY = bInFlipY;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
    else
    {
        ApplySurfaceWaterPreviewRenderOverrides();
        RequestViewportRedraw();
    }
}

void SDWCPartViewport::SetSurfaceWaterTilingPreviewCoverageMode(
    const EDWCSurfaceWaterTilingPreviewCoverageMode InMode)
{
    if (SurfaceWaterPreviewCoverageMode == InMode)
    {
        return;
    }

    SurfaceWaterPreviewCoverageMode = InMode;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewDynamicTextures();
    }
}

void SDWCPartViewport::SetSurfaceWaterTilingPreviewDisplayMode(
    const EDWCSurfaceWaterTilingPreviewDisplayMode InMode)
{
    if (SurfaceWaterPreviewDisplayMode == InMode)
    {
        return;
    }

    SurfaceWaterPreviewDisplayMode = InMode;
    if (SurfaceWaterPreviewMaterial != nullptr)
    {
        ApplySurfaceWaterPreviewRenderOverrides();
        if (OverlayText.IsValid())
        {
            OverlayText->SetText(GetViewportHintText());
            OverlayText->SetColorAndOpacity(GetViewportHintTextColor());
        }
        RequestViewportRedraw();
    }
}

void SDWCPartViewport::RefreshWetPartOverlayMesh()
{
    if (WetPartOverlayComponent == nullptr)
    {
        return;
    }

    WetPartOverlayComponent->ClearAllMeshSections();
    WetPartOverlayComponent->SetMaterial(0, ResolveWetPartOverlayMaterial());

    // Surface Water is rendered on the original skeletal mesh through the
    // selected slot's transient DWC preview material. Procedural geometry remains editor-overlay only.
    if (bSurfaceWaterTilingPreview || !bShowWetPartColors)
    {
        WetPartOverlayComponent->MarkRenderStateDirty();
        RequestViewportRedraw();
        return;
    }

    TArray<FVector>          Vertices;
    TArray<int32>            Indices;
    TArray<FVector>          Normals;
    TArray<FVector2D>        UVs;
    TArray<FLinearColor>     VertexColors;
    TArray<FProcMeshTangent> Tangents;

    const float NormalOffset = CalculateWetPartOverlayOffset(PreviewMeshComponent);

    for (const FWetClothingAssetUVIsland& Island : CurrentSelectableIslands)
    {
        const int32* WetPartID = CurrentWetPartIslandAssignments.Find(Island.UVIslandID);
        const FLinearColor* IslandColor = CurrentWetPartIslandColors.Find(Island.UVIslandID);
        if (WetPartID == nullptr || *WetPartID == 0 || IslandColor == nullptr)
        {
            continue;
        }

        for (const FWetClothingAssetUVTriangle& UVTriangle : Island.UVTriangles)
        {
            const FVector Normal = MakeWetPartOverlayNormal(
                UVTriangle.LocalPositions[0],
                UVTriangle.LocalPositions[1],
                UVTriangle.LocalPositions[2]);

            for (const float OffsetSign : { 1.0f, -1.0f })
            {
                const FVector OffsetNormal = Normal * OffsetSign;
                const int32 BaseVertexIndex = Vertices.Num();

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

    if (!Vertices.IsEmpty())
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

    WetPartOverlayComponent->MarkRenderStateDirty();
    RequestViewportRedraw();
}

void SDWCPartViewport::RefreshSelectionOverlayMesh()
{
    if (SelectionOverlayComponent == nullptr)
    {
        return;
    }

    SelectionOverlayComponent->ClearAllMeshSections();
    SelectionOverlayComponent->SetMaterial(0, ResolveWetPartOverlayMaterial());

    if (CurrentHighlightedUVIslandIDs.Num() == 0)
    {
        SelectionOverlayComponent->MarkRenderStateDirty();
        RequestViewportRedraw();
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

    SelectionOverlayComponent->MarkRenderStateDirty();
    RequestViewportRedraw();
}


void SDWCPartViewport::RefreshMaterialSectionVisibility()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    PreviewMeshComponent->ShowAllMaterialSections(0);
    const int32 IsolatedMaterialSlot = bSurfaceWaterTilingPreview
        ? PreviewMaterialSlotIndex
        : CurrentHighlightedMaterialSlot;
    const bool bIsolateSelectedSlot =
        IsolatedMaterialSlot != INDEX_NONE;
    if (!bIsolateSelectedSlot)
    {
        PreviewMeshComponent->MarkRenderStateDirty();
        return;
    }

    const USkeletalMesh* PreviewMesh = PreviewMeshComponent->GetSkeletalMeshAsset();
    const FSkeletalMeshRenderData* RenderData = PreviewMesh != nullptr ? PreviewMesh->GetResourceForRendering() : nullptr;
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(0))
    {
        PreviewMeshComponent->MarkRenderStateDirty();
        return;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
    for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
    {
        const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
        PreviewMeshComponent->ShowMaterialSection(
            Section.MaterialIndex,
            SectionIndex,
            Section.MaterialIndex == IsolatedMaterialSlot,
            0);
    }
    PreviewMeshComponent->MarkRenderStateDirty();
}

bool SDWCPartViewport::BuildSurfaceWaterPreviewTextures(FString& OutErrorMessage)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || PreviewMaterialSlotIndex == INDEX_NONE || PreviewWetPartID < 0)
    {
        OutErrorMessage = TEXT("Select a wettable Material Slot and Wet Part.");
        return false;
    }

    const FWetClothingEditableWetPartData& Editable = Asset->Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* Slot = Editable.FindMaterialSlot(PreviewMaterialSlotIndex);
    const FWetClothingWetPartEntry* Part = Slot != nullptr ? Slot->FindPart(PreviewWetPartID) : nullptr;
    if (Part == nullptr)
    {
        OutErrorMessage = TEXT("The selected Wet Part could not be resolved.");
        return false;
    }
    if (!Slot->bIsWettableSlot)
    {
        OutErrorMessage = TEXT("Mark the selected Material Slot as Wettable before using the Surface Water Tiling preview.");
        return false;
    }

    TSet<int32> SelectedTriangleIDs;
    for (const FWetClothingAssetUVIsland& Island : CurrentSelectableIslands)
    {
        const int32* AssignedWetPartID = CurrentWetPartIslandAssignments.Find(Island.UVIslandID);
        const int32 EffectiveWetPartID = AssignedWetPartID != nullptr ? *AssignedWetPartID : 0;
        if (EffectiveWetPartID != PreviewWetPartID)
        {
            continue;
        }
        for (const int32 TriangleID : Island.TriangleIDs)
        {
            SelectedTriangleIDs.Add(TriangleID);
        }
    }
    if (SelectedTriangleIDs.IsEmpty())
    {
        OutErrorMessage = TEXT("The selected Wet Part does not contain any UV-island triangles.");
        return false;
    }

    const FWetClothingBakedWetPartData& Baked = Asset->Derived.Inline.BakedWetPartData;
    const FWetClothingBakedWetPartDataSlotTexture* BakedSlot = Baked.FindSlot(PreviewMaterialSlotIndex);
    if (BakedSlot == nullptr || BakedSlot->WetPartDataTexture == nullptr)
    {
        OutErrorMessage = TEXT("Use Build for Runtime > Bake Render Profile Lookup Texture before opening the Surface Water Tiling preview.");
        return false;
    }

    const bool bUseCachedLayout =
        bSurfacePreviewLayoutCacheValid &&
        SurfacePreviewCachedMaterialSlotIndex == PreviewMaterialSlotIndex &&
        SurfacePreviewCachedWetPartID == PreviewWetPartID &&
        SurfacePreviewCachedWidth > 0 &&
        SurfacePreviewCachedHeight > 0 &&
        SurfacePreviewCachedSourcePartDataPixels.Num() ==
            SurfacePreviewCachedWidth * SurfacePreviewCachedHeight &&
        SurfacePreviewCachedSelectedMask.Num() ==
            SurfacePreviewCachedWidth * SurfacePreviewCachedHeight;

    if (!bUseCachedLayout)
    {
        TArray<FColor> SourcePartDataPixels;
        int32 Width = 0;
        int32 Height = 0;
        if (!ReadSurfacePreviewSourcePixels(
                BakedSlot->WetPartDataTexture.Get(),
                SourcePartDataPixels,
                Width,
                Height,
                OutErrorMessage))
        {
            return false;
        }

        TArray<FWetClothingAssetUVIsland> DataUVIslands;
        FString DataUVError;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotDataUVIslands(
                *Asset,
                0,
                PreviewMaterialSlotIndex,
                DataUVIslands,
                &DataUVError))
        {
            OutErrorMessage = DataUVError.IsEmpty()
                ? TEXT("Could not rebuild the selected slot's DWC Data UV triangles.")
                : DataUVError;
            return false;
        }

        TArray<uint8> SelectedMask;
        SelectedMask.Init(0, Width * Height);
        for (const FWetClothingAssetUVIsland& Island : DataUVIslands)
        {
            for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
            {
                if (SelectedTriangleIDs.Contains(Triangle.TriangleID))
                {
                    RasterizeSurfacePreviewTriangleMask(SelectedMask, Width, Height, Triangle);
                }
            }
        }

        const FWetPartProfileAssignment* PartProfile = Editable.FindProfile(*Part);
        FWetnessProfileParameters PartProfileParameters;
        if (!FWetClothingWetPartDataTextureBaker::ResolveProfileParameters(PartProfile, PartProfileParameters))
        {
            OutErrorMessage = TEXT("Could not resolve the selected Wet Part's profile parameters.");
            return false;
        }
        const FString PartProfileStableKey =
            FWetClothingWetPartDataTextureBaker::MakeProfileStableKey(PartProfile, PartProfileParameters);
        uint8 LocalProfileID = 0;
        for (int32 LocalProfileIndex = 0; LocalProfileIndex < Baked.LocalProfiles.Num(); ++LocalProfileIndex)
        {
            if (Baked.LocalProfiles[LocalProfileIndex].StableKey == PartProfileStableKey)
            {
                LocalProfileID = static_cast<uint8>(LocalProfileIndex + 1);
                break;
            }
        }
        if (LocalProfileID == 0)
        {
            OutErrorMessage = TEXT("The selected Wet Part's profile is not present in the baked Render Profile Lookup Texture. Use Build for Runtime > Bake Render Profile Lookup Texture.");
            return false;
        }

        int32 MinSelectedX = Width;
        int32 MinSelectedY = Height;
        int32 MaxSelectedX = 0;
        int32 MaxSelectedY = 0;
        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                const int32 PixelIndex = Y * Width + X;
                if (SelectedMask[PixelIndex] == 0)
                {
                    continue;
                }
                MinSelectedX = FMath::Min(MinSelectedX, X);
                MinSelectedY = FMath::Min(MinSelectedY, Y);
                MaxSelectedX = FMath::Max(MaxSelectedX, X);
                MaxSelectedY = FMath::Max(MaxSelectedY, Y);
            }
        }
        SurfacePreviewCachedSingleCircleCenter = FVector2D(
            (static_cast<float>(MinSelectedX) + static_cast<float>(MaxSelectedX)) * 0.5f,
            (static_cast<float>(MinSelectedY) + static_cast<float>(MaxSelectedY)) * 0.5f);

        DilateSurfacePreviewMask(SelectedMask, Width, Height, Baked.PaddingPixels);
        for (int32 PixelIndex = 0; PixelIndex < SelectedMask.Num(); ++PixelIndex)
        {
            // Preserve the baker's exact texel ownership. This prevents the preview
            // mask from spilling into a neighbouring packed island after dilation.
            if (SelectedMask[PixelIndex] != 0 && SourcePartDataPixels[PixelIndex].R != LocalProfileID)
            {
                SelectedMask[PixelIndex] = 0;
            }
        }

        SurfacePreviewCachedSourcePartDataPixels = MoveTemp(SourcePartDataPixels);
        SurfacePreviewCachedSelectedMask = MoveTemp(SelectedMask);
        SurfacePreviewCachedWidth = Width;
        SurfacePreviewCachedHeight = Height;
        SurfacePreviewCachedLocalProfileID = static_cast<int32>(LocalProfileID);
        SurfacePreviewCachedMaterialSlotIndex = PreviewMaterialSlotIndex;
        SurfacePreviewCachedWetPartID = PreviewWetPartID;
        bSurfacePreviewLayoutCacheValid = true;
    }

    const int32 Width = SurfacePreviewCachedWidth;
    const int32 Height = SurfacePreviewCachedHeight;
    const int32 LocalProfileID = SurfacePreviewCachedLocalProfileID;
    SurfacePreviewLocalProfileID = LocalProfileID;

    const FWetPartProfileAssignment* PreviewPartProfile = Editable.FindProfile(*Part);
    FWetnessProfileParameters PreviewProfileParameters;
    if (!FWetClothingWetPartDataTextureBaker::ResolveProfileParameters(
            PreviewPartProfile,
            PreviewProfileParameters))
    {
        OutErrorMessage = TEXT("Could not resolve the selected Wet Part's profile parameters.");
        return false;
    }
    const FSurfaceWaterProfileParameters& Surface = PreviewProfileParameters.SurfaceWater;
    const FVector2D SingleCircleCenter = SurfacePreviewCachedSingleCircleCenter;
    const float Droplet1StampSizeScale = Part->SurfaceWater.GetResolvedDropletStampSizeScale();
    const float Droplet1HalfWidthPixels = Surface.DropletRadiusPixels > UE_KINDA_SMALL_NUMBER
        ? FMath::Clamp(Surface.DropletRadiusPixels * Droplet1StampSizeScale, 1.0f, 256.0f)
        : 0.0f;
    const float Droplet1HalfHeightPixels = Surface.DropletHeightPixels > UE_KINDA_SMALL_NUMBER
        ? FMath::Clamp(Surface.DropletHeightPixels * Droplet1StampSizeScale, 1.0f, 256.0f)
        : 0.0f;
    const float FlowStampSizeScale = Part->SurfaceWater.GetResolvedDropletFlowStampSizeScale();
    const float FlowHalfWidthPixels = Surface.DropletFlowRadiusPixels > UE_KINDA_SMALL_NUMBER
        ? FMath::Clamp(Surface.DropletFlowRadiusPixels * FlowStampSizeScale, 1.0f, 256.0f)
        : 0.0f;
    const float FlowHalfHeightPixels = Surface.DropletFlowHeightPixels > UE_KINDA_SMALL_NUMBER
        ? FMath::Clamp(Surface.DropletFlowHeightPixels * FlowStampSizeScale, 1.0f, 256.0f)
        : 0.0f;
    TArray<FColor> PreviewPartDataPixels = SurfacePreviewCachedSourcePartDataPixels;
    TArray<float> WetnessPixels;
    TArray<float> DropletPixels;
    TArray<float> FlowDropletPixels;
    WetnessPixels.Init(0.0f, Width * Height);
    DropletPixels.Init(0.0f, Width * Height);
    FlowDropletPixels.Init(0.0f, Width * Height);

    const float SurfaceAmount = FMath::Clamp(PreviewSurfaceWater, 0.0f, 1.0f);
    const uint8 DropletDetailSize = EncodeSurfacePreviewDetailSize(Part->SurfaceWater.DropletDetailSize);
    const uint8 DropletFlowDetailSize =
        EncodeSurfacePreviewDetailSize(Part->SurfaceWater.DropletFlowDetailSize);

    for (int32 Y = 0; Y < Height; ++Y)
    {
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 PixelIndex = Y * Width + X;
            if (SurfacePreviewCachedSelectedMask[PixelIndex] == 0)
            {
                continue;
            }

            PreviewPartDataPixels[PixelIndex].R = LocalProfileID;
            PreviewPartDataPixels[PixelIndex].G = DropletDetailSize;
            PreviewPartDataPixels[PixelIndex].B = DropletFlowDetailSize;
            PreviewPartDataPixels[PixelIndex].A = 0;
            WetnessPixels[PixelIndex] = 0.0f;

            if (SurfaceWaterPreviewCoverageMode == EDWCSurfaceWaterTilingPreviewCoverageMode::SingleCircle)
            {
                const FVector2D PixelPosition(static_cast<float>(X), static_cast<float>(Y));
                const FVector2D CenterDelta = PixelPosition - SingleCircleCenter;
                if (Droplet1HalfWidthPixels > UE_KINDA_SMALL_NUMBER &&
                    Droplet1HalfHeightPixels > UE_KINDA_SMALL_NUMBER)
                {
                    const float EllipseDistance = FMath::Sqrt(
                        FMath::Square(CenterDelta.X / Droplet1HalfWidthPixels) +
                        FMath::Square(CenterDelta.Y / Droplet1HalfHeightPixels));
                    const float EdgeWidth = FMath::Clamp(
                        1.5f / FMath::Max(Droplet1HalfWidthPixels, Droplet1HalfHeightPixels),
                        0.02f,
                        0.25f);
                    const float RegionAlpha = 1.0f - FMath::SmoothStep(
                        1.0f - EdgeWidth,
                        1.0f,
                        EllipseDistance);
                    if (RegionAlpha > UE_KINDA_SMALL_NUMBER)
                    {
                        DropletPixels[PixelIndex] = SurfaceAmount * RegionAlpha;
                    }
                }

                if (FlowHalfWidthPixels > UE_KINDA_SMALL_NUMBER &&
                    FlowHalfHeightPixels > UE_KINDA_SMALL_NUMBER)
                {
                    const float EllipseDistance = FMath::Sqrt(
                        FMath::Square(CenterDelta.X / FlowHalfWidthPixels) +
                        FMath::Square(CenterDelta.Y / FlowHalfHeightPixels));
                    const float EdgeWidth = FMath::Clamp(
                        1.5f / FMath::Max(FlowHalfWidthPixels, FlowHalfHeightPixels),
                        0.02f,
                        0.25f);
                    const float RegionAlpha = 1.0f - FMath::SmoothStep(
                        1.0f - EdgeWidth,
                        1.0f,
                        EllipseDistance);
                    if (RegionAlpha > UE_KINDA_SMALL_NUMBER)
                    {
                        FlowDropletPixels[PixelIndex] = SurfaceAmount * RegionAlpha;
                    }
                }
            }
            else
            {
                DropletPixels[PixelIndex] = SurfaceAmount;
                FlowDropletPixels[PixelIndex] = SurfaceAmount;
            }
        }
    }

    if (!CreateOrUpdateSurfacePreviewByteTexture(SurfacePreviewWetPartDataTexture, PreviewPartDataPixels, Width, Height) ||
        !CreateOrUpdateSurfacePreviewWetnessTexture(SurfacePreviewWetnessMap, WetnessPixels, Width, Height, TF_Nearest) ||
        !CreateOrUpdateSurfacePreviewWetnessTexture(SurfacePreviewDropletRT, DropletPixels, Width, Height, TF_Bilinear) ||
        !CreateOrUpdateSurfacePreviewWetnessTexture(SurfacePreviewFlowDropletRT, FlowDropletPixels, Width, Height, TF_Bilinear))
    {
        OutErrorMessage = TEXT("Could not create the transient Surface Water preview textures.");
        return false;
    }

    OutErrorMessage.Reset();
    return true;
}

void SDWCPartViewport::InvalidateSurfaceWaterPreviewLayoutCache()
{
    bSurfacePreviewLayoutCacheValid = false;
    SurfacePreviewCachedSourcePartDataPixels.Reset();
    SurfacePreviewCachedSelectedMask.Reset();
    SurfacePreviewCachedSingleCircleCenter = FVector2D::ZeroVector;
    SurfacePreviewCachedWidth = 0;
    SurfacePreviewCachedHeight = 0;
    SurfacePreviewCachedLocalProfileID = 0;
    SurfacePreviewCachedMaterialSlotIndex = INDEX_NONE;
    SurfacePreviewCachedWetPartID = INDEX_NONE;
}

void SDWCPartViewport::ApplySurfaceWaterPreviewTextureParameters()
{
    if (SurfaceWaterPreviewMaterial == nullptr)
    {
        return;
    }

    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::WetnessMap(),
        SurfacePreviewWetnessMap);
    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::WetPartDataTexture(),
        SurfacePreviewWetPartDataTexture);
    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::SurfaceDroplet1RT(),
        SurfacePreviewDropletRT);
    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::SurfaceDroplet2RT(),
        SurfacePreviewFlowDropletRT);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterTexelSize(),
        SurfacePreviewWetnessMap != nullptr && SurfacePreviewWetnessMap->GetSizeX() > 0
            ? 1.0f / static_cast<float>(SurfacePreviewWetnessMap->GetSizeX())
            : 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(PreviewSurfaceWaterOverrideParameter, 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(PreviewSurfaceWaterAmountParameter, 0.0f);
}

void SDWCPartViewport::RefreshSurfaceWaterPreviewDynamicTextures()
{
    if (!bSurfaceWaterTilingPreview || SurfaceWaterPreviewMaterial == nullptr)
    {
        RefreshSurfaceWaterPreviewMaterial();
        return;
    }

    FString TextureError;
    if (!BuildSurfaceWaterPreviewTextures(TextureError))
    {
        SurfaceWaterPreviewStatus = TextureError;
        bSurfaceWaterPreviewStatusIsError = true;
        if (OverlayText.IsValid())
        {
            OverlayText->SetText(GetViewportHintText());
            OverlayText->SetColorAndOpacity(GetViewportHintTextColor());
        }
        RequestViewportRedraw();
        return;
    }

    ApplySurfaceWaterPreviewTextureParameters();
    PreviewMeshComponent->MarkRenderStateDirty();
    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
        OverlayText->SetColorAndOpacity(GetViewportHintTextColor());
    }
    RequestViewportRedraw();
}

void SDWCPartViewport::RefreshSurfaceWaterPreviewMaterial()
{
    if (!bSurfaceWaterTilingPreview || PreviewMeshComponent == nullptr)
    {
        return;
    }

    RestoreOriginalMaterials();
    SurfaceWaterPreviewStatus.Reset();
    bSurfaceWaterPreviewStatusIsError = false;
    bSurfaceWaterPreviewFallbackProfileCacheValid = false;

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || PreviewMaterialSlotIndex == INDEX_NONE || PreviewWetPartID < 0)
    {
        SurfaceWaterPreviewStatus = TEXT("Select a Wet Part to preview Surface Water.");
        RequestViewportRedraw();
        return;
    }

    FString TextureError;
    if (!BuildSurfaceWaterPreviewTextures(TextureError))
    {
        SurfaceWaterPreviewStatus = TextureError;
        bSurfaceWaterPreviewStatusIsError = true;
        RequestViewportRedraw();
        return;
    }

    const FWetClothingBakedWetPartData& Baked = Asset->Derived.Inline.BakedWetPartData;
    const int32 LocalProfileIndex = SurfacePreviewLocalProfileID - 1;
    if (!Baked.LocalProfiles.IsValidIndex(LocalProfileIndex))
    {
        SurfaceWaterPreviewStatus = TEXT("The selected Wet Part's baked Surface Water profile could not be resolved.");
        bSurfaceWaterPreviewStatusIsError = true;
        RequestViewportRedraw();
        return;
    }

    const FWetClothingEditableWetPartData& Editable = Asset->Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* Slot = Editable.FindMaterialSlot(PreviewMaterialSlotIndex);
    const FWetClothingWetPartEntry* Part = Slot != nullptr ? Slot->FindPart(PreviewWetPartID) : nullptr;
    if (Slot == nullptr || Part == nullptr)
    {
        SurfaceWaterPreviewStatus = TEXT("The selected Wet Part could not be resolved.");
        bSurfaceWaterPreviewStatusIsError = true;
        RequestViewportRedraw();
        return;
    }

    USkeletalMesh* RuntimeMesh = Asset->GetRuntimeSkeletalMesh();
    UMaterialInterface* SourceMaterial =
        RuntimeMesh != nullptr && RuntimeMesh->GetMaterials().IsValidIndex(PreviewMaterialSlotIndex)
            ? RuntimeMesh->GetMaterials()[PreviewMaterialSlotIndex].MaterialInterface
            : nullptr;
    if (SourceMaterial == nullptr)
    {
        SourceMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
    }

    FWCAMaterialGenerator::FOptions PreviewOptions =
        FWCAMaterialGenerator::MakeOptionsForAsset(
            Asset,
            EDWCSimulationMode::WetnessMapGPU,
            PreviewMaterialSlotIndex);
    PreviewOptions.bUseSurfaceWater = true;
    PreviewOptions.bEnableDWCDataUVSampling = true;
    PreviewOptions.bConnectWetnessMapPath = true;

    if (SurfaceWaterPreviewMaterial == nullptr ||
        SurfaceWaterPreviewMaterialParent != SourceMaterial ||
        SurfaceWaterPreviewMaterialSlotIndex != PreviewMaterialSlotIndex ||
        SurfaceWaterPreviewDataUVChannel != PreviewOptions.DWCDataUVChannelIndex ||
        SurfaceWaterPreviewNormalUVChannel != PreviewOptions.SurfaceWaterNormalUVChannelIndex)
    {
        SurfaceWaterPreviewMaterialParent = SourceMaterial;
        SurfaceWaterPreviewMaterialSlotIndex = PreviewMaterialSlotIndex;
        SurfaceWaterPreviewDataUVChannel = PreviewOptions.DWCDataUVChannelIndex;
        SurfaceWaterPreviewNormalUVChannel = PreviewOptions.SurfaceWaterNormalUVChannelIndex;
        SurfaceWaterPreviewMaterial = nullptr;
        SurfaceWaterPreviewBaseMaterial = nullptr;
        SurfaceWaterPreviewStaticMaterial = nullptr;
        bSurfaceWaterPreviewFallbackProfileCacheValid = false;

        const FWetClothingUnifiedMaterialSetupResult PreviewMaterialSet =
            FWCAMaterialGenerator::CreateTransientUnifiedPreviewMaterial(
                SourceMaterial,
                PreviewOptions);
        if (!PreviewMaterialSet.bSucceeded ||
            PreviewMaterialSet.GeneratedMaterial == nullptr ||
            PreviewMaterialSet.GPUMaterialInstance == nullptr)
        {
            SurfaceWaterPreviewStatus = FString::Printf(
                TEXT("Could not create the transient DWC Surface Water tiling preview material for slot %d. %s"),
                PreviewMaterialSlotIndex,
                *PreviewMaterialSet.Message);
            bSurfaceWaterPreviewStatusIsError = true;
            RequestViewportRedraw();
            return;
        }

        SurfaceWaterPreviewBaseMaterial = PreviewMaterialSet.GeneratedMaterial;
        SurfaceWaterPreviewStaticMaterial = PreviewMaterialSet.GPUMaterialInstance;
        SurfaceWaterPreviewMaterial = UMaterialInstanceDynamic::Create(
            SurfaceWaterPreviewStaticMaterial,
            GetTransientPackage());
    }
    if (SurfaceWaterPreviewMaterial == nullptr)
    {
        SurfaceWaterPreviewStatus = TEXT("Could not create the transient generated-material Surface Water preview instance.");
        bSurfaceWaterPreviewStatusIsError = true;
        RequestViewportRedraw();
        return;
    }

    FWetClothingLocalRenderProfile PreviewLocalProfile = Baked.LocalProfiles[LocalProfileIndex];
    const UWetnessProfile* PreviewSourceProfile = Cast<UWetnessProfile>(PreviewLocalProfile.SourceProfile.TryLoad());
    if (PreviewSourceProfile != nullptr)
    {
        PreviewLocalProfile.Parameters = PreviewSourceProfile->GetParameters();
        PreviewLocalProfile.StableKey = FString::Printf(
            TEXT("WCA.SurfacePreview|%s|%s"),
            *PreviewLocalProfile.SourceProfile.ToString(),
            *PreviewSourceProfile->GetPathName());

        FString NormalizeError;
        if (!FWetClothingSurfaceTextureNormalizer::NormalizeProfileTextures(
                *Asset,
                PreviewSourceProfile->GetParameters(),
                PreviewLocalProfile,
                NormalizeError))
        {
            SurfaceWaterPreviewStatus = FString::Printf(
                TEXT("Could not prepare the selected profile's 512 Surface Water mask/normal textures for WCA preview: %s"),
                *NormalizeError);
            bSurfaceWaterPreviewStatusIsError = true;
        }
    }
    const FWetClothingLocalRenderProfile& LocalProfile = PreviewLocalProfile;
    const FSurfaceWaterProfileParameters& Surface = LocalProfile.Parameters.SurfaceWater;

    UDWCGPUResourceSubsystem* ResourceSubsystem = nullptr;
    if (PreviewScene.IsValid())
    {
        if (UWorld* PreviewWorld = PreviewScene->GetWorld())
        {
            ResourceSubsystem = PreviewWorld->GetSubsystem<UDWCGPUResourceSubsystem>();
        }
    }
    if (ResourceSubsystem != nullptr)
    {
        if (!ResourceSubsystem->ApplyPreviewRenderProfileFallbackProfile(
            Asset,
            PreviewMaterialSlotIndex,
            LocalProfile,
            *SurfaceWaterPreviewMaterial))
        {
            SurfaceWaterPreviewStatus = TEXT("Could not apply the selected Surface Water render profile to the tiling preview material.");
            bSurfaceWaterPreviewStatusIsError = true;
        }

        if (ResourceSubsystem->GetDropletNormalArray() == nullptr ||
            ResourceSubsystem->GetDropletMaskArray() == nullptr)
        {
            SurfaceWaterPreviewStatus = TEXT("Surface Water preview droplet texture-array resources are incomplete. Coverage still renders, but masked detail may be missing until Render Profile Lookup Texture is rebuilt.");
            bSurfaceWaterPreviewStatusIsError = true;
        }
    }
    else
    {
        SurfaceWaterPreviewStatus = TEXT("Could not initialize DWC GPU render resources for the Surface Water preview world. Coverage still renders with material fallback profile values.");
        bSurfaceWaterPreviewStatusIsError = true;
    }

    ApplySurfaceWaterPreviewTextureParameters();
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::GlobalRenderProfileTexelSize(),
        1.0f / static_cast<float>(UDWCGPUResourceSubsystem::GlobalLUTWidth));
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::WetPartDebugStrength(), 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterDebugStrength(), 0.0f);
    bSurfaceWaterPreviewFallbackProfileCacheValid =
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(0))),
            SurfaceWaterPreviewBaseFallbackProfile0) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(1))),
            SurfaceWaterPreviewBaseFallbackProfile1) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(2))),
            SurfaceWaterPreviewBaseFallbackProfile2) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(3))),
            SurfaceWaterPreviewBaseFallbackProfile3) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(4))),
            SurfaceWaterPreviewBaseFallbackProfile4) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(5))),
            SurfaceWaterPreviewBaseFallbackProfile5) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(6))),
            SurfaceWaterPreviewBaseFallbackProfile6);
    ApplySurfaceWaterPreviewRenderOverrides();

    if (PreviewMaterialSlotIndex >= 0 && PreviewMaterialSlotIndex < PreviewMeshComponent->GetNumMaterials())
    {
        PreviewMeshComponent->SetMaterial(PreviewMaterialSlotIndex, SurfaceWaterPreviewMaterial);
    }
    RefreshMaterialSectionVisibility();
    PreviewMeshComponent->MarkRenderStateDirty();

    if (SurfaceWaterPreviewStatus.IsEmpty())
    {
        SurfaceWaterPreviewStatus = TEXT("Using a transient DWC preview material with runtime Render Profile resources and a float state map.");
        bSurfaceWaterPreviewStatusIsError = false;
    }

    if (SurfaceWaterPreviewCoverageMode == EDWCSurfaceWaterTilingPreviewCoverageMode::SingleCircle)
    {
        SurfaceWaterPreviewStatus += FString::Printf(
            TEXT("\nPreview LocalProfileID %d: SurfaceEnabled=%d NormalStrength=%.3g RoughnessBlend=%.3g TargetRoughness=%.3g TotalStrength=%.3g Specular=%.3g."),
            SurfacePreviewLocalProfileID,
            Surface.bEnabled ? 1 : 0,
            Surface.SurfaceWaterNormalStrength,
            Surface.SurfaceWaterRoughnessBlend,
            Surface.SurfaceWaterTargetRoughness,
            Surface.SurfaceWaterTotalStrength,
            Surface.SurfaceWaterSpecular);
        SurfaceWaterPreviewStatus += FString::Printf(
            TEXT("\nSingleCircleSurface=%g Droplet1SpawnChance=%.3g Droplet1StampPx=(%.3g,%.3g) SizeScale=%.3g Droplet1DetailSize=%.3g Droplet2DetailSize=%.3g AbsorbedWetness=0 NormalFlipXY=%d/%d."),
            PreviewSurfaceWater,
            Surface.DropletSpawnProbability,
            Surface.DropletRadiusPixels * Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Surface.DropletHeightPixels * Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Part->SurfaceWater.DropletDetailSize,
            Part->SurfaceWater.DropletFlowDetailSize,
            bSurfaceWaterPreviewFlipNormalX ? 1 : 0,
            bSurfaceWaterPreviewFlipNormalY ? 1 : 0);
    }
    else
    {
        SurfaceWaterPreviewStatus += FString::Printf(
            TEXT("\nPreview LocalProfileID %d: SurfaceEnabled=%d NormalStrength=%.3g RoughnessBlend=%.3g TargetRoughness=%.3g TotalStrength=%.3g Specular=%.3g."),
            SurfacePreviewLocalProfileID,
            Surface.bEnabled ? 1 : 0,
            Surface.SurfaceWaterNormalStrength,
            Surface.SurfaceWaterRoughnessBlend,
            Surface.SurfaceWaterTargetRoughness,
            Surface.SurfaceWaterTotalStrength,
            Surface.SurfaceWaterSpecular);
        SurfaceWaterPreviewStatus += FString::Printf(
            TEXT("\nFullPartSurface=%g Droplet1SpawnChance=%.3g Droplet1StampPx=(%.3g,%.3g) SizeScale=%.3g Droplet1DetailSize=%.3g Droplet2DetailSize=%.3g AbsorbedWetness=0 NormalFlipXY=%d/%d."),
            PreviewSurfaceWater,
            Surface.DropletSpawnProbability,
            Surface.DropletRadiusPixels * Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Surface.DropletHeightPixels * Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Part->SurfaceWater.DropletDetailSize,
            Part->SurfaceWater.DropletFlowDetailSize,
            bSurfaceWaterPreviewFlipNormalX ? 1 : 0,
            bSurfaceWaterPreviewFlipNormalY ? 1 : 0);
    }
    SurfaceWaterPreviewStatus += FString::Printf(
        TEXT("\nDisplayMode=%s."),
        SurfaceWaterPreviewDisplayMode == EDWCSurfaceWaterTilingPreviewDisplayMode::DropletNormal
            ? TEXT("DropletNormal")
            : TEXT("Lit"));
    if (!Surface.bEnabled)
    {
        SurfaceWaterPreviewStatus += TEXT("\nSelected preview profile has Surface Water disabled, so the preview keeps the source material appearance.");
    }
    else if (Surface.SurfaceWaterNormalStrength <= UE_KINDA_SMALL_NUMBER)
    {
        SurfaceWaterPreviewStatus += TEXT("\nSelected preview profile has zero Surface Water Normal Strength, so World Normal remains unchanged.");
    }
    else if (!bSurfaceWaterPreviewDropletsEnabled)
    {
        SurfaceWaterPreviewStatus += TEXT("\nThe droplet normal layer is disabled, so World Normal remains unchanged.");
    }

    const bool bMissingDropletNormal =
        Surface.bEnabled &&
        bSurfaceWaterPreviewDropletsEnabled &&
        LocalProfile.NormalizedDropletNormal == nullptr;
    const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(LocalProfile.SourceProfile.TryLoad());
    const FSurfaceWaterProfileParameters* AuthoredSurface =
        SourceProfile != nullptr ? &SourceProfile->GetParameters().SurfaceWater : nullptr;
    SurfaceWaterPreviewStatus += FString::Printf(
        TEXT("\nNormal data: AuthoredDropletNormal=%d BakedDropletNormal=%d."),
        AuthoredSurface != nullptr && AuthoredSurface->DropletNormalTexture != nullptr ? 1 : 0,
        LocalProfile.NormalizedDropletNormal != nullptr ? 1 : 0);
    SurfaceWaterPreviewStatus += FString::Printf(
        TEXT("\nMask data: AuthoredDropletMask=%d BakedDropletMask=%d."),
        AuthoredSurface != nullptr && AuthoredSurface->DropletMaskTexture != nullptr ? 1 : 0,
        LocalProfile.NormalizedDropletMask != nullptr ? 1 : 0);
    const bool bMissingDropletMask =
        Surface.bEnabled &&
        bSurfaceWaterPreviewDropletsEnabled &&
        LocalProfile.NormalizedDropletMask == nullptr;
    if (bMissingDropletNormal)
    {
        SurfaceWaterPreviewStatus += TEXT("\nSelected profile has no baked Droplet normal texture, so the preview will show coverage without detail normals.");
        bSurfaceWaterPreviewStatusIsError = true;
    }
    if (bMissingDropletMask)
    {
        SurfaceWaterPreviewStatus += TEXT("\nSelected profile has no baked Droplet mask. Surface Water is mask-gated, so coverage resolves to zero. Assign a Droplet mask and rebuild Render Profile Lookup Texture.");
        bSurfaceWaterPreviewStatusIsError = true;
    }

    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
        OverlayText->SetColorAndOpacity(GetViewportHintTextColor());
    }
    RequestViewportRedraw();
}

void SDWCPartViewport::ApplySurfaceWaterPreviewRenderOverrides()
{
    if (SurfaceWaterPreviewMaterial == nullptr)
    {
        return;
    }

    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterNormalFlipX(),
        bSurfaceWaterPreviewFlipNormalX ? 1.0f : 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterNormalFlipY(),
        bSurfaceWaterPreviewFlipNormalY ? 1.0f : 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        PreviewDebugModeParameter,
        SurfaceWaterPreviewDisplayMode == EDWCSurfaceWaterTilingPreviewDisplayMode::DropletNormal ? 4.0f : 0.0f);

    if (!bSurfaceWaterPreviewFallbackProfileCacheValid)
    {
        return;
    }

    FLinearColor Texel0 = SurfaceWaterPreviewBaseFallbackProfile0;
    const FLinearColor Texel1 = SurfaceWaterPreviewBaseFallbackProfile1;
    FLinearColor Texel2 = SurfaceWaterPreviewBaseFallbackProfile2;
    const FLinearColor Texel3 = SurfaceWaterPreviewBaseFallbackProfile3;
    FLinearColor Texel4 = SurfaceWaterPreviewBaseFallbackProfile4;
    const FLinearColor Texel5 = SurfaceWaterPreviewBaseFallbackProfile5;
    const FLinearColor Texel6 = SurfaceWaterPreviewBaseFallbackProfile6;

    if (!bSurfaceWaterPreviewDropletsEnabled)
    {
        Texel0.B = 0.0f;
        Texel0.A = 0.0f;
        Texel2.R = 0.0f;
        Texel2.G = 0.0f;
        Texel4.R = 0.0f;
        Texel4.G = 0.0f;
    }

    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(0), Texel0);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(1), Texel1);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(2), Texel2);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(3), Texel3);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(4), Texel4);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(5), Texel5);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(6), Texel6);
}

FText SDWCPartViewport::GetSurfaceWaterPreviewStatusText() const
{
    return FText::FromString(SurfaceWaterPreviewStatus);
}

FSlateColor SDWCPartViewport::GetSurfaceWaterPreviewStatusColor() const
{
    return bSurfaceWaterPreviewStatusIsError
        ? FSlateColor(FStyleColors::Error)
        : FSlateColor(FStyleColors::ForegroundHover);
}

void SDWCPartViewport::RequestViewportRedraw()
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }

    Invalidate();
}

void SDWCPartViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

TSharedRef<FEditorViewportClient> SDWCPartViewport::MakeEditorViewportClient()
{
    check(PreviewScene.IsValid());
    ViewportClient = MakeShared<FDWCPartViewportClient>(PreviewScene.Get(), SharedThis(this));

    if (PreviewMeshComponent != nullptr)
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
    }

    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SDWCPartViewport::BuildViewportToolbar()
{
    const FName ViewportToolbarName = TEXT("WCAEditor.ViewportToolbar");

    if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
    {
        UToolMenu* const ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(ViewportToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
        ViewportToolbarMenu->StyleName = TEXT("ViewportToolbar");

        ViewportToolbarMenu->AddSection(TEXT("Left"));

        FToolMenuSection& RightSection = ViewportToolbarMenu->AddSection(TEXT("Right"));
        RightSection.Alignment = EToolMenuSectionAlign::Last;
        RightSection.AddEntry(UE::UnrealEd::CreateCameraSubmenu(UE::UnrealEd::FViewportCameraMenuOptions().ShowAll()));
        RightSection.AddEntry(UE::DWCEditor::CreateDWCViewModesSubmenu());
    }

    FToolMenuContext ViewportToolbarContext;
    ViewportToolbarContext.AppendCommandList(GetCommandList());
    ViewportToolbarContext.AddObject(UE::UnrealEd::CreateViewportToolbarDefaultContext(SharedThis(this)));

    return UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext);
}

void SDWCPartViewport::HandleIslandPickedFromClient(int32 UVIslandID, bool bAppendSelection)
{
    if (OnIslandPicked.IsBound())
    {
        OnIslandPicked.Execute(UVIslandID, bAppendSelection);
    }
}

void SDWCPartViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
    SEditorViewport::PopulateViewportOverlays(Overlay);

    Overlay->AddSlot()
        .VAlign(VAlign_Top)
        .HAlign(HAlign_Left)
        .Padding(8.0f)
            [SNew(SBorder)
                 .BorderImage(FAppStyle::Get().GetBrush("FloatingBorder"))
                 .Padding(6.0f)
                     [SAssignNew(OverlayText, STextBlock)
                          .Text(this, &SDWCPartViewport::GetViewportHintText)
                          .ColorAndOpacity(this, &SDWCPartViewport::GetViewportHintTextColor)
                          .AutoWrapText(true)]];
}

void SDWCPartViewport::OnFocusViewportToSelection()
{
    FocusOnPreviewMesh(false);
}

void SDWCPartViewport::CacheOriginalMaterials()
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

void SDWCPartViewport::RestoreOriginalMaterials()
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

UMaterialInterface* SDWCPartViewport::ResolveWetPartOverlayMaterial()
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


FText SDWCPartViewport::GetViewportHintText() const
{
    if (bSurfaceWaterTilingPreview)
    {
        FString Hint = TEXT("Surface Water Tiling uses a transient DWC preview material on the original skeletal mesh.");
        if (CurrentHighlightedMaterialSlot != INDEX_NONE)
        {
            Hint += FString::Printf(TEXT("\nPreviewing material slot %d."), CurrentHighlightedMaterialSlot);
        }
        else
        {
            Hint += TEXT("\nSelect a wettable material slot and Wet Part.");
        }

        if (!SurfaceWaterPreviewStatus.IsEmpty())
        {
            Hint += TEXT("\n") + SurfaceWaterPreviewStatus;
        }
        return FText::FromString(Hint);
    }

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

FSlateColor SDWCPartViewport::GetViewportHintTextColor() const
{
    return bSurfaceWaterTilingPreview
        ? GetSurfaceWaterPreviewStatusColor()
        : FSlateColor(FStyleColors::ForegroundHover);
}

#undef LOCTEXT_NAMESPACE
