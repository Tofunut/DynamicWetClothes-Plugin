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

USTRUCT(BlueprintType)
struct DWC_API FWCAAuthoredData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Part")
    FWetClothingPartData PartData;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Wrinkle")
    FWetClothingWrinkleData WrinkleData;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Transparency")
    FWetClothingTransparencyData TransparencyData;

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = "Wet Clothing")
    TArray<FString> AdditionalProfileSearchPaths;
#endif
};

USTRUCT(BlueprintType)
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

USTRUCT(BlueprintType)
struct DWC_API FWCADerivedInlineData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Runtime")
    TArray<FWetnessProfileParameters> ResolvedWetnessProfileParameters;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Generated Assets")
    TArray<FWetClothingGeneratedWetMaterialOverride> GeneratedWetMaterialOverrides;

    /** Current render-profile lookup baked in DWC Data UV space. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Wet Part Data Texture")
    FWetClothingBakedWetPartData BakedWetPartData;

    /** Metadata only. UV coordinates live exclusively in the prepared mesh's DWC Data UV channel. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|DWC Data UV")
    TArray<FDWCDataUVLODMetadata> DataUVMetadata;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Generated Materials")
    TObjectPtr<UMaterialFunctionInterface> GeneratedEvaluateSurfaceAppearanceFunction = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Editor Derived Data")
    TArray<FDWCEditorUVTopologyData> OriginalUVTopologies;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Bake Status", meta = (ShowOnlyInnerProperties))
    FDWCAssetBakeState BakeState;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    FString SourceMeshSignature;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Validation")
    FDWCTriangleValidationSummary ValidationSummary;
#endif
};

USTRUCT(BlueprintType)
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

USTRUCT(BlueprintType)
struct DWC_API FWCADerivedData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Derived")
    FWCADerivedInlineData Inline;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Derived")
    FWCADerivedBulkData Bulk;
};

USTRUCT(BlueprintType)
struct DWC_API FWCAMetadata
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Version")
    int32 AssetDataVersion = 1;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    TObjectPtr<USkeletalMesh> SourceSkeletalMesh = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    TObjectPtr<USkeletalMesh> DWCSkeletalMesh = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    int32 OriginalUVChannelIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    int32 DWCDataUVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh")
    int32 SimulationLODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Setup", meta = (ShowOnlyInnerProperties))
    FDWCWetClothingAssetSetupSettings SetupSettings;

};

UCLASS(BlueprintType)
class DWC_API UWetClothingAsset : public UDataAsset
{
    GENERATED_BODY()

  public:
    static constexpr int32 CurrentAssetDataVersion = 11;
    static constexpr int32 FirstAssetVersionWithSerializedRuntimeBulkData = 4;
    static constexpr int32 CurrentPrecomputedSimulationDataVersion = 10;
    static constexpr int32 CurrentRuntimeBulkDataVersion = 6;
    static constexpr int32 RuntimeSimulationLODIndex = 0;

    virtual void Serialize(FArchive& Ar) override;
    virtual void PostLoad() override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PreSave(FObjectPreSaveContext SaveContext) override;
    bool InitializeNewAsset(USkeletalMesh* InSourceMesh, const FDWCWetClothingAssetSetupSettings& InSettings, FString* OutErrorMessage = nullptr);
    bool ApplySetupSettings(const FDWCWetClothingAssetSetupSettings& InSettings, FString* OutChangeSummary = nullptr);
    void SetGeneratedDataUVTarget(USkeletalMesh* InRuntimeMesh, int32 InDWCDataUVChannelIndex);
    void SetDataUVMetadata(TArray<FDWCDataUVLODMetadata>&& InMetadata);
    void SetOriginalUVTopologies(TArray<FDWCEditorUVTopologyData>&& InTopologies);
    void MarkGeneratedDataUVOutOfDate();
    void MarkSimulationBakeOutOfDate();
    void MarkWrinkleBakeOutOfDate();
    void MarkVisualBakeOutOfDate();
    void SetLastBakeFailure(const FString& InFailure);
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
    void ClearRuntimeDataEditorSavePreparation();
    void BeginRuntimeDataEditorSaveAttempt();
    void CompleteRuntimeDataEditorSaveAttempt(bool bSaveSucceeded);
    bool IsBakeOutputSavePending(int32 OutputMask) const;

    /** Marks selected serialized runtime outputs dirty after editor-authored data changes. */
    void MarkRuntimeBakeOutputsDirty(int32 OutputMask);

    static FString BuildMeshContentSignature(const USkeletalMesh* SkeletalMesh, int32 LODIndex, int32 UVChannelIndex = INDEX_NONE);
    static void ClearMeshContentSignatureCache();
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
    void LogOriginalUVTopologyMemoryStats() const;

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
