#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"

#include "DataAssets/DWCBakeLayer.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeProjection.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSourceResolver.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

namespace
{
    FName MakeInnerSourceLayerId(const int32 PriorityIndex)
    {
        return FName(*FString::Printf(TEXT("DWCTransparencyInner_%d"), PriorityIndex));
    }

    void FilterSurfaceToMaterialSlot(FDWCRevealBakeSurface& Surface, const int32 MaterialSlotIndex)
    {
        Surface.Triangles.RemoveAll([MaterialSlotIndex](const FDWCRevealBakeSurfaceTriangle& Triangle)
        {
            return Triangle.MaterialSlotIndex != MaterialSlotIndex;
        });

        Surface.Bounds = FBox(ForceInit);
        for (const FDWCRevealBakeSurfaceTriangle& Triangle : Surface.Triangles)
        {
            Surface.Bounds += Triangle.Bounds;
        }
    }

    FDWCBakeResolvedLayer MakeResolvedLayer(
        USkeletalMesh* Mesh,
        const FName LayerId,
        const int32 LayerOrder,
        const float MaxRevealDistance)
    {
        FDWCBakeResolvedLayer Result;
        Result.LayerId = LayerId;
        Result.LayerOrder = LayerOrder;
        Result.ComponentDisplayName = Mesh != nullptr ? Mesh->GetFName() : NAME_None;
        Result.ComponentPath = GetPathNameSafe(Mesh);
        Result.SkeletalMesh = Mesh;
        Result.BakeTransform = FTransform::Identity;
        Result.bCanBeRevealSource = true;
        Result.bCanBeWetOuterLayer = true;
        Result.bBlocksReveal = false;
        Result.MaxRevealDistance = MaxRevealDistance;
        if (Mesh != nullptr)
        {
            for (const FSkeletalMaterial& Material : Mesh->GetMaterials())
            {
                Result.Materials.Add(Material.MaterialInterface);
            }
        }
        return Result;
    }

