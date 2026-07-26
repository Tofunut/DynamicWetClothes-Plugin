#include "SDWCPartViewport.h"

#include "DataAssets/WetClothingAsset.h"
#include "DWCPartViewportClient.h"
#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StaticParameterSet.h"
#include "WetRendering/DWCGPUResourceSubsystem.h"
#include "WetRendering/WetMaterialParameters.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ProceduralMeshComponent.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetPartDataTextureBaker.h"
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"
#include "WetnessProfile/Viewport/WetnessProfilePreviewMaterial.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetViewport"

namespace
{
    constexpr int32 ForceRenderLOD0 = 1; // USkinnedMeshComponent forced LOD is 1-based; 0 means automatic.

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


    constexpr float SurfacePreviewMinDetailSize = 0.25f;
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

    bool CreateOrUpdateSurfacePreviewFloatTexture(
        TObjectPtr<UTexture2D>& Texture,
        const TArray<FLinearColor>& Pixels,
        const int32 Width,
        const int32 Height,
        const TextureFilter Filter)
    {
        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            return false;
        }

        if (Texture == nullptr || Texture->GetSizeX() != Width || Texture->GetSizeY() != Height ||
            Texture->GetPixelFormat() != PF_A32B32G32R32F)
        {
            Texture = UTexture2D::CreateTransient(Width, Height, PF_A32B32G32R32F, NAME_None);
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

    bool ConfigureSurfacePreviewStaticPermutation(
        UMaterialInstanceConstant* Instance,
        UMaterialInterface* Parent,
        FString& OutErrorMessage)
    {
        if (Instance == nullptr || Parent == nullptr)
        {
            OutErrorMessage = TEXT("Could not create the Surface Water preview static material instance.");
            return false;
        }

        Instance->SetParentEditorOnly(Parent);

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid> ParameterIds;
        Parent->GetAllStaticSwitchParameterInfo(ParameterInfos, ParameterIds);

        struct FDesiredSwitch
        {
            FName Name;
            bool Value;
        };
        const FDesiredSwitch DesiredSwitches[] = {
            { FName(TEXT("DWC_UseGPUBackend")), true },
            { DWCWetMaterialParameters::UseSurfaceWater(), true }
        };
        const FName LegacyProfileSwitches[] = {
            DWCWetMaterialParameters::UseDropletNormal(),
            DWCWetMaterialParameters::UseRivuletNormal()
        };

        FStaticParameterSet StaticParameters = Instance->GetStaticParameters();
        StaticParameters.StaticSwitchParameters.RemoveAll(
            [&LegacyProfileSwitches](const FStaticSwitchParameter& Parameter)
            {
                for (const FName& LegacyName : LegacyProfileSwitches)
                {
                    if (Parameter.ParameterInfo.Name == LegacyName)
                    {
                        return true;
                    }
                }
                return false;
            });

        for (const FDesiredSwitch& Desired : DesiredSwitches)
        {
            int32 ParameterIndex = INDEX_NONE;
            for (int32 Index = 0; Index < ParameterInfos.Num(); ++Index)
            {
                if (ParameterInfos[Index].Name == Desired.Name &&
                    ParameterInfos[Index].Association == EMaterialParameterAssociation::GlobalParameter)
                {
                    ParameterIndex = Index;
                    break;
                }
            }

            if (ParameterIndex == INDEX_NONE || !ParameterIds.IsValidIndex(ParameterIndex) ||
                !ParameterIds[ParameterIndex].IsValid())
            {
                OutErrorMessage = FString::Printf(
                    TEXT("The generated material does not expose the required static switch '%s'. Regenerate its DWC materials."),
                    *Desired.Name.ToString());
                return false;
            }

            FStaticSwitchParameter* Existing = StaticParameters.StaticSwitchParameters.FindByPredicate(
                [&](const FStaticSwitchParameter& Parameter)
                {
                    return Parameter.ParameterInfo == ParameterInfos[ParameterIndex];
                });
            if (Existing != nullptr)
            {
                Existing->Value = Desired.Value;
                Existing->bOverride = true;
                Existing->ExpressionGUID = ParameterIds[ParameterIndex];
            }
            else
            {
                StaticParameters.StaticSwitchParameters.Add(FStaticSwitchParameter(
                    ParameterInfos[ParameterIndex],
                    Desired.Value,
                    true,
                    ParameterIds[ParameterIndex]));
            }
        }

        Instance->UpdateStaticPermutation(StaticParameters, nullptr);
        Instance->UpdateCachedData();
        Instance->PostEditChange();
        OutErrorMessage.Reset();
        return true;
    }

    UTexture* LoadSurfacePreviewDefaultNormalTexture()
    {
        if (UTexture* DefaultNormal = LoadObject<UTexture>(
                nullptr,
                TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal")))
        {
            return DefaultNormal;
        }

        return LoadObject<UTexture>(
            nullptr,
            TEXT("/Engine/EngineMaterials/T_Default_Normal.T_Default_Normal"));
    }

    UTexture* LoadSurfacePreviewDefaultStateTexture()
    {
        if (UTexture* Black = LoadObject<UTexture>(
                nullptr,
                TEXT("/Engine/EngineResources/Black.Black")))
        {
            return Black;
        }

        return LoadObject<UTexture>(
            nullptr,
            TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
    }

    UMaterialExpressionScalarParameter* CreateSurfacePreviewScalarParameter(
        UMaterial* Material,
        const FName ParameterName,
        const float DefaultValue,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionScalarParameter::StaticClass(),
                NodeX,
                NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->DefaultValue = DefaultValue;
        }
        return Parameter;
    }

    UMaterialExpressionTextureObjectParameter* CreateSurfacePreviewTextureParameter(
        UMaterial* Material,
        const FName ParameterName,
        UTexture* DefaultTexture,
        const EMaterialSamplerType SamplerType,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionTextureObjectParameter* Parameter = Cast<UMaterialExpressionTextureObjectParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTextureObjectParameter::StaticClass(),
                NodeX,
                NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SamplerType;
            Parameter->Texture = DefaultTexture;
        }
        return Parameter;
    }

    UMaterialExpressionTextureCoordinate* CreateSurfacePreviewTexCoord(
        UMaterial* Material,
        const int32 CoordinateIndex,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionTextureCoordinate* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTextureCoordinate::StaticClass(),
                NodeX,
                NodeY));
        if (TexCoord != nullptr)
        {
            TexCoord->CoordinateIndex = CoordinateIndex;
        }
        return TexCoord;
    }

