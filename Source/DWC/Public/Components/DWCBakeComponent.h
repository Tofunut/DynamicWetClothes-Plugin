//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"
#include "Runtime/Engine/Classes/Components/ActorComponent.h"

#include "DWCBakeComponent.generated.h"

UCLASS(NotBlueprintable, ClassGroup = (DWC), DisplayName = "DWC Bake Component")
class DWC_API UDWCBakeComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    UDWCBakeComponent();

    // Internal editor bake API. Intentionally not exposed to Blueprint.
    bool BuildBakeSnapshot(FDWCBakeSnapshot& OutSnapshot) const;

    // Retained as a reflected property so existing serialized bake sources remain readable,
    // but it is intentionally hidden from Details panels and Blueprint.
    UPROPERTY()
    TArray<FDWCBakeLayer> Layers;

  private:
    bool ResolveLayer(const FDWCBakeLayer& Layer, FDWCBakeResolvedLayer& OutResolvedLayer) const;
    USkeletalMeshComponent* ResolveLayerComponent(const FDWCBakeLayer& Layer) const;
    FTransform MakeBakeTransform(const USceneComponent& SceneComponent) const;
    FDWCBakeSourceContext MakeSourceContext() const;
    FString MakeBuildSignature(const TArray<FDWCBakeResolvedLayer>& ResolvedLayers) const;
};
