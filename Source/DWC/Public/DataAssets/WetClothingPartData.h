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
struct DWC_API FWetClothingLocalRenderProfile
{
    GENERATED_BODY()

    /** Source profile identity used for deterministic runtime deduplication. */
    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    FSoftObjectPath SourceProfile;

    /** Resolved fallback used when the source profile is unavailable at runtime. */
    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    FWetnessProfileParameters Parameters;

    /** Stable build key. Local ID 0 is always neutral and is not stored here. */
    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    FString StableKey;

    /** Array-compatible Derived textures. Runtime never packs the source profile textures directly. */
    UPROPERTY()
    TObjectPtr<UTexture2D> NormalizedDropletMask_DEPRECATED = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture|Surface Texture")
    TObjectPtr<UTexture2D> NormalizedDropletNormal = nullptr;

    UPROPERTY()
    TObjectPtr<UTexture2D> NormalizedRivuletMask_DEPRECATED = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture|Surface Texture")
    TObjectPtr<UTexture2D> NormalizedRivuletNormal = nullptr;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingBakedProfileIDSlotTexture
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    int32 MaterialSlotIndex = INDEX_NONE;

    /** Point-sampled local profile IDs rasterized in this slot's DWC Data UV space. */
    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    TObjectPtr<UTexture2D> ProfileIDTexture = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    FGuid BakeGuid;

    bool IsValid() const
    {
        return MaterialSlotIndex != INDEX_NONE && ProfileIDTexture != nullptr;
    }
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingBakedProfileIDData
{
    GENERATED_BODY()

    /** WCA-wide local profile table. Texture value N maps to LocalProfiles[N - 1]. */
    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    TArray<FWetClothingLocalRenderProfile> LocalProfiles;

    /** One Profile ID Texture per wettable material slot. */
    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    TArray<FWetClothingBakedProfileIDSlotTexture> SlotTextures;

    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    int32 DataUVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    int32 Resolution = 256;

    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    int32 PaddingPixels = 4;

    /** Resolution shared by every normalized surface Mask/Normal texture. */
    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture|Surface Texture")
    int32 SurfaceTextureResolution = 256;

    /** Shared flat normal used as Texture2DArray slice 0. */
    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture|Surface Texture")
    TObjectPtr<UTexture2D> NormalizedNeutralSurfaceNormal = nullptr;

    /** Signature covering the WCA-wide local table and every slot bake. */
    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Profile ID Texture")
    FGuid BakeGuid;

    const FWetClothingBakedProfileIDSlotTexture* FindSlot(const int32 MaterialSlotIndex) const
    {
        return SlotTextures.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingBakedProfileIDSlotTexture& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });
    }

    FWetClothingBakedProfileIDSlotTexture* FindSlot(const int32 MaterialSlotIndex)
    {
        return SlotTextures.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingBakedProfileIDSlotTexture& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });
    }

    bool IsValid() const
    {
        return DataUVChannelIndex != INDEX_NONE &&
               SurfaceTextureResolution > 0 &&
               NormalizedNeutralSurfaceNormal != nullptr &&
               LocalProfiles.Num() <= 254 &&
               !BuildSignature.IsEmpty() &&
               !SlotTextures.IsEmpty() &&
               !SlotTextures.ContainsByPredicate(
                   [](const FWetClothingBakedProfileIDSlotTexture& Slot)
                   {
                       return !Slot.IsValid() || Slot.BuildSignature.IsEmpty();
                   });
    }
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
