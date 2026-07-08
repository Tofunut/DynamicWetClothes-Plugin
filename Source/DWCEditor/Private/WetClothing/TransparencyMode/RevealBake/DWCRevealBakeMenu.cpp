#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeMenu.h"

#include "Bake/DWCBakeProjection.h"
#include "Bake/DWCBakeSurface.h"
#include "Components/DWCBakeComponent.h"
#include "Editor.h"
#include "Misc/MessageDialog.h"
#include "Runtime/Engine/Classes/Engine/Selection.h"
#include "Runtime/Engine/Classes/GameFramework/Actor.h"
#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeLog.h"
#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeMaterialBuilder.h"
#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeSourceResolver.h"
#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeSurfaceCache.h"
#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeSurfaceResolver.h"
#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeTextureWriter.h"
#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeUtilities.h"

int32 FDWCRevealBakeMenu::GetRevealBakeResolution(const UDWCBakeComponent& BakeComponent)
{
    return FMath::Clamp(BakeComponent.RevealBakeResolution, MinRevealBakeResolution, MaxRevealBakeResolution);
}

TArray<AActor*> FDWCRevealBakeMenu::GetSelectedActors()
{
    TArray<AActor*> Actors;
    if (GEditor == nullptr)
    {
        return Actors;
    }

    USelection* SelectedActors = GEditor->GetSelectedActors();
    if (SelectedActors == nullptr)
    {
        return Actors;
    }

    for (FSelectionIterator It(*SelectedActors); It; ++It)
    {
        if (AActor* Actor = Cast<AActor>(*It))
        {
            Actors.Add(Actor);
        }
    }

    return Actors;
}

int32 FDWCRevealBakeMenu::CountRevealHits(const TArray<FDWCBakeRayHit>& Hits)
{
    int32 HitCount = 0;
    for (const FDWCBakeRayHit& Hit : Hits)
    {
        if (Hit.bHit)
        {
            ++HitCount;
        }
    }
    return HitCount;
}

bool FDWCRevealBakeMenu::BuildOuterSurface(
    const AActor&                Actor,
    const FDWCBakeResolvedLayer& OuterLayer,
    const int32                  OuterLayerIndex,
    FDWCRevealBakeSurfaceCache&  SurfaceCache,
    FDWCBakeSurface&             OutOuterSurface,
    FString&                     OutErrorMessage)
{
    const double StartTime = FPlatformTime::Seconds();
    const FDWCBakeSurface* CachedOuterSurface = SurfaceCache.FindOrBuild(
        OuterLayer,
        OuterLayerIndex,
        0,
        OuterLayer.OuterUVChannel,
        OutErrorMessage);
    if (CachedOuterSurface == nullptr)
    {
        return false;
    }

    OutOuterSurface = *CachedOuterSurface;
    UE_LOG(
        LogDWCRevealBake,
        Log,
        TEXT("DWC Reveal Bake: Outer surface ready. Actor='%s', Layer='%s', Triangles=%d, Time=%.2f ms."),
        *Actor.GetName(),
        *OuterLayer.LayerId.ToString(),
        OutOuterSurface.Triangles.Num(),
        FDWCRevealBakeUtilities::GetElapsedMilliseconds(StartTime));
    return true;
}

