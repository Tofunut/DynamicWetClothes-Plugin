//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialColorBakeCache.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCTransparencyMaterialBake, Log, All);

namespace
{
    struct FBakedMaterialColorPayload
    {
        EDWCTransparencyMaterialColorPayloadKind Kind =
            EDWCTransparencyMaterialColorPayloadKind::Texture;
        FIntPoint PhysicalResolution = FIntPoint::ZeroValue;
        TArray<FColor> Pixels;
        bool bSRGB = true;
    };

    struct FBakedMaterialScalarPayload
    {
        EDWCTransparencyMaterialColorPayloadKind Kind =
            EDWCTransparencyMaterialColorPayloadKind::Texture;
        FIntPoint PhysicalResolution = FIntPoint::ZeroValue;
        TArray<uint8> Values;
    };

    struct FBakedMaterialSurfacePayload
    {
        FBakedMaterialColorPayload BaseColor;
        FBakedMaterialColorPayload TangentNormal;
        FBakedMaterialScalarPayload Metallic;
        bool bHasBakedNormalProperty = false;
        bool bHasBakedMetallicProperty = false;
    };

    struct FCacheEntry
    {
        TSharedPtr<const FDWCTransparencyMaterialColorBakeResult> Result;
        uint64 LastUsedSerial = 0;
    };

    TMap<FDWCTransparencyMaterialColorBakeKey, FCacheEntry> GMaterialColorCache;
    uint64 GUseSerial = 0;
    constexpr uint64 DefaultCacheBudgetBytes = 128ull * 1024ull * 1024ull;

    struct FSessionBudgetContext
    {
        TWeakPtr<FDWCEditorResourceGovernor> ResourceGovernor;
        FDWCEditorAsyncOperationIdentity MemoryOwner;
        uint64 CacheBudgetBytes = DefaultCacheBudgetBytes;
    };

    TMap<FObjectKey, FSessionBudgetContext> GSessionBudgetContexts;

    uint64 CalculateResidentBytes()
    {
        uint64 Total = 0;
        for (const TPair<FDWCTransparencyMaterialColorBakeKey, FCacheEntry>& Pair : GMaterialColorCache)
        {
            if (Pair.Value.Result.IsValid())
            {
                Total += Pair.Value.Result->AllocatedBytes;
            }
        }
        return Total;
    }

    uint64 CalculateResidentBytes(const FSoftObjectPath& OwnerAssetPath)
    {
        uint64 Total = 0;
        for (const TPair<FDWCTransparencyMaterialColorBakeKey, FCacheEntry>& Pair : GMaterialColorCache)
        {
            if (Pair.Key.OwnerAssetPath == OwnerAssetPath && Pair.Value.Result.IsValid())
            {
                Total += Pair.Value.Result->AllocatedBytes;
            }
        }
        return Total;
    }

    void TrimCache(
        const FDWCTransparencyMaterialColorBakeKey& ProtectedKey,
        const uint64 CacheBudgetBytes)
    {
        while (CalculateResidentBytes(ProtectedKey.OwnerAssetPath) > CacheBudgetBytes)
        {
            const FDWCTransparencyMaterialColorBakeKey* OldestKey = nullptr;
            uint64 OldestSerial = MAX_uint64;
            for (const TPair<FDWCTransparencyMaterialColorBakeKey, FCacheEntry>& Pair : GMaterialColorCache)
            {
                if (Pair.Key.OwnerAssetPath == ProtectedKey.OwnerAssetPath &&
                    !(Pair.Key == ProtectedKey) && Pair.Value.LastUsedSerial < OldestSerial)
                {
                    OldestKey = &Pair.Key;
                    OldestSerial = Pair.Value.LastUsedSerial;
                }
            }
            if (OldestKey == nullptr)
            {
                break;
            }
            GMaterialColorCache.Remove(*OldestKey);
        }
    }

    bool MakeReadback(
        TArray<FColor>&& Pixels,
        const FIntPoint Resolution,
        const bool bSRGB,
        FWetClothingTextureReadback& OutReadback)
    {
        if (Resolution.X <= 0 || Resolution.Y <= 0 || Pixels.Num() != Resolution.X * Resolution.Y)
        {
            return false;
        }
        TSharedPtr<TArray64<uint8>> RawData = MakeShared<TArray64<uint8>>();
        RawData->SetNumUninitialized(static_cast<int64>(Pixels.Num()) * sizeof(FColor));
        FMemory::Memcpy(RawData->GetData(), Pixels.GetData(), RawData->Num());
        OutReadback.Width = Resolution.X;
        OutReadback.Height = Resolution.Y;
        OutReadback.BytesPerPixel = sizeof(FColor);
        OutReadback.bSRGB = bSRGB;
        OutReadback.Format = TSF_BGRA8;
        OutReadback.AddressX = TA_Clamp;
        OutReadback.AddressY = TA_Clamp;
        OutReadback.RawData = MoveTemp(RawData);
        return OutReadback.IsValid();
    }

