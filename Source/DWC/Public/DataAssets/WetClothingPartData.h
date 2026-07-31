#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "DataAssets/WetnessProfile.h"
#include "DataAssets/WetClothingPrecomputedBoneOptimizationCache.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationSettings.h"
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

/** WCA-wide authored profile record. Wet Parts reference this table by index. */
USTRUCT(BlueprintType)
struct DWC_API FWetPartProfileAssignment
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wetness Profile")
    FSoftObjectPath SourceProfile;

    UPROPERTY(EditAnywhere, Category = "Wetness Profile")
    EWetPartProfileBlendMode BlendMode = EWetPartProfileBlendMode::Standard;

    /** Runtime-safe fallback used when SourceProfile cannot be loaded. */
    UPROPERTY(EditAnywhere, Category = "Wetness Profile", meta = (ShowOnlyInnerProperties))
    FWetnessProfileParameters Parameters;

    FString GetDisplayName() const
    {
        return SourceProfile.IsValid() ? SourceProfile.GetAssetName() : FString();
    }
};

/** Part-local GPU Surface Water size overrides. */
USTRUCT(BlueprintType)
struct DWC_API FWetPartSurfaceWaterSettings
{
    GENERATED_BODY()

    /** Uses a part-local stamp-size scale instead of the Wetness Profile default. */
    UPROPERTY(EditAnywhere, Category = "Surface Water")
    bool bOverrideDropletStampSize = false;

    /** Multiplies the Wetness Profile Droplet Stamp Size before the GPU stamp is recorded. */
    UPROPERTY(EditAnywhere, Category = "Surface Water", meta = (ClampMin = "0.25", ClampMax = "4.0", UIMin = "0.25", UIMax = "4.0", DisplayName = "Droplet Stamp Size Scale", EditCondition = "bOverrideDropletStampSize"))
    float DropletRadiusScale = 1.0f;

    /** Uses a part-local Flow stamp-size scale instead of the Wetness Profile default. */
    UPROPERTY(EditAnywhere, Category = "Surface Water")
    bool bOverrideDropletFlowStampSize = false;

    /** Multiplies both Flow Stamp Width and Height while preserving their Profile-authored aspect ratio. */
    UPROPERTY(EditAnywhere, Category = "Surface Water", meta = (ClampMin = "0.25", ClampMax = "4.0", UIMin = "0.25", UIMax = "4.0", DisplayName = "Flow Stamp Size Scale", EditCondition = "bOverrideDropletFlowStampSize"))
    float DropletFlowSizeScale = 1.0f;

    /** Physical-looking size of the repeating stationary Droplet detail-normal pattern. */
    UPROPERTY(EditAnywhere, Category = "Surface Water", meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "4.0", DisplayName = "Static Droplet Size"))
    float DropletDetailSize = 1.0f;

    /** Physical-looking size of the independently repeating Flow Droplet detail-normal pattern. */
    UPROPERTY(EditAnywhere, Category = "Surface Water", meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "4.0", DisplayName = "Flow Droplet Size"))
    float DropletFlowDetailSize = 1.0f;

    float GetResolvedDropletStampSizeScale() const
    {
        return bOverrideDropletStampSize ? FMath::Clamp(DropletRadiusScale, 0.25f, 4.0f) : 1.0f;
    }

    float GetResolvedDropletFlowStampSizeScale() const
    {
        return bOverrideDropletFlowStampSize ? FMath::Clamp(DropletFlowSizeScale, 0.25f, 4.0f) : 1.0f;
    }

};

/** Part-local data. Material slot and UV channel are owned by the parent WCA/slot. */
USTRUCT(BlueprintType)
struct DWC_API FWetClothingWetPartEntry
{
    GENERATED_BODY()

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

    /** Index into FWetClothingEditableWetPartData::Profiles. Index 0 is the default inline profile. */
    UPROPERTY(EditAnywhere, Category = "Wetness Profile", meta = (ClampMin = "0"))
    int32 ProfileIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Surface Water", meta = (ShowOnlyInnerProperties, DisplayName = "Surface Water (GPU Simulation Only)"))
    FWetPartSurfaceWaterSettings SurfaceWater;
};

/** Authoritative data for one material slot. */
USTRUCT(BlueprintType)
struct DWC_API FWetClothingAuthoredMaterialSlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    bool bIsWettableSlot = false;

    /** Slot-specific Surface Water detail-rendering UV settings. */
    UPROPERTY(EditAnywhere, Category = "Surface Water", meta = (ShowOnlyInnerProperties, DisplayName = "Surface Water Rendering (GPU Simulation Only)", ToolTip = "Technical UV settings used only when a Part in this slot enables GPU Surface Water."))
    FSurfaceWaterMaterialSlotData SurfaceWater;

