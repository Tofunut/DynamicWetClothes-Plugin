//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Serialization/BulkData.h"
#include "UObject/ObjectSaveContext.h"
#include "DataAssets/WetClothingPartData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "DataAssets/WetClothingGPUData.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationSettings.h"
#include "WetClothingAsset.generated.h"

class USkeletalMesh;
class UMaterialFunctionInterface;

USTRUCT()
struct DWC_API FWCAAuthoredData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Part")
    FWetClothingPartData PartData;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Wrinkle")
    FWetClothingWrinkleData WrinkleData;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Transparency")
    FWetClothingTransparencyData TransparencyData;

};

USTRUCT()
struct DWC_API FWCALODVertexColorRuntimeData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "LOD Vertex Color Runtime Data")
    int32 SourceLODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "LOD Vertex Color Runtime Data")
    int32 TargetLODIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "LOD Vertex Color Runtime Data")
    int32 TargetVertexCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "LOD Vertex Color Runtime Data")
    FString MeshSignature;

    UPROPERTY(VisibleAnywhere, Category = "LOD Vertex Color Runtime Data")
    TArray<int32> TargetToSourceVertex;

    bool IsValid() const
    {
        return TargetLODIndex != INDEX_NONE &&
               TargetVertexCount > 0 &&
               TargetToSourceVertex.Num() == TargetVertexCount &&
               !MeshSignature.IsEmpty();
    }
};