    bool MakeG8Readback(
        TArray<uint8>&& Values,
        const FIntPoint Resolution,
        FWetClothingTextureReadback& OutReadback)
    {
        if (Resolution.X <= 0 || Resolution.Y <= 0 || Values.Num() != Resolution.X * Resolution.Y)
        {
            return false;
        }
        TSharedPtr<TArray64<uint8>> RawData = MakeShared<TArray64<uint8>>();
        RawData->Append(Values.GetData(), Values.Num());
        OutReadback.Width = Resolution.X;
        OutReadback.Height = Resolution.Y;
        OutReadback.BytesPerPixel = 1;
        OutReadback.bSRGB = false;
        OutReadback.Format = TSF_G8;
        OutReadback.AddressX = TA_Clamp;
        OutReadback.AddressY = TA_Clamp;
        OutReadback.RawData = MoveTemp(RawData);
        return OutReadback.IsValid();
    }

    bool IsPayloadShapeValid(
        const EDWCTransparencyMaterialColorPayloadKind PayloadKind,
        const FIntPoint LogicalResolution,
        const FWetClothingTextureReadback& TextureData)
    {
        const FIntPoint PhysicalResolution(TextureData.Width, TextureData.Height);
        return TextureData.IsValid() && LogicalResolution.X > 0 && LogicalResolution.Y > 0 &&
            (PayloadKind == EDWCTransparencyMaterialColorPayloadKind::ConstantColor
                ? PhysicalResolution == FIntPoint(1, 1)
                : PhysicalResolution == LogicalResolution);
    }

    FLinearColor SamplePayload(
        const EDWCTransparencyMaterialColorPayloadKind PayloadKind,
        const FWetClothingTextureReadback& TextureData,
        const FVector2D& UV)
    {
        if (!TextureData.IsValid())
        {
            return FLinearColor::Black;
        }
        if (PayloadKind == EDWCTransparencyMaterialColorPayloadKind::ConstantColor)
        {
            return TextureData.GetLinearColor(0, 0);
        }

        const auto ApplyAddress = [](const float Coordinate, const TextureAddress AddressMode)
        {
            switch (AddressMode)
            {
            case TA_Wrap:
                return FMath::Frac(Coordinate);
            case TA_Mirror:
            {
                const float Wrapped = FMath::Frac(Coordinate * 0.5f) * 2.0f;
                return Wrapped <= 1.0f ? Wrapped : 2.0f - Wrapped;
            }
            case TA_Clamp:
            default:
                return FMath::Clamp(Coordinate, 0.0f, 1.0f);
            }
        };
        const float U = ApplyAddress(static_cast<float>(UV.X), TextureData.AddressX);
        const float V = ApplyAddress(static_cast<float>(UV.Y), TextureData.AddressY);
        const float X = U * static_cast<float>(TextureData.Width - 1);
        const float Y = V * static_cast<float>(TextureData.Height - 1);
        const int32 X0 = FMath::FloorToInt(X);
        const int32 Y0 = FMath::FloorToInt(Y);
        const int32 X1 = FMath::Min(X0 + 1, TextureData.Width - 1);
        const int32 Y1 = FMath::Min(Y0 + 1, TextureData.Height - 1);
        const FLinearColor C0 = FMath::Lerp(
            TextureData.GetLinearColor(X0, Y0), TextureData.GetLinearColor(X1, Y0), X - X0);
        const FLinearColor C1 = FMath::Lerp(
            TextureData.GetLinearColor(X0, Y1), TextureData.GetLinearColor(X1, Y1), X - X0);
        return FMath::Lerp(C0, C1, Y - Y0);
    }

    bool ValidatePropertyShape(
        const FIntPoint& PhysicalResolution,
        const int32 LogicalResolution,
        EDWCTransparencyMaterialColorPayloadKind& OutKind)
    {
        const FIntPoint LogicalSize(LogicalResolution, LogicalResolution);
        if (PhysicalResolution == LogicalSize)
        {
            OutKind = EDWCTransparencyMaterialColorPayloadKind::Texture;
            return true;
        }
        if (PhysicalResolution == FIntPoint(1, 1))
        {
            OutKind = EDWCTransparencyMaterialColorPayloadKind::ConstantColor;
            return true;
        }
        return false;
    }

    bool ExtractColorPayload(
        FBakeOutput& Output,
        const EMaterialProperty Property,
        const int32 LogicalResolution,
        const FColor& Fallback,
        const bool bRequired,
        FBakedMaterialColorPayload& OutPayload,
        bool& bOutPropertyAvailable,
        FString& OutError)
    {
        OutPayload = FBakedMaterialColorPayload();
        bOutPropertyAvailable = false;
        TArray<FColor>* PropertyPixels = Output.PropertyData.Find(Property);
        const FIntPoint* PropertySize = Output.PropertySizes.Find(Property);
        if (PropertyPixels == nullptr || PropertySize == nullptr ||
            PropertySize->X <= 0 || PropertySize->Y <= 0 ||
            PropertyPixels->Num() != PropertySize->X * PropertySize->Y)
        {
            if (bRequired)
            {
                OutError = TEXT("The engine MaterialBaking module returned an incomplete Base Color texture.");
                return false;
            }
            OutPayload.Kind = EDWCTransparencyMaterialColorPayloadKind::ConstantColor;
            OutPayload.PhysicalResolution = FIntPoint(1, 1);
            OutPayload.Pixels = { Fallback };
            OutPayload.bSRGB = false;
            return true;
        }

        if (!ValidatePropertyShape(*PropertySize, LogicalResolution, OutPayload.Kind))
        {
            if (bRequired)
            {
                OutError = FString::Printf(
                    TEXT("The engine MaterialBaking module returned an unsupported Base Color size %dx%d; expected %dx%d or a uniform 1x1 result."),
                    PropertySize->X, PropertySize->Y, LogicalResolution, LogicalResolution);
                return false;
            }
            OutPayload.Kind = EDWCTransparencyMaterialColorPayloadKind::ConstantColor;
            OutPayload.PhysicalResolution = FIntPoint(1, 1);
            OutPayload.Pixels = { Fallback };
            OutPayload.bSRGB = false;
            return true;
        }

        for (FColor& Pixel : *PropertyPixels)
        {
            Pixel.A = 255;
        }
        const bool* bLinear = Output.PropertyIsLinearColor.Find(Property);
        OutPayload.PhysicalResolution = *PropertySize;
        OutPayload.Pixels = MoveTemp(*PropertyPixels);
        OutPayload.bSRGB = bLinear == nullptr || !*bLinear;
        bOutPropertyAvailable = true;
        return true;
    }

