#include "WetWrinkleViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/Common/Texture/WetClothingMaterialTextureResolver.h"
#include "WetClothing/WrinkleMode/Material/WetWrinklePreviewMaterialBuilder.h"
#include "WetWrinkleViewportClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "WetWrinkleViewport"

DEFINE_LOG_CATEGORY_STATIC(LogWetWrinklePreviewViewport, Log, All);

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

    float WrapWetWrinkleRasterPreviewUV(float Value)
    {
        return Value - FMath::FloorToFloat(Value);
    }

    FVector2D WrapWetWrinkleRasterPreviewUV(const FVector2D& UV)
    {
        return FVector2D(WrapWetWrinkleRasterPreviewUV(UV.X), WrapWetWrinkleRasterPreviewUV(UV.Y));
    }

    float ComputeWrappedWetWrinkleDelta(float Delta)
    {
        return Delta - FMath::RoundToFloat(Delta);
    }

    FIntPoint ComputeWetWrinklePreviewTextureSize(const UTexture* SourceTexture)
    {
        constexpr int32 TargetPreviewLargestDimension = 1024;
        constexpr int32 MinPreviewDimension = 256;

        const int32 SourceSizeX = SourceTexture != nullptr ? FMath::Max(1, SourceTexture->GetSurfaceWidth()) : TargetPreviewLargestDimension;
        const int32 SourceSizeY = SourceTexture != nullptr ? FMath::Max(1, SourceTexture->GetSurfaceHeight()) : TargetPreviewLargestDimension;
        const int32 LargestDimension = FMath::Max(SourceSizeX, SourceSizeY);
        if (LargestDimension == TargetPreviewLargestDimension)
        {
            return FIntPoint(SourceSizeX, SourceSizeY);
        }

        const float Scale = static_cast<float>(TargetPreviewLargestDimension) / static_cast<float>(LargestDimension);
        return FIntPoint(
            FMath::Max(MinPreviewDimension, FMath::RoundToInt(static_cast<float>(SourceSizeX) * Scale)),
            FMath::Max(MinPreviewDimension, FMath::RoundToInt(static_cast<float>(SourceSizeY) * Scale)));
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

    float ComputeWetWrinkleSmoothStep(float Edge0, float Edge1, float Value)
    {
        if (Edge0 >= Edge1)
        {
            return Value < Edge0 ? 0.0f : 1.0f;
        }

        const float T = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }



    bool AreWetWrinkleSurfaceHitsEquivalentForPreview(const FWetWrinkleSurfaceHit& A, const FWetWrinkleSurfaceHit& B)
    {
        if (A.bHit != B.bHit)
        {
            return false;
        }

        if (!A.bHit)
        {
            return true;
        }

        constexpr double UVToleranceSq = 1.0e-8;
        return A.MaterialSlotIndex == B.MaterialSlotIndex &&
               A.UVChannelIndex == B.UVChannelIndex &&
               A.TriangleID == B.TriangleID &&
               (A.UV - B.UV).SizeSquared() <= UVToleranceSq;
    }

    FVector DecodeWetWrinkleNormal(const FColor& Color)
    {
        FVector DecodedNormal(
            static_cast<float>(Color.R) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.G) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.B) / 255.0f * 2.0f - 1.0f);
        if (DecodedNormal.Z <= UE_SMALL_NUMBER)
        {
            const float XYLengthSq = FMath::Min(DecodedNormal.X * DecodedNormal.X + DecodedNormal.Y * DecodedNormal.Y, 1.0f);
            DecodedNormal.Z = FMath::Sqrt(FMath::Max(1.0f - XYLengthSq, 0.0f));
        }

        return DecodedNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
    }

    FColor EncodeWetWrinkleNormal(const FVector& Normal)
    {
        const FVector SafeNormal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        return FColor(
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.X * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            255);
    }

    struct FWetWrinkleBrushNormalSource
    {
        explicit FWetWrinkleBrushNormalSource(UTexture2D* InTexture)
            : Texture(InTexture)
        {
            if (Texture == nullptr || !Texture->Source.IsValid())
            {
                return;
            }

            SizeX = Texture->Source.GetSizeX();
            SizeY = Texture->Source.GetSizeY();
            SourceFormat = Texture->Source.GetFormat();
            if (SizeX <= 0 ||
                SizeY <= 0 ||
                (SourceFormat != TSF_BGRA8 && SourceFormat != TSF_BGRE8 && SourceFormat != TSF_G8 && SourceFormat != TSF_G16))
            {
                return;
            }

            bFlipGreenChannel = Texture->bFlipGreenChannel;
            MipData = Texture->Source.LockMipReadOnly(0);
        }

        ~FWetWrinkleBrushNormalSource()
        {
            if (Texture != nullptr && MipData != nullptr)
            {
                Texture->Source.UnlockMip(0);
            }
        }

        bool IsValid() const
        {
            return MipData != nullptr;
        }

        FVector SampleNormalTS(const FVector2D& UV) const
        {
            if (!IsValid())
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

            const float ClampedU = FMath::Clamp(UV.X, 0.0f, 0.999f);
            const float ClampedV = FMath::Clamp(UV.Y, 0.0f, 0.999f);
            const int32 PixelX = FMath::Clamp(FMath::FloorToInt(ClampedU * static_cast<float>(SizeX)), 0, SizeX - 1);
            const int32 PixelY = FMath::Clamp(FMath::FloorToInt(ClampedV * static_cast<float>(SizeY)), 0, SizeY - 1);

            if (SourceFormat == TSF_G8)
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

            if (SourceFormat == TSF_G16)
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

            const FColor* ColorData = reinterpret_cast<const FColor*>(MipData);
            const FColor Color = ColorData[PixelY * SizeX + PixelX];
            const float DecodedX = static_cast<float>(Color.R) / 255.0f * 2.0f - 1.0f;
            float DecodedY = -(static_cast<float>(Color.G) / 255.0f * 2.0f - 1.0f);
            if (bFlipGreenChannel)
            {
                DecodedY = -DecodedY;
            }
            FVector DecodedNormal(
                DecodedX,
                DecodedY,
                static_cast<float>(Color.B) / 255.0f * 2.0f - 1.0f);
            if (DecodedNormal.Z <= UE_SMALL_NUMBER)
            {
                const float XYLengthSq = FMath::Min(DecodedNormal.X * DecodedNormal.X + DecodedNormal.Y * DecodedNormal.Y, 1.0f);
                DecodedNormal.Z = FMath::Sqrt(FMath::Max(1.0f - XYLengthSq, 0.0f));
            }

            return DecodedNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        }

        UTexture2D* Texture = nullptr;
        const uint8* MipData = nullptr;
        int32 SizeX = 0;
        int32 SizeY = 0;
        ETextureSourceFormat SourceFormat = TSF_Invalid;
        bool bFlipGreenChannel = false;
    };

    void RasterizeWetWrinkleAccumulatedStamp(const FWetWrinklePatchPlacement& Stamp, const FIntPoint& TextureSize, TArray<FColor>& InOutPixels)
    {
        if (TextureSize.X <= 0 || TextureSize.Y <= 0 || InOutPixels.Num() != TextureSize.X * TextureSize.Y || Stamp.NormalPatchTexture == nullptr ||
            Stamp.BrushRadiusUV <= 0.0f || Stamp.Strength <= 0.0f)
        {
            return;
        }

        FWetWrinkleBrushNormalSource NormalSource(Stamp.NormalPatchTexture);
        if (!NormalSource.IsValid())
        {
            return;
        }

        const FVector2D WrappedCenter = WrapWetWrinkleRasterPreviewUV(Stamp.PositionUV);
        const FVector2D SafeScale(
            FMath::Max(FMath::Abs(Stamp.Scale.X), UE_SMALL_NUMBER),
            FMath::Max(FMath::Abs(Stamp.Scale.Y), UE_SMALL_NUMBER));
        const float EdgeFadeStart = FMath::Clamp(1.0f - Stamp.Falloff, 0.0f, 0.98f);
        const float CosRotation = FMath::Cos(Stamp.RotationRadians);
        const float SinRotation = FMath::Sin(Stamp.RotationRadians);

        for (int32 TileOffsetY = -1; TileOffsetY <= 1; ++TileOffsetY)
        {
            for (int32 TileOffsetX = -1; TileOffsetX <= 1; ++TileOffsetX)
            {
                const FVector2D TileCenter = WrappedCenter + FVector2D(static_cast<float>(TileOffsetX), static_cast<float>(TileOffsetY));
                const int32 MinX = FMath::Clamp(
                    FMath::FloorToInt((TileCenter.X - Stamp.BrushRadiusUV) * static_cast<float>(TextureSize.X)),
                    0,
                    TextureSize.X - 1);
                const int32 MaxX = FMath::Clamp(
                    FMath::CeilToInt((TileCenter.X + Stamp.BrushRadiusUV) * static_cast<float>(TextureSize.X)),
                    0,
                    TextureSize.X - 1);
                const int32 MinY = FMath::Clamp(
                    FMath::FloorToInt((TileCenter.Y - Stamp.BrushRadiusUV) * static_cast<float>(TextureSize.Y)),
                    0,
                    TextureSize.Y - 1);
                const int32 MaxY = FMath::Clamp(
                    FMath::CeilToInt((TileCenter.Y + Stamp.BrushRadiusUV) * static_cast<float>(TextureSize.Y)),
                    0,
                    TextureSize.Y - 1);
                if (MinX > MaxX || MinY > MaxY)
                {
                    continue;
                }

                for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
                {
                    for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
                    {
                        const FVector2D PixelUV(
                            (static_cast<float>(PixelX) + 0.5f) / static_cast<float>(TextureSize.X),
                            (static_cast<float>(PixelY) + 0.5f) / static_cast<float>(TextureSize.Y));
                        const FVector2D DeltaUV(
                            ComputeWrappedWetWrinkleDelta(PixelUV.X - TileCenter.X),
                            ComputeWrappedWetWrinkleDelta(PixelUV.Y - TileCenter.Y));
                        const FVector2D Local = DeltaUV / FMath::Max(Stamp.BrushRadiusUV, UE_SMALL_NUMBER);
                        const float DistanceFromCenter = Local.Size();
                        if (DistanceFromCenter > 1.0f)
                        {
                            continue;
                        }

                        const float EdgeFade = 1.0f - ComputeWetWrinkleSmoothStep(EdgeFadeStart, 1.0f, DistanceFromCenter);
                        if (EdgeFade <= UE_SMALL_NUMBER)
                        {
                            continue;
                        }

                        const float BrushLocalX = (CosRotation * Local.X + SinRotation * Local.Y) / SafeScale.X;
                        const float BrushLocalY = (-SinRotation * Local.X + CosRotation * Local.Y) / SafeScale.Y;
                        if (FMath::Abs(BrushLocalX) > 1.0f || FMath::Abs(BrushLocalY) > 1.0f)
                        {
                            continue;
                        }

                        const FVector2D BrushTextureUV(BrushLocalX * 0.5f + 0.5f, BrushLocalY * 0.5f + 0.5f);
                        const FVector BrushNormalTS = NormalSource.SampleNormalTS(BrushTextureUV);
                        const FVector RotatedBrushNormalTS(
                            BrushNormalTS.X * CosRotation - BrushNormalTS.Y * SinRotation,
                            BrushNormalTS.X * SinRotation + BrushNormalTS.Y * CosRotation,
                            BrushNormalTS.Z);
                        const float StrengthScale = FMath::Max(Stamp.Strength * EdgeFade, 0.0f);
                        const FVector StampNormalTS =
                            FVector(
                                RotatedBrushNormalTS.X * StrengthScale,
                                RotatedBrushNormalTS.Y * StrengthScale,
                                RotatedBrushNormalTS.Z)
                                .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));

                        FColor& Pixel = InOutPixels[PixelY * TextureSize.X + PixelX];
                        const FVector ExistingNormalTS = DecodeWetWrinkleNormal(Pixel);
                        const FVector BlendedNormalTS =
                            FVector(
                                ExistingNormalTS.X + StampNormalTS.X,
                                ExistingNormalTS.Y + StampNormalTS.Y,
                                ExistingNormalTS.Z * StampNormalTS.Z)
                                .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
                        Pixel = EncodeWetWrinkleNormal(BlendedNormalTS);
                    }
                }
            }
        }
    }
} // namespace

void SWetWrinkleViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
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

    RefreshPreviewMesh();
}

SWetWrinkleViewport::~SWetWrinkleViewport()
{
    ReleasePreviewMaterialSlots();
    ReleaseAccumulatedPreviewStates();

    if (PreviewScene.IsValid() && PreviewMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewMeshComponent);
    }

    if (PreviewScene.IsValid() && BrushCursorComponent != nullptr)
    {
        PreviewScene->RemoveComponent(BrushCursorComponent);
    }
}

void SWetWrinkleViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(BrushCursorComponent);
    Collector.AddReferencedObject(CursorMaterial);
    for (FWetWrinklePreviewMaterialSlotState& SlotState : PreviewMaterialSlots)
    {
        Collector.AddReferencedObject(SlotState.MeshOriginalMaterial);
        Collector.AddReferencedObject(SlotState.DwcWetMaterial);
        Collector.AddReferencedObject(SlotState.PreviewSourceMaterial);
        Collector.AddReferencedObject(SlotState.TransientPreviewMaterial);
        Collector.AddReferencedObject(SlotState.TransientPreviewParent);
        Collector.AddReferencedObject(SlotState.PreviewMID);
    }
    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        Collector.AddReferencedObject(PreviewState.SourceTexture);
        Collector.AddReferencedObject(PreviewState.AccumulatedNormalTexture);
    }
    Collector.AddReferencedObject(BrushSettings.BrushHeightTexture);
}

