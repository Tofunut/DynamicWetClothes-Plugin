//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingPartData.h"
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
 * Normal instances render Part and Selection colors through an Original-UV
 * texture sampled by the source skeletal mesh overlay pass. Identical Original UV
 * coordinates intentionally share one preview texel and one Part assignment.
 * Surface Water Tiling instances use a separate transient DWC material and authored
 * profile data; neither path depends on runtime-build material or texture outputs.
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
    void  BeginPreviewUpdate();
    void  EndPreviewUpdate();
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
    void  SetSurfaceWaterTilingPreviewPartSettingsOverride(const FWetPartSurfaceWaterSettings& InSettings);
    void  ClearSurfaceWaterTilingPreviewPartSettingsOverride();
    void  RefreshSurfaceWaterPreviewDynamicTextures();
    void  RefreshSurfaceWaterPreviewMaterial();
    void  FocusOnPreviewMesh(bool bInstant = false);
    void  SetPreviewPaused(bool bInPaused);
    void  SetSelectionOverlayThicknessScale(float InThicknessScale);
    float GetSelectionOverlayThicknessScale() const { return SelectionOverlayThicknessScale; }
    FText GetSurfaceWaterPreviewStatusText() const;
    FSlateColor GetSurfaceWaterPreviewStatusColor() const;
    bool HasSurfaceWaterPreviewError() const { return bSurfaceWaterPreviewStatusIsError; }

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual TSharedPtr<SWidget>               BuildViewportToolbar() override;
    virtual void                              PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void                              OnFocusViewportToSelection() override;

  private:
    void                HandleIslandPickedFromClient(int32 UVIslandID, bool bAppendSelection);
    void                MarkWetPartOverlayDirty();
    void                MarkSelectionOverlayDirty();
    void                MarkSurfacePreviewDirty();
    void                FlushPendingPreviewUpdates();
    void                RefreshWetPartOverlayMesh();
    void                RefreshSelectionOverlayMesh();
    void                RefreshPartPreviewOverlay();
    void                RefreshPartPreviewOverlayMaterial();
    void                ClearPartPreviewOverlay();
    bool                BuildPartPreviewTextures();
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
    struct FPartPreviewTextureCacheEntry
    {
        TObjectPtr<UTexture2D> PartTexture = nullptr;
        TObjectPtr<UTexture2D> SelectionTexture = nullptr;
        bool bHasOverlay = false;
    };

    TWeakObjectPtr<UWetClothingAsset>           WetClothingAsset;
    FOnWetClothingPreviewIslandPicked           OnIslandPicked;
    TSharedPtr<FAdvancedPreviewScene>           PreviewScene;
    TSharedPtr<FDWCPartViewportClient>           ViewportClient;
    TObjectPtr<USkeletalMeshComponent>           PreviewMeshComponent = nullptr;
    TObjectPtr<UMaterialInterface>               WetPartOverlayMaterial = nullptr;
    TObjectPtr<UMaterialInstanceDynamic>         WetPartOverlayMID = nullptr;
    TObjectPtr<UTexture2D>                       PartPreviewColorTexture = nullptr;
    TObjectPtr<UTexture2D>                       PartPreviewSelectionTexture = nullptr;
    // Reusing already-rasterized 1024x1024 Part preview textures makes A -> B -> A
    // material-slot switching cheap and avoids another UpdateResource() upload. The key
    // includes topology object identity and assignment/selection state.
    TMap<uint32, FPartPreviewTextureCacheEntry>  PartPreviewTextureCache;
    // Topology-only cache for the expensive nearest-owner fallback used by UV-degenerate
    // seam/piping triangles. Maps orphan TriangleID -> nearest editable TriangleID.
    TMap<uint32, TMap<int32, int32>>             PartPreviewNearestOwnerTriangleCache;
    TObjectPtr<UMaterialInterface>               SurfaceWaterPreviewMaterialParent = nullptr;
    TObjectPtr<UMaterial>                        SurfaceWaterPreviewBaseMaterial = nullptr;
    TObjectPtr<UMaterialInstanceConstant>        SurfaceWaterPreviewStaticMaterial = nullptr;
    TObjectPtr<UMaterialInstanceDynamic>         SurfaceWaterPreviewMaterial = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewWetnessMap = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewWetPartDataTexture = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewDropletRT = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewFlowDropletRT = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewAuthoredDropletNormal = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewAuthoredDropletMask = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewAuthoredDroplet2Normal = nullptr;
    TObjectPtr<UTexture2D>                      SurfacePreviewAuthoredDroplet2Mask = nullptr;
    TWeakObjectPtr<UTexture2D>                   SurfacePreviewSourceDropletNormal;
    TWeakObjectPtr<UTexture2D>                   SurfacePreviewSourceDropletMask;
    TWeakObjectPtr<UTexture2D>                   SurfacePreviewSourceDroplet2Normal;
    TWeakObjectPtr<UTexture2D>                   SurfacePreviewSourceDroplet2Mask;
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
    // Shared FWCAUVIslandViewCache entries that produced CurrentSelectableIslands.
    // Stable object identity lets repeated refreshes detect unchanged preview topology.
    TArray<TSharedPtr<FWetClothingAssetUVIsland>> CurrentSelectableIslandSources;
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
    TOptional<FWetPartSurfaceWaterSettings>       SurfaceWaterPreviewPartSettingsOverride;
    float                                        SelectionOverlayThicknessScale = 1.0f;
    FString                                      SurfaceWaterPreviewStatus;
    bool                                         bSurfaceWaterPreviewStatusIsError = false;
    int32                                        PreviewUpdateDepth = 0;
    bool                                         bPickableTopologyDirty = false;
    bool                                         bWetPartOverlayDirty = false;
    bool                                         bSelectionOverlayDirty = false;
    bool                                         bSurfacePreviewDirty = false;
    bool                                         bPreviewPaused = false;
};