    bool ExtractMetallicPayload(
        FBakeOutput& Output,
        const int32 LogicalResolution,
        FBakedMaterialScalarPayload& OutPayload,
        bool& bOutPropertyAvailable)
    {
        OutPayload = FBakedMaterialScalarPayload();
        bOutPropertyAvailable = false;
        TArray<FColor>* PropertyPixels = Output.PropertyData.Find(MP_Metallic);
        const FIntPoint* PropertySize = Output.PropertySizes.Find(MP_Metallic);
        if (PropertyPixels == nullptr || PropertySize == nullptr ||
            PropertySize->X <= 0 || PropertySize->Y <= 0 ||
            PropertyPixels->Num() != PropertySize->X * PropertySize->Y ||
            !ValidatePropertyShape(*PropertySize, LogicalResolution, OutPayload.Kind))
        {
            OutPayload.Kind = EDWCTransparencyMaterialColorPayloadKind::ConstantColor;
            OutPayload.PhysicalResolution = FIntPoint(1, 1);
            OutPayload.Values = { 0 };
            return true;
        }

        OutPayload.PhysicalResolution = *PropertySize;
        OutPayload.Values.SetNumUninitialized(PropertyPixels->Num());
        for (int32 PixelIndex = 0; PixelIndex < PropertyPixels->Num(); ++PixelIndex)
        {
            OutPayload.Values[PixelIndex] = (*PropertyPixels)[PixelIndex].R;
        }
        bOutPropertyAvailable = true;
        return true;
    }