void SWetWrinkleViewport::RefreshPreviewMesh()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    PreviewMeshComponent->SetSkeletalMeshAsset(TargetMesh);
    RebuildPreviewMaterialSlots();
    ApplyPreviewWetVertexColors();
    ApplyMaterialSlotVisibility();
    RebuildHitTriangles();
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    ClearBrushCursor();
    ReleaseAccumulatedPreviewStates();
    RefreshStoredStampOverlay();
    RefreshWrinklePreviewMaterials();

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

void SWetWrinkleViewport::SetBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings)
{
    const bool bNeedsTriangleRebuild =
        BrushSettings.UVChannelIndex != InBrushSettings.UVChannelIndex ||
        BrushSettings.MaterialSlotIndex != InBrushSettings.MaterialSlotIndex;

    BrushSettings = InBrushSettings;
    ApplyMaterialSlotVisibility();

    if (bNeedsTriangleRebuild)
    {
        RebuildHitTriangles();
        RefreshStoredStampOverlay(false);
    }

    RefreshBrushCursor();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::RefreshStoredStampOverlay(bool bRebuildAccumulatedPreview)
{
    if (bRebuildAccumulatedPreview)
    {
        MarkAccumulatedPreviewStatesDirty();
    }

    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::SetSelectedStrokeGuid(const FGuid& InStrokeGuid)
{
    if (SelectedStrokeGuid == InStrokeGuid)
    {
        return;
    }

    SelectedStrokeGuid = InStrokeGuid;
    Invalidate();
}

void SWetWrinkleViewport::PreviewBrushAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV)
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
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::ClearExternalBrushPreview()
{
    ClearBrushCursor();
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

bool SWetWrinkleViewport::TryBuildSurfaceHitAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, FWetWrinkleSurfaceHit& OutHit) const
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

bool SWetWrinkleViewport::TraceSurface(const FVector& RayOrigin, const FVector& RayDirection, FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    OutHit.UVChannelIndex = HitTriangleUVChannelIndex != INDEX_NONE ? HitTriangleUVChannelIndex : BrushSettings.UVChannelIndex;

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
        OutHit.UVChannelIndex = HitTriangleUVChannelIndex != INDEX_NONE ? HitTriangleUVChannelIndex : BrushSettings.UVChannelIndex;
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

void SWetWrinkleViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

TSharedRef<FEditorViewportClient> SWetWrinkleViewport::MakeEditorViewportClient()
{
    check(PreviewScene.IsValid());
    ViewportClient = MakeShared<FWetWrinkleViewportClient>(PreviewScene.Get(), SharedThis(this));

    if (PreviewMeshComponent != nullptr)
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
    }

    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SWetWrinkleViewport::BuildViewportToolbar()
{
    const FName ViewportToolbarName = TEXT("WetWrinkleEditor.ViewportToolbar");

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

void SWetWrinkleViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
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

void SWetWrinkleViewport::OnFocusViewportToSelection()
{
    FocusOnPreviewMesh(false);
}

USkeletalMesh* SWetWrinkleViewport::ResolveTargetMesh() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return nullptr;
    }

    if (Asset->TargetMesh != nullptr)
    {
        return Asset->TargetMesh;
    }

    return Asset->TargetMesh;
}

const UWetClothingAsset* SWetWrinkleViewport::ResolveSourceWetClothingAsset() const
{
    return WetClothingAsset.Get();
}

UTexture* SWetWrinkleViewport::ResolveSourceTextureForMaterialSlot(int32 MaterialSlotIndex, int32 UVChannelIndex) const
{
    const UWetClothingAsset* SourceWetClothingAsset = ResolveSourceWetClothingAsset();
    if (SourceWetClothingAsset != nullptr)
    {
        for (const FWetClothingSourceTextureSelection& TextureSelection : SourceWetClothingAsset->PartData.EditableWetPartData.SourceTextureSelections)
        {
            if (TextureSelection.MaterialSlotIndex == MaterialSlotIndex &&
                TextureSelection.UVChannelIndex == UVChannelIndex &&
                TextureSelection.Texture != nullptr)
            {
                return TextureSelection.Texture;
            }
        }
    }

    const USkeletalMesh* TargetMesh = ResolveTargetMesh();
    if (TargetMesh != nullptr && TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        return FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(TargetMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface);
    }

    return nullptr;
}

UMaterialInterface* SWetWrinkleViewport::ResolveDwcWetMaterialForSlot(int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* SourceWetClothingAsset = ResolveSourceWetClothingAsset();
    if (SourceWetClothingAsset == nullptr)
    {
        return nullptr;
    }

    const FWetClothingGeneratedWetMaterialOverride* WetOverride = SourceWetClothingAsset->PartData.GeneratedWetMaterialOverrides.FindByPredicate(
        [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& Entry)
        {
            return Entry.MaterialSlotIndex == MaterialSlotIndex;
        });
    return WetOverride != nullptr ? WetOverride->WetMaterial.Get() : nullptr;
}

void SWetWrinkleViewport::ReleasePreviewMaterialSlots()
{
    PreviewMaterialSlots.Reset();
    MarkPreviewMaterialsNeedReapply();
}

void SWetWrinkleViewport::RebuildPreviewMaterialSlots()
{
    ReleasePreviewMaterialSlots();

    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    const USkeletalMesh* TargetMesh = PreviewMeshComponent->GetSkeletalMeshAsset();
    if (TargetMesh == nullptr)
    {
        return;
    }

    const int32 MaterialCount = PreviewMeshComponent->GetNumMaterials();
    PreviewMaterialSlots.SetNum(MaterialCount);

    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[MaterialIndex];
        SlotState.MaterialSlotIndex = MaterialIndex;

        if (TargetMesh->GetMaterials().IsValidIndex(MaterialIndex))
        {
            SlotState.MeshOriginalMaterial = TargetMesh->GetMaterials()[MaterialIndex].MaterialInterface;
        }

        SlotState.DwcWetMaterial = ResolveDwcWetMaterialForSlot(MaterialIndex);
        SlotState.bUsesDwcWetMaterial = SlotState.DwcWetMaterial != nullptr;
        SlotState.PreviewSourceMaterial = SlotState.bUsesDwcWetMaterial
                                              ? SlotState.DwcWetMaterial
                                              : SlotState.MeshOriginalMaterial;
        if (SlotState.PreviewSourceMaterial == nullptr)
        {
            SlotState.PreviewSourceMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
        }
    }

    ApplyPreviewMaterialsToMesh();
}

