#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "DataAssets/WetnessProfile.h"
#include "DataAssets/WetClothingPrecomputedBoneOptimizationCache.h"
#include "WetClothingPartData.generated.h"

class UMaterialInterface;
class UTexture;
class UTexture2D;

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

    UPROPERTY(VisibleAnywhere, Category = "Generated Wet Material")
    TObjectPtr<UMaterialInterface> WetMaterial = nullptr;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingBakedWetnessProfileMap
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Baked Wetness Profile Map")
    FString ComponentPath;

    UPROPERTY(VisibleAnywhere, Category = "Baked Wetness Profile Map")
    TObjectPtr<UTexture> SourceTexture = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Wetness Profile Map")
    int32 UVChannelIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Baked Wetness Profile Map")
    TArray<int32> MaterialSlotIndices;

    UPROPERTY(VisibleAnywhere, Category = "Baked Wetness Profile Map")
    TObjectPtr<UTexture2D> WetnessProfileMap0 = nullptr;

    UPROPERTY(EditAnywhere, Category = "Baked Wetness Profile Map")
    int32 Resolution = 512;

    UPROPERTY(EditAnywhere, Category = "Baked Wetness Profile Map")
    int32 PaddingPixels = 4;

    UPROPERTY(VisibleAnywhere, Category = "Baked Wetness Profile Map")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Baked Wetness Profile Map")
    FGuid BakeGuid;
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

    // Kept for assets saved before MeshSignature / SourceDataSignature validation was introduced.
    UPROPERTY()
    FString MeshBuildSignature_DEPRECATED;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    FString SourceDataSignature;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 DataVersion = 1;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    TArray<FWetClothingPrecomputedVertexData> Vertices;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    TArray<FWetClothingPrecomputedVertexNeighbors> NeighborGraph;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    FWetClothingPrecomputedBoneOptimizationCache BoneOptimizationCache;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingPartData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Editable")
    FWetClothingEditableWetPartData EditableWetPartData;

    UPROPERTY(VisibleAnywhere, Category = "Generated")
    TArray<FWetClothingGeneratedWetMaterialOverride> GeneratedWetMaterialOverrides;

    UPROPERTY(VisibleAnywhere, Category = "Baked")
    TArray<FWetClothingBakedWetnessProfileMap> BakedWetnessProfileMaps;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed")
    FWetClothingPrecomputedSimulationData PrecomputedSimulationData;
};
