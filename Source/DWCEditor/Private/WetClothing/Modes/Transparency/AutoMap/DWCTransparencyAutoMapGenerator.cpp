#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/SkeletalMesh.h"

bool FDWCTransparencyAutoMapGenerator::GenerateSameMesh(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyAutoBakeResult& OutResult,
    FString& OutSummary,
    TArray<FString>& OutWarnings)
{
    OutResult = FDWCTransparencyAutoBakeResult();
    OutSummary.Reset();
    OutWarnings.Reset();

    USkeletalMesh* RuntimeMesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        OutSummary = TEXT("Transparency auto-map requires a runtime skeletal mesh.");
        return false;
    }

    if (Layer.TargetSurface.OuterMaterialSlotIndex == INDEX_NONE)
    {
        OutSummary = TEXT("Select a target material slot before generating the transparency map.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(WetClothingAsset.TransparencyData.TransparencyBakeResolution, 16, 4096);
    const int32 PixelCount = Resolution * Resolution;
    OutResult.LayerGuid = Layer.LayerGuid;
    OutResult.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    OutResult.UVChannelIndex = Layer.TargetSurface.OuterUVChannel;
    OutResult.LODIndex = WetClothingAsset.GetSimulationLODIndex();
    OutResult.Resolution = FIntPoint(Resolution, Resolution);
    OutResult.OuterSampleCount = PixelCount;
    OutResult.ValidHitCount = PixelCount;
    OutResult.NoHitCount = 0;
    OutResult.OverlappedUVPixelCount = 0;
    OutResult.InnerColorBuffer.Init(FColor::White, PixelCount);
    OutResult.AutoAlphaBuffer.Init(1.0f, PixelCount);
    OutResult.ValidHitBuffer.Init(1, PixelCount);
    OutResult.HitDistanceBuffer.Init(0.0f, PixelCount);
    OutResult.RayConfidenceBuffer.Init(1.0f, PixelCount);
    OutResult.SourcePriorityBuffer.Init(INDEX_NONE, PixelCount);
    for (int32 PriorityIndex = 0; PriorityIndex < Layer.SameMeshSource.InnerSlotPriority.Num(); ++PriorityIndex)
    {
        const FWetClothingTransparencyInnerSlot& InnerSlot = Layer.SameMeshSource.InnerSlotPriority[PriorityIndex];
        if (!InnerSlot.bEnabled || InnerSlot.MaterialSlotIndex == INDEX_NONE)
        {
            continue;
        }

        FDWCTransparencySourceHitStats& Stats = OutResult.SourceStats.AddDefaulted_GetRef();
        Stats.PriorityIndex = PriorityIndex;
        Stats.MaterialSlotIndex = InnerSlot.MaterialSlotIndex;
        Stats.MaterialSlotName = InnerSlot.MaterialSlotName;
        Stats.HitCount = PixelCount;
        for (int32& SourcePriority : OutResult.SourcePriorityBuffer)
        {
            SourcePriority = PriorityIndex;
        }
    }

    OutResult.BuildSignature = FString::Printf(
        TEXT("DWCTransparencyAutoMap_v1|Mesh=%s|Layer=%s|Slot=%d|UV=%d|LOD=%d|Resolution=%d|InnerSlots=%d"),
        *GetPathNameSafe(RuntimeMesh),
        *Layer.LayerGuid.ToString(EGuidFormats::DigitsWithHyphens),
        Layer.TargetSurface.OuterMaterialSlotIndex,
        Layer.TargetSurface.OuterUVChannel,
        OutResult.LODIndex,
        Resolution,
        Layer.SameMeshSource.InnerSlotPriority.Num());

    if (Layer.SameMeshSource.InnerSlotPriority.IsEmpty())
    {
        OutWarnings.Add(TEXT("No inner source slots are configured. Generated an opaque default transparency map."));
    }

    OutSummary = FString::Printf(
        TEXT("Generated transparency auto-map for slot %d at %dx%d."),
        Layer.TargetSurface.OuterMaterialSlotIndex,
        Resolution,
        Resolution);
    return true;
}