void SWetWrinkleViewport::ApplyPreviewMaterialsToMesh()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    for (const FWetWrinklePreviewMaterialSlotState& SlotState : PreviewMaterialSlots)
    {
        UMaterialInterface* MaterialToApply =
            (SlotState.MaterialSlotIndex == ActiveMaterialSlotIndex && SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready &&
             SlotState.PreviewMID != nullptr)
                ? static_cast<UMaterialInterface*>(SlotState.PreviewMID.Get())
                : SlotState.PreviewSourceMaterial.Get();
        PreviewMeshComponent->SetMaterial(SlotState.MaterialSlotIndex, MaterialToApply);
    }

    LastAppliedActivePreviewMaterialSlot = ActiveMaterialSlotIndex;
    bPreviewMaterialsNeedReapply = false;
}

void SWetWrinkleViewport::MarkPreviewMaterialsNeedReapply()
{
    LastAppliedActivePreviewMaterialSlot = INDEX_NONE;
    bPreviewMaterialsNeedReapply = true;
}

UMaterialInterface* SWetWrinkleViewport::GetPreviewSourceMaterial(int32 MaterialSlotIndex) const
{
    return PreviewMaterialSlots.IsValidIndex(MaterialSlotIndex)
               ? PreviewMaterialSlots[MaterialSlotIndex].PreviewSourceMaterial.Get()
               : nullptr;
}

