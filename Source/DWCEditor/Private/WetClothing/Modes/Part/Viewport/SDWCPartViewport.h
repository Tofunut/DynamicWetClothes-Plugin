#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class FAdvancedPreviewScene;
class FDWCPartViewportClient;
class UWetClothingAsset;
class UMaterial;
class UMaterialInterface;
class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USkeletalMeshComponent;
class UTexture2D;

DECLARE_DELEGATE_TwoParams(FOnWetClothingPreviewIslandPicked, int32 /*UVIslandID*/, bool /*bAppendSelection*/);

enum class EDWCSurfaceWaterTilingPreviewCoverageMode : uint8
{
    FullPart,
    SingleCircle
};

enum class EDWCSurfaceWaterTilingPreviewDisplayMode : uint8
{
    Lit,
    DropletNormal
};

/**
 * Part-edit viewport.
 *
 * Normal instances render the source skeletal mesh plus editor-only procedural
 * overlays. Surface Water Tiling instances render the selected slot through a
 * transient DWC preview material and transient preview state textures. Procedural
 * geometry remains exclusive to the normal Part-edit viewport.
 */
class SDWCPartViewport : public SEditorViewport, public FGCObject
{
    friend class FDWCPartViewportClient;

  public:
    SLATE_BEGIN_ARGS(SDWCPartViewport) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(bool, SurfaceWaterTilingPreview)
    SLATE_EVENT(FOnWetClothingPreviewIslandPicked, OnIslandPicked)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SDWCPartViewport() override;