    UMaterialExpressionCustom* CreateSurfacePreviewCustomExpression(
        UMaterial* Material,
        const TCHAR* Description,
        const FString& Code,
        const ECustomMaterialOutputType OutputType,
        const TArray<FName>& InputNames,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionCustom::StaticClass(),
                NodeX,
                NodeY));
        if (Custom == nullptr)
        {
            return nullptr;
        }

        Custom->Description = Description;
        Custom->Code = Code;
        Custom->OutputType = OutputType;
        Custom->Inputs.Reset();
        for (const FName InputName : InputNames)
        {
            FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
            Input.InputName = InputName;
        }
        return Custom;
    }

    bool ConnectSurfacePreviewExpression(
        UMaterialExpression* FromExpression,
        UMaterialExpression* ToExpression,
        const TCHAR* ToInputName)
    {
        return FromExpression != nullptr &&
               ToExpression != nullptr &&
               UMaterialEditingLibrary::ConnectMaterialExpressions(
                   FromExpression,
                   FString(),
                   ToExpression,
                   ToInputName);
    }

    bool BuildSurfaceWaterTilingPreviewMaterialGraph(
        UMaterial* Material,
        const int32 DWCDataUVChannel,
        const int32 SurfaceWaterNormalUVChannel)
    {
        if (Material == nullptr || DWCDataUVChannel < 0 || SurfaceWaterNormalUVChannel < 0)
        {
            return false;
        }

        Material->BlendMode = BLEND_Opaque;
        Material->TwoSided = true;
        Material->SetShadingModel(MSM_DefaultLit);

        UMaterialExpressionTextureCoordinate* DWCDataUV =
            CreateSurfacePreviewTexCoord(Material, DWCDataUVChannel, -1700, -280);
        UMaterialExpressionTextureCoordinate* SurfaceNormalUV =
            CreateSurfacePreviewTexCoord(Material, SurfaceWaterNormalUVChannel, -1700, -120);

        UTexture* DefaultNormal = LoadSurfacePreviewDefaultNormalTexture();
        UTexture* DefaultStateTexture = LoadSurfacePreviewDefaultStateTexture();
        UMaterialExpressionTextureObjectParameter* DropletRT = CreateSurfacePreviewTextureParameter(
            Material,
            DWCWetMaterialParameters::SurfaceDropletRT(),
            DefaultStateTexture,
            SAMPLERTYPE_LinearColor,
            -1700,
            60);
        UMaterialExpressionTextureObjectParameter* RivuletRT = CreateSurfacePreviewTextureParameter(
            Material,
            DWCWetMaterialParameters::SurfaceRivuletRT(),
            DefaultStateTexture,
            SAMPLERTYPE_LinearColor,
            -1700,
            220);
        UMaterialExpressionTextureObjectParameter* DropletNormal = CreateSurfacePreviewTextureParameter(
            Material,
            DWCWetnessProfilePreviewMaterial::DropletNormalTextureParameter,
            DefaultNormal,
            SAMPLERTYPE_Normal,
            -1700,
            400);
        UMaterialExpressionTextureObjectParameter* RivuletNormal = CreateSurfacePreviewTextureParameter(
            Material,
            DWCWetnessProfilePreviewMaterial::RivuletNormalTextureParameter,
            DefaultNormal,
            SAMPLERTYPE_Normal,
            -1700,
            560);

        UMaterialExpressionScalarParameter* SurfaceTime = CreateSurfacePreviewScalarParameter(
            Material, DWCWetMaterialParameters::SurfaceWaterTime(), 0.0f, -1700, 760);
        UMaterialExpressionScalarParameter* SurfaceEnabled = CreateSurfacePreviewScalarParameter(
            Material, DWCWetnessProfilePreviewMaterial::SurfaceEnabledParameter, 1.0f, -1700, 860);
        UMaterialExpressionScalarParameter* DropletsEnabled = CreateSurfacePreviewScalarParameter(
            Material, DWCWetnessProfilePreviewMaterial::DropletsEnabledParameter, 1.0f, -1700, 960);
        UMaterialExpressionScalarParameter* RivuletsEnabled = CreateSurfacePreviewScalarParameter(
            Material, DWCWetnessProfilePreviewMaterial::RivuletsEnabledParameter, 1.0f, -1700, 1060);
        UMaterialExpressionScalarParameter* SurfaceNormalStrength = CreateSurfacePreviewScalarParameter(
            Material, DWCWetnessProfilePreviewMaterial::SurfaceNormalStrengthParameter, 2.0f, -1700, 1160);
        UMaterialExpressionScalarParameter* SurfaceRoughnessStrength = CreateSurfacePreviewScalarParameter(
            Material, DWCWetnessProfilePreviewMaterial::SurfaceRoughnessStrengthParameter, 0.8f, -1700, 1260);
        UMaterialExpressionScalarParameter* SurfaceVisibilityThreshold = CreateSurfacePreviewScalarParameter(
            Material, DWCWetnessProfilePreviewMaterial::SurfaceVisibilityThresholdParameter, 0.2f, -1700, 1360);
        UMaterialExpressionScalarParameter* SurfaceTargetRoughness = CreateSurfacePreviewScalarParameter(
            Material, DWCWetMaterialParameters::SurfaceWaterTargetRoughness(), 0.05f, -1700, 1460);
        UMaterialExpressionScalarParameter* RivuletScrollSpeed = CreateSurfacePreviewScalarParameter(
            Material, DWCWetnessProfilePreviewMaterial::RivuletScrollSpeedParameter, 0.0f, -1700, 1560);
        UMaterialExpressionScalarParameter* DropletDetailSize = CreateSurfacePreviewScalarParameter(
            Material, DWCWetnessProfilePreviewMaterial::DropletDetailSizeParameter, 1.0f, -1700, 1660);
        UMaterialExpressionScalarParameter* RivuletDetailSize = CreateSurfacePreviewScalarParameter(
            Material, DWCWetnessProfilePreviewMaterial::RivuletDetailSizeParameter, 1.0f, -1700, 1760);

        const FString SharedCoverageCode = TEXT(R"(
float DropletAmount = Texture2DSampleLevel(DropletRT, DropletRTSampler, DWCDataUV, 0).r;
float4 RivuletState = Texture2DSampleLevel(RivuletRT, RivuletRTSampler, DWCDataUV, 0);
float RivuletAmount = RivuletState.r;
float Threshold = saturate(SurfaceVisibilityThreshold);
float Feather = 0.03;
float DropletCoverage = smoothstep(Threshold, Threshold + Feather, DropletAmount) * saturate(SurfaceEnabled) * saturate(DropletsEnabled);
float RivuletCoverage = smoothstep(Threshold, Threshold + Feather, RivuletAmount) * saturate(SurfaceEnabled) * saturate(RivuletsEnabled);
float Coverage = max(DropletCoverage, RivuletCoverage);
)");

        UMaterialExpressionCustom* BaseColorExpression = CreateSurfacePreviewCustomExpression(
            Material,
            TEXT("DWC WCA Surface Water Tiling Preview Base Color"),
            FString(TEXT(R"(
)")) + SharedCoverageCode + TEXT(R"(
return float3(0.46, 0.48, 0.50);
)"),
            CMOT_Float3,
            {
                TEXT("DWCDataUV"),
                TEXT("DropletRT"),
                TEXT("RivuletRT"),
                TEXT("SurfaceEnabled"),
                TEXT("DropletsEnabled"),
                TEXT("RivuletsEnabled"),
                TEXT("SurfaceVisibilityThreshold"),
            },
            -760,
            -300);

        UMaterialExpressionCustom* RoughnessExpression = CreateSurfacePreviewCustomExpression(
            Material,
            TEXT("DWC WCA Surface Water Tiling Preview Roughness"),
            FString(TEXT(R"(
)")) + SharedCoverageCode + TEXT(R"(
return lerp(0.62, SurfaceWaterTargetRoughness, saturate(Coverage * SurfaceRoughnessStrength));
)"),
            CMOT_Float1,
            {
                TEXT("DWCDataUV"),
                TEXT("DropletRT"),
                TEXT("RivuletRT"),
                TEXT("SurfaceEnabled"),
                TEXT("DropletsEnabled"),
                TEXT("RivuletsEnabled"),
                TEXT("SurfaceVisibilityThreshold"),
                TEXT("SurfaceRoughnessStrength"),
                TEXT("SurfaceWaterTargetRoughness"),
            },
            -760,
            120);

        UMaterialExpressionCustom* NormalExpression = CreateSurfacePreviewCustomExpression(
            Material,
            TEXT("DWC WCA Surface Water Tiling Preview Normal"),
            FString(TEXT(R"(
)")) + SharedCoverageCode + TEXT(R"(
float2 DropletUV = frac(SurfaceWaterNormalUV / max(DropletDetailSize, 1.0e-4));
float2 RivuletUV = frac(SurfaceWaterNormalUV / max(RivuletDetailSize, 1.0e-4) + float2(0.0, SurfaceTime * RivuletScrollSpeed * 0.08));

float2 DropletXY = Texture2DSampleLevel(DropletNormalTex, DropletNormalTexSampler, DropletUV, 0).rg * 2.0 - 1.0;
float2 RivuletXY = Texture2DSampleLevel(RivuletNormalTex, RivuletNormalTexSampler, RivuletUV, 0).rg * 2.0 - 1.0;

float Strength = clamp(SurfaceNormalStrength * 1.5, 0.0, 12.0);
float2 CombinedXY = DropletXY * DropletCoverage;
CombinedXY += RivuletXY * RivuletCoverage;
CombinedXY *= Strength;
return normalize(float3(CombinedXY, 1.0));
)"),
            CMOT_Float3,
            {
                TEXT("DWCDataUV"),
                TEXT("SurfaceWaterNormalUV"),
                TEXT("DropletRT"),
                TEXT("RivuletRT"),
                TEXT("DropletNormalTex"),
                TEXT("RivuletNormalTex"),
                TEXT("SurfaceTime"),
                TEXT("SurfaceEnabled"),
                TEXT("DropletsEnabled"),
                TEXT("RivuletsEnabled"),
                TEXT("SurfaceNormalStrength"),
                TEXT("SurfaceVisibilityThreshold"),
                TEXT("RivuletScrollSpeed"),
                TEXT("DropletDetailSize"),
                TEXT("RivuletDetailSize"),
            },
            -760,
            540);

        bool bConnected = true;
        bConnected &= ConnectSurfacePreviewExpression(DWCDataUV, BaseColorExpression, TEXT("DWCDataUV"));
        bConnected &= ConnectSurfacePreviewExpression(DropletRT, BaseColorExpression, TEXT("DropletRT"));
        bConnected &= ConnectSurfacePreviewExpression(RivuletRT, BaseColorExpression, TEXT("RivuletRT"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceEnabled, BaseColorExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectSurfacePreviewExpression(DropletsEnabled, BaseColorExpression, TEXT("DropletsEnabled"));
        bConnected &= ConnectSurfacePreviewExpression(RivuletsEnabled, BaseColorExpression, TEXT("RivuletsEnabled"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceVisibilityThreshold, BaseColorExpression, TEXT("SurfaceVisibilityThreshold"));

        bConnected &= ConnectSurfacePreviewExpression(DWCDataUV, RoughnessExpression, TEXT("DWCDataUV"));
        bConnected &= ConnectSurfacePreviewExpression(DropletRT, RoughnessExpression, TEXT("DropletRT"));
        bConnected &= ConnectSurfacePreviewExpression(RivuletRT, RoughnessExpression, TEXT("RivuletRT"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceEnabled, RoughnessExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectSurfacePreviewExpression(DropletsEnabled, RoughnessExpression, TEXT("DropletsEnabled"));
        bConnected &= ConnectSurfacePreviewExpression(RivuletsEnabled, RoughnessExpression, TEXT("RivuletsEnabled"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceVisibilityThreshold, RoughnessExpression, TEXT("SurfaceVisibilityThreshold"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceRoughnessStrength, RoughnessExpression, TEXT("SurfaceRoughnessStrength"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceTargetRoughness, RoughnessExpression, TEXT("SurfaceWaterTargetRoughness"));

        bConnected &= ConnectSurfacePreviewExpression(DWCDataUV, NormalExpression, TEXT("DWCDataUV"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceNormalUV, NormalExpression, TEXT("SurfaceWaterNormalUV"));
        bConnected &= ConnectSurfacePreviewExpression(DropletRT, NormalExpression, TEXT("DropletRT"));
        bConnected &= ConnectSurfacePreviewExpression(RivuletRT, NormalExpression, TEXT("RivuletRT"));
        bConnected &= ConnectSurfacePreviewExpression(DropletNormal, NormalExpression, TEXT("DropletNormalTex"));
        bConnected &= ConnectSurfacePreviewExpression(RivuletNormal, NormalExpression, TEXT("RivuletNormalTex"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceTime, NormalExpression, TEXT("SurfaceTime"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceEnabled, NormalExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectSurfacePreviewExpression(DropletsEnabled, NormalExpression, TEXT("DropletsEnabled"));
        bConnected &= ConnectSurfacePreviewExpression(RivuletsEnabled, NormalExpression, TEXT("RivuletsEnabled"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceNormalStrength, NormalExpression, TEXT("SurfaceNormalStrength"));
        bConnected &= ConnectSurfacePreviewExpression(SurfaceVisibilityThreshold, NormalExpression, TEXT("SurfaceVisibilityThreshold"));
        bConnected &= ConnectSurfacePreviewExpression(RivuletScrollSpeed, NormalExpression, TEXT("RivuletScrollSpeed"));
        bConnected &= ConnectSurfacePreviewExpression(DropletDetailSize, NormalExpression, TEXT("DropletDetailSize"));
        bConnected &= ConnectSurfacePreviewExpression(RivuletDetailSize, NormalExpression, TEXT("RivuletDetailSize"));

        bConnected &= BaseColorExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(BaseColorExpression, FString(), MP_BaseColor);
        bConnected &= RoughnessExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(RoughnessExpression, FString(), MP_Roughness);
        bConnected &= NormalExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(NormalExpression, FString(), MP_Normal);
        if (!bConnected)
        {
            return false;
        }

        Material->UpdateCachedExpressionData();
        const TArray<FString> CompileErrors = UMaterialEditingLibrary::RecompileMaterial(Material);
        return CompileErrors.IsEmpty();
    }

    int32 ResolveSurfacePreviewNormalUVChannel(const UWetClothingAsset& Asset, const int32 MaterialSlotIndex)
    {
        const FWetClothingAuthoredMaterialSlot* AuthoredSlot =
            Asset.Authored.PartData.EditableWetPartData.FindMaterialSlot(MaterialSlotIndex);
        if (AuthoredSlot != nullptr && AuthoredSlot->SurfaceWater.SurfaceWaterNormalUVChannel != INDEX_NONE)
        {
            return AuthoredSlot->SurfaceWater.SurfaceWaterNormalUVChannel;
        }
        return Asset.GetOriginalUVChannelIndex();
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
    PreviewMeshComponent->SetForcedLOD(ForceRenderLOD0);
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
    Collector.AddReferencedObject(SurfacePreviewRivuletRT);
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
    PreviewMeshComponent->SetForcedLOD(ForceRenderLOD0);
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
    SurfaceWaterPreviewDataUVChannel = INDEX_NONE;
    SurfaceWaterPreviewNormalUVChannel = INDEX_NONE;
    SurfaceWaterPreviewStatus.Reset();
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
    PreviewMaterialSlotIndex = MaterialSlotIndex;
    PreviewWetPartID = WetPartID;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
}

void SDWCPartViewport::SetPreviewWetness(const float AbsorbedWetness, const float SurfaceWater)
{
    PreviewAbsorbedWetness = FMath::Clamp(AbsorbedWetness, 0.0f, 1.0f);
    PreviewSurfaceWater = FMath::Clamp(SurfaceWater, 0.0f, 1.0f);
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
}

void SDWCPartViewport::SetSurfaceWaterTilingPreviewCoverageMode(
    const EDWCSurfaceWaterTilingPreviewCoverageMode InMode)
{
    SurfaceWaterPreviewCoverageMode = InMode;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
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
    // selected slot's generated material. Procedural geometry remains editor-overlay only.
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
    const bool bIsolateSelectedSlot =
        CurrentHighlightedMaterialSlot != INDEX_NONE &&
        !bSurfaceWaterTilingPreview;
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
            Section.MaterialIndex == CurrentHighlightedMaterialSlot,
            0);
    }
    PreviewMeshComponent->MarkRenderStateDirty();
}

bool SDWCPartViewport::BuildSurfaceWaterPreviewTextures(FString& OutErrorMessage)
{
    SurfacePreviewLocalProfileID = 0;

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || PreviewMaterialSlotIndex == INDEX_NONE || PreviewWetPartID <= 0)
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

    const FWetClothingBakedWetPartData& Baked = Asset->Derived.Inline.BakedWetPartData;
    const FWetClothingBakedWetPartDataSlotTexture* BakedSlot = Baked.FindSlot(PreviewMaterialSlotIndex);
    if (BakedSlot == nullptr || BakedSlot->WetPartDataTexture == nullptr)
    {
        OutErrorMessage = TEXT("Bake Render Profile Data before opening the Surface Water Tiling preview.");
        return false;
    }

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

    TSet<int32> SelectedTriangleIDs;
    for (const FWetClothingAssetUVIsland& Island : CurrentSelectableIslands)
    {
        const int32* AssignedWetPartID = CurrentWetPartIslandAssignments.Find(Island.UVIslandID);
        if (AssignedWetPartID == nullptr || *AssignedWetPartID != PreviewWetPartID)
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
        OutErrorMessage = TEXT("The selected Wet Part's profile is not present in the baked Render Profile Data. Re-bake Render Profile Data.");
        return false;
    }

    SurfacePreviewLocalProfileID = static_cast<int32>(LocalProfileID);

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
    const FVector2D SingleCircleCenter(
        (static_cast<float>(MinSelectedX) + static_cast<float>(MaxSelectedX)) * 0.5f,
        (static_cast<float>(MinSelectedY) + static_cast<float>(MaxSelectedY)) * 0.5f);
    const float SingleCircleRadiusPixels = FMath::Clamp(7.0f * Part->SurfaceWater.DropletRadiusScale, 2.0f, 32.0f);

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

    TArray<FColor> PreviewPartDataPixels = SourcePartDataPixels;
    TArray<FLinearColor> WetnessPixels;
    TArray<FLinearColor> DropletPixels;
    TArray<FLinearColor> RivuletPixels;
    WetnessPixels.Init(FLinearColor::Black, Width * Height);
    DropletPixels.Init(FLinearColor::Black, Width * Height);
    RivuletPixels.Init(FLinearColor::Black, Width * Height);

    const float SurfaceAmount = FMath::Clamp(PreviewSurfaceWater, 0.0f, 1.0f);
    const uint8 DropletDetailSize = EncodeSurfacePreviewDetailSize(Part->SurfaceWater.DropletDetailSize);
    const uint8 RivuletDetailSize = EncodeSurfacePreviewDetailSize(Part->SurfaceWater.RivuletDetailSize);

    for (int32 Y = 0; Y < Height; ++Y)
    {
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 PixelIndex = Y * Width + X;
            if (SelectedMask[PixelIndex] == 0)
            {
                continue;
            }

            PreviewPartDataPixels[PixelIndex].R = LocalProfileID;
            PreviewPartDataPixels[PixelIndex].G = DropletDetailSize;
            PreviewPartDataPixels[PixelIndex].B = RivuletDetailSize;
            PreviewPartDataPixels[PixelIndex].A = 0;
            WetnessPixels[PixelIndex] = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);

            if (SurfaceWaterPreviewCoverageMode == EDWCSurfaceWaterTilingPreviewCoverageMode::SingleCircle)
            {
                const float DistanceSquared = FVector2D::DistSquared(
                    FVector2D(static_cast<float>(X), static_cast<float>(Y)),
                    SingleCircleCenter);
                if (DistanceSquared > FMath::Square(SingleCircleRadiusPixels))
                {
                    continue;
                }
            }

            DropletPixels[PixelIndex] = FLinearColor(SurfaceAmount, 0.0f, 60.0f, 1.0f);
            RivuletPixels[PixelIndex] = FLinearColor(SurfaceAmount, 0.0f, 60.0f, 0.75f);
        }
    }

    if (!CreateOrUpdateSurfacePreviewByteTexture(SurfacePreviewWetPartDataTexture, PreviewPartDataPixels, Width, Height) ||
        !CreateOrUpdateSurfacePreviewFloatTexture(SurfacePreviewWetnessMap, WetnessPixels, Width, Height, TF_Nearest) ||
        !CreateOrUpdateSurfacePreviewFloatTexture(SurfacePreviewDropletRT, DropletPixels, Width, Height, TF_Bilinear) ||
        !CreateOrUpdateSurfacePreviewFloatTexture(SurfacePreviewRivuletRT, RivuletPixels, Width, Height, TF_Bilinear))
    {
        OutErrorMessage = TEXT("Could not create the transient Surface Water preview textures.");
        return false;
    }

    OutErrorMessage.Reset();
    return true;
}

void SDWCPartViewport::RefreshSurfaceWaterPreviewMaterial()
{
    if (!bSurfaceWaterTilingPreview || PreviewMeshComponent == nullptr)
    {
        return;
    }

    RestoreOriginalMaterials();
    SurfaceWaterPreviewStatus.Reset();

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || PreviewMaterialSlotIndex == INDEX_NONE || PreviewWetPartID <= 0)
    {
        SurfaceWaterPreviewStatus = TEXT("Select a Wet Part to preview Surface Water.");
        RequestViewportRedraw();
        return;
    }

    FString TextureError;
    if (!BuildSurfaceWaterPreviewTextures(TextureError))
    {
        SurfaceWaterPreviewStatus = TextureError;
        RequestViewportRedraw();
        return;
    }

    const FWetClothingBakedWetPartData& Baked = Asset->Derived.Inline.BakedWetPartData;
    const int32 LocalProfileIndex = SurfacePreviewLocalProfileID - 1;
    if (!Baked.LocalProfiles.IsValidIndex(LocalProfileIndex))
    {
        SurfaceWaterPreviewStatus = TEXT("The selected Wet Part's baked Surface Water profile could not be resolved.");
        RequestViewportRedraw();
        return;
    }

    const FWetClothingEditableWetPartData& Editable = Asset->Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* Slot = Editable.FindMaterialSlot(PreviewMaterialSlotIndex);
    const FWetClothingWetPartEntry* Part = Slot != nullptr ? Slot->FindPart(PreviewWetPartID) : nullptr;
    if (Slot == nullptr || Part == nullptr)
    {
        SurfaceWaterPreviewStatus = TEXT("The selected Wet Part could not be resolved.");
        RequestViewportRedraw();
        return;
    }

    const FWetClothingGeneratedWetMaterialOverride* MaterialOverride =
        Asset->Derived.Inline.GeneratedWetMaterialOverrides.FindByPredicate(
            [this](const FWetClothingGeneratedWetMaterialOverride& Candidate)
            {
                return Candidate.MaterialSlotIndex == PreviewMaterialSlotIndex;
            });
    UMaterialInstanceConstant* GPUMaterial = MaterialOverride != nullptr
        ? MaterialOverride->GPUMaterialInstance.Get()
        : nullptr;
    if (GPUMaterial == nullptr)
    {
        SurfaceWaterPreviewStatus = TEXT("Generate GPU wet materials for this Material Slot before previewing Surface Water.");
        RequestViewportRedraw();
        return;
    }

    if (SurfaceWaterPreviewMaterial == nullptr || SurfaceWaterPreviewMaterialParent != GPUMaterial)
    {
        SurfaceWaterPreviewMaterialParent = GPUMaterial;
        SurfaceWaterPreviewMaterial = UMaterialInstanceDynamic::Create(
            GPUMaterial,
            GetTransientPackage());
        SurfaceWaterPreviewStaticMaterial = NewObject<UMaterialInstanceConstant>(
            GetTransientPackage(),
            NAME_None,
            RF_Transient);

        FString StaticPermutationError;
        if (!ConfigureSurfacePreviewStaticPermutation(
                SurfaceWaterPreviewStaticMaterial,
                GPUMaterial,
                StaticPermutationError))
        {
            SurfaceWaterPreviewStatus = StaticPermutationError;
            SurfaceWaterPreviewMaterial = nullptr;
            RequestViewportRedraw();
            return;
        }

        SurfaceWaterPreviewMaterial = UMaterialInstanceDynamic::Create(
            SurfaceWaterPreviewStaticMaterial,
            GetTransientPackage());
    }
    if (SurfaceWaterPreviewMaterial == nullptr)
    {
        SurfaceWaterPreviewStatus = TEXT("Could not create the transient generated-material Surface Water preview instance.");
        RequestViewportRedraw();
        return;
    }

    const FWetClothingLocalRenderProfile& LocalProfile = Baked.LocalProfiles[LocalProfileIndex];
    const FSurfaceWaterProfileParameters& Surface = LocalProfile.Parameters.SurfaceWater;

    UDWCGPUResourceSubsystem* ResourceSubsystem = nullptr;
    bool bAppliedSelectedProfileFallback = false;
    if (PreviewScene.IsValid())
    {
        if (UWorld* PreviewWorld = PreviewScene->GetWorld())
        {
            ResourceSubsystem = PreviewWorld->GetSubsystem<UDWCGPUResourceSubsystem>();
        }
    }
    if (ResourceSubsystem != nullptr)
    {
        TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMIDs;
        PreviewMIDs.Init(nullptr, FMath::Max(PreviewMeshComponent->GetNumMaterials(), PreviewMaterialSlotIndex + 1));
        PreviewMIDs[PreviewMaterialSlotIndex] = SurfaceWaterPreviewMaterial;
        ResourceSubsystem->ApplyResourcesToMaterials(
            Asset,
            PreviewMIDs,
            EDWCRenderResourceUsage::FullGPU);
        bAppliedSelectedProfileFallback = ResourceSubsystem->ApplyPreviewRenderProfileFallback(
            Asset,
            PreviewMaterialSlotIndex,
            SurfacePreviewLocalProfileID,
            *SurfaceWaterPreviewMaterial);

        if (ResourceSubsystem->GetDropletNormalArray() == nullptr ||
            ResourceSubsystem->GetRivuletNormalArray() == nullptr)
        {
            SurfaceWaterPreviewStatus = TEXT("Surface Water preview normal resources are incomplete. Coverage still renders, but detail normals may be flat until Render Profile Data is re-baked.");
        }
    }
    else
    {
        SurfaceWaterPreviewStatus = TEXT("Could not initialize DWC GPU render resources for the Surface Water preview world. Coverage still renders with material fallback profile values.");
    }

    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::WetnessMap(),
        SurfacePreviewWetnessMap);
    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::WetPartDataTexture(),
        SurfacePreviewWetPartDataTexture);
    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::SurfaceDropletRT(),
        SurfacePreviewDropletRT);
    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::SurfaceRivuletRT(),
        SurfacePreviewRivuletRT);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterTime(),
        0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::GlobalRenderProfileTexelSize(),
        1.0f / static_cast<float>(UDWCGPUResourceSubsystem::GlobalLUTWidth));
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterTexelSize(),
        SurfacePreviewWetnessMap != nullptr && SurfacePreviewWetnessMap->GetSizeX() > 0
            ? 1.0f / static_cast<float>(SurfacePreviewWetnessMap->GetSizeX())
            : 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::WetPartDebugStrength(),
        0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterDebugStrength(),
        0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterTargetRoughness(),
        0.05f);

    if (PreviewMaterialSlotIndex >= 0 && PreviewMaterialSlotIndex < PreviewMeshComponent->GetNumMaterials())
    {
        PreviewMeshComponent->SetMaterial(PreviewMaterialSlotIndex, SurfaceWaterPreviewMaterial);
    }
    RefreshMaterialSectionVisibility();
    PreviewMeshComponent->MarkRenderStateDirty();

    if (SurfaceWaterPreviewStatus.IsEmpty())
    {
        SurfaceWaterPreviewStatus = bAppliedSelectedProfileFallback
            ? TEXT("Using the selected slot's generated GPU material with the selected Part's baked Surface Water profile and float state maps.")
            : TEXT("Using the selected slot's generated GPU material with a forced GPU Surface Water preview permutation and float state maps.");
    }
    {
        if (SurfaceWaterPreviewCoverageMode == EDWCSurfaceWaterTilingPreviewCoverageMode::SingleCircle)
        {
            SurfaceWaterPreviewStatus += FString::Printf(
                TEXT("\nPreview LocalProfileID %d: SurfaceEnabled=%d Droplets=%d Rivulets=%d NormalStrength=%.3g RoughnessStrength=%.3g. SingleCircleSurface=%g RadiusScale=%.3g AbsorbedWetness=0."),
                SurfacePreviewLocalProfileID,
                Surface.bEnabled ? 1 : 0,
                Surface.bEnableDroplets ? 1 : 0,
                Surface.bEnableRivulets ? 1 : 0,
                Surface.SurfaceWaterNormalStrength,
                Surface.SurfaceWaterRoughnessStrength,
                PreviewSurfaceWater,
                Part->SurfaceWater.DropletRadiusScale);
        }
        else
        {
            SurfaceWaterPreviewStatus += FString::Printf(
                TEXT("\nPreview LocalProfileID %d: SurfaceEnabled=%d Droplets=%d Rivulets=%d NormalStrength=%.3g RoughnessStrength=%.3g. FullPartSurface=%g AbsorbedWetness=0."),
                SurfacePreviewLocalProfileID,
                Surface.bEnabled ? 1 : 0,
                Surface.bEnableDroplets ? 1 : 0,
                Surface.bEnableRivulets ? 1 : 0,
                Surface.SurfaceWaterNormalStrength,
                Surface.SurfaceWaterRoughnessStrength,
                PreviewSurfaceWater);
        }
        if (!Surface.bEnabled)
        {
            SurfaceWaterPreviewStatus += TEXT("\nSelected preview profile has Surface Water disabled, so the preview keeps the source material appearance.");
        }
        else if (Surface.SurfaceWaterNormalStrength <= UE_KINDA_SMALL_NUMBER)
        {
            SurfaceWaterPreviewStatus += TEXT("\nSelected preview profile has zero Surface Water Normal Strength, so World Normal remains unchanged.");
        }
        else if (!Surface.bEnableDroplets && !Surface.bEnableRivulets)
        {
            SurfaceWaterPreviewStatus += TEXT("\nSelected preview profile has no Droplet/Rivulet normals enabled, so World Normal remains unchanged.");
        }
        const bool bMissingDropletNormal =
            Surface.bEnableDroplets && LocalProfile.NormalizedDropletNormal == nullptr;
        const bool bMissingRivuletNormal =
            Surface.bEnableRivulets && LocalProfile.NormalizedRivuletNormal == nullptr;
        if (bMissingDropletNormal && bMissingRivuletNormal)
        {
            SurfaceWaterPreviewStatus += TEXT("\nSelected profile has no baked Droplet/Rivulet normal textures, so the preview will show surface coverage without detail normals.");
        }
        else if (bMissingDropletNormal)
        {
            SurfaceWaterPreviewStatus += TEXT("\nSelected profile has no baked Droplet normal texture; only Rivulet detail normals can appear.");
        }
        else if (bMissingRivuletNormal)
        {
            SurfaceWaterPreviewStatus += TEXT("\nSelected profile has no baked Rivulet normal texture; only Droplet detail normals can appear.");
        }
    }

    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
    }
    RequestViewportRedraw();
}

FText SDWCPartViewport::GetSurfaceWaterPreviewStatusText() const
{
    return FText::FromString(SurfaceWaterPreviewStatus);
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
                     [SAssignNew(OverlayText, SRichTextBlock)
                          .Text(GetViewportHintText())]];
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
        FString Hint = TEXT("Surface Water Tiling uses the selected slot's generated GPU material on the original skeletal mesh.");
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

#undef LOCTEXT_NAMESPACE
