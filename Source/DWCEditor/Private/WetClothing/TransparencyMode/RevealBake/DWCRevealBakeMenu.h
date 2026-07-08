#pragma once

#include "CoreMinimal.h"

class AActor;
class UDWCBakeComponent;
class FDWCRevealBakeSurfaceCache;
struct FDWCBakeRayHit;
struct FDWCBakeResolvedLayer;
struct FDWCBakeSnapshot;
struct FDWCBakeSurface;
struct FDWCBakeTexelSample;
struct FDWCBakeTexelSamplingSettings;
struct FDWCRevealBakeTextureSet;
struct FDWCRevealBakeTextureWriteSettings;

class FDWCRevealBakeMenu
{
  public:
    static void BakeSelectedActors();

  private:
    static constexpr int32 MinRevealBakeResolution = 16;
    static constexpr int32 MaxRevealBakeResolution = 8192;

    static int32 GetRevealBakeResolution(const UDWCBakeComponent& BakeComponent);
    static TArray<AActor*> GetSelectedActors();
    static int32 CountRevealHits(const TArray<FDWCBakeRayHit>& Hits);

    static bool BuildOuterSurface(
        const AActor&                Actor,
        const FDWCBakeResolvedLayer& OuterLayer,
        int32                        OuterLayerIndex,
        FDWCRevealBakeSurfaceCache&  SurfaceCache,
        FDWCBakeSurface&             OutOuterSurface,
        FString&                     OutErrorMessage);

    static bool BuildSourceSurfaces(
        const AActor&                Actor,
        const FDWCBakeSnapshot&      Snapshot,
        const FDWCBakeResolvedLayer& OuterLayer,
        FDWCRevealBakeSurfaceCache&  SurfaceCache,
        TArray<FDWCBakeSurface>&     OutSourceSurfaces,
        FString&                     OutErrorMessage);

    static bool BuildOuterTexelSamples(
        const AActor&                    Actor,
        const UDWCBakeComponent&         BakeComponent,
        const FDWCBakeResolvedLayer&     OuterLayer,
        const FDWCBakeSurface&           OuterSurface,
        FDWCBakeTexelSamplingSettings&   OutSamplingSettings,
        TArray<FDWCBakeTexelSample>&     OutSamples,
        FString&                         OutErrorMessage);

    static bool ProjectRevealSamples(
        const AActor&                       Actor,
        const FDWCBakeResolvedLayer&        OuterLayer,
        const FDWCBakeSurface&              OuterSurface,
        const TArray<FDWCBakeSurface>&      SourceSurfaces,
        const TArray<FDWCBakeTexelSample>&  Samples,
        TArray<FDWCBakeRayHit>&             OutHits,
        FString&                            OutErrorMessage);

    static FDWCRevealBakeTextureWriteSettings BuildTextureWriteSettings(
        const AActor&                       Actor,
        const UDWCBakeComponent&            BakeComponent,
        const FDWCBakeSnapshot&             Snapshot,
        const FDWCBakeResolvedLayer&        OuterLayer,
        const FDWCBakeTexelSamplingSettings& SamplingSettings,
        const TArray<FDWCBakeSurface>&      SourceSurfaces);

    static bool WriteRevealTextures(
        const AActor&                               Actor,
        const FDWCBakeResolvedLayer&                OuterLayer,
        const TArray<FDWCBakeRayHit>&               Hits,
        const FDWCRevealBakeTextureWriteSettings&   TextureSettings,
        FDWCRevealBakeTextureSet&                   OutTextureSet,
        FString&                                    OutErrorMessage);

    static void ApplyRevealMaterialPreview(
        const AActor&                              Actor,
        const UDWCBakeComponent&                   BakeComponent,
        const FDWCBakeSnapshot&                    Snapshot,
        const FDWCBakeResolvedLayer&               OuterLayer,
        const FDWCRevealBakeTextureSet&            TextureSet,
        const FDWCRevealBakeTextureWriteSettings&  TextureSettings);

    static bool BakeOuterLayer(
        const AActor&                Actor,
        const UDWCBakeComponent&     BakeComponent,
        const FDWCBakeSnapshot&      Snapshot,
        const FDWCBakeResolvedLayer& OuterLayer,
        int32                       OuterLayerIndex,
        FDWCRevealBakeSurfaceCache&  SurfaceCache,
        FString&                     OutErrorMessage);
};
