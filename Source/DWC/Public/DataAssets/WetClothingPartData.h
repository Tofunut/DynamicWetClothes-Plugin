#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "DataAssets/WetnessProfile.h"
#include "DataAssets/WetClothingPrecomputedBoneOptimizationCache.h"
#include "WetClothingPartData.generated.h"

class UMaterial;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UTexture;

UENUM(BlueprintType)
enum class EWetPartProfileBlendMode : uint8
{
    Standard UMETA(DisplayName = "Standard")
};

USTRUCT(BlueprintType)
struct DWC_API FWetPartProfileAssignment
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wetness Profile")
    FString SourceProfileName;

    UPROPERTY(VisibleAnywhere, Category = "Wetness Profile")
    FSoftObjectPath SourceProfile;

    UPROPERTY(EditAnywhere, Category = "Wetness Profile")
    EWetPartProfileBlendMode BlendMode = EWetPartProfileBlendMode::Standard;

    UPROPERTY(EditAnywhere, Category = "Wetness Profile", meta = (ShowOnlyInnerProperties))
    FWetnessProfileParameters Parameters;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingSourceTextureSelection
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    FString ComponentPath;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    int32 UVChannelIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    TObjectPtr<UTexture> Texture = nullptr;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingWettableMaterialSlotState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    FString ComponentPath;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    bool bIsWettableSlot = false;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingWetPartEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    FString ComponentPath;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    int32 UVChannelIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    int32 WetPartID = 0;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    FString DisplayName;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    bool bViewEnabled = true;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    TArray<int32> AssignedUVIslandIDs;

    UPROPERTY(EditAnywhere, Category = "Wetness Profile")
    FWetPartProfileAssignment ProfileAssignment;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingEditableWetPartData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    TArray<FWetClothingSourceTextureSelection> SourceTextureSelections;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    TArray<FWetClothingWettableMaterialSlotState> WettableMaterialSlots;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    TArray<FWetClothingWetPartEntry> WetPartEntries;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingGeneratedWetMaterialOverride
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Generated Wet Material")
    FString ComponentPath;

    UPROPERTY(VisibleAnywhere, Category = "Generated Wet Material")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Generated Wet Material")
    TObjectPtr<UMaterialInterface> SourceMaterial = nullptr;

    /** Shared generated material graph containing both CPU and GPU branches. */
    UPROPERTY(VisibleAnywhere, Category = "Generated Wet Material")
    TObjectPtr<UMaterial> GeneratedMaterial = nullptr;

    /** CPU shader permutation: DWC_UseGPUBackend = false. */
    UPROPERTY(VisibleAnywhere, Category = "Generated Wet Material")
    TObjectPtr<UMaterialInstanceConstant> CPUMaterialInstance = nullptr;

    /** GPU shader permutation: DWC_UseGPUBackend = true. */
    UPROPERTY(VisibleAnywhere, Category = "Generated Wet Material")
    TObjectPtr<UMaterialInstanceConstant> GPUMaterialInstance = nullptr;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingPrecomputedVertexData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 WetPartID = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 WetPartEntryIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 UVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 UVIslandID = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    FVector2D SurfaceWaterUV = FVector2D::ZeroVector;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    bool bHasSurfaceWaterUV = false;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    bool bIsWettable = false;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingPrecomputedVertexNeighbors
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    TArray<int32> Neighbors;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingPrecomputedSimulationData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    bool bIsValid = false;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 LODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 VertexCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    FString MeshSignature;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    FString SourceDataSignature;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 DataVersion = 1;

    UPROPERTY(Transient, VisibleAnywhere, Category = "Precomputed Simulation Data")
    TArray<FWetClothingPrecomputedVertexData> Vertices;

    UPROPERTY(Transient, VisibleAnywhere, Category = "Precomputed Simulation Data")
    TArray<FWetClothingPrecomputedVertexNeighbors> NeighborGraph;

    UPROPERTY(Transient, VisibleAnywhere, Category = "Precomputed Simulation Data")
    FWetClothingPrecomputedBoneOptimizationCache BoneOptimizationCache;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingPartData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Editable")
    FWetClothingEditableWetPartData EditableWetPartData;
};