void SWetWrinkleViewport::ApplyPreviewWetVertexColors()
{
    if (PreviewMeshComponent == nullptr || PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return;
    }

    const FSkeletalMeshRenderData* RenderData = PreviewMeshComponent->GetSkeletalMeshAsset()->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(0))
    {
        return;
    }

    const int32 VertexCount = RenderData->LODRenderData[0].GetNumVertices();
    if (VertexCount <= 0)
    {
        return;
    }

    TArray<FLinearColor> PreviewVertexColors;
    PreviewVertexColors.Init(FLinearColor::White, VertexCount);
    PreviewMeshComponent->SetVertexColorOverride_LinearColor(0, PreviewVertexColors);
    PreviewMeshComponent->MarkRenderStateDirty();
}

void SWetWrinkleViewport::RefreshWrinklePreviewMaterials()
{
    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (ActiveMaterialSlotIndex != INDEX_NONE)
    {
        const bool bWasReady =
            PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex) &&
            PreviewMaterialSlots[ActiveMaterialSlotIndex].PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready &&
            PreviewMaterialSlots[ActiveMaterialSlotIndex].PreviewMID != nullptr;
        EnsurePreviewMaterialForSlot(ActiveMaterialSlotIndex);
        const bool bIsReady =
            PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex) &&
            PreviewMaterialSlots[ActiveMaterialSlotIndex].PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready &&
            PreviewMaterialSlots[ActiveMaterialSlotIndex].PreviewMID != nullptr;
        if (!bWasReady && bIsReady)
        {
            bPreviewMaterialsNeedReapply = true;
        }
        ResetPreviewMaterialParameters(ActiveMaterialSlotIndex);

        if (PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex))
        {
            FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[ActiveMaterialSlotIndex];
            if (SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready && SlotState.PreviewMID != nullptr)
            {
                UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(ActiveMaterialSlotIndex, BrushSettings.UVChannelIndex);
                if (UTexture2D* AccumulatedPreviewTexture =
                        ResolveAccumulatedPreviewTexture(SourceTexture, ActiveMaterialSlotIndex, BrushSettings.UVChannelIndex))
                {
                    SlotState.PreviewMID->SetTextureParameterValue(
                        WetWrinklePreviewMaterialParameters::AccumulatedNormal,
                        AccumulatedPreviewTexture);
                    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedEnabled, 1.0f);
                    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedStrength, 1.0f);
                }

                if (BrushSettings.bShowPreview && CurrentSurfaceHit.bHit && CurrentSurfaceHit.MaterialSlotIndex == ActiveMaterialSlotIndex &&
                    CurrentSurfaceHit.UVChannelIndex == BrushSettings.UVChannelIndex && BrushSettings.BrushHeightTexture != nullptr)
                {
                    SlotState.PreviewMID->SetTextureParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverNormal,
                        BrushSettings.BrushHeightTexture.Get());
                    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverEnabled, 1.0f);
                    SlotState.PreviewMID->SetVectorParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverCenterUV,
                        FLinearColor(CurrentSurfaceHit.UV.X, CurrentSurfaceHit.UV.Y, 0.0f, 0.0f));
                    SlotState.PreviewMID->SetScalarParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverRadiusUV,
                        FMath::Max(BrushSettings.BrushRadiusUV, UE_SMALL_NUMBER));
                    SlotState.PreviewMID->SetScalarParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverRotation,
                        BrushSettings.RotationRadians);
                    SlotState.PreviewMID->SetVectorParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverScale,
                        FLinearColor(1.0f, 1.0f, 0.0f, 0.0f));
                    SlotState.PreviewMID->SetScalarParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverStrength,
                        FMath::Clamp(BrushSettings.Strength, 0.0f, 4.0f));
                    SlotState.PreviewMID->SetScalarParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverFalloff,
                        FMath::Clamp(BrushSettings.Falloff, 0.0f, 1.0f));
                }
            }
        }
    }

    if (bPreviewMaterialsNeedReapply || LastAppliedActivePreviewMaterialSlot != ActiveMaterialSlotIndex)
    {
        ApplyPreviewMaterialsToMesh();
    }
}

bool SWetWrinkleViewport::EnsurePreviewMaterialForSlot(int32 MaterialSlotIndex)
{
    if (!PreviewMaterialSlots.IsValidIndex(MaterialSlotIndex))
    {
        return false;
    }

    FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[MaterialSlotIndex];
    if (SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready && SlotState.PreviewMID != nullptr)
    {
        return true;
    }

    if (SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Failed ||
        SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Unsupported)
    {
        return false;
    }

    if (SlotState.PreviewSourceMaterial == nullptr)
    {
        SlotState.PreviewStatus = EWetWrinklePreviewMaterialStatus::Unsupported;
        SlotState.PreviewBuildError = TEXT("Preview source material is not available.");
        return false;
    }

    FWetWrinklePreviewMaterialBuildResult BuildResult =
        FWetWrinklePreviewMaterialBuilder::Build(SlotState.PreviewSourceMaterial.Get());
    if (!BuildResult.bSucceeded || BuildResult.PreviewMID == nullptr)
    {
        SlotState.PreviewStatus = EWetWrinklePreviewMaterialStatus::Failed;
        SlotState.PreviewBuildError = BuildResult.ErrorMessage;
        UE_LOG(
            LogWetWrinklePreviewViewport,
            Warning,
            TEXT("Failed to build wrinkle preview material for slot %d (%s): %s"),
            MaterialSlotIndex,
            *GetNameSafe(SlotState.PreviewSourceMaterial),
            *SlotState.PreviewBuildError);
        return false;
    }

    SlotState.TransientPreviewMaterial = BuildResult.TransientBaseMaterial;
    SlotState.TransientPreviewParent = BuildResult.TransientMaterialParent;
    SlotState.PreviewMID = BuildResult.PreviewMID;
    SlotState.PreviewStatus = EWetWrinklePreviewMaterialStatus::Ready;
    SlotState.PreviewBuildError.Reset();
    ResetPreviewMaterialParameters(MaterialSlotIndex);
    return true;
}