bool FDWCRevealBakeMenu::BuildSourceSurfaces(
    const AActor&                Actor,
    const FDWCBakeSnapshot&      Snapshot,
    const FDWCBakeResolvedLayer& OuterLayer,
    FDWCRevealBakeSurfaceCache&  SurfaceCache,
    TArray<FDWCBakeSurface>&     OutSourceSurfaces,
    FString&                     OutErrorMessage)
{
    const double StartTime = FPlatformTime::Seconds();
    OutSourceSurfaces = FDWCRevealBakeSurfaceResolver::BuildSourceSurfacesForOuter(Snapshot, OuterLayer, SurfaceCache, OutErrorMessage);
    if (OutSourceSurfaces.Num() == 0)
    {
        OutErrorMessage = FString::Printf(TEXT("No source surfaces found for outer layer '%s'."), *OuterLayer.LayerId.ToString());
        return false;
    }

    UE_LOG(
        LogDWCRevealBake,
        Log,
        TEXT("DWC Reveal Bake: Source surfaces ready. Actor='%s', OuterLayer='%s', Surfaces=%d, Triangles=%d, Time=%.2f ms."),
        *Actor.GetName(),
        *OuterLayer.LayerId.ToString(),
        OutSourceSurfaces.Num(),
        FDWCRevealBakeSurfaceResolver::CountTriangles(OutSourceSurfaces),
        FDWCRevealBakeUtilities::GetElapsedMilliseconds(StartTime));
    return true;
}

bool FDWCRevealBakeMenu::BuildOuterTexelSamples(
    const AActor&                  Actor,
    const UDWCBakeComponent&       BakeComponent,
    const FDWCBakeResolvedLayer&   OuterLayer,
    const FDWCBakeSurface&         OuterSurface,
    FDWCBakeTexelSamplingSettings& OutSamplingSettings,
    TArray<FDWCBakeTexelSample>&   OutSamples,
    FString&                       OutErrorMessage)
{
    const int32 RevealBakeResolution = GetRevealBakeResolution(BakeComponent);
    OutSamplingSettings = FDWCBakeTexelSamplingSettings();
    OutSamplingSettings.Resolution = FIntPoint(RevealBakeResolution, RevealBakeResolution);
    OutSamplingSettings.MaterialSlotIndex = INDEX_NONE;

    const double StartTime = FPlatformTime::Seconds();
    if (!FDWCBakeTexelSampler::BuildOuterTexelSamples(OuterSurface, OutSamplingSettings, OutSamples, &OutErrorMessage))
    {
        return false;
    }

    UE_LOG(
        LogDWCRevealBake,
        Log,
        TEXT("DWC Reveal Bake: Texel sampling complete. Actor='%s', Layer='%s', Resolution=%dx%d, Samples=%d, Time=%.2f ms."),
        *Actor.GetName(),
        *OuterLayer.LayerId.ToString(),
        OutSamplingSettings.Resolution.X,
        OutSamplingSettings.Resolution.Y,
        OutSamples.Num(),
        FDWCRevealBakeUtilities::GetElapsedMilliseconds(StartTime));
    return true;
}

bool FDWCRevealBakeMenu::ProjectRevealSamples(
    const AActor&                      Actor,
    const FDWCBakeResolvedLayer&       OuterLayer,
    const FDWCBakeSurface&             OuterSurface,
    const TArray<FDWCBakeSurface>&     SourceSurfaces,
    const TArray<FDWCBakeTexelSample>& Samples,
    TArray<FDWCBakeRayHit>&            OutHits,
    FString&                           OutErrorMessage)
{
    FDWCBakeRayProjectionSettings ProjectionSettings;
    const double StartTime = FPlatformTime::Seconds();
    if (!FDWCBakeRayProjector::ProjectSamplesToSources(
            OuterSurface,
            SourceSurfaces,
            Samples,
            ProjectionSettings,
            OutHits,
            &OutErrorMessage))
    {
        return false;
    }

    UE_LOG(
        LogDWCRevealBake,
        Log,
        TEXT("DWC Reveal Bake: Ray projection complete. Actor='%s', Layer='%s', Hits=%d/%d, Time=%.2f ms."),
        *Actor.GetName(),
        *OuterLayer.LayerId.ToString(),
        CountRevealHits(OutHits),
        OutHits.Num(),
        FDWCRevealBakeUtilities::GetElapsedMilliseconds(StartTime));
    return true;
}