    bool BakeMaterialSurface(
        USkeletalMesh& SourceMesh,
        UMaterialInterface& Material,
        const FTransform& BakeTransform,
        const int32 MaterialSlotIndex,
        const int32 SourceUVChannel,
        const int32 Resolution,
        FBakedMaterialSurfacePayload& OutPayload,
        FString& OutError)
    {
        check(IsInGameThread());
        OutPayload = FBakedMaterialSurfacePayload();
        FMeshDescription* MeshDescription = SourceMesh.GetMeshDescription(0);
        if (MeshDescription == nullptr)
        {
            OutError = FString::Printf(
                TEXT("Source mesh '%s' has no editable LOD 0 MeshDescription for material color baking."),
                *SourceMesh.GetName());
            return false;
        }

        const FStaticMeshConstAttributes MeshAttributes(*MeshDescription);
        const TVertexInstanceAttributesConstRef<FVector2f> VertexInstanceUVs =
            MeshAttributes.GetVertexInstanceUVs();
        if (!VertexInstanceUVs.IsValid() ||
            SourceUVChannel < 0 || SourceUVChannel >= VertexInstanceUVs.GetNumChannels())
        {
            OutError = FString::Printf(
                TEXT("Source mesh '%s' has no UV channel %d in its LOD 0 MeshDescription."),
                *SourceMesh.GetName(), SourceUVChannel);
            return false;
        }

        bool bHasSelectedTriangle = false;
        bool bHasRasterizableUVTriangle = false;
        for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
        {
            if (MeshDescription->GetTrianglePolygonGroup(TriangleID).GetValue() != MaterialSlotIndex)
            {
                continue;
            }
            bHasSelectedTriangle = true;
            const TArrayView<const FVertexInstanceID> VertexInstances =
                MeshDescription->GetTriangleVertexInstances(TriangleID);
            if (VertexInstances.Num() != 3)
            {
                continue;
            }
            const FVector2f UV0 = VertexInstanceUVs.Get(VertexInstances[0], SourceUVChannel);
            const FVector2f UV1 = VertexInstanceUVs.Get(VertexInstances[1], SourceUVChannel);
            const FVector2f UV2 = VertexInstanceUVs.Get(VertexInstances[2], SourceUVChannel);
            if (!UV0.ContainsNaN() && !UV1.ContainsNaN() && !UV2.ContainsNaN())
            {
                const FVector2f Edge01 = UV1 - UV0;
                const FVector2f Edge02 = UV2 - UV0;
                if (FMath::Abs(Edge01.X * Edge02.Y - Edge01.Y * Edge02.X) > UE_SMALL_NUMBER)
                {
                    bHasRasterizableUVTriangle = true;
                    break;
                }
            }
        }
        if (!bHasSelectedTriangle || !bHasRasterizableUVTriangle)
        {
            OutError = FString::Printf(
                TEXT("Source mesh '%s' slot %d has no rasterizable LOD 0 triangle on UV channel %d."),
                *SourceMesh.GetName(), MaterialSlotIndex, SourceUVChannel);
            return false;
        }

        FMaterialData MaterialData;
        MaterialData.Material = &Material;
        MaterialData.PropertySizes.Add(MP_BaseColor, FIntPoint(Resolution, Resolution));
        MaterialData.PropertySizes.Add(MP_Normal, FIntPoint(Resolution, Resolution));
        MaterialData.PropertySizes.Add(MP_Metallic, FIntPoint(Resolution, Resolution));
        MaterialData.bPerformBorderSmear = true;
        MaterialData.bPerformShrinking = false;
        MaterialData.BlendMode = BLEND_Opaque;
        MaterialData.BackgroundColor = FColor::Black;

        FMeshData MeshData;
        MeshData.MeshDescription = MeshDescription;
        MeshData.MaterialIndices.Add(MaterialSlotIndex);
        MeshData.TextureCoordinateIndex = SourceUVChannel;
        MeshData.TextureCoordinateBox = FBox2D(FVector2D::ZeroVector, FVector2D(1.0, 1.0));
        FPrimitiveData PrimitiveData(&SourceMesh);
        PrimitiveData.LocalToWorld = BakeTransform.ToMatrixWithScale();
        PrimitiveData.ActorPosition = BakeTransform.GetTranslation();
        PrimitiveData.WorldBounds = PrimitiveData.LocalBounds.TransformBy(BakeTransform);
        MeshData.PrimitiveData = PrimitiveData;

        TArray<FMaterialData*> MaterialSettings{&MaterialData};
        TArray<FMeshData*> MeshSettings{&MeshData};
        TArray<FBakeOutput> Outputs;
        IMaterialBakingModule& Module =
            FModuleManager::LoadModuleChecked<IMaterialBakingModule>(TEXT("MaterialBaking"));
        Module.BakeMaterials(MaterialSettings, MeshSettings, Outputs);
        if (Outputs.Num() != 1)
        {
            OutError = TEXT("The engine MaterialBaking module returned no material surface output.");
            return false;
        }

        FBakeOutput& Output = Outputs[0];
        bool bBaseColorAvailable = false;
        if (!ExtractColorPayload(
                Output, MP_BaseColor, Resolution, FColor::Black, true,
                OutPayload.BaseColor, bBaseColorAvailable, OutError))
        {
            return false;
        }
        if (!bBaseColorAvailable)
        {
            OutError = TEXT("The engine MaterialBaking module returned an incomplete Base Color texture.");
            return false;
        }
        if (!ExtractColorPayload(
                Output, MP_Normal, Resolution, FColor(128, 128, 255, 255), false,
                OutPayload.TangentNormal, OutPayload.bHasBakedNormalProperty, OutError))
        {
            return false;
        }
        OutPayload.TangentNormal.bSRGB = false;
        if (!ExtractMetallicPayload(
                Output, Resolution, OutPayload.Metallic, OutPayload.bHasBakedMetallicProperty))
        {
            OutError = TEXT("Could not normalize the baked material Metallic payload.");
            return false;
        }
        return true;
    }
}

void FDWCTransparencyMaterialColorBakeCache::ConfigureResourceGovernor(
    UWetClothingAsset& Asset,
    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor,
    const FGuid& SessionEpoch,
    const uint64 InCacheBudgetBytes)
{
    check(IsInGameThread());
    Clear(&Asset);
    FSessionBudgetContext& Context = GSessionBudgetContexts.Add(FObjectKey(&Asset));
    Context.ResourceGovernor = ResourceGovernor;
    Context.CacheBudgetBytes = FMath::Max<uint64>(InCacheBudgetBytes, 1);
    Context.MemoryOwner.Key.Namespace = TEXT("DWC.TransparencyMaterialColorCache");
    Context.MemoryOwner.SessionEpoch = SessionEpoch.IsValid() ? SessionEpoch : FGuid::NewGuid();
    Context.MemoryOwner.OperationId = 1;
    Context.MemoryOwner.Generation = 1;
}

bool FDWCTransparencyMaterialColorBakeKey::IsValid() const
{
    return SourceMeshPath.IsValid() && MaterialSlotIndex != INDEX_NONE &&
        SourceUVChannel >= 0 && LogicalResolution > 0 && !MaterialBakeSignature.IsEmpty();
}

bool FDWCTransparencyMaterialColorBakeKey::operator==(
    const FDWCTransparencyMaterialColorBakeKey& Other) const
{
    return SourceMeshPath == Other.SourceMeshPath &&
        OwnerAssetPath == Other.OwnerAssetPath &&
        MaterialSlotIndex == Other.MaterialSlotIndex &&
        SourceUVChannel == Other.SourceUVChannel && LogicalResolution == Other.LogicalResolution &&
        MaterialBakeSignature == Other.MaterialBakeSignature;
}

uint32 GetTypeHash(const FDWCTransparencyMaterialColorBakeKey& Key)
{
    uint32 Hash = GetTypeHash(Key.OwnerAssetPath);
    Hash = HashCombineFast(Hash, GetTypeHash(Key.SourceMeshPath));
    Hash = HashCombineFast(Hash, GetTypeHash(Key.MaterialSlotIndex));
    Hash = HashCombineFast(Hash, GetTypeHash(Key.SourceUVChannel));
    Hash = HashCombineFast(Hash, GetTypeHash(Key.LogicalResolution));
    return HashCombineFast(Hash, GetTypeHash(Key.MaterialBakeSignature));
}

