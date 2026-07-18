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

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "DWC|Reveal Preview")
    void ApplyRevealPreviewBlend();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "DWC|Reveal Preview")
    void ClearRevealPreviewBlend();

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Bake")
    TArray<FDWCBakeLayer> Layers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Bake", meta = (ClampMin = "16", UIMin = "128", UIMax = "4096"))
    int32 RevealBakeResolution = 1024;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Bake", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0"))
    float RevealMaskFeatherRadiusPixels = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview", meta = (ClampMin = "0.0", ClampMax = "100.0"))
    float RevealPreviewBlendPercent = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
    float RevealMaskMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
    float RevealConfidenceMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview")
    FLinearColor RevealPreviewUnderColor = FLinearColor(0.8f, 0.55f, 0.42f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview")
    FName RevealPreviewBlendParameterName = TEXT("DWC_RevealPreviewBlend");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview")
    FName UseRevealPreviewParameterName = TEXT("DWC_UseRevealPreview");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview")
    FName RevealMaskMultiplierParameterName = TEXT("DWC_RevealMaskMultiplier");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview")
    FName RevealConfidenceMultiplierParameterName = TEXT("DWC_RevealConfidenceMultiplier");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview")
    FName RevealPreviewUnderColorParameterName = TEXT("DWC_RevealPreviewUnderColor");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview|Lookup")
    FName RevealLookupMapParameterName = TEXT("DWC_RevealLookupMap");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview|Lookup")
    FName RevealColorMapParameterName = TEXT("DWC_RevealColorMap");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview|Lookup")
    FName RevealMaskMapParameterName = TEXT("DWC_RevealMaskMap");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview|Lookup")
    FName RevealConfidenceMapParameterName = TEXT("DWC_RevealConfidenceMap");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview|Lookup")
    FName RevealSourceTexture0ParameterName = TEXT("DWC_RevealSourceTexture0");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview|Lookup")
    FName RevealSourceTexture1ParameterName = TEXT("DWC_RevealSourceTexture1");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview|Lookup")
    FName RevealSourceTexture2ParameterName = TEXT("DWC_RevealSourceTexture2");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Reveal Preview|Lookup")
    FName RevealSourceTexture3ParameterName = TEXT("DWC_RevealSourceTexture3");

  private:
    bool ResolveLayer(const FDWCBakeLayer& Layer, FDWCBakeResolvedLayer& OutResolvedLayer) const;
    USkeletalMeshComponent* ResolveLayerComponent(const FDWCBakeLayer& Layer) const;
    void ApplyRevealPreviewBlendToLayer(const FDWCBakeLayer& Layer, bool bEnabled) const;
    FTransform MakeBakeTransform(const USceneComponent& SceneComponent) const;
    FDWCBakeSourceContext MakeSourceContext() const;
    FString MakeBuildSignature(const TArray<FDWCBakeResolvedLayer>& ResolvedLayers) const;
};