    float ApplyTextureAddress(const float Coordinate, const TextureAddress AddressMode)
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
    }

    FLinearColor SampleTextureBilinear(const FWetClothingTextureReadback& TextureData, const FVector2D& UV)
    {
        if (!TextureData.IsValid())
        {
            return FLinearColor::White;
        }

        const float U = ApplyTextureAddress(static_cast<float>(UV.X), TextureData.AddressX);
        const float V = ApplyTextureAddress(static_cast<float>(UV.Y), TextureData.AddressY);
        const float X = U * static_cast<float>(TextureData.Width - 1);
        const float Y = V * static_cast<float>(TextureData.Height - 1);
        const int32 X0 = FMath::FloorToInt(X);
        const int32 Y0 = FMath::FloorToInt(Y);
        const int32 X1 = FMath::Min(X0 + 1, TextureData.Width - 1);
        const int32 Y1 = FMath::Min(Y0 + 1, TextureData.Height - 1);
        const float AlphaX = X - static_cast<float>(X0);
        const float AlphaY = Y - static_cast<float>(Y0);
        const FLinearColor C0 = FMath::Lerp(TextureData.GetLinearColor(X0, Y0), TextureData.GetLinearColor(X1, Y0), AlphaX);
        const FLinearColor C1 = FMath::Lerp(TextureData.GetLinearColor(X0, Y1), TextureData.GetLinearColor(X1, Y1), AlphaX);
        return FMath::Lerp(C0, C1, AlphaY);
    }

    float CalculateAutoAlpha(const FWetClothingTransparencyRaySettings& Settings, const FDWCRevealBakeRayHit& Hit)
    {
        const float FullDistance = FMath::Max(Settings.FullTransparencyDistance, 0.0f);
        const float NoTransparencyDistance = FMath::Max(Settings.NoTransparencyDistance, FullDistance + UE_SMALL_NUMBER);
        const float DistanceAlpha = 1.0f - FMath::Clamp(
            (Hit.Distance - FullDistance) / (NoTransparencyDistance - FullDistance),
            0.0f,
            1.0f);
        return FMath::Clamp(DistanceAlpha * Hit.Confidence, 0.0f, 1.0f);
    }
}

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
        OutSummary = TEXT("Transparency auto-map requires a DWC runtime skeletal mesh.");
        return false;
    }
    if (Layer.TargetSurface.OuterMaterialSlotIndex == INDEX_NONE)
    {
        OutSummary = TEXT("Select a Transparency Target Part before generating the transparency map.");
        return false;
    }
    if (Layer.SameMeshSource.InnerSlotPriority.IsEmpty())
    {
        OutSummary = TEXT("Add at least one enabled Inner Source Part before generating the transparency map.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(WetClothingAsset.Authored.TransparencyData.TransparencyBakeResolution, 16, 4096);
    const int32 LODIndex = WetClothingAsset.GetSimulationLODIndex();
    const FIntPoint BakeResolution(Resolution, Resolution);
    const int32 PixelCount = Resolution * Resolution;

    const FDWCBakeResolvedLayer OuterLayer = MakeResolvedLayer(
        RuntimeMesh,
        FName(TEXT("DWCTransparencyOuter")),
        MAX_int32 / 2,
        Layer.RaySettings.MaxRayDistance);
    FDWCRevealBakeSurface OuterSurface;
    FString BuildError;
    if (!FDWCRevealBakeSurfaceBuilder::BuildReferencePoseSurface(
            OuterLayer,
            LODIndex,
            Layer.TargetSurface.OuterUVChannel,
            OuterSurface,
            &BuildError))
    {
        OutSummary = FString::Printf(TEXT("Failed to build the transparency target surface: %s"), *BuildError);
        return false;
    }

    FDWCRevealBakeTexelSamplingSettings SamplingSettings;
    SamplingSettings.Resolution = BakeResolution;
    SamplingSettings.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    TArray<FDWCRevealBakeTexelSample> OuterSamples;
    int32 OverlappedPixelCount = 0;
    if (!FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            OuterSurface,
            SamplingSettings,
            OuterSamples,
            &BuildError,
            &OverlappedPixelCount))
    {
        OutSummary = FString::Printf(TEXT("Failed to rasterize the transparency target UVs: %s"), *BuildError);
        return false;
    }
    if (OuterSamples.IsEmpty())
    {
        OutSummary = TEXT("No outer texel samples were generated. Check the selected target slot and DWC Data UV channel.");
        return false;
    }

    TArray<FDWCRevealBakeSurface> SourceSurfaces;
    TMap<FName, int32> PriorityBySourceLayerId;
    TMap<FName, FWetClothingTextureReadback> SourceTextureDataByLayerId;
    TMap<FName, int32> StatsIndexBySourceLayerId;
    SourceSurfaces.Reserve(Layer.SameMeshSource.InnerSlotPriority.Num());

    for (int32 PriorityIndex = 0; PriorityIndex < Layer.SameMeshSource.InnerSlotPriority.Num(); ++PriorityIndex)
    {
        const FWetClothingTransparencyInnerSlot& InnerSlot = Layer.SameMeshSource.InnerSlotPriority[PriorityIndex];
        if (!InnerSlot.bEnabled || InnerSlot.MaterialSlotIndex == INDEX_NONE)
        {
            continue;
        }
        if (!RuntimeMesh->GetMaterials().IsValidIndex(InnerSlot.MaterialSlotIndex))
        {
            OutWarnings.Add(FString::Printf(TEXT("Inner Source Part priority %d references an unavailable material slot."), PriorityIndex));
            continue;
        }

        const FName SourceLayerId = MakeInnerSourceLayerId(PriorityIndex);
        const FDWCBakeResolvedLayer SourceLayer = MakeResolvedLayer(
            RuntimeMesh,
            SourceLayerId,
            PriorityIndex,
            InnerSlot.MaxHitDistance);
        FDWCRevealBakeSurface SourceSurface;
        if (!FDWCRevealBakeSurfaceBuilder::BuildReferencePoseSurface(
                SourceLayer,
                LODIndex,
                InnerSlot.SourceUVChannel,
                SourceSurface,
                &BuildError))
        {
            OutWarnings.Add(FString::Printf(TEXT("Inner Source Part '%s' was skipped: %s"), *InnerSlot.MaterialSlotName.ToString(), *BuildError));
            continue;
        }

        FilterSurfaceToMaterialSlot(SourceSurface, InnerSlot.MaterialSlotIndex);
        if (SourceSurface.Triangles.IsEmpty())
        {
            OutWarnings.Add(FString::Printf(TEXT("Inner Source Part '%s' has no triangles at the selected LOD."), *InnerSlot.MaterialSlotName.ToString()));
            continue;
        }

        FDWCTransparencySourceHitStats& Stats = OutResult.SourceStats.AddDefaulted_GetRef();
        Stats.PriorityIndex = PriorityIndex;
        Stats.MaterialSlotIndex = InnerSlot.MaterialSlotIndex;
        Stats.MaterialSlotName = InnerSlot.MaterialSlotName;
        StatsIndexBySourceLayerId.Add(SourceLayerId, OutResult.SourceStats.Num() - 1);
        PriorityBySourceLayerId.Add(SourceLayerId, PriorityIndex);

        if (UMaterialInterface* SourceMaterial = RuntimeMesh->GetMaterials()[InnerSlot.MaterialSlotIndex].MaterialInterface)
        {
            if (UTexture2D* SourceTexture = FDWCRevealBakeSourceResolver::ResolveRevealSourceBaseColorTexture(SourceMaterial))
            {
                FWetClothingTextureReadback TextureData;
                FString TextureError;
                if (FWetClothingTextureReadbackUtils::TryReadTextureSourceData(SourceTexture, TextureData, TextureError))
                {
                    SourceTextureDataByLayerId.Add(SourceLayerId, MoveTemp(TextureData));
                }
                else
                {
                    OutWarnings.Add(FString::Printf(TEXT("Inner Source Part '%s' has no readable base-color source texture: %s"), *InnerSlot.MaterialSlotName.ToString(), *TextureError));
                }
            }
            else
            {
                OutWarnings.Add(FString::Printf(TEXT("Inner Source Part '%s' has no resolvable base-color texture; white will be used for its valid hits."), *InnerSlot.MaterialSlotName.ToString()));
            }
        }
        SourceSurfaces.Add(MoveTemp(SourceSurface));
    }

    if (SourceSurfaces.IsEmpty())
    {
        OutSummary = TEXT("No eligible Inner Source Part surface could be built for transparency ray projection.");
        return false;
    }

    FDWCRevealBakeRayProjectionSettings ProjectionSettings;
    ProjectionSettings.RayStartOffset = Layer.RaySettings.RayStartOffset;
    ProjectionSettings.RayLengthScale = 1.0f;
    ProjectionSettings.MinHitDistance = Layer.RaySettings.MinHitDistance;
    ProjectionSettings.bRespectSourceLayerOrder = true;
    ProjectionSettings.bRespectBlockers = false;
    ProjectionSettings.bPreferLowerSourceLayerOrder = true;
    ProjectionSettings.bRespectPerSourceMaxDistance = true;
    ProjectionSettings.bUseNormalAlignmentConfidence = false;

    TArray<FDWCRevealBakeRayHit> RayHits;
    if (!FDWCRevealBakeRayProjector::ProjectSamplesToSources(
            OuterSurface,
            SourceSurfaces,
            OuterSamples,
            ProjectionSettings,
            RayHits,
            &BuildError))
    {
        OutSummary = FString::Printf(TEXT("Transparency ray projection failed: %s"), *BuildError);
        return false;
    }

    OutResult.LayerGuid = Layer.LayerGuid;
    OutResult.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    OutResult.UVChannelIndex = Layer.TargetSurface.OuterUVChannel;
    OutResult.LODIndex = LODIndex;
    OutResult.Resolution = BakeResolution;
    OutResult.OuterSampleCount = OuterSamples.Num();
    OutResult.OverlappedUVPixelCount = OverlappedPixelCount;
    OutResult.InnerColorBuffer.Init(FColor::Black, PixelCount);
    OutResult.AutoAlphaBuffer.Init(0, PixelCount);
    OutResult.OuterCoverageBuffer.Init(0, PixelCount);
    OutResult.ValidHitBuffer.Init(0, PixelCount);
    OutResult.HitDistanceBuffer.Init(0.0f, PixelCount);
    OutResult.RayConfidenceBuffer.Init(0, PixelCount);
    OutResult.SourcePriorityBuffer.Init(INDEX_NONE, PixelCount);

    for (const FDWCRevealBakeTexelSample& Sample : OuterSamples)
    {
        if (Sample.Pixel.X >= 0 && Sample.Pixel.Y >= 0 &&
            Sample.Pixel.X < Resolution && Sample.Pixel.Y < Resolution)
        {
            OutResult.OuterCoverageBuffer[Sample.Pixel.Y * Resolution + Sample.Pixel.X] = 1;
        }
    }

    for (const FDWCRevealBakeRayHit& Hit : RayHits)
    {
        if (Hit.Pixel.X < 0 || Hit.Pixel.Y < 0 || Hit.Pixel.X >= Resolution || Hit.Pixel.Y >= Resolution)
        {
            continue;
        }
        const int32 PixelIndex = Hit.Pixel.Y * Resolution + Hit.Pixel.X;
        if (!Hit.bHit)
        {
            ++OutResult.NoHitCount;
            continue;
        }

        ++OutResult.ValidHitCount;
        OutResult.ValidHitBuffer[PixelIndex] = 1;
        OutResult.HitDistanceBuffer[PixelIndex] = Hit.Distance;
        OutResult.RayConfidenceBuffer[PixelIndex] = static_cast<uint8>(FMath::RoundToInt(
            FMath::Clamp(Hit.Confidence, 0.0f, 1.0f) * 255.0f));
        OutResult.AutoAlphaBuffer[PixelIndex] = static_cast<uint8>(FMath::RoundToInt(
            CalculateAutoAlpha(Layer.RaySettings, Hit) * 255.0f));
        if (const int32* PriorityIndex = PriorityBySourceLayerId.Find(Hit.SourceLayerId))
        {
            OutResult.SourcePriorityBuffer[PixelIndex] = static_cast<int16>(
                FMath::Clamp(*PriorityIndex, 0, static_cast<int32>(MAX_int16)));
        }
        if (const int32* StatsIndex = StatsIndexBySourceLayerId.Find(Hit.SourceLayerId);
            StatsIndex != nullptr && OutResult.SourceStats.IsValidIndex(*StatsIndex))
        {
            ++OutResult.SourceStats[*StatsIndex].HitCount;
        }
        if (const FWetClothingTextureReadback* SourceTexture = SourceTextureDataByLayerId.Find(Hit.SourceLayerId))
        {
            OutResult.InnerColorBuffer[PixelIndex] = SampleTextureBilinear(*SourceTexture, Hit.SourceUV).ToFColor(true);
        }
        else
        {
            OutResult.InnerColorBuffer[PixelIndex] = FColor::White;
        }
    }

    const FDWCDataUVLODMetadata* DataUVMetadata = WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
    FString SignatureSource = FString::Printf(
        TEXT("DWCTransparencyAutoMap_v3|Mesh=%s|Layer=%s|Slot=%d|UV=%d|LOD=%d|Resolution=%d|Address=%d|RayStart=%.9g|Min=%.9g|Full=%.9g|None=%.9g|Max=%.9g|DataUV=%s"),
        *GetPathNameSafe(RuntimeMesh),
        *Layer.LayerGuid.ToString(EGuidFormats::DigitsWithHyphens),
        Layer.TargetSurface.OuterMaterialSlotIndex,
        Layer.TargetSurface.OuterUVChannel,
        LODIndex,
        Resolution,
        static_cast<int32>(Layer.TargetSurface.UVAddressMode),
        Layer.RaySettings.RayStartOffset,
        Layer.RaySettings.MinHitDistance,
        Layer.RaySettings.FullTransparencyDistance,
        Layer.RaySettings.NoTransparencyDistance,
        Layer.RaySettings.MaxRayDistance,
        DataUVMetadata != nullptr ? *DataUVMetadata->DataUVOutputSignature : TEXT("Missing"));
    for (int32 PriorityIndex = 0; PriorityIndex < Layer.SameMeshSource.InnerSlotPriority.Num(); ++PriorityIndex)
    {
        const FWetClothingTransparencyInnerSlot& InnerSlot =
            Layer.SameMeshSource.InnerSlotPriority[PriorityIndex];
        SignatureSource += FString::Printf(
            TEXT("|Inner=%d,%d,%d,%d,%.9g,%s"),
            PriorityIndex,
            InnerSlot.bEnabled ? 1 : 0,
            InnerSlot.MaterialSlotIndex,
            InnerSlot.SourceUVChannel,
            InnerSlot.MaxHitDistance,
            *InnerSlot.MaterialSlotName.ToString());
        if (RuntimeMesh->GetMaterials().IsValidIndex(InnerSlot.MaterialSlotIndex))
        {
            SignatureSource += FString::Printf(
                TEXT(",%s"),
                *GetPathNameSafe(RuntimeMesh->GetMaterials()[InnerSlot.MaterialSlotIndex].MaterialInterface));
        }
    }
    OutResult.BuildSignature = FMD5::HashAnsiString(*SignatureSource);

    if (OverlappedPixelCount > 0)
    {
        OutWarnings.Add(FString::Printf(
            TEXT("The target DWC Data UV contains %d overlapping rasterized pixel(s). A dedicated Transparency UV channel is recommended."),
            OverlappedPixelCount));
    }

    OutSummary = FString::Printf(
        TEXT("Generated %d outer samples: %d valid hit(s), %d no-hit sample(s)."),
        OutResult.OuterSampleCount,
        OutResult.ValidHitCount,
        OutResult.NoHitCount);
    return true;
}
