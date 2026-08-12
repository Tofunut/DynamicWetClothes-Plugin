//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyType1SourceProvider.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialBakeResolutionResolver.h"
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialColorBakeCache.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

namespace
{
    FName MakeSourceLayerId(const int32 PriorityIndex)
    {
        return FName(*FString::Printf(TEXT("DWCTransparencyInner_%d"), PriorityIndex));
    }
}

uint64 FDWCTransparencyType1SourceBindings::GetAllocatedBytes() const
{
    uint64 Bytes = SurfacesBySourceLayerId.GetAllocatedSize() + Warnings.GetAllocatedSize();
    // Surface pixels are immutable SharedCacheCPU residents and retain their
    // own accountable lease. A worker snapshot owns only this map of handles;
    // charging the pixels here would reserve the same memory twice.
    return Bytes;
}

bool FDWCTransparencyType1SourceProvider::AddValidatedBinding(
    UWetClothingAsset& Asset,
    const FWetClothingTransparencyInnerSlot& InnerSlot,
    const int32 PriorityIndex,
    const FDWCRevealBakeSurface& SourceSurface,
    FDWCTransparencyType1SourceBindings& OutBindings,
    FString& OutError)
{
    check(IsInGameThread());
    OutError.Reset();
    USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    if (SourceMesh == nullptr)
    {
        OutError = TEXT("Type 1 transparency generation requires the WCA source skeletal mesh.");
        return false;
    }

    bool bHasUsableGeometry = false;
    bool bHasUsableUV = false;
    for (const FDWCRevealBakeSurfaceTriangle& Triangle : SourceSurface.Triangles)
    {
        const bool bFiniteGeometry =
            !Triangle.Positions[0].ContainsNaN() &&
            !Triangle.Positions[1].ContainsNaN() &&
            !Triangle.Positions[2].ContainsNaN() &&
            !Triangle.Normals[0].ContainsNaN() &&
            !Triangle.Normals[1].ContainsNaN() &&
            !Triangle.Normals[2].ContainsNaN();
        const bool bFiniteUV =
            FMath::IsFinite(Triangle.UVs[0].X) && FMath::IsFinite(Triangle.UVs[0].Y) &&
            FMath::IsFinite(Triangle.UVs[1].X) && FMath::IsFinite(Triangle.UVs[1].Y) &&
            FMath::IsFinite(Triangle.UVs[2].X) && FMath::IsFinite(Triangle.UVs[2].Y);
        if (!bFiniteGeometry || !bFiniteUV)
        {
            continue;
        }
        const FVector Edge01 = Triangle.Positions[1] - Triangle.Positions[0];
        const FVector Edge02 = Triangle.Positions[2] - Triangle.Positions[0];
        bHasUsableGeometry |= FVector::CrossProduct(Edge01, Edge02).SizeSquared() > UE_SMALL_NUMBER;
        const FVector2D UVEdge01 = Triangle.UVs[1] - Triangle.UVs[0];
        const FVector2D UVEdge02 = Triangle.UVs[2] - Triangle.UVs[0];
        bHasUsableUV |= FMath::Abs(UVEdge01.X * UVEdge02.Y - UVEdge01.Y * UVEdge02.X) > UE_SMALL_NUMBER;
    }
    if (SourceSurface.Triangles.IsEmpty() || !bHasUsableGeometry)
    {
        OutError = FString::Printf(
            TEXT("Inner Source Part '%s' has no usable LOD 0 surface triangles."),
            *InnerSlot.MaterialSlotName.ToString());
        return false;
    }

    UMaterialInterface* EffectiveMaterial =
        SourceMesh->GetMaterials().IsValidIndex(InnerSlot.MaterialSlotIndex)
        ? SourceMesh->GetMaterials()[InnerSlot.MaterialSlotIndex].MaterialInterface
        : nullptr;
    if (EffectiveMaterial == nullptr)
    {
        OutError = FString::Printf(
            TEXT("Inner Source Part '%s' has no effective source material."),
            *InnerSlot.MaterialSlotName.ToString());
        return false;
    }

    const FDWCTransparencyResolvedMaterialBakeResolution SourceBakeResolution =
        FDWCTransparencyMaterialBakeResolutionResolver::Resolve(EffectiveMaterial);
    FString BakeError;
    TSharedPtr<const FDWCTransparencyMaterialColorBakeResult> Result =
        FDWCTransparencyMaterialColorBakeCache::ResolveOrBake(
            Asset, *SourceMesh, InnerSlot.MaterialSlotIndex, InnerSlot.SourceUVChannel,
            SourceBakeResolution.Resolution, BakeError);
    if (!Result.IsValid())
    {
        OutError = FString::Printf(
            TEXT("Inner Source Part '%s' could not bake its original material Base Color: %s"),
            *InnerSlot.MaterialSlotName.ToString(), *BakeError);
        return false;
    }
    if (Result->PayloadKind == EDWCTransparencyMaterialColorPayloadKind::Texture && !bHasUsableUV)
    {
        OutError = FString::Printf(
            TEXT("Inner Source Part '%s' uses a texture Base Color but its selected UV channel has no usable area."),
            *InnerSlot.MaterialSlotName.ToString());
        return false;
    }
    OutBindings.SurfacesBySourceLayerId.Add(MakeSourceLayerId(PriorityIndex), MoveTemp(Result));
    return true;
}
