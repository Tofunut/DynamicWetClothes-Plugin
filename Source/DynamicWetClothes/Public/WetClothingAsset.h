#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPath.h"
#include "WetnessProfile.h"
#include "WetClothingAsset.generated.h"

class USkeletalMesh;
class UMaterialInterface;
class UTexture;
class UTexture2D;

UENUM(BlueprintType)
enum class EWetClothingPartBlendMode : uint8
{
    Standard UMETA(DisplayName = "Standard")
};

USTRUCT(BlueprintType)
struct FWetClothingPartProfileAssignment
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wetness Profile")
    FString SourceProfileName;

    UPROPERTY(VisibleAnywhere, Category = "Wetness Profile")
    FSoftObjectPath SourceProfile;

    UPROPERTY(EditAnywhere, Category = "Wetness Profile")
    EWetClothingPartBlendMode BlendMode = EWetClothingPartBlendMode::Standard;

    UPROPERTY(EditAnywhere, Category = "Wetness Profile", meta = (ShowOnlyInnerProperties))
    FWetnessProfileParameters Parameters;
};

USTRUCT(BlueprintType)
struct FWetClothingAssetTextureSelection
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Clothing")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing")
    int32 UVChannelIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing")
    TObjectPtr<UTexture> Texture = nullptr;
};

USTRUCT(BlueprintType)
struct FWetClothingAssetWetPartEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    int32 UVChannelIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    int32 WetPartID = 0;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    FString Name;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    bool bViewEnabled = true;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    TArray<int32> AssignedIslandIDs;

    UPROPERTY(EditAnywhere, Category = "Wetness Profile")
    FWetClothingPartProfileAssignment ProfileAssignment;
};

USTRUCT(BlueprintType)
struct FWetClothingAssetWetMaterialOverride
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Material")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Wet Material")
    TObjectPtr<UMaterialInterface> SourceMaterial = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Material")
    TObjectPtr<UMaterialInterface> WetMaterial = nullptr;
};

USTRUCT(BlueprintType)
struct FWetClothingAssetBakedVertexData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    int32 WetPartID = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    int32 WetPartEntryIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    int32 UVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    int32 IslandID = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct FWetClothingAssetBakedVertexNeighbors
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    TArray<int32> Neighbors;
};

USTRUCT(BlueprintType)
struct FWetClothingAssetBakedRuntimeData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    bool bIsValid = false;

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    int32 LODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    int32 VertexCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    FString MeshBuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    TArray<FWetClothingAssetBakedVertexData> Vertices;

    UPROPERTY(VisibleAnywhere, Category = "Runtime Data")
    TArray<FWetClothingAssetBakedVertexNeighbors> NeighborGraph;
};

USTRUCT(BlueprintType)
struct FWetClothingAssetBakedWetnessProfileMap
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wetness Profile Map")
    TObjectPtr<UTexture> SourceTexture = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wetness Profile Map")
    int32 UVChannelIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Wetness Profile Map")
    TArray<int32> MaterialSlotIndices;

    UPROPERTY(VisibleAnywhere, Category = "Wetness Profile Map")
    TObjectPtr<UTexture2D> WetnessProfileMap0 = nullptr;

    UPROPERTY(EditAnywhere, Category = "Wetness Profile Map")
    int32 Resolution = 512;

    UPROPERTY(EditAnywhere, Category = "Wetness Profile Map")
    int32 PaddingPixels = 4;

    UPROPERTY(VisibleAnywhere, Category = "Wetness Profile Map")
    FGuid BakeGuid;
};

UCLASS(BlueprintType)
class DYNAMICWETCLOTHES_API UWetClothingAsset : public UDataAsset
{
    GENERATED_BODY()

  public:
    bool RebuildRuntimeData(FString* OutErrorMessage = nullptr, int32 LODIndex = 0);
    void ClearRuntimeData();
    bool IsRuntimeDataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    const FWetClothingAssetBakedRuntimeData& GetBakedRuntimeData() const { return BakedRuntimeData; }

    UPROPERTY(EditAnywhere, Category = "Wet Clothing")
    TObjectPtr<USkeletalMesh> TargetMesh = nullptr;

    UPROPERTY()
    TArray<FWetClothingAssetTextureSelection> TextureSelections;

    UPROPERTY()
    TArray<FWetClothingAssetWetPartEntry> WetPartEntries;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Wet Materials")
    TArray<FWetClothingAssetWetMaterialOverride> WetMaterialOverrides;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Runtime Data")
    FWetClothingAssetBakedRuntimeData BakedRuntimeData;

    UPROPERTY(VisibleAnywhere, Category = "Wet Clothing|Wetness Profile Maps")
    TArray<FWetClothingAssetBakedWetnessProfileMap> BakedWetnessProfileMaps;

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = "Wet Clothing")
    TArray<FString> AdditionalProfileSearchPaths;
#endif
};
