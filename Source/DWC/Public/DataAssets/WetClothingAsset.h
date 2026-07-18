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

UCLASS(BlueprintType)
class DWC_API UWetClothingAsset : public UDataAsset
{
    GENERATED_BODY()

  public:
    static constexpr int32 CurrentAssetDataVersion = 5;
    static constexpr int32 FirstAssetVersionWithSerializedRuntimeBulkData = 4;
    static constexpr int32 CurrentPrecomputedSimulationDataVersion = 4;
    static constexpr int32 CurrentRuntimeBulkDataVersion = 2;
    static constexpr int32 RuntimeSimulationLODIndex = 0;

    virtual void Serialize(FArchive& Ar) override;
    virtual void PostLoad() override;

#if WITH_EDITOR
    virtual void PreSave(FObjectPreSaveContext SaveContext) override;
    bool InitializeNewAsset(USkeletalMesh* InSourceMesh, const FDWCWetClothingAssetSetupSettings& InSettings, FString* OutErrorMessage = nullptr);
    bool ApplySetupSettings(const FDWCWetClothingAssetSetupSettings& InSettings, FString* OutChangeSummary = nullptr);
    void SetGeneratedDataUVTarget(USkeletalMesh* InRuntimeMesh, int32 InDWCDataUVChannelIndex);
    void SetDataUVMetadata(TArray<FDWCDataUVLODMetadata>&& InMetadata);
    void SetOriginalUVTopologies(TArray<FDWCEditorUVTopologyData>&& InTopologies);
    void MarkGeneratedDataUVOutOfDate();
    void MarkSimulationBakeOutOfDate();
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
    bool RebuildPrecomputedSimulationData(FString* OutErrorMessage = nullptr, int32 LODIndex = 0);
    bool RebuildGPURuntimeData(FString* OutErrorMessage = nullptr);
    bool BakeGPUWetnessMaps(FString* OutErrorMessage = nullptr);
    bool RebuildRuntimeDataForSave(FString* OutErrorMessage = nullptr);
    bool CanPrepareRuntimeDataForEditorSave(FString* OutSkipReason = nullptr) const;
    bool PrepareRuntimeDataForEditorSave(FString* OutErrorMessage = nullptr);
    void ClearRuntimeDataEditorSavePreparation();
    void BeginRuntimeDataEditorSaveAttempt();
    void CompleteRuntimeDataEditorSaveAttempt(bool bSaveSucceeded);
    bool IsBakeOutputSavePending(int32 OutputMask) const;

    static FString BuildMeshContentSignature(const USkeletalMesh* SkeletalMesh, int32 LODIndex, int32 UVChannelIndex = INDEX_NONE);
    static void ClearMeshContentSignatureCache();
#endif
    void ClearPrecomputedSimulationData();
    void ClearGPUWetMapData();
    void ClearGPUMapData();

    bool IsPrecomputedSimulationDataMetadataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    bool IsPrecomputedSimulationDataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    bool IsGPURuntimeDataMetadataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    bool IsGPURuntimeDataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    bool IsGPUWetMapDataMetadataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    bool IsGPUWetMapDataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    FString GetPrecomputedSimulationDataValidationSummary(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    bool IsMaterialSlotWettable(int32 MaterialSlotIndex) const;
    bool HasAnyWettableMaterialSlot() const;
    bool HasWrinkleBakeContent() const;
    bool HasTransparencyBakeContent() const;

    const FWetClothingPrecomputedSimulationData& GetPrecomputedSimulationData(int32 LODIndex = 0) const;
    const FDWCGPULODBakeData& GetGPUWetMapRuntimeData(int32 LODIndex = 0) const;
    bool IsCurrentAssetDataVersion() const { return AssetDataVersion == CurrentAssetDataVersion; }
    bool HasCPURuntimeDataPayload() const;
    bool HasGPURuntimeDataPayload() const;
    bool HasGPUMapDataPayload() const;

    USkeletalMesh* GetSourceSkeletalMesh() const { return SourceSkeletalMesh.Get(); }
    USkeletalMesh* GetDWCSkeletalMesh() const { return DWCSkeletalMesh.Get(); }

    UFUNCTION(BlueprintPure, Category = "Wet Clothing|Mesh")
    USkeletalMesh* GetRuntimeSkeletalMesh() const
    {
        return DWCSkeletalMesh.Get();
    }

#if WITH_EDITORONLY_DATA
    USkeletalMesh* GetEditorPreviewSkeletalMesh() const
    {
        return DWCSkeletalMesh != nullptr ? DWCSkeletalMesh.Get() : SourceSkeletalMesh.Get();
    }
#endif

    UFUNCTION(BlueprintPure, Category = "Wet Clothing|Mesh")
    int32 GetOriginalUVChannelIndex() const { return OriginalUVChannelIndex; }

    UFUNCTION(BlueprintPure, Category = "Wet Clothing|Mesh")
    int32 GetSimulationLODIndex() const { return RuntimeSimulationLODIndex; }

    UFUNCTION(BlueprintPure, Category = "Wet Clothing|Mesh")
    int32 GetDWCDataUVChannelIndex() const { return DWCDataUVChannelIndex; }

    const FDWCDataUVLODMetadata* FindDataUVMetadataForLOD(int32 LODIndex) const;
    bool HasValidDataUVForLOD(int32 LODIndex) const;
    int32 GetDataUVMetadataLODCount() const { return DataUVMetadataPerLOD.Num(); }
    const FDWCWetClothingAssetSetupSettings& GetSetupSettings() const { return SetupSettings; }

#if WITH_EDITORONLY_DATA
    const FDWCEditorUVTopologyData* FindOriginalUVTopologyForLOD(int32 LODIndex) const;
    const FDWCAssetBakeState& GetBakeState() const { return BakeState; }
    const FString& GetSourceMeshSignature() const { return SourceMeshSignature; }
    const FDWCTriangleValidationSummary& GetValidationSummary() const { return ValidationSummary; }
    void SetValidationSummary(const FDWCTriangleValidationSummary& InSummary);
#endif

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Version")
    int32 AssetDataVersion = 1;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Part")
    FWetClothingPartData PartData;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Wrinkle")
    FWetClothingWrinkleData WrinkleData;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Transparency")
    FWetClothingTransparencyData TransparencyData;

    UPROPERTY(EditAnywhere, Category = "Surface Water", meta = (ShowOnlyInnerProperties))
    FSurfaceWaterSimulationSettings SurfaceWaterSettings;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|GPU Wet Map")
    TArray<FDWCGPULODBakeData> BakedGPUWetMapLODs;

    /** Metadata only. UV coordinates live exclusively in DWCSkeletalMesh's DWC Data UV channel. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|DWC Data UV")
    TArray<FDWCDataUVLODMetadata> DataUVMetadataPerLOD;

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = "Wet Clothing")
    TArray<FString> AdditionalProfileSearchPaths;
#endif

  private:
    void EnsureRuntimeBulkDataLoaded() const;
    bool LoadRuntimeBulkData(bool bForceProgressDialog = false) const;
    void StoreRuntimeDataToBulkData();
    bool HasRuntimeBulkPayload() const;
    void MarkRuntimeBulkDataDirty(int32 OutputMask = 0);
    void ClearRuntimeBulkData();

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USkeletalMesh> SourceSkeletalMesh = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USkeletalMesh> DWCSkeletalMesh = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh", meta = (AllowPrivateAccess = "true"))
    int32 OriginalUVChannelIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh", meta = (AllowPrivateAccess = "true"))
    int32 DWCDataUVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh", meta = (AllowPrivateAccess = "true"))
    int32 SimulationLODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Setup", meta = (ShowOnlyInnerProperties, AllowPrivateAccess = "true"))
    FDWCWetClothingAssetSetupSettings SetupSettings;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Editor Derived Data", meta = (AllowPrivateAccess = "true"))
    TArray<FDWCEditorUVTopologyData> OriginalUVTopologiesPerLOD;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Bake Status", meta = (ShowOnlyInnerProperties, AllowPrivateAccess = "true"))
    FDWCAssetBakeState BakeState;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Mesh", meta = (AllowPrivateAccess = "true"))
    FString SourceMeshSignature;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Validation", meta = (AllowPrivateAccess = "true"))
    FDWCTriangleValidationSummary ValidationSummary;
#endif

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