USTRUCT()
struct DWC_API FWCADerivedInlineData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Runtime")
    TArray<FWetnessProfileParameters> ResolvedWetnessProfileParameters;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Generated Assets")
    TArray<FWetClothingGeneratedWetMaterialOverride> GeneratedWetMaterialOverrides;

    /** Current render-profile lookup baked in DWC UV Channel space. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Wet Part Data Texture")
    FWetClothingBakedWetPartData BakedWetPartData;

    /** Metadata only. UV coordinates live exclusively in the prepared mesh's DWC UV Channel. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|DWC UV Channel")
    TArray<FDWCDataUVLODMetadata> DataUVMetadata;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Generated Materials")
    TObjectPtr<UMaterialFunctionInterface> GeneratedEvaluateSurfaceAppearanceFunction = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Editor Derived Data")
    TArray<FDWCEditorUVTopologyData> OriginalUVTopologies;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Bake Status", meta = (ShowOnlyInnerProperties))
    FDWCAssetBakeState BakeState;

    /** Original Mesh content accepted at WCA creation or the last successful DWC UV rebuild. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    FString SourceMeshSignature;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Validation")
    FDWCTriangleValidationSummary ValidationSummary;

    /** Material slots whose DWC UV data is invalid, including build failures and Original Mesh content changes. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|DWC UV Channel")
    TArray<int32> FailedDataUVMaterialSlotIndices;

    /** Per-slot, per-LOD outcome from the most recent explicit DWC UV build. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|DWC UV Channel")
    TArray<FDWCDataUVSlotLODResult> LastDataUVSlotLODResults;

    /** Most recent explicit DWC UV Channel generation failure shown by Part Edit. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|DWC UV Channel")
    FString LastDataUVGenerationFailure;

    /** Persistent editor topology revision used by UV-island and preview caches. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Editor Derived Data")
    uint64 PreviewTopologyRevision = 1;
#endif
};

USTRUCT()
struct DWC_API FWCADerivedBulkData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Runtime Data")
    FWetClothingPrecomputedSimulationData NeighborRuntimeData;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|GPU Runtime Data")
    TArray<FDWCGPULODBakeData> GPURuntimeData;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|LOD Vertex Color Runtime Data")
    TArray<FWCALODVertexColorRuntimeData> LODVertexColorRuntimeData;
};

USTRUCT()
struct DWC_API FWCADerivedData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Derived")
    FWCADerivedInlineData Inline;

    // Rebuildable runtime payloads must not be copied into every editor undo snapshot.
    UPROPERTY(VisibleAnywhere, NonTransactional, Category = "Wet Clothing|Derived")
    FWCADerivedBulkData Bulk;
};

USTRUCT()
struct DWC_API FWCAMetadata
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Version")
    int32 AssetDataVersion = 1;

    /** Stable ownership identity for generated assets. It is independent of package path and object name. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Identity")
    FGuid AssetGuid;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    TObjectPtr<USkeletalMesh> SourceSkeletalMesh = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    TObjectPtr<USkeletalMesh> DWCSkeletalMesh = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    int32 OriginalUVChannelIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    int32 DWCDataUVChannelIndex = INDEX_NONE;

    /** Set after the first successful DWC UV Channel commit. The packed layout and Original UV island identities never rebuild afterwards. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    bool bDataUVLayoutSealed = false;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    int32 SimulationLODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Setup", meta = (ShowOnlyInnerProperties))
    FDWCWetClothingAssetSetupSettings SetupSettings;

#if WITH_EDITORONLY_DATA
    /** Typed move/rename-safe inventory of generated outputs owned by this WCA. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Generated Assets")
    TArray<TSoftObjectPtr<UObject>> GeneratedAssetManifest;
#endif

};

UCLASS(BlueprintType)
class DWC_API UWetClothingAsset : public UDataAsset
{
    GENERATED_BODY()

  public:
    /** Version 13 keys authored wrinkle/transparency data by material slot only. */
    static constexpr int32 CurrentAssetDataVersion = 13;
    static constexpr int32 FirstAssetVersionWithSerializedRuntimeBulkData = 4;
    static constexpr int32 CurrentPrecomputedSimulationDataVersion = 11;
    static constexpr int32 CurrentRuntimeBulkDataVersion = 7;
    static constexpr int32 RuntimeSimulationLODIndex = 0;

    virtual void Serialize(FArchive& Ar) override;
    virtual void PostLoad() override;
    static FString BuildMeshContentSignature(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 UVChannelIndex = INDEX_NONE);

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PreSave(FObjectPreSaveContext SaveContext) override;
    bool InitializeNewAsset(USkeletalMesh* InSourceMesh, const FDWCWetClothingAssetSetupSettings& InSettings, FString* OutErrorMessage = nullptr);
    bool ApplySetupSettings(const FDWCWetClothingAssetSetupSettings& InSettings, FString* OutChangeSummary = nullptr);

    /** True after the first successful DWC UV Channel build seals the Original UV topology and packed layout. */
    bool HasLockedDataUVLayout() const;

    /** Atomically stores and seals the first successful DWC UV Channel layout. This API refuses every later replacement attempt. */
    bool CommitInitialDataUVLayout(
        USkeletalMesh* InRuntimeMesh,
        int32 InDWCDataUVChannelIndex,
        TArray<FDWCDataUVLODMetadata>&& InMetadata,
        TArray<FDWCEditorUVTopologyData>&& InTopologies,
        FString* OutErrorMessage = nullptr);

    /** Atomically replaces an existing generated DWC UV Channel layout after a deliberate editor rebuild. */
    bool ReplaceDataUVLayout(
        USkeletalMesh* InRuntimeMesh,
        int32 InDWCDataUVChannelIndex,
        TArray<FDWCDataUVLODMetadata>&& InMetadata,
        TArray<FDWCEditorUVTopologyData>&& InTopologies,
        FString* OutErrorMessage = nullptr);

    /** Commits a channel-only relocation after the prepared mesh UV values have been copied verbatim. */
    bool CommitDataUVChannelRelocation(int32 InDWCDataUVChannelIndex, FString* OutErrorMessage = nullptr);

    /** Removes stored per-LOD DWC UV/runtime records outside the supplied retained LOD set. */
    int32 PruneDataUVLODData(const TSet<int32>& RetainedLODIndices);
    const TArray<FDWCDataUVLODMetadata>& GetDataUVMetadata() const { return Derived.Inline.DataUVMetadata; }
    void MarkGeneratedDataUVOutOfDate();
    void MarkSimulationBakeOutOfDate();
    void MarkWrinkleBakeOutOfDate();
    void MarkVisualBakeOutOfDate();
    void SetLastBakeFailure(const FString& InFailure);
    /** Returns true when the referenced Original Mesh no longer matches the content signature accepted by this WCA. */
    bool HasSourceMeshContentChanged(FString* OutCurrentSignature = nullptr) const;
    void SetCPURuntimeDataStatus(EDWCBakeStatus InStatus, const FString& InFailure = FString());
    void SetGPURuntimeDataStatus(EDWCBakeStatus InStatus, const FString& InFailure = FString());
    void SetGPUMapBakeStatus(EDWCBakeStatus InStatus, const FString& InFailure = FString());
    void SetWrinkleBakeStatus(EDWCBakeStatus InStatus, const FString& InFailure = FString());
    void SetTransparencyBakeStatus(EDWCBakeStatus InStatus, const FString& InFailure = FString());
    void MarkBakeOutputGenerated(int32 OutputMask);
    void MarkBakeOutputsSaved(int32 OutputMask);
    bool HasGeneratedBakeOutput(int32 OutputMask) const;
    bool HasSavedBakeOutput(int32 OutputMask) const;
    void RefreshBakeState(bool bRunDeepValidation = false);
    bool RebuildPrecomputedSimulationData(FString* OutErrorMessage = nullptr);
    bool RebuildGPURuntimeData(FString* OutErrorMessage = nullptr);
    bool BakeGPUWetnessMaps(FString* OutErrorMessage = nullptr);
    bool RebuildRuntimeDataForSave(FString* OutErrorMessage = nullptr);
    bool CanPrepareRuntimeDataForEditorSave(FString* OutSkipReason = nullptr) const;
    bool PrepareRuntimeDataForEditorSave(FString* OutErrorMessage = nullptr);
    void ReleaseLoadedRuntimeBulkPayloadForEditor();
    uint64 GetResidentRuntimeBulkPayloadBytesForEditor() const;
    void ClearRuntimeDataEditorSavePreparation();
    /** Saves a targeted CPU/GPU runtime segment without rebuilding the other segment during PreSave. */
    void SkipNextRuntimeDataPreSaveRebuild();
    void BeginRuntimeDataEditorSaveAttempt();
    void CompleteRuntimeDataEditorSaveAttempt(bool bSaveSucceeded);
    bool IsBakeOutputSavePending(int32 OutputMask) const;

    /** Invalidates selected derived outputs after editor-authored data changes without marking stale payloads as save-pending. */
    void MarkRuntimeBakeOutputsDirty(int32 OutputMask);

    static void ClearMeshContentSignatureCache();

    /** Ensures this WCA has a stable generated-asset owner identity. */
    void EnsureAssetGuid();

    /** Stores this WCA's owner identity on a generated UObject package. Refuses to steal an asset tagged to another WCA. */
    bool TagGeneratedAsset(UObject* GeneratedAsset);

    /** Reads a generated UObject's owner identity without modifying it. */
    bool TryGetGeneratedAssetOwnerGuid(const UObject* GeneratedAsset, FGuid& OutOwnerGuid) const;

    /** True only when the generated UObject package is tagged with this WCA's owner identity. */
    bool IsGeneratedAssetOwnedByThisWCA(const UObject* GeneratedAsset) const;

    /** Resolves the move/rename-safe inventory of currently owned generated outputs. */
    void GetOwnedGeneratedAssets(TArray<UObject*>& OutAssets, UClass* RequiredClass = nullptr) const;

    /** Removes stale or explicitly deleted entries from the generated-output inventory. */
    void RemoveGeneratedAssetFromManifest(const UObject* GeneratedAsset);

    /** Advances the persistent editor topology revision after mesh/UV topology changes. */
    void BumpPreviewTopologyRevision();

    /** Persistent revision used by editor-only topology caches. */
    uint64 GetPreviewTopologyRevision() const
    {
#if WITH_EDITORONLY_DATA
        return FMath::Max<uint64>(Derived.Inline.PreviewTopologyRevision, 1);
#else
        return 1;
#endif
    }