FDWCRevealBakeTextureWriteSettings FDWCRevealBakeMenu::BuildTextureWriteSettings(
    const AActor&                         Actor,
    const UDWCBakeComponent&              BakeComponent,
    const FDWCBakeSnapshot&               Snapshot,
    const FDWCBakeResolvedLayer&          OuterLayer,
    const FDWCBakeTexelSamplingSettings&  SamplingSettings,
    const TArray<FDWCBakeSurface>&        SourceSurfaces)
{
    FDWCRevealBakeTextureWriteSettings TextureSettings;
    TextureSettings.Resolution = SamplingSettings.Resolution;
    TextureSettings.PackagePath = FDWCRevealBakeUtilities::GetDefaultRevealBakePackagePath();
    TextureSettings.MaskFeatherRadiusPixels = FMath::Max(0.0f, BakeComponent.RevealMaskFeatherRadiusPixels);
    TextureSettings.AssetNamePrefix = FString::Printf(
        TEXT("T_DWCReveal_%s_%s"),
        *FDWCRevealBakeUtilities::SanitizeAssetToken(Actor.GetName()),
        *FDWCRevealBakeUtilities::SanitizeAssetToken(OuterLayer.LayerId.ToString()));
    TextureSettings.SourceLayerIds = FDWCRevealBakeSourceResolver::BuildRevealSourceLayerIds(SourceSurfaces);
    FDWCRevealBakeSourceResolver::PopulateSourceLayerTextures(Snapshot, TextureSettings.SourceLayerIds, TextureSettings);
    return TextureSettings;
}

bool FDWCRevealBakeMenu::WriteRevealTextures(
    const AActor&                             Actor,
    const FDWCBakeResolvedLayer&              OuterLayer,
    const TArray<FDWCBakeRayHit>&             Hits,
    const FDWCRevealBakeTextureWriteSettings& TextureSettings,
    FDWCRevealBakeTextureSet&                 OutTextureSet,
    FString&                                  OutErrorMessage)
{
    const double StartTime = FPlatformTime::Seconds();
    if (!FDWCRevealBakeTextureWriter::WriteTextures(Hits, TextureSettings, OutTextureSet, &OutErrorMessage))
    {
        return false;
    }

    UE_LOG(
        LogDWCRevealBake,
        Log,
        TEXT("DWC Reveal Bake: Texture write complete. Actor='%s', Layer='%s', SourceLayers=%d, Time=%.2f ms."),
        *Actor.GetName(),
        *OuterLayer.LayerId.ToString(),
        TextureSettings.SourceLayerIds.Num(),
        FDWCRevealBakeUtilities::GetElapsedMilliseconds(StartTime));
    return true;
}

void FDWCRevealBakeMenu::ApplyRevealMaterialPreview(
    const AActor&                             Actor,
    const UDWCBakeComponent&                  BakeComponent,
    const FDWCBakeSnapshot&                   Snapshot,
    const FDWCBakeResolvedLayer&              OuterLayer,
    const FDWCRevealBakeTextureSet&           TextureSet,
    const FDWCRevealBakeTextureWriteSettings& TextureSettings)
{
    const double StartTime = FPlatformTime::Seconds();
    FDWCRevealBakeMaterialBuilder::ApplyLookupPreviewToOuterMaterials(
        Actor,
        BakeComponent,
        Snapshot,
        OuterLayer,
        TextureSet,
        TextureSettings.SourceLayerIds);
    UE_LOG(
        LogDWCRevealBake,
        Log,
        TEXT("DWC Reveal Bake: Preview material update complete. Actor='%s', Layer='%s', Time=%.2f ms."),
        *Actor.GetName(),
        *OuterLayer.LayerId.ToString(),
        FDWCRevealBakeUtilities::GetElapsedMilliseconds(StartTime));
}