void SWetWrinkleViewport::ResetPreviewMaterialParameters(int32 MaterialSlotIndex)
{
    if (!PreviewMaterialSlots.IsValidIndex(MaterialSlotIndex))
    {
        return;
    }

    FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[MaterialSlotIndex];
    if (SlotState.PreviewMID == nullptr)
    {
        return;
    }

    SlotState.PreviewMID->SetScalarParameterValue(
        WetWrinklePreviewMaterialParameters::UVChannel,
        static_cast<float>(FMath::Max(BrushSettings.UVChannelIndex, 0)));
    SlotState.PreviewMID->SetTextureParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedNormal, nullptr);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedEnabled, 0.0f);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedStrength, 1.0f);
    SlotState.PreviewMID->SetTextureParameterValue(WetWrinklePreviewMaterialParameters::HoverNormal, nullptr);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverEnabled, 0.0f);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverRotation, 0.0f);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverRadiusUV, 0.0f);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverStrength, 0.0f);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverFalloff, 0.5f);
    SlotState.PreviewMID->SetVectorParameterValue(WetWrinklePreviewMaterialParameters::HoverCenterUV, FLinearColor::Black);
    SlotState.PreviewMID->SetVectorParameterValue(
        WetWrinklePreviewMaterialParameters::HoverScale,
        FLinearColor(1.0f, 1.0f, 0.0f, 0.0f));
}

void SWetWrinkleViewport::AppendAccumulatedPreviewStamp(const FWetWrinklePatchPlacement& Stamp)
{
    if (Stamp.MaterialSlotIndex == INDEX_NONE || Stamp.UVChannelIndex < 0)
    {
        return;
    }

    FWetWrinkleAccumulatedPreviewState* PreviewState =
        FindOrAddAccumulatedPreviewState(Stamp.SourceTexture.Get(), Stamp.MaterialSlotIndex, Stamp.UVChannelIndex);
    if (PreviewState == nullptr)
    {
        return;
    }

    if (PreviewState->bDirty || PreviewState->AccumulatedNormalTexture == nullptr ||
        PreviewState->AccumulatedNormalTexture->GetPlatformData() == nullptr ||
        !PreviewState->AccumulatedNormalTexture->GetPlatformData()->Mips.IsValidIndex(0))
    {
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewMaterials();
        return;
    }

    PreviewState->TextureSize = ComputeWetWrinklePreviewTextureSize(PreviewState->SourceTexture);
    if (PreviewState->TextureSize.X <= 0 || PreviewState->TextureSize.Y <= 0 ||
        PreviewState->AccumulatedNormalTexture->GetSizeX() != PreviewState->TextureSize.X ||
        PreviewState->AccumulatedNormalTexture->GetSizeY() != PreviewState->TextureSize.Y)
    {
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewMaterials();
        return;
    }

    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(PreviewState->TextureSize.X * PreviewState->TextureSize.Y);

    FTexture2DMipMap& Mip = PreviewState->AccumulatedNormalTexture->GetPlatformData()->Mips[0];
    const void* MipData = Mip.BulkData.LockReadOnly();
    FMemory::Memcpy(Pixels.GetData(), MipData, Pixels.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();

    RasterizeWetWrinkleAccumulatedStamp(Stamp, PreviewState->TextureSize, Pixels);

    void* WritableMipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(WritableMipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();
    PreviewState->AccumulatedNormalTexture->UpdateResource();
    PreviewState->bDirty = false;

    RefreshWrinklePreviewMaterials();
}

void SWetWrinkleViewport::ReleaseAccumulatedPreviewStates()
{
    AccumulatedPreviewStates.Reset();
}

void SWetWrinkleViewport::MarkAccumulatedPreviewStatesDirty()
{
    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        PreviewState.bDirty = true;
    }
}

FWetWrinkleAccumulatedPreviewState* SWetWrinkleViewport::FindOrAddAccumulatedPreviewState(
    UTexture* SourceTexture,
    int32 MaterialSlotIndex,
    int32 UVChannelIndex)
{
    if (MaterialSlotIndex == INDEX_NONE || UVChannelIndex < 0)
    {
        return nullptr;
    }

    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        if (PreviewState.MaterialSlotIndex == MaterialSlotIndex && PreviewState.UVChannelIndex == UVChannelIndex)
        {
            if (PreviewState.SourceTexture.Get() != SourceTexture)
            {
                PreviewState.SourceTexture = SourceTexture;
                PreviewState.bDirty = true;
            }
            return &PreviewState;
        }
    }

    FWetWrinkleAccumulatedPreviewState& NewState = AccumulatedPreviewStates.AddDefaulted_GetRef();
    NewState.SourceTexture = SourceTexture;
    NewState.MaterialSlotIndex = MaterialSlotIndex;
    NewState.UVChannelIndex = UVChannelIndex;
    NewState.TextureSize = ComputeWetWrinklePreviewTextureSize(SourceTexture);
    NewState.bDirty = true;
    return &NewState;
}