bool FDWCTransparencyMaterialColorBakeResult::InitializePayload(
    const EDWCTransparencyMaterialColorPayloadKind InPayloadKind,
    const FIntPoint InLogicalResolution,
    const FIntPoint InPhysicalResolution,
    TArray<FColor>&& InPixels,
    const bool bSRGB,
    FString& OutError)
{
    OutError.Reset();
    if (InLogicalResolution.X <= 0 || InLogicalResolution.Y <= 0 ||
        InPhysicalResolution.X <= 0 || InPhysicalResolution.Y <= 0 ||
        InPixels.Num() != InPhysicalResolution.X * InPhysicalResolution.Y)
    {
        OutError = TEXT("The material color payload dimensions are incomplete.");
        return false;
    }
    if (InPayloadKind == EDWCTransparencyMaterialColorPayloadKind::ConstantColor)
    {
        if (InPhysicalResolution != FIntPoint(1, 1))
        {
            OutError = TEXT("A constant material color payload must contain exactly one texel.");
            return false;
        }
    }
    else if (InPhysicalResolution != InLogicalResolution)
    {
        OutError = TEXT("A texture material color payload must match its logical resolution.");
        return false;
    }

    FWetClothingTextureReadback Readback;
    if (!MakeReadback(MoveTemp(InPixels), InPhysicalResolution, bSRGB, Readback))
    {
        OutError = TEXT("Could not create an immutable material color payload.");
        return false;
    }
    return InitializePayloadFromReadback(
        InPayloadKind, InLogicalResolution, MoveTemp(Readback), OutError);
}

bool FDWCTransparencyMaterialColorBakeResult::InitializePayloadFromReadback(
    const EDWCTransparencyMaterialColorPayloadKind InPayloadKind,
    const FIntPoint InLogicalResolution,
    FWetClothingTextureReadback&& InTextureData,
    FString& OutError)
{
    OutError.Reset();
    const FIntPoint ReadbackResolution(InTextureData.Width, InTextureData.Height);
    if (InLogicalResolution.X <= 0 || InLogicalResolution.Y <= 0 || !InTextureData.IsValid())
    {
        OutError = TEXT("The cached material color payload is incomplete.");
        return false;
    }
    if (InPayloadKind == EDWCTransparencyMaterialColorPayloadKind::ConstantColor)
    {
        if (ReadbackResolution != FIntPoint(1, 1))
        {
            OutError = TEXT("The cached constant material color payload is not 1x1.");
            return false;
        }
    }
    else if (ReadbackResolution != InLogicalResolution)
    {
        OutError = TEXT("The cached texture material color payload does not match its logical resolution.");
        return false;
    }

    PayloadKind = InPayloadKind;
    LogicalResolution = InLogicalResolution;
    PhysicalResolution = ReadbackResolution;
    TextureData = MoveTemp(InTextureData);
    AllocatedBytes = TextureData.RawData.IsValid() ? TextureData.RawData->GetAllocatedSize() : 0;
    return true;
}

bool FDWCTransparencyMaterialColorBakeResult::InitializeSurfacePayloadFromReadbacks(
    const EDWCTransparencyMaterialColorPayloadKind InNormalPayloadKind,
    FWetClothingTextureReadback&& InNormalTextureData,
    const bool bInHasBakedNormalProperty,
    const EDWCTransparencyMaterialColorPayloadKind InMetallicPayloadKind,
    FWetClothingTextureReadback&& InMetallicTextureData,
    const bool bInHasBakedMetallicProperty,
    FString& OutError)
{
    OutError.Reset();
    if (!IsValid())
    {
        OutError = TEXT("A valid Base Color payload is required before adding material surface properties.");
        return false;
    }
    if (!IsPayloadShapeValid(InNormalPayloadKind, LogicalResolution, InNormalTextureData) ||
        !IsPayloadShapeValid(InMetallicPayloadKind, LogicalResolution, InMetallicTextureData))
    {
        OutError = TEXT("The cached material Normal or Metallic payload dimensions are incomplete.");
        return false;
    }

    NormalPayloadKind = InNormalPayloadKind;
    NormalPhysicalResolution = FIntPoint(InNormalTextureData.Width, InNormalTextureData.Height);
    NormalTextureData = MoveTemp(InNormalTextureData);
    MetallicPayloadKind = InMetallicPayloadKind;
    MetallicPhysicalResolution = FIntPoint(InMetallicTextureData.Width, InMetallicTextureData.Height);
    MetallicTextureData = MoveTemp(InMetallicTextureData);
    bHasBakedNormalProperty = bInHasBakedNormalProperty;
    bHasBakedMetallicProperty = bInHasBakedMetallicProperty;
    AllocatedBytes =
        (TextureData.RawData.IsValid() ? TextureData.RawData->GetAllocatedSize() : 0) +
        (NormalTextureData.RawData.IsValid() ? NormalTextureData.RawData->GetAllocatedSize() : 0) +
        (MetallicTextureData.RawData.IsValid() ? MetallicTextureData.RawData->GetAllocatedSize() : 0);
    return true;
}

