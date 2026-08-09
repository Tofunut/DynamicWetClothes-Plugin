//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyType1SourceProvider.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/SkeletalMesh.h"
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialColorBakeCache.h"

namespace
{
    FName MakeSourceLayerId(const int32 PriorityIndex)
    {
        return FName(*FString::Printf(TEXT("DWCTransparencyInner_%d"), PriorityIndex));
    }
}

uint64 FDWCTransparencyType1SourceBindings::GetAllocatedBytes() const
{
    uint64 Bytes = ColorsBySourceLayerId.GetAllocatedSize() + Warnings.GetAllocatedSize();
    // Pixel payloads are shared cache residents. Account them once in the job
    // estimate so scheduler admission reflects the memory held by this snapshot.
    TSet<const FDWCTransparencyMaterialColorBakeResult*> UniqueResults;
    for (const TPair<FName, TSharedPtr<const FDWCTransparencyMaterialColorBakeResult>>& Pair : ColorsBySourceLayerId)
    {
        if (Pair.Value.IsValid() && !UniqueResults.Contains(Pair.Value.Get()))
        {
            UniqueResults.Add(Pair.Value.Get());
            Bytes += Pair.Value->AllocatedBytes;
        }
    }
    return Bytes;
}

bool FDWCTransparencyType1SourceProvider::BuildBindings(
    UWetClothingAsset& Asset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyType1SourceBindings& OutBindings,
    FString& OutError)
{
    check(IsInGameThread());
    OutBindings = FDWCTransparencyType1SourceBindings();
    OutError.Reset();
    USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    if (SourceMesh == nullptr)
    {
        OutError = TEXT("Type 1 transparency generation requires the WCA source skeletal mesh.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(
        Asset.Authored.TransparencyData.TransparencyBakeResolution, 16, 4096);
    for (int32 PriorityIndex = 0; PriorityIndex < Layer.SameMeshSource.InnerSlotPriority.Num(); ++PriorityIndex)
    {
        const FWetClothingTransparencyInnerSlot& Inner =
            Layer.SameMeshSource.InnerSlotPriority[PriorityIndex];
        if (Inner.MaterialSlotIndex == INDEX_NONE)
        {
            continue;
        }
        FString BakeError;
        TSharedPtr<const FDWCTransparencyMaterialColorBakeResult> Result =
            FDWCTransparencyMaterialColorBakeCache::ResolveOrBake(
                Asset, *SourceMesh, Inner.MaterialSlotIndex, Inner.SourceUVChannel,
                Resolution, BakeError);
        if (!Result.IsValid())
        {
            OutError = FString::Printf(
                TEXT("Inner Source Part '%s' could not bake its original material Base Color: %s"),
                *Inner.MaterialSlotName.ToString(), *BakeError);
            return false;
        }
        OutBindings.ColorsBySourceLayerId.Add(MakeSourceLayerId(PriorityIndex), MoveTemp(Result));
    }
    if (OutBindings.ColorsBySourceLayerId.IsEmpty())
    {
        OutError = TEXT("No Type 1 source material color could be prepared.");
        return false;
    }
    return true;
}