bool FDWCRevealBakeMenu::BakeOuterLayer(
    const AActor&                Actor,
    const UDWCBakeComponent&     BakeComponent,
    const FDWCBakeSnapshot&      Snapshot,
    const FDWCBakeResolvedLayer& OuterLayer,
    const int32                  OuterLayerIndex,
    FDWCRevealBakeSurfaceCache&  SurfaceCache,
    FString&                     OutErrorMessage)
{
    const double LayerStartTime = FPlatformTime::Seconds();
    UE_LOG(
        LogDWCRevealBake,
        Log,
        TEXT("DWC Reveal Bake: Begin outer layer. Actor='%s', Layer='%s'."),
        *Actor.GetName(),
        *OuterLayer.LayerId.ToString());

    FDWCBakeSurface OuterSurface;
    if (!BuildOuterSurface(Actor, OuterLayer, OuterLayerIndex, SurfaceCache, OuterSurface, OutErrorMessage))
    {
        return false;
    }

    TArray<FDWCBakeSurface> SourceSurfaces;
    if (!BuildSourceSurfaces(Actor, Snapshot, OuterLayer, SurfaceCache, SourceSurfaces, OutErrorMessage))
    {
        return false;
    }

    FDWCBakeTexelSamplingSettings SamplingSettings;
    TArray<FDWCBakeTexelSample> Samples;
    if (!BuildOuterTexelSamples(Actor, BakeComponent, OuterLayer, OuterSurface, SamplingSettings, Samples, OutErrorMessage))
    {
        return false;
    }

    TArray<FDWCBakeRayHit> Hits;
    if (!ProjectRevealSamples(Actor, OuterLayer, OuterSurface, SourceSurfaces, Samples, Hits, OutErrorMessage))
    {
        return false;
    }

    const FDWCRevealBakeTextureWriteSettings TextureSettings =
        BuildTextureWriteSettings(Actor, BakeComponent, Snapshot, OuterLayer, SamplingSettings, SourceSurfaces);

    FDWCRevealBakeTextureSet TextureSet;
    if (!WriteRevealTextures(Actor, OuterLayer, Hits, TextureSettings, TextureSet, OutErrorMessage))
    {
        return false;
    }

    ApplyRevealMaterialPreview(Actor, BakeComponent, Snapshot, OuterLayer, TextureSet, TextureSettings);

    UE_LOG(
        LogDWCRevealBake,
        Log,
        TEXT("DWC Reveal Bake: End outer layer. Actor='%s', Layer='%s', Total=%.2f ms."),
        *Actor.GetName(),
        *OuterLayer.LayerId.ToString(),
        FDWCRevealBakeUtilities::GetElapsedMilliseconds(LayerStartTime));
    return true;
}

