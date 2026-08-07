//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"
#include "GPU/DWCGPUBackend.h"

class FAdvancedPreviewScene;
class FWetnessProfileViewportClient;
class UMaterial;
class UMaterialInstanceDynamic;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UPrimitiveComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;
class UTexture;
class UWetnessProfile;
class STextBlock;

class SWetnessProfileViewport : public SEditorViewport, public FGCObject
{
  public:
    enum class EPreviewMode : uint8
    {
        Lit,
        Absorbed,
        SurfaceCoverage,
        FinalDropletCoverage,
        DropletNormal,
        DropletStampTest
    };

    enum class EPreviewBehavior : uint8
    {
        Manual,
        Simulation
    };

    SLATE_BEGIN_ARGS(SWetnessProfileViewport) {}
    SLATE_ARGUMENT(UWetnessProfile*, WetnessProfile)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetnessProfileViewport() override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("SWetnessProfileViewport");
    }

    /** Updates only MID parameters and invalidates the viewport. */
    void RefreshFromProfile();
    virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
    virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
    void FocusOnPreviewMesh(bool bInstant = false);

    void SetPreviewAbsorbedWater(float InAmount);
    float GetPreviewAbsorbedWater() const { return PreviewAbsorbedWater; }

    void SetPreviewSurfaceWater(float InAmount);
    float GetPreviewSurfaceWater() const { return PreviewSurfaceWater; }

    void SetPreviewDropletDetailSizes(float InDroplet1DetailSize, float InDroplet2DetailSize);
    void SetInteractionCursorScale(float InScale);
    float GetInteractionCursorScale() const { return InteractionCursorScale; }

    void SetPreviewMode(EPreviewMode InPreviewMode);
    EPreviewMode GetPreviewMode() const { return PreviewMode; }

    void SetPreviewBehavior(EPreviewBehavior InBehavior);
    EPreviewBehavior GetPreviewBehavior() const { return PreviewBehavior; }
    void SetPreviewAnimationEnabled(bool bInEnabled);
    void SetPreviewAnimationSpeed(float InSpeed);
    void SetPreviewLoopEnabled(bool bInEnabled);
    void SetPreviewSimulationTarget(
        bool bHasSelection,
        bool bSurfaceSelected,
        bool bSecondarySelected,
        bool bSelectedChannelEnabled);
    void SetPreviewSimulationLayers(bool bAbsorbedEnabled, bool bSurfaceEnabled);
    void SetPreviewDropletVisibility(bool bDroplet1Enabled, bool bDroplet2Enabled);
    void RestartPreviewSimulation();
    void ApplyPreviewSplash();
    bool IsPreviewAnimationEnabled() const { return bPreviewAnimationEnabled; }
    bool IsPreviewLoopEnabled() const { return bPreviewLoopEnabled; }
    bool IsPreviewAbsorbedLayerEnabled() const { return bPreviewAbsorbedLayerEnabled; }
    bool IsPreviewSurfaceLayerEnabled() const { return bPreviewSurfaceLayerEnabled; }
    bool IsPreviewDroplet1Enabled() const { return bPreviewDroplet1Enabled; }
    bool IsPreviewDroplet2Enabled() const { return bPreviewDroplet2Enabled; }
    float GetPreviewAnimationSpeed() const { return PreviewAnimationSpeed; }
    float GetPreviewAnimationTime() const { return PreviewAnimationTime; }
    static constexpr float GetPreviewLoopDuration() { return 8.0f; }

    void SetPreviewSkeletalMeshOverride(USkeletalMesh* InPreviewMesh);
    void ClearPreviewSkeletalMeshOverride();
    void UseSpherePreview();
    USkeletalMesh* GetDisplayedPreviewSkeletalMesh() const;
    bool IsUsingPreviewMeshOverride() const { return bHasPreviewMeshOverride; }

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
    virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void OnFocusViewportToSelection() override;

  private:
    void InitializePreviewComponents();
    void ApplyResolvedPreviewMesh(bool bFocus);
    USkeletalMesh* ResolveProfilePreviewSkeletalMesh() const;
    UPrimitiveComponent* GetActivePreviewComponent() const;
    void RebuildGeneratedSpherePreviewMaterial();
    void RebuildGeneratedPreviewMaterials(USkeletalMesh* SkeletalMesh);
    void RefreshPreviewMaterialParameters();
    void RefreshGeneratedPreviewMaterialParameters();
    void RefreshGeneratedPreviewAnimationTime();
    bool EnsureGPUPreviewSimulator();
    void ShutdownGPUPreviewSimulator();
    void TickGPUPreviewSimulation(float InDeltaTime);
    void BindGPUPreviewTextures();
    FVector2f ResolveScenarioSplashUV() const;
    bool TryResolveCameraCenterSplashUV(FVector2f& OutUV) const;
    void RefreshScenarioSplashUV();
    void RefreshScenarioSplashUVFromCamera();
    void ScheduleSimulationRestart();
    void UpdateRealtimeState();
    FText GetOverlayText() const;

    TWeakObjectPtr<UWetnessProfile> WetnessProfile;
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<FWetnessProfileViewportClient> ViewportClient;
    TObjectPtr<UStaticMeshComponent> PreviewMeshComponent = nullptr;
    TObjectPtr<USkeletalMeshComponent> PreviewSkeletalMeshComponent = nullptr;
    TObjectPtr<UStaticMesh> PreviewSphereMesh = nullptr;
    TObjectPtr<USkeletalMesh> PreviewMeshOverride = nullptr;
    TObjectPtr<UMaterialInterface> PreviewBaseMaterial = nullptr;
    TObjectPtr<UMaterialInstanceDynamic> PreviewMaterialInstance = nullptr;
    TObjectPtr<USkeletalMesh> GeneratedPreviewMesh = nullptr;
    TArray<TObjectPtr<UMaterial>> GeneratedPreviewMaterials;
    TArray<TObjectPtr<UMaterialInstanceConstant>> GeneratedPreviewMaterialInstances;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> GeneratedPreviewDynamicMaterials;
    int32 GeneratedPreviewMaterialSlotCount = 0;
    bool bGeneratedSpherePreviewMaterialValid = false;
    TObjectPtr<UTexture2D> PreviewWetnessMapTexture = nullptr;
    TObjectPtr<UTexture2D> PreviewWetPartDataTexture = nullptr;
    TObjectPtr<UTexture2D> PreviewSurfaceDropletTexture = nullptr;
    TObjectPtr<UTexture2D> PreviewSurfaceFlowDropletTexture = nullptr;
    TObjectPtr<UTexture> PreviewDefaultNormalTexture = nullptr;
    TObjectPtr<UTexture> PreviewDefaultMaskTexture = nullptr;
    TSharedPtr<STextBlock> OverlayText;
    TUniquePtr<IDWCGPUPreviewSimulator> GPUPreviewSimulator;

    bool bHasPreviewMeshOverride = false;
    bool bPreviewAnimationEnabled = true;
    bool bPreviewLoopEnabled = false;
    bool bGPUPreviewUnavailable = false;
    bool bPreviewAbsorbedLayerEnabled = true;
    bool bPreviewSurfaceLayerEnabled = true;
    bool bPreviewDroplet1Enabled = true;
    bool bPreviewDroplet2Enabled = false;
    bool bHasPreviewWaterSelection = false;
    bool bPreviewSurfaceSelection = false;
    bool bPreviewSecondarySelection = false;
    bool bPreviewSelectedChannelEnabled = false;
    float PreviewAbsorbedWater = 0.5f;
    float PreviewSurfaceWater = 1.0f;
    float PreviewDroplet1DetailSize = 1.0f;
    float PreviewDroplet2DetailSize = 1.0f;
    EPreviewMode PreviewMode = EPreviewMode::Lit;
    EPreviewBehavior PreviewBehavior = EPreviewBehavior::Manual;
    float PreviewAnimationSpeed = 1.0f;
    float PreviewAnimationTime = 0.0f;
    float PreviewSimulationAccumulator = 0.0f;
    float PendingSimulationRestartDelay = -1.0f;
    uint32 LastSimulationParameterHash = 0u;
    bool bHasSimulationParameterHash = false;
    float InteractionCursorScale = 1.15f;
    FVector2f PreviewScenarioSplashUV = FVector2f(0.5f, 0.5f);
};