#endif
    void ClearPrecomputedSimulationData();
    void ClearGPUWetMapData();
    void ClearGPUMapData();

    bool IsPrecomputedSimulationDataMetadataValidForMesh(const USkeletalMesh* SkeletalMesh) const;
    bool IsPrecomputedSimulationDataValidForMesh(const USkeletalMesh* SkeletalMesh) const;
    bool IsGPURuntimeDataMetadataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    bool IsGPURuntimeDataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    bool IsGPUWetMapDataMetadataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    bool IsGPUWetMapDataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    FString GetPrecomputedSimulationDataValidationSummary(const USkeletalMesh* SkeletalMesh) const;
    bool IsMaterialSlotWettable(int32 MaterialSlotIndex) const;
    bool HasAnyWettableMaterialSlot() const;
    bool DoesMaterialSlotUseSurfaceWater(int32 MaterialSlotIndex) const;
    bool UsesSurfaceWater() const;
    bool HasWrinkleBakeContent() const;
    bool HasTransparencyBakeContent() const;
    const FWetClothingPrecomputedSimulationData& GetPrecomputedSimulationData() const;
    const FDWCGPULODBakeData& GetGPUWetMapRuntimeData(int32 LODIndex = 0) const;
    const FDWCGPULODBakeData& GetGPUWetMapRuntimeDataMetadata(int32 LODIndex = 0) const;
    bool IsCurrentAssetDataVersion() const { return Metadata.AssetDataVersion == CurrentAssetDataVersion; }
    bool HasCPURuntimeDataPayload() const;
    bool HasGPURuntimeDataPayload() const;
    bool HasGPUMapDataPayload() const;

    USkeletalMesh* GetSourceSkeletalMesh() const { return Metadata.SourceSkeletalMesh.Get(); }
    USkeletalMesh* GetDWCSkeletalMesh() const { return Metadata.DWCSkeletalMesh.Get(); }
    const FGuid& GetAssetGuid() const { return Metadata.AssetGuid; }

    UFUNCTION(BlueprintPure, Category = "Wet Clothing|Mesh")
    USkeletalMesh* GetRuntimeSkeletalMesh() const
    {
        return Metadata.DWCSkeletalMesh.Get();
    }