#if WITH_EDITORONLY_DATA
    /** Distinguishes an explicit None selection from an unresolved slot. */
    UPROPERTY(EditAnywhere, Category = "Wet Part")
    bool bHasSourceTextureSelection = false;

    /** Editor preview/UV background only. Runtime never consumes this texture. */
    UPROPERTY(EditAnywhere, Category = "Wet Part")
    TObjectPtr<UTexture> SourceTexture = nullptr;
#endif

    UPROPERTY(EditAnywhere, Category = "Wet Part")
    TArray<FWetClothingWetPartEntry> WetPartEntries;

    FWetClothingWetPartEntry* FindPart(const int32 WetPartID)
    {
        return WetPartEntries.FindByPredicate(
            [WetPartID](const FWetClothingWetPartEntry& Candidate)
            {
                return Candidate.WetPartID == WetPartID;
            });
    }

    const FWetClothingWetPartEntry* FindPart(const int32 WetPartID) const
    {
        return WetPartEntries.FindByPredicate(
            [WetPartID](const FWetClothingWetPartEntry& Candidate)
            {
                return Candidate.WetPartID == WetPartID;
            });
    }
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingEditableWetPartData
{
    GENERATED_BODY()

    /** One authoritative record per material slot. */
    UPROPERTY(EditAnywhere, Category = "Wet Part")
    TArray<FWetClothingAuthoredMaterialSlot> MaterialSlots;

    /** WCA-wide deduplicated authored profile table. Index 0 is always the default inline profile. */
    UPROPERTY(EditAnywhere, Category = "Wetness Profile")
    TArray<FWetPartProfileAssignment> Profiles;

    FWetClothingAuthoredMaterialSlot* FindMaterialSlot(const int32 MaterialSlotIndex)
    {
        return MaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingAuthoredMaterialSlot& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });
    }

    const FWetClothingAuthoredMaterialSlot* FindMaterialSlot(const int32 MaterialSlotIndex) const
    {
        return MaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingAuthoredMaterialSlot& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });
    }

    FWetClothingAuthoredMaterialSlot& FindOrAddMaterialSlot(const int32 MaterialSlotIndex)
    {
        if (FWetClothingAuthoredMaterialSlot* Existing = FindMaterialSlot(MaterialSlotIndex))
        {
            return *Existing;
        }

        FWetClothingAuthoredMaterialSlot& Added = MaterialSlots.AddDefaulted_GetRef();
        Added.MaterialSlotIndex = MaterialSlotIndex;
        return Added;
    }

    void EnsureDefaultProfile()
    {
        if (Profiles.IsEmpty())
        {
            Profiles.AddDefaulted();
        }
    }

    FWetPartProfileAssignment* FindProfile(const int32 ProfileIndex)
    {
        return Profiles.IsValidIndex(ProfileIndex) ? &Profiles[ProfileIndex] : nullptr;
    }

    const FWetPartProfileAssignment* FindProfile(const int32 ProfileIndex) const
    {
        return Profiles.IsValidIndex(ProfileIndex) ? &Profiles[ProfileIndex] : nullptr;
    }

    const FWetPartProfileAssignment* FindProfile(const FWetClothingWetPartEntry& Entry) const
    {
        return FindProfile(Entry.ProfileIndex);
    }

    int32 FindOrAddProfile(
        const FSoftObjectPath& SourceProfile,
        const FWetnessProfileParameters& Parameters,
        const EWetPartProfileBlendMode BlendMode = EWetPartProfileBlendMode::Standard)
    {
        EnsureDefaultProfile();
        if (!SourceProfile.IsValid())
        {
            return 0;
        }

        const int32 ExistingIndex = Profiles.IndexOfByPredicate(
            [&SourceProfile, BlendMode](const FWetPartProfileAssignment& Candidate)
            {
                return Candidate.SourceProfile == SourceProfile && Candidate.BlendMode == BlendMode;
            });
        if (ExistingIndex != INDEX_NONE)
        {
            Profiles[ExistingIndex].Parameters = Parameters;
            return ExistingIndex;
        }

        FWetPartProfileAssignment& Added = Profiles.AddDefaulted_GetRef();
        Added.SourceProfile = SourceProfile;
        Added.BlendMode = BlendMode;
        Added.Parameters = Parameters;
        return Profiles.Num() - 1;
    }

    /** Removes unreferenced authored profiles while preserving index 0 and remapping every Part. */
    void CompactProfiles()
    {
        EnsureDefaultProfile();

        TSet<int32> UsedProfileIndices;
        UsedProfileIndices.Add(0);
        for (const FWetClothingAuthoredMaterialSlot& Slot : MaterialSlots)
        {
            for (const FWetClothingWetPartEntry& Entry : Slot.WetPartEntries)
            {
                if (Profiles.IsValidIndex(Entry.ProfileIndex))
                {
                    UsedProfileIndices.Add(Entry.ProfileIndex);
                }
            }
        }

        TArray<int32> OldToNew;
        OldToNew.Init(INDEX_NONE, Profiles.Num());
        TArray<FWetPartProfileAssignment> Compacted;
        Compacted.Reserve(UsedProfileIndices.Num());
        for (int32 OldIndex = 0; OldIndex < Profiles.Num(); ++OldIndex)
        {
            if (!UsedProfileIndices.Contains(OldIndex))
            {
                continue;
            }
            OldToNew[OldIndex] = Compacted.Add(Profiles[OldIndex]);
        }

        for (FWetClothingAuthoredMaterialSlot& Slot : MaterialSlots)
        {
            for (FWetClothingWetPartEntry& Entry : Slot.WetPartEntries)
            {
                Entry.ProfileIndex = OldToNew.IsValidIndex(Entry.ProfileIndex) && OldToNew[Entry.ProfileIndex] != INDEX_NONE
                    ? OldToNew[Entry.ProfileIndex]
                    : 0;
            }
        }
        Profiles = MoveTemp(Compacted);
    }
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingGeneratedWetMaterialOverride
{
    GENERATED_BODY()

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
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    FSoftObjectPath SourceProfile;

    /** Authoritative non-editor snapshot; editor/PIE may temporarily override it from SourceProfile. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    FWetnessProfileParameters Parameters;

    /** Stable build key. Local ID 0 is always neutral and is not stored here. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    FString StableKey;

    /** Authored source identities retained without hard-loading the source textures at runtime. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    FSoftObjectPath SourceDropletNormal;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    FSoftObjectPath SourceDropletMask;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    FSoftObjectPath SourceDropletFlowNormal;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    FSoftObjectPath SourceDropletFlowMask;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    FSoftObjectPath SourceDropletFlowNoise;

    /** Array-compatible authored textures retained as hard references for runtime Texture2DArray upload. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    TObjectPtr<UTexture2D> NormalizedDropletNormal = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    TObjectPtr<UTexture2D> NormalizedDropletMask = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    TObjectPtr<UTexture2D> NormalizedDropletFlowNormal = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    TObjectPtr<UTexture2D> NormalizedDropletFlowMask = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    TObjectPtr<UTexture2D> NormalizedDropletFlowNoise = nullptr;

};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingBakedWetPartDataSlotTexture
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    int32 MaterialSlotIndex = INDEX_NONE;

    /** Point-sampled Wet Part data: R=Local Profile ID, G=Static Droplet Size, B=Flow Droplet Size, A=reserved. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    TObjectPtr<UTexture2D> WetPartDataTexture = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    FGuid BakeGuid;

    bool IsValid() const
    {
        return MaterialSlotIndex != INDEX_NONE && WetPartDataTexture != nullptr;
    }
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingBakedWetPartData
{
    GENERATED_BODY()

    /** WCA-wide local profile table. Texture value N maps to LocalProfiles[N - 1]. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    TArray<FWetClothingLocalRenderProfile> LocalProfiles;

    /** One Wet Part Data Texture per wettable material slot. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    TArray<FWetClothingBakedWetPartDataSlotTexture> SlotTextures;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    int32 DataUVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    int32 Resolution = 256;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    int32 PaddingPixels = 4;

    /** Required resolution for every authored Surface Water texture uploaded to the runtime arrays. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    int32 SurfaceTextureResolution = 512;

    /** Project-wide shared flat normal used as Texture2DArray slice 0. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture|Surface Texture")
    TObjectPtr<UTexture2D> NormalizedNeutralSurfaceNormal = nullptr;

    /** Signature covering the WCA-wide local table and every slot bake. */
    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Wet Part Data Texture")
    FGuid BakeGuid;

    const FWetClothingBakedWetPartDataSlotTexture* FindSlot(const int32 MaterialSlotIndex) const
    {
        return SlotTextures.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingBakedWetPartDataSlotTexture& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });
    }

    FWetClothingBakedWetPartDataSlotTexture* FindSlot(const int32 MaterialSlotIndex)
    {
        return SlotTextures.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingBakedWetPartDataSlotTexture& Candidate)
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
                   [](const FWetClothingBakedWetPartDataSlotTexture& Slot)
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

    /** Direct index into Authored.PartData.EditableWetPartData.Profiles. */
    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 ProfileIndex = INDEX_NONE;

    /** INDEX_NONE means this vertex is not part of a wettable material slot. */
    UPROPERTY(VisibleAnywhere, Category = "Precomputed Simulation Data")
    int32 MaterialSlotIndex = INDEX_NONE;

    bool IsWettable() const
    {
        return MaterialSlotIndex != INDEX_NONE && ProfileIndex != INDEX_NONE;
    }
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
