//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"
#include "Runtime/Engine/Classes/Components/ActorComponent.h"

#include "DWCBakeComponent.generated.h"

UCLASS(ClassGroup = (DWC), DisplayName = "DWC Bake Component", meta = (BlueprintSpawnableComponent))
class DWC_API UDWCBakeComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    UDWCBakeComponent();

    UFUNCTION(BlueprintCallable, Category = "DWC|Bake")
    bool BuildBakeSnapshot(FDWCBakeSnapshot& OutSnapshot) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Bake")
    TArray<FDWCBakeLayer> Layers;

  private:
    bool ResolveLayer(const FDWCBakeLayer& Layer, FDWCBakeResolvedLayer& OutResolvedLayer) const;
    USkeletalMeshComponent* ResolveLayerComponent(const FDWCBakeLayer& Layer) const;
    FTransform MakeBakeTransform(const USceneComponent& SceneComponent) const;
    FDWCBakeSourceContext MakeSourceContext() const;
    FString MakeBuildSignature(const TArray<FDWCBakeResolvedLayer>& ResolvedLayers) const;
};