bool FDWCTransparencyMaterialColorBakeResult::IsValid() const
{
    if (!Key.IsValid() || !TextureData.IsValid() ||
        LogicalResolution.X <= 0 || LogicalResolution.Y <= 0 ||
        PhysicalResolution != FIntPoint(TextureData.Width, TextureData.Height))
    {
        return false;
    }
    return PayloadKind == EDWCTransparencyMaterialColorPayloadKind::ConstantColor
        ? PhysicalResolution == FIntPoint(1, 1)
        : PhysicalResolution == LogicalResolution;
}

bool FDWCTransparencyMaterialColorBakeResult::HasCompleteSurfacePayload() const
{
    return IsValid() &&
        IsPayloadShapeValid(NormalPayloadKind, LogicalResolution, NormalTextureData) &&
        IsPayloadShapeValid(MetallicPayloadKind, LogicalResolution, MetallicTextureData);
}

FLinearColor FDWCTransparencyMaterialColorBakeResult::Sample(const FVector2D& UV) const
{
    if (!IsValid())
    {
        return FLinearColor::White;
    }
    return SamplePayload(PayloadKind, TextureData, UV);
}

FVector3f FDWCTransparencyMaterialColorBakeResult::SampleTangentNormal(const FVector2D& UV) const
{
    if (!HasCompleteSurfacePayload())
    {
        return FVector3f(0.0f, 0.0f, 1.0f);
    }
    const FLinearColor Encoded = SamplePayload(NormalPayloadKind, NormalTextureData, UV);
    const FVector3f Decoded(
        Encoded.R * 2.0f - 1.0f,
        Encoded.G * 2.0f - 1.0f,
        Encoded.B * 2.0f - 1.0f);
    return Decoded.IsNearlyZero() ? FVector3f(0.0f, 0.0f, 1.0f) : Decoded.GetSafeNormal();
}

float FDWCTransparencyMaterialColorBakeResult::SampleMetallic(const FVector2D& UV) const
{
    return HasCompleteSurfacePayload()
        ? FMath::Clamp(SamplePayload(MetallicPayloadKind, MetallicTextureData, UV).R, 0.0f, 1.0f)
        : 0.0f;
}

TSharedPtr<const FDWCTransparencyMaterialColorBakeResult>
FDWCTransparencyMaterialColorBakeCache::ResolveOrBake(
    UWetClothingAsset& Asset,
    USkeletalMesh& SourceMesh,
    const int32 MaterialSlotIndex,
    const int32 SourceUVChannel,
    const int32 Resolution,
    FString& OutError)
{
    check(IsInGameThread());
    OutError.Reset();
    if (!SourceMesh.GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        OutError = FString::Printf(TEXT("Source material slot %d is unavailable."), MaterialSlotIndex);
        return nullptr;
    }
    UMaterialInterface* Material = SourceMesh.GetMaterials()[MaterialSlotIndex].MaterialInterface;
    if (Material == nullptr)
    {
        OutError = FString::Printf(TEXT("Source material slot %d has no material."), MaterialSlotIndex);
        return nullptr;
    }

    return ResolveOrBake(
        Asset, SourceMesh, *Material, FTransform::Identity, MaterialSlotIndex,
        SourceUVChannel, Resolution, OutError);
}