UTexture2D* SWetWrinkleViewport::ResolveAccumulatedPreviewTexture(UTexture* SourceTexture, int32 MaterialSlotIndex, int32 UVChannelIndex)
{
    FWetWrinkleAccumulatedPreviewState* PreviewState = FindOrAddAccumulatedPreviewState(SourceTexture, MaterialSlotIndex, UVChannelIndex);
    if (PreviewState == nullptr)
    {
        return nullptr;
    }

    if (PreviewState->bDirty && !RebuildAccumulatedPreviewTexture(*PreviewState))
    {
        return nullptr;
    }

    return PreviewState->AccumulatedNormalTexture;
}

bool SWetWrinkleViewport::RebuildAccumulatedPreviewTexture(FWetWrinkleAccumulatedPreviewState& PreviewState)
{
    PreviewState.bDirty = false;

    if (PreviewState.MaterialSlotIndex == INDEX_NONE || PreviewState.UVChannelIndex < 0)
    {
        PreviewState.AccumulatedNormalTexture = nullptr;
        return false;
    }

    PreviewState.TextureSize = ComputeWetWrinklePreviewTextureSize(PreviewState.SourceTexture);
    if (PreviewState.TextureSize.X <= 0 || PreviewState.TextureSize.Y <= 0)
    {
        PreviewState.AccumulatedNormalTexture = nullptr;
        return false;
    }

    TArray<FColor> Pixels;
    Pixels.Init(EncodeWetWrinkleNormal(FVector(0.0f, 0.0f, 1.0f)), PreviewState.TextureSize.X * PreviewState.TextureSize.Y);

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset != nullptr)
    {
        for (const FWetWrinklePatchStroke& Stroke : Asset->WrinkleData.EditablePatchStrokes)
        {
            if (!Stroke.bEnabled)
            {
                continue;
            }

            for (const FWetWrinklePatchPlacement& Stamp : Stroke.PatchPlacements)
            {
                if (Stamp.MaterialSlotIndex != PreviewState.MaterialSlotIndex || Stamp.UVChannelIndex != PreviewState.UVChannelIndex)
                {
                    continue;
                }

                RasterizeWetWrinkleAccumulatedStamp(Stamp, PreviewState.TextureSize, Pixels);
            }
        }
    }

    const bool bNeedsNewTexture = PreviewState.AccumulatedNormalTexture == nullptr ||
                                  PreviewState.AccumulatedNormalTexture->GetSizeX() != PreviewState.TextureSize.X ||
                                  PreviewState.AccumulatedNormalTexture->GetSizeY() != PreviewState.TextureSize.Y;
    if (bNeedsNewTexture)
    {
        PreviewState.AccumulatedNormalTexture =
            UTexture2D::CreateTransient(PreviewState.TextureSize.X, PreviewState.TextureSize.Y, PF_B8G8R8A8);
        if (PreviewState.AccumulatedNormalTexture == nullptr)
        {
            return false;
        }

        PreviewState.AccumulatedNormalTexture->SRGB = false;
        PreviewState.AccumulatedNormalTexture->CompressionSettings = TC_Normalmap;
        PreviewState.AccumulatedNormalTexture->MipGenSettings = TMGS_NoMipmaps;
        PreviewState.AccumulatedNormalTexture->Filter = TF_Bilinear;
        PreviewState.AccumulatedNormalTexture->AddressX = TA_Wrap;
        PreviewState.AccumulatedNormalTexture->AddressY = TA_Wrap;
        PreviewState.AccumulatedNormalTexture->LODGroup = TEXTUREGROUP_WorldNormalMap;
    }

    FTexture2DMipMap& Mip = PreviewState.AccumulatedNormalTexture->GetPlatformData()->Mips[0];
    void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();
    PreviewState.AccumulatedNormalTexture->UpdateResource();
    return true;
}

int32 SWetWrinkleViewport::ResolveActivePreviewMaterialSlot() const
{
    if (PreviewMaterialSlots.IsValidIndex(BrushSettings.MaterialSlotIndex))
    {
        return BrushSettings.MaterialSlotIndex;
    }

    return CurrentSurfaceHit.bHit && PreviewMaterialSlots.IsValidIndex(CurrentSurfaceHit.MaterialSlotIndex)
               ? CurrentSurfaceHit.MaterialSlotIndex
               : INDEX_NONE;
}

void SWetWrinkleViewport::ApplyMaterialSlotVisibility()
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

void SWetWrinkleViewport::RebuildHitTriangles()
{
    HitTriangles.Reset();
    HitTriangleUVChannelIndex = INDEX_NONE;

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    if (TargetMesh == nullptr || BrushSettings.UVChannelIndex == INDEX_NONE)
    {
        return;
    }

    auto AppendHitTrianglesForUVChannel = [this, TargetMesh](int32 UVChannelIndex)
    {
        const int32 MaterialCount = TargetMesh->GetMaterials().Num();
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialCount; ++MaterialSlotIndex)
        {
            if (BrushSettings.MaterialSlotIndex != INDEX_NONE && BrushSettings.MaterialSlotIndex != MaterialSlotIndex)
            {
                continue;
            }

            TArray<FWetClothingAssetUVIsland> Islands;
            if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(TargetMesh, 0, UVChannelIndex, MaterialSlotIndex, Islands, nullptr))
            {
                continue;
            }

            for (const FWetClothingAssetUVIsland& Island : Islands)
            {
                HitTriangles.Append(Island.UVTriangles);
            }
        }
    };

    AppendHitTrianglesForUVChannel(BrushSettings.UVChannelIndex);
    if (HitTriangles.Num() > 0)
    {
        HitTriangleUVChannelIndex = BrushSettings.UVChannelIndex;
    }
}

