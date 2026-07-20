#pragma once

#include "CoreMinimal.h"
#include "Core/DWCSimulationMode.h"
#include "Core/WetClothingSettings.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "WetSimulation/WetSimulationStage.h"

class FAdvancedPreviewScene;
class FWetnessProfileViewportClient;
class UMaterialInterface;
class UProceduralMeshComponent;
class USkeletalMeshComponent;
class SRichTextBlock;
class UStaticMeshComponent;
class UWetnessProfile;
struct FWetSimulationStageArgs;

class SWetnessProfileViewport : public SEditorViewport, public FGCObject
{
  public:
    SLATE_BEGIN_ARGS(SWetnessProfileViewport) {}
    SLATE_ARGUMENT(UWetnessProfile*, WetnessProfile)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetnessProfileViewport() override;

    virtual void    AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("SWetnessProfileViewport");
    }

    void RefreshPreviewScene();
    void FocusOnPreviewMesh(bool bInstant = false);
    void SetPreviewSimulationMode(EDWCSimulationMode NewMode);
    EDWCSimulationMode GetPreviewSimulationMode() const { return PreviewSimulationMode; }
    void SetPreviewRainRadius(float InRadius);
    float GetPreviewRainRadius() const { return PreviewRainRadius; }
    void SetPreviewRainAmountScale(float InAmountScale);
    float GetPreviewRainAmountScale() const { return PreviewRainAmountScale; }
    void SetPreviewWetnessDebugColorEnabled(bool bEnabled);
    bool IsPreviewWetnessDebugColorEnabled() const { return bPreviewWetnessDebugColorEnabled; }

  protected:
    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual void                              PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void                              OnFocusViewportToSelection() override;

  private:
    struct FPreviewRainParticle
    {
        FVector Position = FVector::ZeroVector;
        float   Speed = 0.0f;
    };

    void InitializePreviewComponents();
    void ResetPreviewSimulation();
    void ResetRainParticles();
    void RebuildPreviewWetnessRuntime();
    void RefreshPreviewWetnessProfileParameters();
    void UpdatePreviewSimulation(float DeltaSeconds);
    void UpdateRainParticles(float DeltaSeconds);
    void UpdateWetnessSimulation(float DeltaSeconds);
    void AddWetnessAtWorldPoint(const FVector& WorldPoint, float Amount);
    FWetSimulationStageArgs MakePreviewWetSimulationArgs();
    void RebuildWetnessOverlayMesh();
    void RebuildRainParticleMesh();
    void RefreshRainVisuals();
    void RespawnRainParticle(FPreviewRainParticle& Particle, bool bRandomizeHeight);
    int32 GetWetnessSampleIndex(int32 LatIndex, int32 LonIndex) const;
    FVector GetWetnessSamplePosition(int32 LatIndex, int32 LonIndex, float Radius) const;
    UMaterialInterface* ResolveWhitePreviewMaterial();
    UMaterialInterface* ResolveTranslucentVertexColorMaterial();
    FText GetOverlayText() const;

    TWeakObjectPtr<UWetnessProfile>           WetnessProfile;
    TSharedPtr<FAdvancedPreviewScene>         PreviewScene;
    TSharedPtr<FWetnessProfileViewportClient> ViewportClient;
    TObjectPtr<UStaticMeshComponent>          PreviewMeshComponent = nullptr;
    TObjectPtr<USkeletalMeshComponent>        PreviewSimulationMeshComponent = nullptr;
    TObjectPtr<UProceduralMeshComponent>      WetnessOverlayComponent = nullptr;
    TObjectPtr<UProceduralMeshComponent>      RainParticleComponent = nullptr;
    TObjectPtr<UMaterialInterface>            WhitePreviewMaterial = nullptr;
    TObjectPtr<UMaterialInterface>            TranslucentVertexColorMaterial = nullptr;
    TSharedPtr<SRichTextBlock>                OverlayText;
    TUniquePtr<FWetClothingRuntimeData>       PreviewRuntimeData;
    TUniquePtr<FAbsorbedWetnessSimulationState> PreviewSimulationState;
    TUniquePtr<FWetSimulationStage>           PreviewSimulationStage;
    TUniquePtr<FWetClothingMeshSampler>       PreviewMeshSampler;
    FWetClothingSettings                      PreviewWetnessSettings;
    EDWCSimulationMode                        PreviewSimulationMode = EDWCSimulationMode::VertexCPU;
    TArray<FPreviewRainParticle>              RainParticles;
    FRandomStream                             PreviewRandomStream;
    float                                     PreviewTimeSeconds = 0.0f;
    float                                     PreviewWetnessUpdateAccumulator = 0.0f;
    float                                     PreviewRainRadius = 92.0f;
    float                                     PreviewRainAmountScale = 1.0f;
    bool                                      bPreviewWetnessDebugColorEnabled = true;
    bool                                      bWetnessOverlayDirty = true;
    bool                                      bPreviewCameraInitialized = false;
};