#if WITH_EDITORONLY_DATA
    USkeletalMesh* GetEditorPreviewSkeletalMesh() const
    {
        return Metadata.DWCSkeletalMesh != nullptr ? Metadata.DWCSkeletalMesh.Get() : Metadata.SourceSkeletalMesh.Get();
    }
#endif

    UFUNCTION(BlueprintPure, Category = "Wet Clothing|Mesh")
    int32 GetOriginalUVChannelIndex() const { return Metadata.OriginalUVChannelIndex; }

    /** Returns the explicit Surface Normal UV, or Original UV when the setup uses Same as Original. */
    UFUNCTION(BlueprintPure, Category = "Wet Clothing|Mesh")
    int32 GetSurfaceWaterNormalUVChannelIndex() const
    {
        return Metadata.SetupSettings.SurfaceWaterNormalUVChannelIndex != INDEX_NONE
            ? Metadata.SetupSettings.SurfaceWaterNormalUVChannelIndex
            : Metadata.OriginalUVChannelIndex;
    }

    UFUNCTION(BlueprintPure, Category = "Wet Clothing|Mesh")
    int32 GetSimulationLODIndex() const { return RuntimeSimulationLODIndex; }

    UFUNCTION(BlueprintPure, Category = "Wet Clothing|Mesh")
    int32 GetDWCDataUVChannelIndex() const { return Metadata.DWCDataUVChannelIndex; }

    const FDWCDataUVLODMetadata* FindDataUVMetadataForLOD(int32 LODIndex) const;
    bool HasValidDataUVForLOD(int32 LODIndex) const;
    int32 GetDataUVMetadataLODCount() const { return Derived.Inline.DataUVMetadata.Num(); }
    const FDWCWetClothingAssetSetupSettings& GetSetupSettings() const { return Metadata.SetupSettings; }

#if WITH_EDITORONLY_DATA
    const FDWCEditorUVTopologyData* FindOriginalUVTopologyForLOD(int32 LODIndex) const;
    const FDWCAssetBakeState& GetBakeState() const { return Derived.Inline.BakeState; }
    const FString& GetSourceMeshSignature() const { return Derived.Inline.SourceMeshSignature; }
    const FDWCTriangleValidationSummary& GetValidationSummary() const { return Derived.Inline.ValidationSummary; }
    void SetValidationSummary(const FDWCTriangleValidationSummary& InSummary);
#endif

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Authored")
    FWCAAuthoredData Authored;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Derived")
    FWCADerivedData Derived;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Metadata")
    FWCAMetadata Metadata;

  private:
    void EnsureRuntimeBulkDataLoaded() const;
    bool LoadRuntimeBulkData(bool bForceProgressDialog = false) const;
    void StoreRuntimeDataToBulkData();
    bool HasRuntimeBulkPayload() const;
    void MarkRuntimeBulkDataDirty(int32 OutputMask = 0);
    void ClearRuntimeBulkData();

#if WITH_EDITOR
    void RefreshBakeStateFast();
    void RefreshBakeStateDeep();
    void RefreshBakeStateInternal(bool bRunDeepValidation);

    bool bRuntimeDataRebuildInProgress = false;
    bool bSkipNextPreSaveRuntimeDataRebuild = false;
    bool bRuntimeDataEditorSaveAttemptActive = false;
    int32 PendingRuntimeSaveOutputMask = 0;
    int32 EditorSavePendingOutputMaskSnapshot = 0;
    int32 EditorSaveSavedOutputMaskSnapshot = 0;
#endif

    mutable FByteBulkData RuntimeBulkData;
    mutable bool bRuntimeBulkDataLoaded = false;
    mutable bool bRuntimeBulkDataLoadFailed = false;
    bool bRuntimeBulkDataDirty = false;
};