    virtual void    AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("SDWCPartViewport");
    }

    void  RefreshPreviewMesh();
    void  SetHighlightedMaterialSlot(int32 SlotIndex);
    void  ClearMaterialSlotHighlight();
    void  SetSelectableIslands(const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& InIslands);
    void  SetHighlightedUVIslandIDs(const TSet<int32>& InUVIslandIDs);
    void  ClearHighlightedIsland();
    void  SetWetPartIslandAssignments(const TMap<int32, int32>& InUVIslandToWetPartID, const TMap<int32, FLinearColor>& InIslandColors);
    void  ClearWetPartIslandColors();
    void  SetShowWetPartColors(bool bInShowWetPartColors);
    void  SetWetPartColorIntensity(float InIntensity);
    void  SetPreviewWetPart(int32 MaterialSlotIndex, int32 WetPartID);
    void  SetPreviewWetness(float AbsorbedWetness, float SurfaceWater);
    void  SetSurfaceWaterPreviewDropletsEnabled(bool bInDropletsEnabled);
    void  SetSurfaceWaterPreviewNormalFlip(bool bInFlipX, bool bInFlipY);
    void  SetSurfaceWaterTilingPreviewCoverageMode(EDWCSurfaceWaterTilingPreviewCoverageMode InMode);
    void  SetSurfaceWaterTilingPreviewDisplayMode(EDWCSurfaceWaterTilingPreviewDisplayMode InMode);
    void  RefreshSurfaceWaterPreviewDynamicTextures();
    void  RefreshSurfaceWaterPreviewMaterial();
    void  FocusOnPreviewMesh(bool bInstant = false);
    void  SetSelectionOverlayThicknessScale(float InThicknessScale);
    float GetSelectionOverlayThicknessScale() const { return SelectionOverlayThicknessScale; }
    FText GetSurfaceWaterPreviewStatusText() const;
    FSlateColor GetSurfaceWaterPreviewStatusColor() const;

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual TSharedPtr<SWidget>               BuildViewportToolbar() override;
    virtual void                              PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void                              OnFocusViewportToSelection() override;

  private:
    void                HandleIslandPickedFromClient(int32 UVIslandID, bool bAppendSelection);
    void                RefreshWetPartOverlayMesh();
    void                RefreshSelectionOverlayMesh();
    void                RefreshMaterialSectionVisibility();
    bool                BuildSurfaceWaterPreviewTextures(FString& OutErrorMessage);
    void                InvalidateSurfaceWaterPreviewLayoutCache();
    void                ApplySurfaceWaterPreviewTextureParameters();
    void                ApplySurfaceWaterPreviewRenderOverrides();
    void                RequestViewportRedraw();
    void                CacheOriginalMaterials();
    void                RestoreOriginalMaterials();
    UMaterialInterface* ResolveWetPartOverlayMaterial();

  private:
    TWeakObjectPtr<UWetClothingAsset>           WetClothingAsset;
    FOnWetClothingPreviewIslandPicked           OnIslandPicked;
    TSharedPtr<FAdvancedPreviewScene>           PreviewScene;
    TSharedPtr<FDWCPartViewportClient>           ViewportClient;
    TObjectPtr<USkeletalMeshComponent>           PreviewMeshComponent = nullptr;
    TObjectPtr<UProceduralMeshComponent>         WetPartOverlayComponent = nullptr;
    TObjectPtr<UProceduralMeshComponent>         SelectionOverlayComponent = nullptr;
    TObjectPtr<UMaterialInterface>               WetPartOverlayMaterial = nullptr;
    TObjectPtr<UMaterialInterface>               SurfaceWaterPreviewMaterialParent = nullptr;
    TObjectPtr<UMaterial>                        SurfaceWaterPreviewBaseMaterial = nullptr;
    TObjectPtr<UMaterialInstanceConstant>        SurfaceWaterPreviewStaticMaterial = nullptr;
    TObjectPtr<UMaterialInstanceDynamic>         SurfaceWaterPreviewMaterial = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewWetnessMap = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewWetPartDataTexture = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewDropletRT = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewFlowDropletRT = nullptr;
    TArray<FColor>                              SurfacePreviewCachedSourcePartDataPixels;
    TArray<uint8>                               SurfacePreviewCachedSelectedMask;
    FVector2D                                   SurfacePreviewCachedSingleCircleCenter = FVector2D::ZeroVector;
    int32                                       SurfacePreviewCachedWidth = 0;
    int32                                       SurfacePreviewCachedHeight = 0;
    int32                                       SurfacePreviewCachedLocalProfileID = 0;
    int32                                       SurfacePreviewCachedMaterialSlotIndex = INDEX_NONE;
    int32                                       SurfacePreviewCachedWetPartID = INDEX_NONE;
    bool                                        bSurfacePreviewLayoutCacheValid = false;
    bool                                        bSurfaceWaterPreviewFallbackProfileCacheValid = false;
    FLinearColor                                SurfaceWaterPreviewBaseFallbackProfile0 = FLinearColor::Black;
    FLinearColor                                SurfaceWaterPreviewBaseFallbackProfile1 = FLinearColor::Black;
    FLinearColor                                SurfaceWaterPreviewBaseFallbackProfile2 = FLinearColor::Black;
    FLinearColor                                SurfaceWaterPreviewBaseFallbackProfile3 = FLinearColor::Black;
    FLinearColor                                SurfaceWaterPreviewBaseFallbackProfile4 = FLinearColor::Black;
    FLinearColor                                SurfaceWaterPreviewBaseFallbackProfile5 = FLinearColor::Black;
    FLinearColor                                SurfaceWaterPreviewBaseFallbackProfile6 = FLinearColor::Black;
    TArray<TObjectPtr<UMaterialInterface>>       OriginalPreviewMaterials;
    TArray<FWetClothingAssetUVIsland>            CurrentSelectableIslands;
    TSet<int32>                                  CurrentHighlightedUVIslandIDs;
    TMap<int32, int32>                           CurrentWetPartIslandAssignments;
    TMap<int32, FLinearColor>                    CurrentWetPartIslandColors;
    int32                                        CurrentHighlightedMaterialSlot = INDEX_NONE;
    int32                                        PreviewMaterialSlotIndex = INDEX_NONE;
    int32                                        PreviewWetPartID = INDEX_NONE;
    int32                                        SurfacePreviewLocalProfileID = 0;
    int32                                        SurfaceWaterPreviewMaterialSlotIndex = INDEX_NONE;
    int32                                        SurfaceWaterPreviewDataUVChannel = INDEX_NONE;
    int32                                        SurfaceWaterPreviewNormalUVChannel = INDEX_NONE;
    bool                                         bSurfaceWaterTilingPreview = false;
    bool                                         bShowWetPartColors = true;
    float                                        WetPartColorIntensity = 1.0f;
    EDWCSurfaceWaterTilingPreviewCoverageMode    SurfaceWaterPreviewCoverageMode = EDWCSurfaceWaterTilingPreviewCoverageMode::FullPart;
    float                                        PreviewAbsorbedWetness = 0.0f;
    float                                        PreviewSurfaceWater = 1.0f;
    bool                                         bSurfaceWaterPreviewDropletsEnabled = true;
    bool                                         bSurfaceWaterPreviewFlipNormalX = false;
    bool                                         bSurfaceWaterPreviewFlipNormalY = false;
    EDWCSurfaceWaterTilingPreviewDisplayMode     SurfaceWaterPreviewDisplayMode = EDWCSurfaceWaterTilingPreviewDisplayMode::Lit;
    float                                        SelectionOverlayThicknessScale = 1.0f;
    FString                                      SurfaceWaterPreviewStatus;
    bool                                         bSurfaceWaterPreviewStatusIsError = false;
};