TSharedPtr<const FDWCTransparencyMaterialColorBakeResult>
FDWCTransparencyMaterialColorBakeCache::ResolveOrBake(
    UWetClothingAsset& Asset,
    USkeletalMesh& SourceMesh,
    UMaterialInterface& EffectiveMaterial,
    const FTransform& BakeTransform,
    const int32 MaterialSlotIndex,
    const int32 SourceUVChannel,
    const int32 Resolution,
    FString& OutError)
{
    check(IsInGameThread());
    OutError.Reset();
    if (MaterialSlotIndex == INDEX_NONE)
    {
        OutError = TEXT("A valid source material slot is required.");
        return nullptr;
    }

    FDWCTransparencyMaterialColorBakeKey Key;
    Key.SourceMeshPath = FSoftObjectPath(&SourceMesh);
    Key.OwnerAssetPath = FSoftObjectPath(&Asset);
    Key.MaterialSlotIndex = MaterialSlotIndex;
    Key.SourceUVChannel = SourceUVChannel;
    Key.LogicalResolution = Resolution;
    Key.MaterialBakeSignature = FDWCTransparencySignatureService::BuildMaterialBakeSignature(
        &EffectiveMaterial, SourceUVChannel, Resolution) + FString::Printf(
            TEXT("|Placement=%s"), *BakeTransform.ToHumanReadableString());
    if (!Key.IsValid())
    {
        OutError = TEXT("Could not build a valid source material color cache key.");
        return nullptr;
    }

    if (FCacheEntry* Existing = GMaterialColorCache.Find(Key))
    {
        Existing->LastUsedSerial = ++GUseSerial;
        UE_LOG(
            LogDWCTransparencyMaterialBake, VeryVerbose,
            TEXT("Reused resident source material color for '%s' slot %d UV%d at %d."),
            *SourceMesh.GetName(), MaterialSlotIndex, SourceUVChannel, Resolution);
        return Existing->Result;
    }

    TSharedPtr<FDWCTransparencyMaterialColorBakeResult> Result =
        MakeShared<FDWCTransparencyMaterialColorBakeResult>();
    Result->Key = Key;
    FDWCTransparencyMaterialColorCacheReference PersistentReference;
    UTexture2D* PersistentBaseColorTexture = nullptr;
    UTexture2D* PersistentNormalTexture = nullptr;
    UTexture2D* PersistentMetallicTexture = nullptr;
    if (FDWCTransparencyTempAssetStore::FindCurrentSourceMaterialSurface(
            Asset, SourceMesh, MaterialSlotIndex, SourceUVChannel, Resolution,
            Key.MaterialBakeSignature, true, PersistentReference,
            PersistentBaseColorTexture, PersistentNormalTexture, PersistentMetallicTexture))
    {
        FWetClothingTextureReadback PersistentBaseColorReadback;
        FWetClothingTextureReadback PersistentNormalReadback;
        FWetClothingTextureReadback PersistentMetallicReadback;
        if (FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
                PersistentBaseColorTexture, PersistentBaseColorReadback, OutError) &&
            FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
                PersistentNormalTexture, PersistentNormalReadback, OutError) &&
            FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
                PersistentMetallicTexture, PersistentMetallicReadback, OutError))
        {
            const FIntPoint ActualBaseColorResolution(
                PersistentBaseColorReadback.Width, PersistentBaseColorReadback.Height);
            const FIntPoint ActualNormalResolution(
                PersistentNormalReadback.Width, PersistentNormalReadback.Height);
            const FIntPoint ActualMetallicResolution(
                PersistentMetallicReadback.Width, PersistentMetallicReadback.Height);
            const bool bBaseColorMetadataMatches =
                PersistentReference.PayloadResolution == FIntPoint::ZeroValue ||
                PersistentReference.PayloadResolution == ActualBaseColorResolution;
            const bool bNormalMetadataMatches =
                PersistentReference.NormalPayloadResolution == ActualNormalResolution;
            const bool bMetallicMetadataMatches =
                PersistentReference.MetallicPayloadResolution == ActualMetallicResolution;
            EDWCTransparencyMaterialColorPayloadKind BaseColorPayloadKind = PersistentReference.PayloadKind;
            if (PersistentReference.PayloadResolution == FIntPoint::ZeroValue &&
                ActualBaseColorResolution == FIntPoint(1, 1))
            {
                BaseColorPayloadKind = EDWCTransparencyMaterialColorPayloadKind::ConstantColor;
            }
            if (bBaseColorMetadataMatches && bNormalMetadataMatches && bMetallicMetadataMatches &&
                Result->InitializePayloadFromReadback(
                    BaseColorPayloadKind, FIntPoint(Resolution, Resolution),
                    MoveTemp(PersistentBaseColorReadback), OutError) &&
                Result->InitializeSurfacePayloadFromReadbacks(
                    PersistentReference.NormalPayloadKind, MoveTemp(PersistentNormalReadback),
                    PersistentReference.bHasBakedNormalProperty,
                    PersistentReference.MetallicPayloadKind, MoveTemp(PersistentMetallicReadback),
                    PersistentReference.bHasBakedMetallicProperty, OutError))
            {
                Result->bLoadedFromPersistentCache = true;
                UE_LOG(
                    LogDWCTransparencyMaterialBake, Verbose,
                    TEXT("Loaded persistent source material surface for '%s' slot %d UV%d (logical=%d, color=%dx%d, normal=%dx%d, metallic=%dx%d)."),
                    *SourceMesh.GetName(), MaterialSlotIndex, SourceUVChannel, Resolution,
                    Result->PhysicalResolution.X, Result->PhysicalResolution.Y,
                    Result->NormalPhysicalResolution.X, Result->NormalPhysicalResolution.Y,
                    Result->MetallicPhysicalResolution.X, Result->MetallicPhysicalResolution.Y);
            }
        }
    }

    if (!Result->HasCompleteSurfacePayload())
    {
        const double BakeStartSeconds = FPlatformTime::Seconds();
        FBakedMaterialSurfacePayload Payload;
        if (!BakeMaterialSurface(
                SourceMesh, EffectiveMaterial, BakeTransform, MaterialSlotIndex, SourceUVChannel,
                Resolution, Payload, OutError))
        {
            return nullptr;
        }
        UTexture2D* CommittedBaseColorTexture = nullptr;
        UTexture2D* CommittedNormalTexture = nullptr;
        UTexture2D* CommittedMetallicTexture = nullptr;
        if (!FDWCTransparencyTempAssetStore::CommitSourceMaterialSurface(
                Asset, SourceMesh, MaterialSlotIndex, SourceUVChannel,
                FIntPoint(Resolution, Resolution),
                Payload.BaseColor.PhysicalResolution, Payload.BaseColor.Kind,
                Payload.BaseColor.Pixels, Payload.BaseColor.bSRGB,
                Payload.TangentNormal.PhysicalResolution, Payload.TangentNormal.Kind,
                Payload.TangentNormal.Pixels, Payload.bHasBakedNormalProperty,
                Payload.Metallic.PhysicalResolution, Payload.Metallic.Kind,
                Payload.Metallic.Values, Payload.bHasBakedMetallicProperty,
                Key.MaterialBakeSignature, CommittedBaseColorTexture, CommittedNormalTexture,
                CommittedMetallicTexture, OutError))
        {
            return nullptr;
        }
        if (!Result->InitializePayload(
                Payload.BaseColor.Kind, FIntPoint(Resolution, Resolution), Payload.BaseColor.PhysicalResolution,
                MoveTemp(Payload.BaseColor.Pixels), Payload.BaseColor.bSRGB, OutError))
        {
            return nullptr;
        }
        FWetClothingTextureReadback NormalReadback;
        FWetClothingTextureReadback MetallicReadback;
        if (!MakeReadback(
                MoveTemp(Payload.TangentNormal.Pixels), Payload.TangentNormal.PhysicalResolution,
                false, NormalReadback) ||
            !MakeG8Readback(
                MoveTemp(Payload.Metallic.Values), Payload.Metallic.PhysicalResolution,
                MetallicReadback) ||
            !Result->InitializeSurfacePayloadFromReadbacks(
                Payload.TangentNormal.Kind, MoveTemp(NormalReadback), Payload.bHasBakedNormalProperty,
                Payload.Metallic.Kind, MoveTemp(MetallicReadback), Payload.bHasBakedMetallicProperty,
                OutError))
        {
            if (OutError.IsEmpty())
            {
                OutError = TEXT("Could not create immutable material Normal or Metallic payloads.");
            }
            return nullptr;
        }
        UE_LOG(
            LogDWCTransparencyMaterialBake, Display,
            TEXT("Baked source material surface for '%s' slot %d UV%d in %.1f ms (logical=%d, color=%dx%d, normal=%dx%d, metallic=%dx%d, normal=%s, metallic=%s, %llu bytes)."),
            *SourceMesh.GetName(), MaterialSlotIndex, SourceUVChannel,
            (FPlatformTime::Seconds() - BakeStartSeconds) * 1000.0,
            Resolution,
            Result->PhysicalResolution.X, Result->PhysicalResolution.Y,
            Result->NormalPhysicalResolution.X, Result->NormalPhysicalResolution.Y,
            Result->MetallicPhysicalResolution.X, Result->MetallicPhysicalResolution.Y,
            Result->bHasBakedNormalProperty ? TEXT("baked") : TEXT("flat fallback"),
            Result->bHasBakedMetallicProperty ? TEXT("baked") : TEXT("zero fallback"),
            Result->AllocatedBytes);
    }

    const FSessionBudgetContext* SessionContext = GSessionBudgetContexts.Find(FObjectKey(&Asset));
    if (const TSharedPtr<FDWCEditorResourceGovernor> Governor =
            SessionContext != nullptr ? SessionContext->ResourceGovernor.Pin() : nullptr)
    {
        FDWCEditorResourceReservationRequest Request;
        Request.Pool = EDWCEditorResourcePool::SharedCacheCPU;
        Request.Bytes = FMath::Max<uint64>(Result->AllocatedBytes, 1);
        Request.Owner = SessionContext->MemoryOwner;
        Request.DebugName = TEXT("Transparency material color cache");
        FDWCEditorMemoryLease Lease = Governor->TryAcquire(Request);
        while (!Lease.IsValid() && !GMaterialColorCache.IsEmpty())
        {
            const FDWCTransparencyMaterialColorBakeKey* OldestKey = nullptr;
            uint64 OldestSerial = MAX_uint64;
            for (const TPair<FDWCTransparencyMaterialColorBakeKey, FCacheEntry>& Pair : GMaterialColorCache)
            {
                if (Pair.Key.OwnerAssetPath == Key.OwnerAssetPath &&
                    Pair.Value.LastUsedSerial < OldestSerial)
                {
                    OldestKey = &Pair.Key;
                    OldestSerial = Pair.Value.LastUsedSerial;
                }
            }
            if (OldestKey == nullptr)
            {
                break;
            }
            const FDWCTransparencyMaterialColorBakeKey KeyToRemove = *OldestKey;
            GMaterialColorCache.Remove(KeyToRemove);
            Lease = Governor->TryAcquire(Request);
        }
        if (!Lease.IsValid())
        {
            OutError = TEXT("The shared editor cache budget cannot retain the material color result.");
            return nullptr;
        }
        Result->MemoryLease = MakeShared<FDWCEditorMemoryLease, ESPMode::ThreadSafe>(MoveTemp(Lease));
    }

    FCacheEntry& Entry = GMaterialColorCache.Add(Key);
    Entry.Result = Result;
    Entry.LastUsedSerial = ++GUseSerial;
    TrimCache(Key, SessionContext != nullptr ? SessionContext->CacheBudgetBytes : DefaultCacheBudgetBytes);
    OutError.Reset();
    return Result;
}

void FDWCTransparencyMaterialColorBakeCache::InvalidateMesh(const USkeletalMesh* SourceMesh)
{
    if (SourceMesh == nullptr)
    {
        return;
    }
    const FSoftObjectPath Path(SourceMesh);
    for (auto It = GMaterialColorCache.CreateIterator(); It; ++It)
    {
        if (It.Key().SourceMeshPath == Path)
        {
            It.RemoveCurrent();
        }
    }
}

void FDWCTransparencyMaterialColorBakeCache::Clear()
{
    GMaterialColorCache.Reset();
    GSessionBudgetContexts.Reset();
}

void FDWCTransparencyMaterialColorBakeCache::Clear(const UWetClothingAsset* Asset)
{
    if (Asset == nullptr)
    {
        return;
    }
    const FSoftObjectPath AssetPath(Asset);
    for (auto It = GMaterialColorCache.CreateIterator(); It; ++It)
    {
        if (It.Key().OwnerAssetPath == AssetPath)
        {
            It.RemoveCurrent();
        }
    }
    GSessionBudgetContexts.Remove(FObjectKey(Asset));
}