void FDWCRevealBakeMenu::BakeSelectedActors()
{
    const double BakeStartTime = FPlatformTime::Seconds();
    const TArray<AActor*> SelectedActors = GetSelectedActors();
    if (SelectedActors.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Select an actor with a DWC Bake Component first.")));
        return;
    }

    int32 BakedLayerCount = 0;
    TArray<FString> Errors;

    for (AActor* Actor : SelectedActors)
    {
        if (Actor == nullptr)
        {
            continue;
        }

        const double ActorStartTime = FPlatformTime::Seconds();
        int32 ActorBakedLayerCount = 0;
        UE_LOG(LogDWCRevealBake, Log, TEXT("DWC Reveal Bake: Begin actor. Actor='%s'."), *Actor->GetName());

        //Find if there is any BakeComponent in Actor
        UDWCBakeComponent* BakeComponent = Actor->FindComponentByClass<UDWCBakeComponent>();
        if (BakeComponent == nullptr)
        {
            UE_LOG(
                LogDWCRevealBake,
                Log,
                TEXT("DWC Reveal Bake: Skip actor without DWC Bake Component. Actor='%s', Time=%.2f ms."),
                *Actor->GetName(),
                FDWCRevealBakeUtilities::GetElapsedMilliseconds(ActorStartTime));
            continue;
        }

        FDWCBakeSnapshot Snapshot;
        const double SnapshotStartTime = FPlatformTime::Seconds();
        if (!BakeComponent->BuildBakeSnapshot(Snapshot))
        {
            Errors.Add(FString::Printf(TEXT("%s: Failed to build DWC bake snapshot."), *Actor->GetName()));
            UE_LOG(
                LogDWCRevealBake,
                Warning,
                TEXT("DWC Reveal Bake: Snapshot build failed. Actor='%s', Time=%.2f ms."),
                *Actor->GetName(),
                FDWCRevealBakeUtilities::GetElapsedMilliseconds(SnapshotStartTime));
            UE_LOG(
                LogDWCRevealBake,
                Warning,
                TEXT("DWC Reveal Bake: End actor with failure. Actor='%s', BakedOuterLayers=%d, Total=%.2f ms."),
                *Actor->GetName(),
                ActorBakedLayerCount,
                FDWCRevealBakeUtilities::GetElapsedMilliseconds(ActorStartTime));
            continue;
        }
        UE_LOG(
            LogDWCRevealBake,
            Log,
            TEXT("DWC Reveal Bake: Snapshot build complete. Actor='%s', Layers=%d, Time=%.2f ms."),
            *Actor->GetName(),
            Snapshot.Layers.Num(),
            FDWCRevealBakeUtilities::GetElapsedMilliseconds(SnapshotStartTime));

        FDWCRevealBakeSurfaceCache SurfaceCache;
        for (int32 LayerIndex = 0; LayerIndex < Snapshot.Layers.Num(); ++LayerIndex)
        {
            const FDWCBakeResolvedLayer& Layer = Snapshot.Layers[LayerIndex];
            if (!Layer.bCanBeWetOuterLayer)
            {
                continue;
            }

            FString ErrorMessage;
            if (BakeOuterLayer(*Actor, *BakeComponent, Snapshot, Layer, LayerIndex, SurfaceCache, ErrorMessage))
            {
                ++BakedLayerCount;
                ++ActorBakedLayerCount;
            }
            else
            {
                Errors.Add(FString::Printf(
                    TEXT("%s/%s: %s"),
                    *Actor->GetName(),
                    *Layer.LayerId.ToString(),
                    ErrorMessage.IsEmpty() ? TEXT("Unknown bake failure.") : *ErrorMessage));
            }
        }

        UE_LOG(
            LogDWCRevealBake,
            Log,
            TEXT("DWC Reveal Bake: End actor. Actor='%s', BakedOuterLayers=%d, Total=%.2f ms."),
            *Actor->GetName(),
            ActorBakedLayerCount,
            FDWCRevealBakeUtilities::GetElapsedMilliseconds(ActorStartTime));
    }

    FString ResultMessage = FString::Printf(
        TEXT("DWC reveal bake complete.\nBaked outer layers: %d\nOutput: %s"),
        BakedLayerCount,
        FDWCRevealBakeUtilities::GetDefaultRevealBakePackagePath());

    if (Errors.Num() > 0)
    {
        ResultMessage += TEXT("\n\nErrors:\n");
        const int32 MaxErrorsToShow = FMath::Min(Errors.Num(), 8);
        for (int32 ErrorIndex = 0; ErrorIndex < MaxErrorsToShow; ++ErrorIndex)
        {
            ResultMessage += FString::Printf(TEXT("- %s\n"), *Errors[ErrorIndex]);
        }
        if (Errors.Num() > MaxErrorsToShow)
        {
            ResultMessage += FString::Printf(TEXT("- ...and %d more."), Errors.Num() - MaxErrorsToShow);
        }
    }

    UE_LOG(
        LogDWCRevealBake,
        Log,
        TEXT("DWC Reveal Bake: Complete. SelectedActors=%d, BakedOuterLayers=%d, Errors=%d, Total=%.2f ms."),
        SelectedActors.Num(),
        BakedLayerCount,
        Errors.Num(),
        FDWCRevealBakeUtilities::GetElapsedMilliseconds(BakeStartTime));

    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ResultMessage));
}