void SWetWrinkleViewport::HandleSurfaceHitFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (AreWetWrinkleSurfaceHitsEquivalentForPreview(CurrentSurfaceHit, SurfaceHit))
    {
        return;
    }

    CurrentSurfaceHit = SurfaceHit;
    RefreshBrushCursor();
    RefreshWrinklePreviewMaterials();

    if (OnSurfaceHitChanged.IsBound())
    {
        OnSurfaceHitChanged.Execute(CurrentSurfaceHit);
    }
}

void SWetWrinkleViewport::BeginPaintStrokeFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (OnPaintStrokeStarted.IsBound())
    {
        OnPaintStrokeStarted.Execute(SurfaceHit);
    }
}

void SWetWrinkleViewport::RequestPaintStampFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (OnPaintStampRequested.IsBound())
    {
        OnPaintStampRequested.Execute(SurfaceHit);
    }
}

void SWetWrinkleViewport::EndPaintStrokeFromClient()
{
    if (OnPaintStrokeEnded.IsBound())
    {
        OnPaintStrokeEnded.Execute();
    }
}

void SWetWrinkleViewport::RefreshBrushCursor()
{
    if (BrushCursorComponent == nullptr)
    {
        return;
    }

    BrushCursorComponent->SetVisibility(false, true);
    BrushCursorComponent->ClearAllMeshSections();
    BrushCursorComponent->MarkRenderStateDirty();

    if (!BrushSettings.bShowPreview || !CurrentSurfaceHit.bHit)
    {
        return;
    }

    const float Radius = CalculateBrushCursorWorldRadius();
    if (Radius <= UE_SMALL_NUMBER)
    {
        return;
    }

    const float InnerRadius = Radius * 0.92f;
    const float NormalOffset = FMath::Max(Radius * 0.12f, 1.0f);
    TArray<FVector> RingVertices;
    TArray<int32> RingIndices;
    TArray<FVector> RingNormals;
    TArray<FVector2D> RingUVs;
    TArray<FLinearColor> RingVertexColors;
    TArray<FProcMeshTangent> RingTangents;

    const FLinearColor CursorColor(1.0f, 0.55f, 0.08f, 1.0f);
    const FVector SurfacePosition = CurrentSurfaceHit.WorldPosition;
    FVector SurfaceNormal = CurrentSurfaceHit.WorldNormal.GetSafeNormal();
    if (SurfaceNormal.IsNearlyZero())
    {
        SurfaceNormal = FVector::UpVector;
    }

    FVector SurfaceTangent = CurrentSurfaceHit.WorldTangent.GetSafeNormal();
    FVector SurfaceBitangent = CurrentSurfaceHit.WorldBitangent.GetSafeNormal();
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

    AppendWetWrinkleRingMesh(
        RingVertices,
        RingIndices,
        RingNormals,
        RingUVs,
        RingVertexColors,
        RingTangents,
        SurfacePosition + SurfaceNormal * NormalOffset,
        SurfaceNormal,
        SurfaceTangent,
        SurfaceBitangent,
        Radius,
        InnerRadius,
        CursorColor);

    if (RingVertices.Num() > 0)
    {
        BrushCursorComponent->SetMaterial(0, ResolveCursorMaterial());
        BrushCursorComponent->CreateMeshSection_LinearColor(
            0,
            RingVertices,
            RingIndices,
            RingNormals,
            RingUVs,
            RingVertexColors,
            RingTangents,
            false,
            false);
    }

    BrushCursorComponent->SetVisibility(true, true);
    BrushCursorComponent->MarkRenderStateDirty();
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
}

void SWetWrinkleViewport::ClearBrushCursor()
{
    if (BrushCursorComponent != nullptr)
    {
        BrushCursorComponent->SetVisibility(false, true);
        BrushCursorComponent->ClearAllMeshSections();
        BrushCursorComponent->MarkRenderStateDirty();
        if (ViewportClient.IsValid())
        {
            ViewportClient->Invalidate();
        }
    }
}

UMaterialInterface* SWetWrinkleViewport::ResolveCursorMaterial()
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

float SWetWrinkleViewport::CalculateBrushCursorWorldRadius() const
{
    if (PreviewMeshComponent == nullptr || PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return 5.0f;
    }

    const FBoxSphereBounds Bounds = PreviewMeshComponent->CalcBounds(PreviewMeshComponent->GetComponentTransform());
    const float MeshRadius = FMath::Max(1.0f, static_cast<float>(Bounds.SphereRadius));
    return FMath::Clamp(MeshRadius * BrushSettings.BrushRadiusUV, 0.25f, MeshRadius * 0.35f);
}

FText SWetWrinkleViewport::GetViewportHintText() const
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

void SWetWrinkleViewport::FindProjectedSurfacesAtUV(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex,
    const FVector2D& UV,
    TArray<FWetWrinkleProjectedSurface>& OutSurfaces) const
{
    OutSurfaces.Reset();

    if (PreviewMeshComponent == nullptr || UVChannelIndex != HitTriangleUVChannelIndex)
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

bool SWetWrinkleViewport::TryProjectUVToWorld(
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
