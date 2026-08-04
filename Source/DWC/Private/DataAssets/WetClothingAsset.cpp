#include "DataAssets/WetClothingAsset.h"

#include "CoreGlobals.h"
#include "Engine/SkeletalMesh.h"
#include "DerivedData/DWCMeshContentSignature.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/SecureHash.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "RuntimeState/WetBoneOptimizationCacheBuilder.h"
#include "RuntimeState/WetGPUMapBakeBuilder.h"
#include "RuntimeState/DWCOriginalUVRuntimeTopology.h"
#include "RuntimeState/WCALODVertexColorBuilder.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "DataAssets/WetnessProfile.h"
#include "Utility/DWCError.h"
#include "Utility/DWCLog.h"
#include "Utility/DWCDataUVBufferView.h"

namespace
{
    static constexpr float CoincidentVertexNeighborTolerance = 0.001f;
    static constexpr int32 DWCRuntimeBulkPayloadMagic = 0x44574342; // DWCB
    static constexpr int32 DWCMinSupportedRuntimeBulkDataVersion = 1;
    static constexpr int64 RuntimeBulkProgressThresholdBytes = 8ll * 1024ll * 1024ll;
    static constexpr float DWCRuntimeBulkHeaderProgress = 0.2f;
    static constexpr float DWCRuntimeBulkCPUVertexProgress = 0.25f;
    static constexpr float DWCRuntimeBulkCPUNeighborProgress = 0.45f;
    static constexpr float DWCRuntimeBulkCPUBoneProgress = 0.30f;
    static constexpr float DWCRuntimeBulkCPUProgress =
        DWCRuntimeBulkCPUVertexProgress +
        DWCRuntimeBulkCPUNeighborProgress +
        DWCRuntimeBulkCPUBoneProgress;
    static constexpr float DWCRuntimeBulkGPUHeaderProgress = 0.3f;
    static constexpr float DWCRuntimeBulkGPUProgress = 1.5f;
    static constexpr float DWCRuntimeBulkLockProgress = 0.15f;
    static constexpr float DWCRuntimeBulkAllocateProgress = 0.2f;
    static constexpr float DWCRuntimeBulkCopyProgress = 0.65f;
    static constexpr float DWCRuntimeBulkCopyTotalProgress =
        DWCRuntimeBulkLockProgress +
        DWCRuntimeBulkAllocateProgress +
        DWCRuntimeBulkCopyProgress;
    static constexpr float DWCRuntimeBulkSerializationProgress =
        DWCRuntimeBulkHeaderProgress +
        DWCRuntimeBulkCPUProgress +
        DWCRuntimeBulkGPUHeaderProgress +
        DWCRuntimeBulkGPUProgress;

    int32 GetLODCount(const USkeletalMesh* Mesh)
    {
        const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        return RenderData != nullptr ? RenderData->LODRenderData.Num() : 0;
    }

    int32 GetLODUVChannelCount(const USkeletalMesh* Mesh, const int32 LODIndex)
    {
        const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            return 0;
        }
        return static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumTexCoords());
    }

    void ClampSetupLODRangeToMesh(
        const USkeletalMesh* Mesh,
        FDWCWetClothingAssetSetupSettings& Settings)
    {
        const int32 LODCount = GetLODCount(Mesh);
        if (LODCount <= 0)
        {
            Settings.FirstGeneratedLODIndex = 0;
            Settings.LastGeneratedLODIndex = 0;
            return;
        }

        const int32 LastAvailableLODIndex = LODCount - 1;
        Settings.FirstGeneratedLODIndex = FMath::Clamp(Settings.FirstGeneratedLODIndex, 0, LastAvailableLODIndex);
        Settings.LastGeneratedLODIndex = FMath::Clamp(
            Settings.LastGeneratedLODIndex,
            Settings.FirstGeneratedLODIndex,
            LastAvailableLODIndex);
    }

    bool ResolveSetupLODRangeForMesh(
        const USkeletalMesh* Mesh,
        const FDWCWetClothingAssetSetupSettings& Settings,
        int32& OutFirstLODIndex,
        int32& OutLastLODIndex,
        FString* OutErrorMessage = nullptr)
    {
        const int32 LODCount = GetLODCount(Mesh);
        if (LODCount <= 0)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("The DWC Prepared Skeletal Mesh has no render LOD data."));
            OutFirstLODIndex = 0;
            OutLastLODIndex = 0;
            return false;
        }

        const int32 LastAvailableLODIndex = LODCount - 1;
        OutFirstLODIndex = FMath::Clamp(Settings.FirstGeneratedLODIndex, 0, LastAvailableLODIndex);
        OutLastLODIndex = FMath::Clamp(Settings.LastGeneratedLODIndex, OutFirstLODIndex, LastAvailableLODIndex);
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
    }

    bool DoesMappedLODRangeHavePayload(
        const USkeletalMesh* Mesh,
        const FDWCWetClothingAssetSetupSettings& Settings,
        TFunctionRef<bool(int32)> Predicate)
    {
        int32 FirstLODIndex = 0;
        int32 LastLODIndex = 0;
        if (!ResolveSetupLODRangeForMesh(Mesh, Settings, FirstLODIndex, LastLODIndex))
        {
            return false;
        }

        for (int32 LODIndex = FirstLODIndex; LODIndex <= LastLODIndex; ++LODIndex)
        {
            if (!Predicate(LODIndex))
            {
                return false;
            }
        }
        return true;
    }

    bool ResolveSavedDataUVLODRange(
        const FDWCWetClothingAssetSetupSettings& Settings,
        const TArray<FDWCDataUVLODMetadata>& DataUVMetadata,
        int32& OutFirstLODIndex,
        int32& OutLastLODIndex)
    {
        OutFirstLODIndex = FMath::Max(0, Settings.FirstGeneratedLODIndex);
        OutLastLODIndex = Settings.LastGeneratedLODIndex;

        int32 HighestSavedLODIndex = INDEX_NONE;
        for (const FDWCDataUVLODMetadata& Metadata : DataUVMetadata)
        {
            HighestSavedLODIndex = FMath::Max(HighestSavedLODIndex, Metadata.LODIndex);
        }

        if (HighestSavedLODIndex == INDEX_NONE)
        {
            OutLastLODIndex = OutFirstLODIndex;
            return false;
        }
        if (OutFirstLODIndex > HighestSavedLODIndex)
        {
            OutLastLODIndex = HighestSavedLODIndex;
            return false;
        }

        if (OutLastLODIndex == MAX_int32)
        {
            OutLastLODIndex = HighestSavedLODIndex;
        }
        else if (OutLastLODIndex > HighestSavedLODIndex)
        {
            OutLastLODIndex = HighestSavedLODIndex;
            return false;
        }

        OutLastLODIndex = FMath::Max(OutFirstLODIndex, OutLastLODIndex);
        return true;
    }

    bool DoesSavedDataUVLODRangeHavePayload(
        const FDWCWetClothingAssetSetupSettings& Settings,
        const TArray<FDWCDataUVLODMetadata>& DataUVMetadata,
        TFunctionRef<bool(int32)> Predicate)
    {
        int32 FirstLODIndex = 0;
        int32 LastLODIndex = 0;
        if (!ResolveSavedDataUVLODRange(Settings, DataUVMetadata, FirstLODIndex, LastLODIndex))
        {
            return false;
        }

        for (int32 LODIndex = FirstLODIndex; LODIndex <= LastLODIndex; ++LODIndex)
        {
            if (!Predicate(LODIndex))
            {
                return false;
            }
        }
        return true;
    }

    bool HasUsableBakedWrinkleMapForGroup(
        const UWetClothingAsset& Asset,
        const int32 MaterialSlotIndex,
        const int32 UVChannelIndex,
        const int32 LODIndex)
    {
        const FWetWrinkleBakedMapSet* BakedMap = Asset.Authored.WrinkleData.BakedWrinkleMaps.FindByPredicate(
            [MaterialSlotIndex, UVChannelIndex, LODIndex](const FWetWrinkleBakedMapSet& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                       Candidate.UVChannelIndex == UVChannelIndex &&
                       Candidate.LODIndex == LODIndex;
            });

        return BakedMap != nullptr &&
               BakedMap->BakedWrinkleNormalMap != nullptr &&
#if WITH_EDITORONLY_DATA
               BakedMap->BakedWrinkleMask != nullptr &&
#endif
               BakedMap->BakeGuid.IsValid() &&
               !BakedMap->BuildSignature.IsEmpty();
    }

    bool DoesWrinkleBakeGroupHaveContent(
        const UWetClothingAsset& Asset,
        const int32 MaterialSlotIndex,
        const int32 UVChannelIndex,
        const int32 LODIndex)
    {
        const FWetClothingWrinkleData& WrinkleData = Asset.Authored.WrinkleData;
        const bool bIncludeDisabled = WrinkleData.BakeSettings.bIncludeDisabledPatches;

        for (const FWetWrinklePatchPlacement& Patch : WrinkleData.EditablePatches)
        {
            if ((!Patch.bEnabled && !bIncludeDisabled) ||
                Patch.MaterialSlotIndex != MaterialSlotIndex ||
                Patch.UVChannelIndex != UVChannelIndex ||
                Patch.WrinkleNormalTexture == nullptr)
            {
                continue;
            }
            return true;
        }

        for (const FWetProceduralRidgeStroke& Stroke : WrinkleData.EditableProceduralRidgeStrokes)
        {
            if ((!Stroke.bEnabled && !bIncludeDisabled) ||
                Stroke.MaterialSlotIndex != MaterialSlotIndex ||
                Stroke.UVChannelIndex != UVChannelIndex ||
                Stroke.LODIndex != LODIndex ||
                Stroke.Points.Num() < 2 ||
                Stroke.WidthUV <= 0.0f ||
                Stroke.Strength <= 0.0f)
            {
                continue;
            }
            return true;
        }

        return false;
    }

    bool AreRequiredWrinkleBakeAssetReferencesValid(const UWetClothingAsset& Asset)
    {
        const FWetClothingWrinkleData& WrinkleData = Asset.Authored.WrinkleData;
        const int32 UVChannelIndex = Asset.GetDWCDataUVChannelIndex();
        TArray<int32> LODIndices = WrinkleData.BakeSettings.TargetLODIndices;
        if (LODIndices.IsEmpty())
        {
            LODIndices.Add(UWetClothingAsset::RuntimeSimulationLODIndex);
        }

        TSet<int32> MaterialSlotIndices;
        for (const FWetWrinklePatchPlacement& Patch : WrinkleData.EditablePatches)
        {
            if ((!Patch.bEnabled && !WrinkleData.BakeSettings.bIncludeDisabledPatches) ||
                Patch.MaterialSlotIndex == INDEX_NONE ||
                Patch.UVChannelIndex != UVChannelIndex ||
                Patch.WrinkleNormalTexture == nullptr ||
                !Asset.IsMaterialSlotWettable(Patch.MaterialSlotIndex) ||
                WrinkleData.IsUsingCustomWrinkleNormalMap(
                    Patch.MaterialSlotIndex,
                    UVChannelIndex,
                    UWetClothingAsset::RuntimeSimulationLODIndex))
            {
                continue;
            }
            MaterialSlotIndices.Add(Patch.MaterialSlotIndex);
        }

        for (const FWetProceduralRidgeStroke& Stroke : WrinkleData.EditableProceduralRidgeStrokes)
        {
            if ((!Stroke.bEnabled && !WrinkleData.BakeSettings.bIncludeDisabledPatches) ||
                Stroke.MaterialSlotIndex == INDEX_NONE ||
                Stroke.UVChannelIndex != UVChannelIndex ||
                Stroke.Points.Num() < 2 ||
                Stroke.WidthUV <= 0.0f ||
                Stroke.Strength <= 0.0f ||
                !Asset.IsMaterialSlotWettable(Stroke.MaterialSlotIndex) ||
                WrinkleData.IsUsingCustomWrinkleNormalMap(
                    Stroke.MaterialSlotIndex,
                    UVChannelIndex,
                    UWetClothingAsset::RuntimeSimulationLODIndex))
            {
                continue;
            }
            MaterialSlotIndices.Add(Stroke.MaterialSlotIndex);
        }

        bool bFoundRequiredBake = false;
        for (const int32 MaterialSlotIndex : MaterialSlotIndices)
        {
            for (const int32 RequestedLODIndex : LODIndices)
            {
                const int32 LODIndex = FMath::Max(0, RequestedLODIndex);
                if (!DoesWrinkleBakeGroupHaveContent(Asset, MaterialSlotIndex, UVChannelIndex, LODIndex))
                {
                    continue;
                }

                bFoundRequiredBake = true;
                if (!HasUsableBakedWrinkleMapForGroup(Asset, MaterialSlotIndex, UVChannelIndex, LODIndex))
                {
                    return false;
                }
            }
        }

        return bFoundRequiredBake;
    }

    bool AreRequiredTransparencyBakeAssetReferencesValid(const UWetClothingAsset& Asset)
    {
        bool bFoundRequiredLayer = false;
        const int32 LODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;

        for (const FWetClothingTransparencyLayerData& Layer : Asset.Authored.TransparencyData.TransparencyLayers)
        {
            const int32 MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
            if (Layer.SourceType != EDWCTransparencySourceType::SameMeshMaterialSlots ||
                MaterialSlotIndex == INDEX_NONE ||
                !Asset.IsMaterialSlotWettable(MaterialSlotIndex))
            {
                continue;
            }

            bFoundRequiredLayer = true;
            const FWetClothingBakedTransparencyMap* BakedMap = Layer.BakedMaps.FindByPredicate(
                [MaterialSlotIndex, &Layer, LODIndex](const FWetClothingBakedTransparencyMap& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                           Candidate.UVChannelIndex == Layer.TargetSurface.OuterUVChannel &&
                           Candidate.LODIndex == LODIndex;
                });
            if (BakedMap == nullptr || !BakedMap->IsRuntimeUsable())
            {
                return false;
            }
        }

        return bFoundRequiredLayer;
    }

    bool IsWettableMaterialSlot(
        const FWetClothingEditableWetPartData& WetPartData,
        const int32 MaterialSlotIndex)
    {
        const FWetClothingAuthoredMaterialSlot* Slot = WetPartData.FindMaterialSlot(MaterialSlotIndex);
        return Slot != nullptr && Slot->bIsWettableSlot;
    }

    FString MakeWetnessProfileParametersHash(const FWetnessProfileParameters& Parameters)
    {
        const FAbsorbedWetnessProfileParameters& Absorbed = Parameters.AbsorbedWetness;
        const FResolvedAbsorbedWaterSimulationParameters AbsorbedSimulation =
            Parameters.ResolveAbsorbedWaterSimulation();
        const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;

        const FString AbsorbedKey = FString::Printf(
            TEXT("Abs{%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g}"),
            Absorbed.bEnabled ? 1 : 0,
            Parameters.GetAbsorptionFraction(),
            AbsorbedSimulation.AbsorptionMultiplier,
            AbsorbedSimulation.SpreadRatePerSecond,
            AbsorbedSimulation.DryRatePerSecond,
            AbsorbedSimulation.GravityFlowStrength,
            Parameters.GetAbsorbedDarkeningStrength(),
            Parameters.GetAbsorbedGlossinessStrength());

        const FString SurfaceKeyHead = FString::Printf(
            TEXT("Surf{%d,Dry{%.9g},D1{%.9g,%.9g,%.9g},"),
            Surface.bEnabled ? 1 : 0,
            Parameters.GetDropletDryRatePerSecond(),
            Surface.DropletSpawnProbability,
            Surface.DropletRadiusPixels,
            Surface.DropletHeightPixels);

        const FString SurfaceKeyTail = FString::Printf(
            TEXT("D1Render{%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%s,%s},D2{%.9g,%.9g,%.9g,%.9g},D2Render{%s,%s,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g}}"),
            Surface.SurfaceWaterTargetRoughness,
            Surface.SurfaceWaterNormalStrength,
            Surface.SurfaceWaterRoughnessBlend,
            Surface.SurfaceWaterTotalStrength,
            Surface.SurfaceWaterColorBlend,
            Surface.SurfaceWaterSpecular,
            *GetPathNameSafe(Surface.DropletNormalTexture.Get()),
            *GetPathNameSafe(Surface.DropletMaskTexture.Get()),
            Surface.DropletFlowSpawnProbability,
            Surface.DropletFlowRadiusPixels,
            Surface.DropletFlowHeightPixels,
            Surface.DropletFlowSpawnPositionSpread,
            *GetPathNameSafe(Surface.DropletFlowNormalTexture.Get()),
            *GetPathNameSafe(Surface.DropletFlowMaskTexture.Get()),
            Surface.DropletFlowTargetRoughness,
            Surface.DropletFlowRoughnessBlend,
            Surface.DropletFlowTotalStrength,
            Surface.DropletFlowColorBlend,
            Surface.DropletFlowNormalStrength,
            Surface.DropletFlowSpecular);

        const FString ParameterKey = AbsorbedKey + TEXT("|") + SurfaceKeyHead + SurfaceKeyTail;
        return FMD5::HashAnsiString(*ParameterKey);
    }

    FString MakeResolvedWetnessProfileTableKey(const TArray<FWetnessProfileParameters>& ResolvedProfiles)
    {
        FString Key = FString::Printf(TEXT("Profiles=%d"), ResolvedProfiles.Num());
        for (int32 ProfileIndex = 0; ProfileIndex < ResolvedProfiles.Num(); ++ProfileIndex)
        {
            Key += FString::Printf(
                TEXT("|Profile[%d]=Hash:%s"),
                ProfileIndex,
                *MakeWetnessProfileParametersHash(ResolvedProfiles[ProfileIndex]));
        }
        return Key;
    }

    bool AreResolvedWetnessProfileTablesEquivalent(
        const TArray<FWetnessProfileParameters>& A,
        const TArray<FWetnessProfileParameters>& B)
    {
        return MakeResolvedWetnessProfileTableKey(A) == MakeResolvedWetnessProfileTableKey(B);
    }

    bool IsGPUMapPayloadCompatibleWithCurrentSetup(
        const UWetClothingAsset& Asset,
        const FDWCGPULODBakeData& Data)
    {
        const int32 ExpectedResolution = Asset.GetSetupSettings().GetGPUSimulationMapResolution();
        if (ExpectedResolution <= 0 ||
            Data.MaterialSlotMapCount <= 0)
        {
            return false;
        }
        if (Data.MaterialSlots.Num() == 0)
        {
            return Asset.HasGPUMapDataPayload();
        }
        if (Data.MaterialSlots.Num() != Data.MaterialSlotMapCount)
        {
            return false;
        }

        const int64 ExpectedTexelCount64 =
            static_cast<int64>(ExpectedResolution) * static_cast<int64>(ExpectedResolution);
        if (ExpectedTexelCount64 > MAX_int32)
        {
            return false;
        }
        const int32 ExpectedTexelCount = static_cast<int32>(ExpectedTexelCount64);

        TSet<int32> SeenMaterialSlots;
        for (const FDWCGPUMaterialSlotBakeData& Slot : Data.MaterialSlots)
        {
            if (Slot.MaterialSlotIndex == INDEX_NONE ||
                !Asset.IsMaterialSlotWettable(Slot.MaterialSlotIndex) ||
                SeenMaterialSlots.Contains(Slot.MaterialSlotIndex) ||
                Slot.UVChannelIndex != Asset.GetDWCDataUVChannelIndex() ||
                Slot.Resolution != ExpectedResolution ||
                Slot.TexelTriangleIDs.Num() != ExpectedTexelCount ||
                Slot.PackedTexelBarycentricXY.Num() != ExpectedTexelCount ||
                Slot.RestTexelAreas.Num() != ExpectedTexelCount ||
                Slot.ValidMask.Num() != ExpectedTexelCount)
            {
                return false;
            }

            const bool bUsesSurfaceWater = Asset.DoesMaterialSlotUseSurfaceWater(Slot.MaterialSlotIndex);
            if (bUsesSurfaceWater)
            {
                const int32 ExpectedSurfaceResolution = Asset.GetSetupSettings().GetSurfaceWaterRTResolution();
                const int64 ExpectedSurfaceTexelCount64 =
                    static_cast<int64>(ExpectedSurfaceResolution) * ExpectedSurfaceResolution;
                if (ExpectedSurfaceResolution <= 0 || ExpectedSurfaceTexelCount64 > MAX_int32)
                {
                    return false;
                }
                const int32 ExpectedSurfaceTexelCount = static_cast<int32>(ExpectedSurfaceTexelCount64);
                if (Slot.SurfaceWaterResolution != ExpectedSurfaceResolution ||
                    Slot.SurfaceTexelTriangleIDs.Num() != ExpectedSurfaceTexelCount ||
                    Slot.SurfacePackedTexelBarycentricXY.Num() != ExpectedSurfaceTexelCount ||
                    Slot.SurfaceRestTexelAreas.Num() != ExpectedSurfaceTexelCount ||
                    Slot.SurfaceValidMask.Num() != ExpectedSurfaceTexelCount)
                {
                    return false;
                }
            }
            else if (Slot.SurfaceWaterResolution != 0 ||
                     !Slot.SurfaceTexelTriangleIDs.IsEmpty() ||
                     !Slot.SurfacePackedTexelBarycentricXY.IsEmpty() ||
                     !Slot.SurfaceRestTexelAreas.IsEmpty() ||
                     !Slot.SurfaceValidMask.IsEmpty())
            {
                return false;
            }

            SeenMaterialSlots.Add(Slot.MaterialSlotIndex);
        }

        return !SeenMaterialSlots.IsEmpty();
    }

    FString MakeSourceDataSignature(
        const FWetClothingEditableWetPartData& WetPartData,
        const bool bIncludeEditorDisplayFields)
    {
        FString Signature = TEXT("DWC_SourceData_v4");

        TArray<int32> SlotIndices;
        for (int32 SlotIndex = 0; SlotIndex < WetPartData.MaterialSlots.Num(); ++SlotIndex)
        {
            if (!bIncludeEditorDisplayFields && !WetPartData.MaterialSlots[SlotIndex].bIsWettableSlot)
            {
                continue;
            }
            SlotIndices.Add(SlotIndex);
        }
        SlotIndices.Sort(
            [&WetPartData](const int32 A, const int32 B)
            {
                return WetPartData.MaterialSlots[A].MaterialSlotIndex <
                       WetPartData.MaterialSlots[B].MaterialSlotIndex;
            });

        int32 IncludedPartCount = 0;
        for (const int32 SlotIndex : SlotIndices)
        {
            for (const FWetClothingWetPartEntry& Entry : WetPartData.MaterialSlots[SlotIndex].WetPartEntries)
            {
                if (bIncludeEditorDisplayFields || Entry.WetPartID != 0)
                {
                    ++IncludedPartCount;
                }
            }
        }

        Signature += FString::Printf(TEXT("|MaterialSlots=%d|WetPartEntries=%d"), SlotIndices.Num(), IncludedPartCount);
        for (const int32 SlotIndex : SlotIndices)
        {
            const FWetClothingAuthoredMaterialSlot& Slot = WetPartData.MaterialSlots[SlotIndex];
            Signature += FString::Printf(
                TEXT("|Slot{%d,%d"),
                Slot.MaterialSlotIndex,
                Slot.bIsWettableSlot ? 1 : 0);

            TArray<int32> EntryIndices;
            for (int32 EntryIndex = 0; EntryIndex < Slot.WetPartEntries.Num(); ++EntryIndex)
            {
                if (bIncludeEditorDisplayFields || Slot.WetPartEntries[EntryIndex].WetPartID != 0)
                {
                    EntryIndices.Add(EntryIndex);
                }
            }
            EntryIndices.Sort(
                [&Slot](const int32 A, const int32 B)
                {
                    return Slot.WetPartEntries[A].WetPartID < Slot.WetPartEntries[B].WetPartID;
                });

            Signature += FString::Printf(TEXT(",Parts=%d"), EntryIndices.Num());
            for (const int32 EntryIndex : EntryIndices)
            {
                const FWetClothingWetPartEntry& Entry = Slot.WetPartEntries[EntryIndex];
                const FWetPartProfileAssignment* Profile = WetPartData.FindProfile(Entry);
                TArray<int32> AssignedIslandIDs = Entry.AssignedUVIslandIDs;
                AssignedIslandIDs.Sort();

                Signature += FString::Printf(
                    TEXT("|Entry{%d,ProfileIndex=%d"),
                    Entry.WetPartID,
                    Entry.ProfileIndex);
                if (bIncludeEditorDisplayFields)
                {
                    Signature += FString::Printf(
                        TEXT(",%s,%d,%s"),
                        *Entry.DisplayName,
                        Entry.bViewEnabled ? 1 : 0,
                        Profile != nullptr ? *Profile->GetDisplayName() : TEXT(""));
                }
                Signature += FString::Printf(
                    TEXT(",Profile=%s,Blend=%d,OverrideDropletStampSize=%d,DropletRadiusScale=%.9g,OverrideDropletFlowStampSize=%d,DropletFlowSizeScale=%.9g,DropletDetailSize=%.9g,DropletFlowDetailSize=%.9g"),
                    Profile != nullptr ? *Profile->SourceProfile.ToString() : TEXT(""),
                    Profile != nullptr ? static_cast<int32>(Profile->BlendMode) : 0,
                    Entry.SurfaceWater.bOverrideDropletStampSize ? 1 : 0,
                    Entry.SurfaceWater.DropletRadiusScale,
                    Entry.SurfaceWater.bOverrideDropletFlowStampSize ? 1 : 0,
                    Entry.SurfaceWater.DropletFlowSizeScale,
                    Entry.SurfaceWater.DropletDetailSize,
                    Entry.SurfaceWater.DropletFlowDetailSize);
                Signature += FString::Printf(TEXT(",Islands=%d"), AssignedIslandIDs.Num());
                for (const int32 IslandID : AssignedIslandIDs)
                {
                    Signature += FString::Printf(TEXT(",%d"), IslandID);
                }
                Signature += TEXT("}");
            }
            Signature += TEXT("}");
        }

        return Signature;
    }

    FString MakeSourceDataSignature(const FWetClothingEditableWetPartData& WetPartData)
    {
        return MakeSourceDataSignature(WetPartData, false);
    }


    void ResolveWetnessProfilesForDerivedInline(
        FWetClothingEditableWetPartData& WetPartData,
        TArray<FWetnessProfileParameters>& OutResolvedParameters)
    {
        OutResolvedParameters.Reset();
        OutResolvedParameters.Reserve(WetPartData.Profiles.Num());

        for (FWetPartProfileAssignment& ProfileAssignment : WetPartData.Profiles)
        {
            FWetnessProfileParameters Parameters = ProfileAssignment.Parameters;
#if WITH_EDITOR
            if (ProfileAssignment.SourceProfile.IsValid())
            {
                UObject* SourceObject = ProfileAssignment.SourceProfile.ResolveObject();
                if (SourceObject == nullptr)
                {
                    SourceObject = ProfileAssignment.SourceProfile.TryLoad();
                }

                if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(SourceObject))
                {
                    Parameters = SourceProfile->GetParameters();
                }
                else
                {
                    UE_LOG(
                        LogDWC,
                        Warning,
                        TEXT("WetClothingAsset: Failed to resolve Wetness Profile '%s' while refreshing WCA snapshot. Using the WCA fallback profile."),
                        *ProfileAssignment.SourceProfile.ToString());
                }
            }
#endif

            // Keep the authored fallback and the derived runtime snapshot on the same revision.
            // Non-editor builds never need to resolve the source profile asset.
            ProfileAssignment.Parameters = Parameters;
            OutResolvedParameters.Add(Parameters);
        }
    }

    bool ValidateSetupUVChannels(
        const USkeletalMesh* SourceMesh,
        const FDWCWetClothingAssetSetupSettings& Settings,
        FString* OutErrorMessage)
    {
        const int32 LODCount = GetLODCount(SourceMesh);
        if (LODCount <= 0)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("The Source Skeletal Mesh has no render LOD data."));
            return false;
        }

        constexpr int32 SourceLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
        const int32 UVChannelCount = GetLODUVChannelCount(SourceMesh, SourceLODIndex);
        if (Settings.OriginalUVChannelIndex < 0 || Settings.OriginalUVChannelIndex >= UVChannelCount)
        {
            DWC::Error::SetMessage(
                OutErrorMessage,
                FString::Printf(
                    TEXT("Original UV Channel UV%d is unavailable on LOD%d. That LOD has %d UV channel(s)."),
                    Settings.OriginalUVChannelIndex,
                    SourceLODIndex,
                    UVChannelCount));
            return false;
        }
        if (Settings.PreferredDWCDataUVChannelIndex < 0 || Settings.PreferredDWCDataUVChannelIndex > 3)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC UV Channel must be between UV0 and UV3."));
            return false;
        }
        if (Settings.PreferredDWCDataUVChannelIndex == Settings.OriginalUVChannelIndex)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC UV Channel cannot be the same as Original UV Channel."));
            return false;
        }
        return true;
    }

    void EnterRuntimeBulkProgressFrame(FScopedSlowTask* SlowTask, const float Work, const FText& Message)
    {
        if (SlowTask != nullptr)
        {
            SlowTask->EnterProgressFrame(Work, Message);
        }
    }

    void EnterRuntimeBulkProgressTo(
        FScopedSlowTask* SlowTask,
        float& ConsumedWork,
        const float TargetWork,
        const FText& Message)
    {
        const float Delta = TargetWork - ConsumedWork;
        if (Delta > KINDA_SMALL_NUMBER)
        {
            EnterRuntimeBulkProgressFrame(SlowTask, Delta, Message);
            ConsumedWork += Delta;
        }
    }

    template <typename ElementType>
    void SerializeArray(FArchive& Ar, TArray<ElementType>& Array, TFunctionRef<void(FArchive&, ElementType&)> SerializeElement)
    {
        int32 Num = Array.Num();
        Ar << Num;
        if (Ar.IsLoading())
        {
            Array.SetNum(Num);
        }
        for (ElementType& Element : Array)
        {
            SerializeElement(Ar, Element);
        }
    }

    template <typename ElementType>
    void SerializeArrayWithProgress(
        FArchive& Ar,
        TArray<ElementType>& Array,
        TFunctionRef<void(FArchive&, ElementType&)> SerializeElement,
        FScopedSlowTask* SlowTask,
        const float Work,
        const TCHAR* LoadingLabel,
        const TCHAR* SavingLabel)
    {
        int32 Num = Array.Num();
        Ar << Num;
        if (Ar.IsLoading())
        {
            Array.SetNum(Num);
        }

        constexpr int32 ProgressInterval = 8192;
        float ConsumedWork = 0.0f;
        for (int32 ElementIndex = 0; ElementIndex < Array.Num(); ++ElementIndex)
        {
            SerializeElement(Ar, Array[ElementIndex]);
            if ((ElementIndex + 1) % ProgressInterval == 0 || ElementIndex + 1 == Array.Num())
            {
                const float TargetWork = Array.Num() > 0
                                             ? Work * static_cast<float>(ElementIndex + 1) / static_cast<float>(Array.Num())
                                             : Work;
                EnterRuntimeBulkProgressTo(
                    SlowTask,
                    ConsumedWork,
                    TargetWork,
                    FText::FromString(FString::Printf(
                        TEXT("%s %d/%d..."),
                        Ar.IsLoading() ? LoadingLabel : SavingLabel,
                        ElementIndex + 1,
                        Array.Num())));
            }
        }

        EnterRuntimeBulkProgressTo(
            SlowTask,
            ConsumedWork,
            Work,
            FText::FromString(FString::Printf(
                TEXT("%s complete."),
                Ar.IsLoading() ? LoadingLabel : SavingLabel)));
    }

    void SkipRuntimeBulkBytes(FArchive& Ar, const int64 ByteCount)
    {
        if (ByteCount > 0)
        {
            Ar.Seek(Ar.Tell() + ByteCount);
        }
    }

    template <typename ElementType>
    void SkipPODArray(FArchive& Ar)
    {
        int32 Num = 0;
        Ar << Num;
        if (Ar.IsLoading() && Num > 0)
        {
            SkipRuntimeBulkBytes(Ar, static_cast<int64>(Num) * sizeof(ElementType));
        }
    }

    template <typename ElementType>
    void SerializeOrSkipArrayWithProgress(
        FArchive& Ar,
        TArray<ElementType>& Array,
        TFunctionRef<void(FArchive&, ElementType&)> SerializeElement,
        TFunctionRef<void(FArchive&)> SkipElement,
        const bool bLoadPayload,
        FScopedSlowTask* SlowTask,
        const float Work,
        const TCHAR* LoadingLabel,
        const TCHAR* SavingLabel)
    {
        if (!Ar.IsLoading() || bLoadPayload)
        {
            SerializeArrayWithProgress<ElementType>(
                Ar,
                Array,
                SerializeElement,
                SlowTask,
                Work,
                LoadingLabel,
                SavingLabel);
            return;
        }

        int32 Num = 0;
        Ar << Num;
        for (int32 ElementIndex = 0; ElementIndex < Num; ++ElementIndex)
        {
            SkipElement(Ar);
        }

        EnterRuntimeBulkProgressFrame(
            SlowTask,
            Work,
            FText::FromString(FString::Printf(TEXT("Skipped %s %d record(s)."), LoadingLabel, Num)));
    }

    void SerializeResolvedBoneRule(FArchive& Ar, FWetClothingPrecomputedResolvedBoneIncludeRule& Rule)
    {
        Ar << Rule.TargetBoneIndex;
        Ar << Rule.IncludedBoneIndices;
    }

    void SerializeBoneOptimizationCache(FArchive& Ar, FWetClothingPrecomputedBoneOptimizationCache& Cache)
    {
        Ar << Cache.bIsValid;
        Ar << Cache.CacheFormatVersion;
        Ar << Cache.LODIndex;
        Ar << Cache.VertexCount;
        Ar << Cache.BoneCount;
        Ar << Cache.BoneNames;
        Ar << Cache.BoneStartOffsets;
        Ar << Cache.FlatVertexIndices;
        SerializeArray<FWetClothingPrecomputedResolvedBoneIncludeRule>(
            Ar,
            Cache.ResolvedIncludeRules,
            [](FArchive& InnerAr, FWetClothingPrecomputedResolvedBoneIncludeRule& Rule)
            {
                SerializeResolvedBoneRule(InnerAr, Rule);
            });
        Ar << Cache.MeshBuildSignature;
        Ar << Cache.SkeletonSignature;
        Ar << Cache.SkinWeightSignature;
    }

    void SerializePrecomputedVertex(FArchive& Ar, FWetClothingPrecomputedVertexData& Vertex)
    {
        Ar << Vertex.WetPartID;
        Ar << Vertex.ProfileIndex;
        Ar << Vertex.MaterialSlotIndex;
    }

    void SerializePrecomputedNeighbors(FArchive& Ar, FWetClothingPrecomputedVertexNeighbors& Neighbors)
    {
        Ar << Neighbors.Neighbors;
    }

    void SerializeCPUPrecomputedPayload(
        FArchive& Ar,
        FWetClothingPrecomputedSimulationData& Data,
        FScopedSlowTask* SlowTask = nullptr)
    {
        SerializeArrayWithProgress<FWetClothingPrecomputedVertexData>(
            Ar,
            Data.Vertices,
            [](FArchive& InnerAr, FWetClothingPrecomputedVertexData& Vertex)
            {
                SerializePrecomputedVertex(InnerAr, Vertex);
            },
            SlowTask,
            DWCRuntimeBulkCPUVertexProgress,
            TEXT("Loading CPU wettable vertices"),
            TEXT("Serializing CPU wettable vertices"));

        SerializeArrayWithProgress<FWetClothingPrecomputedVertexNeighbors>(
            Ar,
            Data.NeighborGraph,
            [](FArchive& InnerAr, FWetClothingPrecomputedVertexNeighbors& Neighbors)
            {
                SerializePrecomputedNeighbors(InnerAr, Neighbors);
            },
            SlowTask,
            DWCRuntimeBulkCPUNeighborProgress,
            TEXT("Loading CPU neighbor graph records"),
            TEXT("Serializing CPU neighbor graph records"));

        EnterRuntimeBulkProgressFrame(
            SlowTask,
            DWCRuntimeBulkCPUBoneProgress,
            Ar.IsLoading()
                ? NSLOCTEXT("WetClothingAsset", "LoadCPUBoneRuntimeBulk", "Loading CPU bone optimization cache...")
                : NSLOCTEXT("WetClothingAsset", "StoreCPUBoneRuntimeBulk", "Serializing CPU bone optimization cache..."));
        SerializeBoneOptimizationCache(Ar, Data.BoneOptimizationCache);
    }

    void SerializeGPUProfile(
        FArchive& Ar,
        FDWCGPUProfileParameters& Profile,
        const int32 PayloadVersion)
    {
        Ar << Profile.AbsorptionMultiplier;
        Ar << Profile.SpreadRatePerSecond;
        Ar << Profile.DryRatePerSecond;
        Ar << Profile.GravityFlowStrength;
        if (PayloadVersion >= 7)
        {
            Ar << Profile.DropletDryRatePerSecond;
        }
        else if (Ar.IsLoading())
        {
            Profile.DropletDryRatePerSecond = Profile.DryRatePerSecond;
        }
    }

    void SkipGPUProfile(FArchive& Ar, const int32 PayloadVersion)
    {
        float Value = 0.0f;
        Ar << Value;
        Ar << Value;
        Ar << Value;
        Ar << Value;
        if (PayloadVersion >= 7)
        {
            Ar << Value;
        }
    }

    void SerializeGPUTriangle(
        FArchive& Ar,
        FDWCGPUBakedTriangle& Triangle,
        const int32 PayloadVersion)
    {
        Ar << Triangle.TriangleID;
        Ar << Triangle.RenderTriangleID;
        Ar << Triangle.MaterialSlotIndex;
        Ar << Triangle.RenderSectionIndex;
        Ar << Triangle.UVChannelIndex;
        Ar << Triangle.UVIslandID;
        Ar << Triangle.VertexIndices.X;
        Ar << Triangle.VertexIndices.Y;
        Ar << Triangle.VertexIndices.Z;
        Ar << Triangle.UV0;
        Ar << Triangle.UV1;
        Ar << Triangle.UV2;
        if (PayloadVersion >= 3)
        {
            Ar << Triangle.DataToSurfaceWaterNormalUV;
        }
        else if (Ar.IsLoading())
        {
            Triangle.DataToSurfaceWaterNormalUV = FVector4(1.0, 0.0, 0.0, 1.0);
        }
        Ar << Triangle.RestSurfaceArea;
        Ar << Triangle.ProfileIndex;
    }

    void SkipGPUTriangle(FArchive& Ar, const int32 PayloadVersion)
    {
        int32 IntValue = 0;
        FVector2D UV = FVector2D::ZeroVector;
        FVector4 Transform = FVector4(1.0, 0.0, 0.0, 1.0);
        float FloatValue = 0.0f;

        Ar << IntValue; // TriangleID
        Ar << IntValue; // RenderTriangleID
        Ar << IntValue; // MaterialSlotIndex
        Ar << IntValue; // RenderSectionIndex
        Ar << IntValue; // UVChannelIndex
        Ar << IntValue; // UVIslandID
        Ar << IntValue; // VertexIndices.X
        Ar << IntValue; // VertexIndices.Y
        Ar << IntValue; // VertexIndices.Z
        Ar << UV;
        Ar << UV;
        Ar << UV;
        if (PayloadVersion >= 3)
        {
            Ar << Transform;
        }
        Ar << FloatValue;
        Ar << IntValue; // ProfileIndex
    }

    void SerializeGPUIncident(FArchive& Ar, FDWCGPUVertexIncidentTriangles& Incident)
    {
        Ar << Incident.SourceVertexIndex;
        Ar << Incident.TriangleIDs;
    }

    void SkipGPUIncident(FArchive& Ar)
    {
        int32 SourceVertexIndex = INDEX_NONE;
        Ar << SourceVertexIndex;
        SkipPODArray<int32>(Ar);
    }

    void SerializeGPUSeamDestination(FArchive& Ar, FDWCGPUSeamDestination& Destination)
    {
        Ar << Destination.DestinationTexelIndex;
        Ar << Destination.IncomingStartIndex;
        Ar << Destination.IncomingCount;
    }

    void SerializeGPUSeamIncoming(FArchive& Ar, FDWCGPUSeamIncoming& Incoming)
    {
        Ar << Incoming.SourceTexelIndex;
        Ar << Incoming.Weight;
    }

    void SkipGPUSeamDestinations(FArchive& Ar)
    {
        int32 Num = 0;
        Ar << Num;
        if (Ar.IsLoading() && Num > 0)
        {
            SkipRuntimeBulkBytes(Ar, static_cast<int64>(Num) * sizeof(int32) * 3);
        }
    }

    void SkipGPUSeamIncoming(FArchive& Ar)
    {
        int32 Num = 0;
        Ar << Num;
        if (Ar.IsLoading() && Num > 0)
        {
            SkipRuntimeBulkBytes(Ar, static_cast<int64>(Num) * (sizeof(int32) + sizeof(float)));
        }
    }

    void SerializeGPUMaterialSlot(FArchive& Ar, FDWCGPUMaterialSlotBakeData& Slot)
    {
        Ar << Slot.MaterialSlotIndex;
        Ar << Slot.UVChannelIndex;
        Ar << Slot.Resolution;
        Ar << Slot.TexelTriangleIDs;
        Ar << Slot.PackedTexelBarycentricXY;
        Ar << Slot.RestTexelAreas;
        Ar << Slot.ValidMask;
        Ar << Slot.SurfaceWaterResolution;
        Ar << Slot.SurfaceTexelTriangleIDs;
        Ar << Slot.SurfacePackedTexelBarycentricXY;
        Ar << Slot.SurfaceRestTexelAreas;
        Ar << Slot.SurfaceValidMask;
        SerializeArray<FDWCGPUSeamDestination>(
            Ar,
            Slot.SeamDestinations,
            [](FArchive& InnerAr, FDWCGPUSeamDestination& Destination)
            {
                SerializeGPUSeamDestination(InnerAr, Destination);
            });
        SerializeArray<FDWCGPUSeamIncoming>(
            Ar,
            Slot.SeamIncoming,
            [](FArchive& InnerAr, FDWCGPUSeamIncoming& Incoming)
            {
                SerializeGPUSeamIncoming(InnerAr, Incoming);
            });
    }

    void SkipGPUMaterialSlot(FArchive& Ar)
    {
        int32 IntValue = 0;
        Ar << IntValue; // MaterialSlotIndex
        Ar << IntValue; // UVChannelIndex
        Ar << IntValue; // Resolution
        SkipPODArray<int32>(Ar);
        SkipPODArray<uint32>(Ar);
        SkipPODArray<float>(Ar);
        SkipPODArray<uint8>(Ar);
        Ar << IntValue; // SurfaceWaterResolution
        SkipPODArray<int32>(Ar);
        SkipPODArray<uint32>(Ar);
        SkipPODArray<float>(Ar);
        SkipPODArray<uint8>(Ar);
        SkipGPUSeamDestinations(Ar);
        SkipGPUSeamIncoming(Ar);
    }

    void SerializeGPULODMetadata(FArchive& Ar, FDWCGPULODBakeData& Data)
    {
        Ar << Data.RuntimeDataVersion;
        Ar << Data.BulkDataVersion;
        Ar << Data.bRuntimeDataValid;
        Ar << Data.MapBakeVersion;
        Ar << Data.bMapDataValid;
        Ar << Data.LODIndex;
        Ar << Data.MeshSignature;
        Ar << Data.SourceDataSignature;
        Ar << Data.RuntimeSignature;
        Ar << Data.MapSignature;
        Ar << Data.ProfileCount;
        Ar << Data.TriangleCount;
        Ar << Data.VertexIncidentRecordCount;
        Ar << Data.MaterialSlotMapCount;
        Ar << Data.RuntimeBuildGuid;
        Ar << Data.MapBakeGuid;
    }

    void SerializeGPULODPayload(
        FArchive& Ar,
        FDWCGPULODBakeData& Data,
        const int32 PayloadVersion,
        const int32 GPUOnlyLoadedLODIndex,
        FScopedSlowTask* SlowTask = nullptr,
        const float Work = 0.0f,
        const int32 GPUIndex = 0,
        const int32 GPUCount = 1)
    {
        constexpr float MetadataRatio = 0.10f;
        constexpr float ProfileRatio = 0.10f;
        constexpr float TriangleRatio = 0.30f;
        constexpr float IncidentRatio = 0.20f;
        constexpr float MaterialSlotRatio = 0.30f;

        if (PayloadVersion >= 2)
        {
            EnterRuntimeBulkProgressFrame(
                SlowTask,
                Work * MetadataRatio,
                FText::FromString(FString::Printf(
                    TEXT("%s GPU runtime metadata %d/%d..."),
                    Ar.IsLoading() ? TEXT("Loading") : TEXT("Serializing"),
                    GPUIndex + 1,
                    GPUCount)));
            SerializeGPULODMetadata(Ar, Data);
        }

        const bool bLoadPayload =
            !Ar.IsLoading() ||
            GPUOnlyLoadedLODIndex == INDEX_NONE ||
            (PayloadVersion >= 2 && Data.LODIndex == GPUOnlyLoadedLODIndex) ||
            (PayloadVersion < 2 &&
             GPUOnlyLoadedLODIndex == UWetClothingAsset::RuntimeSimulationLODIndex &&
             GPUIndex == 0);

        if (Ar.IsLoading() && !bLoadPayload)
        {
            Data.Profiles.Reset();
            Data.Triangles.Reset();
            Data.VertexIncidentTriangles.Reset();
            Data.MaterialSlots.Reset();
        }

        SerializeOrSkipArrayWithProgress<FDWCGPUProfileParameters>(
            Ar,
            Data.Profiles,
            [PayloadVersion](FArchive& InnerAr, FDWCGPUProfileParameters& Profile)
            {
                SerializeGPUProfile(InnerAr, Profile, PayloadVersion);
            },
            [PayloadVersion](FArchive& InnerAr)
            {
                SkipGPUProfile(InnerAr, PayloadVersion);
            },
            bLoadPayload,
            SlowTask,
            Work * ProfileRatio,
            TEXT("Loading GPU wetness profiles"),
            TEXT("Serializing GPU wetness profiles"));
        SerializeOrSkipArrayWithProgress<FDWCGPUBakedTriangle>(
            Ar,
            Data.Triangles,
            [PayloadVersion](FArchive& InnerAr, FDWCGPUBakedTriangle& Triangle)
            {
                SerializeGPUTriangle(InnerAr, Triangle, PayloadVersion);
            },
            [PayloadVersion](FArchive& InnerAr)
            {
                SkipGPUTriangle(InnerAr, PayloadVersion);
            },
            bLoadPayload,
            SlowTask,
            Work * TriangleRatio,
            TEXT("Loading GPU runtime triangles"),
            TEXT("Serializing GPU runtime triangles"));
        SerializeOrSkipArrayWithProgress<FDWCGPUVertexIncidentTriangles>(
            Ar,
            Data.VertexIncidentTriangles,
            [](FArchive& InnerAr, FDWCGPUVertexIncidentTriangles& Incident)
            {
                SerializeGPUIncident(InnerAr, Incident);
            },
            [](FArchive& InnerAr)
            {
                SkipGPUIncident(InnerAr);
            },
            bLoadPayload,
            SlowTask,
            Work * IncidentRatio,
            TEXT("Loading GPU vertex incident records"),
            TEXT("Serializing GPU vertex incident records"));
        SerializeOrSkipArrayWithProgress<FDWCGPUMaterialSlotBakeData>(
            Ar,
            Data.MaterialSlots,
            [](FArchive& InnerAr, FDWCGPUMaterialSlotBakeData& Slot)
            {
                SerializeGPUMaterialSlot(InnerAr, Slot);
            },
            [](FArchive& InnerAr)
            {
                SkipGPUMaterialSlot(InnerAr);
            },
            bLoadPayload,
            SlowTask,
            Work * MaterialSlotRatio,
            TEXT("Loading GPU material-slot map payloads"),
            TEXT("Serializing GPU material-slot map payloads"));
    }

    void SerializeRuntimeBulkPayload(
        FArchive& Ar,
        FWetClothingPrecomputedSimulationData& CPUData,
        TArray<FDWCGPULODBakeData>& GPUData,
        const int32 GPUOnlyLoadedLODIndex = INDEX_NONE,
        FScopedSlowTask* SlowTask = nullptr)
    {
        EnterRuntimeBulkProgressFrame(
            SlowTask,
            DWCRuntimeBulkHeaderProgress,
            Ar.IsLoading()
                ? NSLOCTEXT("WetClothingAsset", "ReadRuntimeBulkHeader", "Reading DWC runtime payload header...")
                : NSLOCTEXT("WetClothingAsset", "WriteRuntimeBulkHeader", "Writing DWC runtime payload header..."));

        int32 Magic = DWCRuntimeBulkPayloadMagic;
        int32 Version = UWetClothingAsset::CurrentRuntimeBulkDataVersion;
        Ar << Magic;
        Ar << Version;
        if (Ar.IsLoading() &&
            (Magic != DWCRuntimeBulkPayloadMagic ||
             Version < DWCMinSupportedRuntimeBulkDataVersion ||
             Version > UWetClothingAsset::CurrentRuntimeBulkDataVersion))
        {
            Ar.SetError();
            return;
        }

        SerializeCPUPrecomputedPayload(Ar, CPUData, SlowTask);

        EnterRuntimeBulkProgressFrame(
            SlowTask,
            DWCRuntimeBulkGPUHeaderProgress,
            Ar.IsLoading()
                ? NSLOCTEXT("WetClothingAsset", "ReadGPURuntimeLODCount", "Reading GPU runtime data layout...")
                : NSLOCTEXT("WetClothingAsset", "WriteGPURuntimeLODCount", "Writing GPU runtime data layout..."));
        int32 GPUNum = GPUData.Num();
        Ar << GPUNum;
        if (Ar.IsLoading())
        {
            // Runtime bulk v1 stored only the transient arrays. Keep the UPROPERTY metadata
            // loaded by Super::Serialize when the LOD count matches, otherwise validation
            // loses bRuntimeDataValid/signature/count fields and reports missing GPU data.
            if (GPUData.Num() != GPUNum)
            {
                GPUData.SetNum(GPUNum);
            }
        }
        const float GPUProgressPerLOD = GPUNum > 0
                                            ? DWCRuntimeBulkGPUProgress / static_cast<float>(GPUNum)
                                            : DWCRuntimeBulkGPUProgress;
        if (GPUNum == 0)
        {
            EnterRuntimeBulkProgressFrame(
                SlowTask,
                GPUProgressPerLOD,
                Ar.IsLoading()
                    ? NSLOCTEXT("WetClothingAsset", "NoGPURuntimeBulkToLoad", "No GPU runtime payloads to load.")
                    : NSLOCTEXT("WetClothingAsset", "NoGPURuntimeBulkToStore", "No GPU runtime payloads to serialize."));
        }
        for (int32 GPUIndex = 0; GPUIndex < GPUData.Num(); ++GPUIndex)
        {
            FDWCGPULODBakeData& Data = GPUData[GPUIndex];
            const FString GPUProgressMessage = Ar.IsLoading()
                                                   ? FString::Printf(
                                                         TEXT("Loading GPU runtime payload %d/%d..."),
                                                         GPUIndex + 1,
                                                         GPUData.Num())
                                                   : FString::Printf(
                                                         TEXT("Serializing GPU runtime payload %d/%d..."),
                                                         GPUIndex + 1,
                                                         GPUData.Num());
            EnterRuntimeBulkProgressFrame(
                SlowTask,
                GPUProgressPerLOD * 0.05f,
                FText::FromString(GPUProgressMessage));
            SerializeGPULODPayload(
                Ar,
                Data,
                Version,
                GPUOnlyLoadedLODIndex,
                SlowTask,
                GPUProgressPerLOD * 0.95f,
                GPUIndex,
                GPUData.Num());
        }
    }

    void AddNeighbor(TArray<FWetClothingPrecomputedVertexNeighbors>& NeighborGraph, int32 A, int32 B)
    {
        if (NeighborGraph.IsValidIndex(A) && NeighborGraph.IsValidIndex(B) && A != B)
        {
            NeighborGraph[A].Neighbors.AddUnique(B);
        }
    }

    FIntVector MakeCoincidentVertexPositionKey(const FVector3f& Position)
    {
        static constexpr float QuantizeScale = 1.0f / CoincidentVertexNeighborTolerance;
        return FIntVector(
            FMath::RoundToInt(Position.X * QuantizeScale),
            FMath::RoundToInt(Position.Y * QuantizeScale),
            FMath::RoundToInt(Position.Z * QuantizeScale));
    }

    void ConnectCoincidentPositionNeighbors(
        const FSkeletalMeshLODRenderData&              LODData,
        const TArray<FWetClothingPrecomputedVertexData>& VertexData,
        TArray<FWetClothingPrecomputedVertexNeighbors>& NeighborGraph)
    {
        TMap<FIntVector, TArray<int32>> VerticesByPosition;
        const int32                     VertexCount = LODData.GetNumVertices();
        VerticesByPosition.Reserve(VertexCount);

        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            if (!VertexData.IsValidIndex(VertexIndex) || !VertexData[VertexIndex].IsWettable())
            {
                continue;
            }

            const FVector3f Position = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
            VerticesByPosition.FindOrAdd(MakeCoincidentVertexPositionKey(Position)).Add(VertexIndex);
        }

        const float ToleranceSquared = FMath::Square(CoincidentVertexNeighborTolerance);
        for (const TPair<FIntVector, TArray<int32>>& Pair : VerticesByPosition)
        {
            const TArray<int32>& Vertices = Pair.Value;
            for (int32 IndexA = 0; IndexA < Vertices.Num(); ++IndexA)
            {
                const int32     VertexA = Vertices[IndexA];
                const FVector3f PositionA = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexA);

                for (int32 IndexB = IndexA + 1; IndexB < Vertices.Num(); ++IndexB)
                {
                    const int32     VertexB = Vertices[IndexB];
                    const FVector3f PositionB = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexB);
                    if ((PositionA - PositionB).SizeSquared() > ToleranceSquared)
                    {
                        continue;
                    }

                    AddNeighbor(NeighborGraph, VertexA, VertexB);
                    AddNeighbor(NeighborGraph, VertexB, VertexA);
                }
            }
        }
    }

    void BuildNeighborGraph(
        const FSkeletalMeshLODRenderData&              LODData,
        const TArray<uint32>&                          IndexBuffer,
        const TArray<FWetClothingPrecomputedVertexData>& VertexData,
        TArray<FWetClothingPrecomputedVertexNeighbors>& OutNeighborGraph)
    {
        OutNeighborGraph.SetNum(LODData.GetNumVertices());

        for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
        {
            if (!Section.IsValid())
            {
                continue;
            }

            const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
            const int32 LastIndex = FMath::Min(FirstIndex + static_cast<int32>(Section.NumTriangles * 3), IndexBuffer.Num());

            for (int32 TriangleIndex = FirstIndex; TriangleIndex + 2 < LastIndex; TriangleIndex += 3)
            {
                const int32 Index0 = static_cast<int32>(IndexBuffer[TriangleIndex]);
                const int32 Index1 = static_cast<int32>(IndexBuffer[TriangleIndex + 1]);
                const int32 Index2 = static_cast<int32>(IndexBuffer[TriangleIndex + 2]);

                const bool b0 = VertexData.IsValidIndex(Index0) && VertexData[Index0].IsWettable();
                const bool b1 = VertexData.IsValidIndex(Index1) && VertexData[Index1].IsWettable();
                const bool b2 = VertexData.IsValidIndex(Index2) && VertexData[Index2].IsWettable();

                if (b0 && b1)
                {
                    AddNeighbor(OutNeighborGraph, Index0, Index1);
                    AddNeighbor(OutNeighborGraph, Index1, Index0);
                }
                if (b0 && b2)
                {
                    AddNeighbor(OutNeighborGraph, Index0, Index2);
                    AddNeighbor(OutNeighborGraph, Index2, Index0);
                }
                if (b1 && b2)
                {
                    AddNeighbor(OutNeighborGraph, Index1, Index2);
                    AddNeighbor(OutNeighborGraph, Index2, Index1);
                }
            }
        }

        ConnectCoincidentPositionNeighbors(LODData, VertexData, OutNeighborGraph);
    }
    void FilterBoneOptimizationCacheByWettableVertices(
        FWetBoneOptimizationCache& RuntimeBoneOptimizationCache,
        const TArray<FWetClothingPrecomputedVertexData>& VertexData)
    {
        FWetBonePrimaryVertexCache& PrimaryCache = RuntimeBoneOptimizationCache.PrimaryVertexCache;
        if (PrimaryCache.BoneCount <= 0 || PrimaryCache.BoneStartOffsets.Num() != PrimaryCache.BoneCount + 1)
        {
            return;
        }

        TArray<int32> VertexCountsByBone;
        VertexCountsByBone.Init(0, PrimaryCache.BoneCount);

        for (int32 BoneIndex = 0; BoneIndex < PrimaryCache.BoneCount; ++BoneIndex)
        {
            const int32 StartOffset = PrimaryCache.BoneStartOffsets[BoneIndex];
            const int32 EndOffset = PrimaryCache.BoneStartOffsets[BoneIndex + 1];
            for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
            {
                if (!PrimaryCache.FlatVertexIndices.IsValidIndex(Offset))
                {
                    continue;
                }

                const int32 VertexIndex = PrimaryCache.FlatVertexIndices[Offset];
                if (VertexData.IsValidIndex(VertexIndex) && VertexData[VertexIndex].IsWettable())
                {
                    ++VertexCountsByBone[BoneIndex];
                }
            }
        }

        TArray<int32> NewStartOffsets;
        NewStartOffsets.SetNumZeroed(PrimaryCache.BoneCount + 1);
        for (int32 BoneIndex = 0; BoneIndex < PrimaryCache.BoneCount; ++BoneIndex)
        {
            NewStartOffsets[BoneIndex + 1] = NewStartOffsets[BoneIndex] + VertexCountsByBone[BoneIndex];
        }

        TArray<int32> NewFlatVertexIndices;
        NewFlatVertexIndices.SetNumUninitialized(NewStartOffsets.Last());

        TArray<int32> WriteOffsets = NewStartOffsets;
        for (int32 BoneIndex = 0; BoneIndex < PrimaryCache.BoneCount; ++BoneIndex)
        {
            const int32 StartOffset = PrimaryCache.BoneStartOffsets[BoneIndex];
            const int32 EndOffset = PrimaryCache.BoneStartOffsets[BoneIndex + 1];
            for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
            {
                if (!PrimaryCache.FlatVertexIndices.IsValidIndex(Offset))
                {
                    continue;
                }

                const int32 VertexIndex = PrimaryCache.FlatVertexIndices[Offset];
                if (VertexData.IsValidIndex(VertexIndex) && VertexData[VertexIndex].IsWettable())
                {
                    NewFlatVertexIndices[WriteOffsets[BoneIndex]++] = VertexIndex;
                }
            }
        }

        PrimaryCache.BoneStartOffsets = MoveTemp(NewStartOffsets);
        PrimaryCache.FlatVertexIndices = MoveTemp(NewFlatVertexIndices);
    }

    const FDWCGPULODBakeData* FindGPULODData(const TArray<FDWCGPULODBakeData>& Data, const int32 LODIndex)
    {
        return Data.FindByPredicate(
            [LODIndex](const FDWCGPULODBakeData& Candidate)
            {
                return Candidate.LODIndex == LODIndex;
            });
    }

    int32 RemoveNonSimulationGPULODData(TArray<FDWCGPULODBakeData>& Data)
    {
        return Data.RemoveAll(
            [](const FDWCGPULODBakeData& Candidate)
            {
                return Candidate.LODIndex != UWetClothingAsset::RuntimeSimulationLODIndex;
            });
    }

} // namespace

void UWetClothingAsset::ClearPrecomputedSimulationData()
{
    // RuntimeBulkData stores CPU and GPU payloads together. Load it before mutating
    // one segment so saving the change cannot discard the still-valid other segment.
    if (!LoadRuntimeBulkData())
    {
        return;
    }
#if WITH_EDITOR
    ClearMeshContentSignatureCache();
#endif
    Derived.Bulk.NeighborRuntimeData = FWetClothingPrecomputedSimulationData();
    MarkRuntimeBulkDataDirty(DWCBakeOutput::CPURuntimeData);
}

void UWetClothingAsset::ClearGPUWetMapData()
{
    // RuntimeBulkData stores CPU and GPU payloads together. Load it before mutating
    // one segment so saving the change cannot discard the still-valid other segment.
    if (!LoadRuntimeBulkData())
    {
        return;
    }
#if WITH_EDITOR
    ClearMeshContentSignatureCache();
#endif
    Derived.Bulk.GPURuntimeData.Reset();
    MarkRuntimeBulkDataDirty(DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps);
}

void UWetClothingAsset::ClearGPUMapData()
{
    if (!LoadRuntimeBulkData())
    {
        return;
    }
#if WITH_EDITOR
    ClearMeshContentSignatureCache();
#endif
    for (FDWCGPULODBakeData& Data : Derived.Bulk.GPURuntimeData)
    {
        Data.bMapDataValid = false;
        Data.MapBakeVersion = 0;
        Data.MapSignature.Reset();
        Data.MaterialSlotMapCount = 0;
        Data.MaterialSlots.Reset();
        Data.MapBakeGuid.Invalidate();
    }
    MarkRuntimeBulkDataDirty(DWCBakeOutput::GPUMaps);
}

const FWetClothingPrecomputedSimulationData& UWetClothingAsset::GetPrecomputedSimulationData() const
{
    EnsureRuntimeBulkDataLoaded();
    return Derived.Bulk.NeighborRuntimeData;
}

const FDWCGPULODBakeData& UWetClothingAsset::GetGPUWetMapRuntimeData(const int32 LODIndex) const
{
    EnsureRuntimeBulkDataLoaded();
    if (const FDWCGPULODBakeData* Data = FindGPULODData(Derived.Bulk.GPURuntimeData, LODIndex))
    {
        return *Data;
    }

    static const FDWCGPULODBakeData EmptyData;
    return EmptyData;
}

const FDWCGPULODBakeData& UWetClothingAsset::GetGPUWetMapRuntimeDataMetadata(const int32 LODIndex) const
{
    if (const FDWCGPULODBakeData* Data = FindGPULODData(Derived.Bulk.GPURuntimeData, LODIndex))
    {
        return *Data;
    }

    static const FDWCGPULODBakeData EmptyData;
    return EmptyData;
}

bool UWetClothingAsset::HasCPURuntimeDataPayload() const
{
    return Derived.Bulk.NeighborRuntimeData.bIsValid &&
           (Derived.Bulk.NeighborRuntimeData.Vertices.Num() > 0 ||
            (!bRuntimeBulkDataLoaded && !bRuntimeBulkDataLoadFailed && HasRuntimeBulkPayload()));
}

bool UWetClothingAsset::HasGPURuntimeDataPayload() const
{
    return Derived.Bulk.GPURuntimeData.ContainsByPredicate(
        [this](const FDWCGPULODBakeData& Data)
        {
            return Data.bRuntimeDataValid &&
                   Data.TriangleCount > 0 &&
                   (Data.Triangles.Num() > 0 ||
                    (!bRuntimeBulkDataLoaded && !bRuntimeBulkDataLoadFailed && HasRuntimeBulkPayload()));
        });
}

bool UWetClothingAsset::HasGPUMapDataPayload() const
{
    return Derived.Bulk.GPURuntimeData.ContainsByPredicate(
        [this](const FDWCGPULODBakeData& Data)
        {
            return Data.bMapDataValid &&
                   Data.MaterialSlotMapCount > 0 &&
                   (Data.MaterialSlots.Num() > 0 ||
                    (!bRuntimeBulkDataLoaded && !bRuntimeBulkDataLoadFailed && HasRuntimeBulkPayload()));
        });
}

bool UWetClothingAsset::IsMaterialSlotWettable(const int32 MaterialSlotIndex) const
{
    return IsWettableMaterialSlot(Authored.PartData.EditableWetPartData, MaterialSlotIndex);
}

bool UWetClothingAsset::HasAnyWettableMaterialSlot() const
{
    return Authored.PartData.EditableWetPartData.MaterialSlots.ContainsByPredicate(
        [](const FWetClothingAuthoredMaterialSlot& Slot)
        {
            return Slot.bIsWettableSlot && Slot.MaterialSlotIndex != INDEX_NONE;
        });
}

bool UWetClothingAsset::DoesMaterialSlotUseSurfaceWater(const int32 MaterialSlotIndex) const
{
    const FWetClothingEditableWetPartData& WetPartData = Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* Slot = WetPartData.FindMaterialSlot(MaterialSlotIndex);
    if (Slot == nullptr || !Slot->bIsWettableSlot || Slot->MaterialSlotIndex == INDEX_NONE)
    {
        return false;
    }

    for (const FWetClothingWetPartEntry& Entry : Slot->WetPartEntries)
    {
        if (Entry.WetPartID == 0)
        {
            continue;
        }

        const int32 ProfileIndex = WetPartData.Profiles.IsValidIndex(Entry.ProfileIndex)
            ? Entry.ProfileIndex
            : 0;
        FWetnessProfileParameters Parameters;
        const bool bHasAuthoredProfile = WetPartData.Profiles.IsValidIndex(ProfileIndex);
        bool bHasParameters = false;
        if (bHasAuthoredProfile)
        {
            const FWetPartProfileAssignment& ProfileAssignment = WetPartData.Profiles[ProfileIndex];
#if WITH_EDITOR
            if (ProfileAssignment.SourceProfile.IsValid())
            {
                UObject* SourceObject = ProfileAssignment.SourceProfile.ResolveObject();
                if (SourceObject == nullptr)
                {
                    SourceObject = ProfileAssignment.SourceProfile.TryLoad();
                }

                if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(SourceObject))
                {
                    Parameters = SourceProfile->GetParameters();
                    bHasParameters = true;
                }
            }
#endif
            if (!bHasParameters)
            {
                Parameters = Derived.Inline.ResolvedWetnessProfileParameters.IsValidIndex(ProfileIndex)
                    ? Derived.Inline.ResolvedWetnessProfileParameters[ProfileIndex]
                    : ProfileAssignment.Parameters;
                bHasParameters = true;
            }
        }
        else if (Derived.Inline.ResolvedWetnessProfileParameters.IsValidIndex(ProfileIndex))
        {
            Parameters = Derived.Inline.ResolvedWetnessProfileParameters[ProfileIndex];
            bHasParameters = true;
        }

        if (bHasParameters && Parameters.SupportsSurfaceWater())
        {
            return true;
        }
    }

    return false;
}

bool UWetClothingAsset::UsesSurfaceWater() const
{
    return Authored.PartData.EditableWetPartData.MaterialSlots.ContainsByPredicate(
        [this](const FWetClothingAuthoredMaterialSlot& Slot)
        {
            return DoesMaterialSlotUseSurfaceWater(Slot.MaterialSlotIndex);
        });
}

bool UWetClothingAsset::HasWrinkleBakeContent() const
{
    for (const FWetWrinklePatchPlacement& Patch : Authored.WrinkleData.EditablePatches)
    {
        if (!Patch.bEnabled && !Authored.WrinkleData.BakeSettings.bIncludeDisabledPatches)
        {
            continue;
        }
        if (Patch.MaterialSlotIndex != INDEX_NONE && IsMaterialSlotWettable(Patch.MaterialSlotIndex))
        {
            return true;
        }
    }

    for (const FWetProceduralRidgeStroke& Stroke : Authored.WrinkleData.EditableProceduralRidgeStrokes)
    {
        if (!Stroke.bEnabled && !Authored.WrinkleData.BakeSettings.bIncludeDisabledPatches)
        {
            continue;
        }
        if (Stroke.MaterialSlotIndex != INDEX_NONE &&
            Stroke.Points.Num() >= 2 &&
            IsMaterialSlotWettable(Stroke.MaterialSlotIndex))
        {
            return true;
        }
    }
    return false;
}

bool UWetClothingAsset::HasTransparencyBakeContent() const
{
    if (!HasAnyWettableMaterialSlot())
    {
        return false;
    }

    return Authored.TransparencyData.TransparencyLayers.ContainsByPredicate(
        [this](const FWetClothingTransparencyLayerData& Layer)
        {
            return Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots &&
                   Layer.TargetSurface.OuterMaterialSlotIndex != INDEX_NONE &&
                   IsMaterialSlotWettable(Layer.TargetSurface.OuterMaterialSlotIndex);
        });
}

const FDWCDataUVLODMetadata* UWetClothingAsset::FindDataUVMetadataForLOD(const int32 LODIndex) const
{
    return Derived.Inline.DataUVMetadata.FindByPredicate(
        [LODIndex](const FDWCDataUVLODMetadata& Data)
        {
            return Data.LODIndex == LODIndex;
        });
}

bool UWetClothingAsset::HasValidDataUVForLOD(const int32 LODIndex) const
{
    const FDWCDataUVLODMetadata* DataUVMetadata = FindDataUVMetadataForLOD(LODIndex);
    if (DataUVMetadata == nullptr ||
        !DataUVMetadata->bIsValid ||
        DataUVMetadata->UVChannelIndex != Metadata.DWCDataUVChannelIndex ||
        DataUVMetadata->GeneratorVersion != DWCGeneratedDataVersion::DataUV)
    {
        return false;
    }

    USkeletalMesh* RuntimeMesh = GetDWCSkeletalMesh();
    FDWCDataUVBufferView View;
    if (!View.Initialize(RuntimeMesh, LODIndex, Metadata.DWCDataUVChannelIndex) ||
        DataUVMetadata->RenderVertexCount != View.NumVertices())
    {
        return false;
    }

#if WITH_EDITOR
    const FString CurrentOriginalUVSignature = BuildMeshContentSignature(
        RuntimeMesh,
        LODIndex,
        Metadata.OriginalUVChannelIndex);
    const FString CurrentDataUVSignature = BuildMeshContentSignature(
        RuntimeMesh,
        LODIndex,
        Metadata.DWCDataUVChannelIndex);
    if (CurrentOriginalUVSignature.IsEmpty() ||
        CurrentDataUVSignature.IsEmpty() ||
        DataUVMetadata->MeshInputSignature != CurrentOriginalUVSignature ||
        DataUVMetadata->DataUVOutputSignature != CurrentDataUVSignature)
    {
        return false;
    }
#endif

    return true;
}

void UWetClothingAsset::Serialize(FArchive& Ar)
{
    const bool bSerializePersistentRuntimeBulkData = Ar.IsPersistent();
    if (Ar.IsSaving() && bSerializePersistentRuntimeBulkData)
    {
        StoreRuntimeDataToBulkData();
        Metadata.AssetDataVersion = CurrentAssetDataVersion;
    }

    Super::Serialize(Ar);

    if (!bSerializePersistentRuntimeBulkData)
    {
        return;
    }

    bool bHasSerializedRuntimeBulkData = RuntimeBulkData.GetBulkDataSize() > 0;
    // Version 4 already appended this custom BulkData block. Older layouts are not
    // supported semantically, but their serialized bytes still must be consumed.
    const bool bLoadSerializedRuntimeBulkData =
        Ar.IsLoadingFromCookedPackage() ||
        Metadata.AssetDataVersion >= FirstAssetVersionWithSerializedRuntimeBulkData;
    if (Ar.IsSaving() || bLoadSerializedRuntimeBulkData)
    {
        Ar << bHasSerializedRuntimeBulkData;
        if (bHasSerializedRuntimeBulkData)
        {
            if (Ar.IsSaving())
            {
                RuntimeBulkData.SetBulkDataFlags(
                    BULKDATA_Force_NOT_InlinePayload |
                    BULKDATA_LazyLoadable);
                RuntimeBulkData.ClearBulkDataFlags(BULKDATA_ForceInlinePayload);
            }

            RuntimeBulkData.Serialize(Ar, this, INDEX_NONE, false, EFileRegionType::None);

            if (Ar.IsLoadingFromCookedPackage())
            {
                Metadata.AssetDataVersion = CurrentAssetDataVersion;
            }
        }
    }

    if (Ar.IsLoading())
    {
        bRuntimeBulkDataLoaded = !bHasSerializedRuntimeBulkData || RuntimeBulkData.GetBulkDataSize() == 0;
        bRuntimeBulkDataLoadFailed = false;
        bRuntimeBulkDataDirty = false;
    }
}

void UWetClothingAsset::PostLoad()
{
    const double PostLoadStartTime = FPlatformTime::Seconds();
    Super::PostLoad();



    FWetClothingEditableWetPartData& EditableWetPartData = Authored.PartData.EditableWetPartData;
    EditableWetPartData.EnsureDefaultProfile();
    for (FWetClothingAuthoredMaterialSlot& Slot : EditableWetPartData.MaterialSlots)
    {
        for (FWetClothingWetPartEntry& Entry : Slot.WetPartEntries)
        {
            if (!EditableWetPartData.Profiles.IsValidIndex(Entry.ProfileIndex))
            {
                Entry.ProfileIndex = 0;
            }
        }
    }

    Metadata.SetupSettings.NormalizeMapResolutions();
    Metadata.SetupSettings.SimulationLODIndex = RuntimeSimulationLODIndex;
    Metadata.SimulationLODIndex = RuntimeSimulationLODIndex;
    if (Metadata.DWCSkeletalMesh != nullptr && Metadata.DWCSkeletalMesh == Metadata.SourceSkeletalMesh)
    {
        // Direct source-mesh modification is no longer supported. Old assets must rebuild a dedicated prepared mesh.
        Metadata.DWCSkeletalMesh = nullptr;
        Metadata.DWCDataUVChannelIndex = INDEX_NONE;
    }
    bRuntimeBulkDataLoaded = RuntimeBulkData.GetBulkDataSize() == 0;
    bRuntimeBulkDataLoadFailed = false;
    bRuntimeBulkDataDirty = false;
#if WITH_EDITORONLY_DATA
    // Seal any legacy asset that already owns persistent UV/topology payloads before schema validation.
    // Unsupported layouts remain inspectable but can never be regenerated in place.
    const bool bHadPersistentDataUVLayout =
        Metadata.DWCSkeletalMesh != nullptr &&
        Metadata.DWCDataUVChannelIndex != INDEX_NONE &&
        !Derived.Inline.DataUVMetadata.IsEmpty() &&
        !Derived.Inline.OriginalUVTopologies.IsEmpty();
    Metadata.bDataUVLayoutSealed = Metadata.bDataUVLayoutSealed || bHadPersistentDataUVLayout;

    if (Metadata.AssetDataVersion != CurrentAssetDataVersion)
    {
        // Unsupported asset schemas are invalidated and must be rebuilt with the current data contract.
        Derived.Inline.DataUVMetadata.Reset();
        Derived.Inline.BakeState.GeneratedDataUV = EDWCBakeStatus::Required;
        Derived.Inline.BakeState.OriginalUVTopology = EDWCBakeStatus::Required;
        Derived.Inline.BakeState.CPURuntimeData = Metadata.SetupSettings.bBuildCPUVertexSimulationData
                                       ? EDWCBakeStatus::Required
                                       : EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPURuntimeData = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
                                       ? EDWCBakeStatus::Required
                                       : EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPUMaps = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
                                ? EDWCBakeStatus::Required
                                : EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GeneratedOutputMask = 0;
        Derived.Inline.BakeState.SavedOutputMask = 0;
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("WetClothingAsset: '%s' uses unsupported asset schema version %d (current: %d). Create a new WCA for this mesh, then rebuild its runtime outputs."),
            *GetNameSafe(this),
            Metadata.AssetDataVersion,
            CurrentAssetDataVersion);
    }
    else if (Derived.Inline.DataUVMetadata.IsEmpty())
    {
        Derived.Inline.BakeState.GeneratedDataUV = EDWCBakeStatus::Required;
    }
    else
    {
        const bool bMetadataMatchesReferences = DoesSavedDataUVLODRangeHavePayload(
            Metadata.SetupSettings,
            Derived.Inline.DataUVMetadata,
            [this](const int32 LODIndex)
            {
                const FDWCDataUVLODMetadata* DataUVMetadata = FindDataUVMetadataForLOD(LODIndex);
                return DataUVMetadata != nullptr &&
                       DataUVMetadata->bIsValid &&
                       Metadata.DWCSkeletalMesh != nullptr &&
                       Metadata.DWCDataUVChannelIndex != INDEX_NONE &&
                       DataUVMetadata->UVChannelIndex == Metadata.DWCDataUVChannelIndex &&
                       DataUVMetadata->RenderVertexCount > 0 &&
                       !DataUVMetadata->MeshInputSignature.IsEmpty() &&
                       !DataUVMetadata->DataUVOutputSignature.IsEmpty();
            });
        Derived.Inline.BakeState.GeneratedDataUV = bMetadataMatchesReferences
                                         ? EDWCBakeStatus::Valid
                                         : EDWCBakeStatus::OutOfDate;
    }

#endif
#if WITH_EDITOR
    PendingRuntimeSaveOutputMask = 0;
    EditorSavePendingOutputMaskSnapshot = 0;
    EditorSaveSavedOutputMaskSnapshot = 0;
    bRuntimeDataEditorSaveAttemptActive = false;
#endif
    UE_LOG(
        LogDWC,
        Display,
        TEXT("WetClothingAsset PostLoad: '%s' completed in %.2f ms (runtime bulk loaded=%s, bulk size=%.2f MB)."),
        *GetNameSafe(this),
        (FPlatformTime::Seconds() - PostLoadStartTime) * 1000.0,
        bRuntimeBulkDataLoaded ? TEXT("true") : TEXT("false"),
        static_cast<double>(RuntimeBulkData.GetBulkDataSize()) / (1024.0 * 1024.0));
}

#if WITH_EDITOR
void UWetClothingAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UWetClothingAsset::EnsureRuntimeBulkDataLoaded() const
{
    const bool bHasBulkPayload = RuntimeBulkData.GetBulkDataSize() > 0;
    const bool bMissingCPUPrecomputedPayload =
        Derived.Bulk.NeighborRuntimeData.bIsValid &&
        Derived.Bulk.NeighborRuntimeData.VertexCount > 0 &&
        (Derived.Bulk.NeighborRuntimeData.Vertices.Num() == 0 ||
         Derived.Bulk.NeighborRuntimeData.NeighborGraph.Num() == 0);
    const bool bMissingGPURuntimePayload = Derived.Bulk.GPURuntimeData.ContainsByPredicate(
        [](const FDWCGPULODBakeData& Data)
        {
            return Data.bRuntimeDataValid &&
                   Data.TriangleCount > 0 &&
                   (Data.Profiles.Num() == 0 ||
                    Data.Triangles.Num() == 0 ||
                    Data.VertexIncidentTriangles.Num() == 0);
        });
    const bool bMissingGPUMapPayload = Derived.Bulk.GPURuntimeData.ContainsByPredicate(
        [](const FDWCGPULODBakeData& Data)
        {
            return Data.bMapDataValid &&
                   Data.MaterialSlotMapCount > 0 &&
                   Data.MaterialSlots.Num() == 0;
        });
    const bool bNeedsBulkPayloadLoad =
        bHasBulkPayload &&
        (bMissingCPUPrecomputedPayload || bMissingGPURuntimePayload || bMissingGPUMapPayload);

    if (bRuntimeBulkDataLoaded && (!bNeedsBulkPayloadLoad || bRuntimeBulkDataLoadFailed))
    {
        return;
    }

    UWetClothingAsset* MutableThis = const_cast<UWetClothingAsset*>(this);
    if (bNeedsBulkPayloadLoad)
    {
        MutableThis->bRuntimeBulkDataLoaded = false;
    }
    MutableThis->LoadRuntimeBulkData();
}

bool UWetClothingAsset::LoadRuntimeBulkData(const bool bForceProgressDialog) const
{
    if (bRuntimeBulkDataLoaded)
    {
        return !bRuntimeBulkDataLoadFailed;
    }

    if (RuntimeBulkData.GetBulkDataSize() <= 0)
    {
        bRuntimeBulkDataLoaded = true;
        bRuntimeBulkDataLoadFailed = false;
        return true;
    }

    const int64 BulkSize = RuntimeBulkData.GetBulkDataSize();
    if (BulkSize > MAX_int32)
    {
        bRuntimeBulkDataLoaded = true;
        bRuntimeBulkDataLoadFailed = true;
        UE_LOG(
            LogTemp,
            Error,
            TEXT("WetClothingAsset: Runtime bulk payload on '%s' is too large to load (%lld bytes)."),
            *GetNameSafe(this),
            BulkSize);
        return false;
    }

    TUniquePtr<FScopedSlowTask> SlowTask;
    if (GIsEditor && (bForceProgressDialog || BulkSize >= RuntimeBulkProgressThresholdBytes))
    {
        SlowTask = MakeUnique<FScopedSlowTask>(
            DWCRuntimeBulkCopyTotalProgress + DWCRuntimeBulkSerializationProgress + 0.25f,
            FText::FromString(FString::Printf(
                TEXT("Lazy-loading %.1f MB of WCA runtime payload..."),
                static_cast<double>(BulkSize) / (1024.0 * 1024.0))));
        SlowTask->MakeDialog(false);
        SlowTask->EnterProgressFrame(
            DWCRuntimeBulkLockProgress,
            NSLOCTEXT("WetClothingAsset", "LockRuntimeBulkBytes", "Opening WCA runtime bulk payload from the asset package..."));
    }

    const void* BulkBytes = RuntimeBulkData.LockReadOnly();
    if (BulkBytes == nullptr)
    {
        bRuntimeBulkDataLoaded = true;
        bRuntimeBulkDataLoadFailed = true;
        UE_LOG(
            LogTemp,
            Error,
            TEXT("WetClothingAsset: Failed to lock runtime bulk payload on '%s' for reading (%lld bytes)."),
            *GetNameSafe(this),
            BulkSize);
        return false;
    }

    TArray<uint8> Bytes;
    const int32 ByteCount = IntCastChecked<int32>(RuntimeBulkData.GetBulkDataSize());
    if (SlowTask.IsValid())
    {
        SlowTask->EnterProgressFrame(
            DWCRuntimeBulkAllocateProgress,
            FText::FromString(FString::Printf(
                TEXT("Allocating %.1f MB for WCA runtime payload..."),
                static_cast<double>(ByteCount) / (1024.0 * 1024.0))));
    }
    Bytes.SetNumUninitialized(ByteCount);

    constexpr int32 RuntimeBulkCopyChunkBytes = 16 * 1024 * 1024;
    const int32 CopyChunkCount = FMath::Max(1, FMath::DivideAndRoundUp(ByteCount, RuntimeBulkCopyChunkBytes));
    const float CopyWorkPerChunk = DWCRuntimeBulkCopyProgress / static_cast<float>(CopyChunkCount);
    const uint8* SourceBytes = static_cast<const uint8*>(BulkBytes);
    int32 CopiedBytes = 0;
    for (int32 ChunkIndex = 0; ChunkIndex < CopyChunkCount; ++ChunkIndex)
    {
        const int32 ChunkBytes = FMath::Min(RuntimeBulkCopyChunkBytes, ByteCount - CopiedBytes);
        if (SlowTask.IsValid())
        {
            SlowTask->EnterProgressFrame(
                CopyWorkPerChunk,
                FText::FromString(FString::Printf(
                    TEXT("Copying WCA runtime payload bytes %.1f / %.1f MB..."),
                    static_cast<double>(CopiedBytes) / (1024.0 * 1024.0),
                    static_cast<double>(ByteCount) / (1024.0 * 1024.0))));
        }
        FMemory::Memcpy(Bytes.GetData() + CopiedBytes, SourceBytes + CopiedBytes, ChunkBytes);
        CopiedBytes += ChunkBytes;
    }
    RuntimeBulkData.Unlock();

    FMemoryReader Reader(Bytes, true);
    UWetClothingAsset* MutableThis = const_cast<UWetClothingAsset*>(this);
    SerializeRuntimeBulkPayload(
        Reader,
        MutableThis->Derived.Bulk.NeighborRuntimeData,
        MutableThis->Derived.Bulk.GPURuntimeData,
        RuntimeSimulationLODIndex,
        SlowTask.Get());
    if (Reader.IsError())
    {
        MutableThis->Derived.Bulk.NeighborRuntimeData.Vertices.Reset();
        MutableThis->Derived.Bulk.NeighborRuntimeData.NeighborGraph.Reset();
        MutableThis->Derived.Bulk.NeighborRuntimeData.BoneOptimizationCache.Reset();
        for (FDWCGPULODBakeData& Data : MutableThis->Derived.Bulk.GPURuntimeData)
        {
            Data.Profiles.Reset();
            Data.Triangles.Reset();
            Data.VertexIncidentTriangles.Reset();
            Data.MaterialSlots.Reset();
        }
        bRuntimeBulkDataLoaded = true;
        bRuntimeBulkDataLoadFailed = true;
        UE_LOG(
            LogTemp,
            Error,
            TEXT("WetClothingAsset: Failed to deserialize runtime bulk payload on '%s' (%d bytes). Re-save or rebuild the Wet Clothing Asset runtime data."),
            *GetNameSafe(this),
            Bytes.Num());
        return false;
    }

    if (SlowTask.IsValid())
    {
        SlowTask->EnterProgressFrame(
            0.25f,
            NSLOCTEXT("WetClothingAsset", "FinalizeRuntimeBulkLoad", "Finalizing WCA runtime data load..."));
    }

    if (RemoveNonSimulationGPULODData(MutableThis->Derived.Bulk.GPURuntimeData) > 0 && GIsEditor)
    {
        MutableThis->bRuntimeBulkDataDirty = true;
    }
    bRuntimeBulkDataLoaded = true;
    bRuntimeBulkDataLoadFailed = false;
    return true;
}

#if WITH_EDITOR
void UWetClothingAsset::ReleaseLoadedRuntimeBulkPayloadForEditor()
{
    if (bRuntimeBulkDataDirty || RuntimeBulkData.GetBulkDataSize() <= 0)
    {
        return;
    }

    Derived.Bulk.NeighborRuntimeData.Vertices.Empty();
    Derived.Bulk.NeighborRuntimeData.NeighborGraph.Empty();
    Derived.Bulk.NeighborRuntimeData.BoneOptimizationCache.Reset();

    for (FDWCGPULODBakeData& Data : Derived.Bulk.GPURuntimeData)
    {
        Data.Profiles.Empty();
        Data.Triangles.Empty();
        Data.VertexIncidentTriangles.Empty();
        Data.MaterialSlots.Empty();
    }

    bRuntimeBulkDataLoaded = false;
    bRuntimeBulkDataLoadFailed = false;
}
#endif

void UWetClothingAsset::StoreRuntimeDataToBulkData()
{
    constexpr int32 RuntimeSavedOutputMask =
        DWCBakeOutput::CPURuntimeData |
        DWCBakeOutput::GPURuntimeData |
        DWCBakeOutput::GPUMaps;
#if WITH_EDITORONLY_DATA
    int32 MetadataOutputsBeingSaved = 0;
    int32 RuntimeOutputsBeingSaved = 0;
    if (Derived.Inline.DataUVMetadata.Num() > 0)
    {
        MetadataOutputsBeingSaved |= DWCBakeOutput::GeneratedDataUV;
    }
    if (!Derived.Inline.OriginalUVTopologies.IsEmpty())
    {
        MetadataOutputsBeingSaved |= DWCBakeOutput::OriginalUVTopology;
    }
    if (HasCPURuntimeDataPayload())
    {
        RuntimeOutputsBeingSaved |= DWCBakeOutput::CPURuntimeData;
    }
    if (HasGPURuntimeDataPayload())
    {
        RuntimeOutputsBeingSaved |= DWCBakeOutput::GPURuntimeData;
    }
    if (HasGPUMapDataPayload())
    {
        RuntimeOutputsBeingSaved |= DWCBakeOutput::GPUMaps;
    }
    if (DWCBuildStatus::IsUsable(Derived.Inline.BakeState.WrinkleMaps))
    {
        MetadataOutputsBeingSaved |= DWCBakeOutput::WrinkleMaps;
    }
    if (DWCBuildStatus::IsUsable(Derived.Inline.BakeState.TransparencyMaps))
    {
        MetadataOutputsBeingSaved |= DWCBakeOutput::TransparencyMaps;
    }
    Derived.Inline.BakeState.GeneratedOutputMask |= MetadataOutputsBeingSaved | RuntimeOutputsBeingSaved;
    Derived.Inline.BakeState.SavedOutputMask |= MetadataOutputsBeingSaved;
#endif

    if (!bRuntimeBulkDataDirty)
    {
#if WITH_EDITORONLY_DATA
        Derived.Inline.BakeState.SavedOutputMask |= RuntimeOutputsBeingSaved;
#endif
        return;
    }

#if WITH_EDITORONLY_DATA
    Derived.Inline.BakeState.SavedOutputMask &= ~RuntimeSavedOutputMask;
#endif

    RemoveNonSimulationGPULODData(Derived.Bulk.GPURuntimeData);

    const bool bHasCPUData = Derived.Bulk.NeighborRuntimeData.bIsValid &&
                             (Derived.Bulk.NeighborRuntimeData.Vertices.Num() > 0 ||
                              Derived.Bulk.NeighborRuntimeData.NeighborGraph.Num() > 0 ||
                              Derived.Bulk.NeighborRuntimeData.BoneOptimizationCache.bIsValid);
    const bool bHasGPUData = Derived.Bulk.GPURuntimeData.ContainsByPredicate(
        [](const FDWCGPULODBakeData& Data)
        {
            return Data.Profiles.Num() > 0 || Data.Triangles.Num() > 0 ||
                   Data.VertexIncidentTriangles.Num() > 0 || Data.MaterialSlots.Num() > 0;
        });

    if (!bHasCPUData && !bHasGPUData)
    {
        RuntimeBulkData.RemoveBulkData();
        bRuntimeBulkDataLoaded = true;
        bRuntimeBulkDataLoadFailed = false;
        bRuntimeBulkDataDirty = false;
#if WITH_EDITOR
        PendingRuntimeSaveOutputMask &= ~RuntimeSavedOutputMask;
#endif
        return;
    }

    for (FDWCGPULODBakeData& Data : Derived.Bulk.GPURuntimeData)
    {
        if (Data.bRuntimeDataValid || Data.bMapDataValid)
        {
            Data.BulkDataVersion = FDWCGPULODBakeData::CurrentBulkDataVersion;
        }
    }

    TUniquePtr<FScopedSlowTask> SlowTask;
    if (GIsEditor)
    {
        SlowTask = MakeUnique<FScopedSlowTask>(
            DWCRuntimeBulkSerializationProgress + 1.0f + 0.25f,
            NSLOCTEXT("WetClothingAsset", "StoreRuntimePayloadToBulk", "Saving DWC runtime bulk data..."));
        SlowTask->MakeDialog(false);
    }

    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes, true);
    SerializeRuntimeBulkPayload(
        Writer,
        Derived.Bulk.NeighborRuntimeData,
        Derived.Bulk.GPURuntimeData,
        INDEX_NONE,
        SlowTask.Get());
    if (Writer.IsError() || Bytes.IsEmpty())
    {
        return;
    }

    if (SlowTask.IsValid())
    {
        SlowTask->EnterProgressFrame(
            1.0f,
            FText::FromString(FString::Printf(
                TEXT("Writing %.1f MB of WCA runtime data to the asset package..."),
                static_cast<double>(Bytes.Num()) / (1024.0 * 1024.0))));
    }

    RuntimeBulkData.RemoveBulkData();
    RuntimeBulkData.SetBulkDataFlags(
        BULKDATA_Force_NOT_InlinePayload |
        BULKDATA_LazyLoadable);
    RuntimeBulkData.ClearBulkDataFlags(BULKDATA_ForceInlinePayload);
    RuntimeBulkData.Lock(LOCK_READ_WRITE);
    void* BulkBytes = RuntimeBulkData.Realloc(Bytes.Num());
    FMemory::Memcpy(BulkBytes, Bytes.GetData(), Bytes.Num());
    RuntimeBulkData.Unlock();

    if (SlowTask.IsValid())
    {
        SlowTask->EnterProgressFrame(
            0.25f,
            NSLOCTEXT("WetClothingAsset", "FinalizeRuntimeBulkStore", "Finalizing WCA runtime data save..."));
    }

    Metadata.AssetDataVersion = CurrentAssetDataVersion;
    bRuntimeBulkDataLoaded = true;
    bRuntimeBulkDataLoadFailed = false;
    bRuntimeBulkDataDirty = false;
#if WITH_EDITORONLY_DATA
    Derived.Inline.BakeState.SavedOutputMask |= RuntimeOutputsBeingSaved;
#endif
#if WITH_EDITOR
    PendingRuntimeSaveOutputMask &= ~RuntimeSavedOutputMask;
#endif
}

bool UWetClothingAsset::HasRuntimeBulkPayload() const
{
    return RuntimeBulkData.GetBulkDataSize() > 0;
}

void UWetClothingAsset::MarkRuntimeBulkDataDirty(const int32 OutputMask)
{
#if WITH_EDITOR
    ClearMeshContentSignatureCache();
#endif
    // Callers must load the aggregate payload before mutating one of its segments.
    // Setting bRuntimeBulkDataLoaded=true without deserializing the payload creates a
    // false "loaded but empty" state and can erase valid data during the next save.
    if (!ensureMsgf(
            bRuntimeBulkDataLoaded && !bRuntimeBulkDataLoadFailed,
            TEXT("WetClothingAsset '%s' modified runtime bulk data before loading the existing payload."),
            *GetNameSafe(this)))
    {
        return;
    }
    bRuntimeBulkDataDirty = true;
    Metadata.AssetDataVersion = CurrentAssetDataVersion;
#if WITH_EDITOR
    const int32 RuntimeOutputMask = OutputMask != 0
                                        ? OutputMask
                                        : (DWCBakeOutput::CPURuntimeData |
                                           DWCBakeOutput::GPURuntimeData |
                                           DWCBakeOutput::GPUMaps);
    PendingRuntimeSaveOutputMask |= RuntimeOutputMask;
    if (bRuntimeDataEditorSaveAttemptActive)
    {
        EditorSavePendingOutputMaskSnapshot |= RuntimeOutputMask;
    }
    MarkPackageDirty();
#endif
}

void UWetClothingAsset::ClearRuntimeBulkData()
{
    RuntimeBulkData.RemoveBulkData();
    bRuntimeBulkDataLoaded = true;
    bRuntimeBulkDataLoadFailed = false;
    bRuntimeBulkDataDirty = false;
}


#if WITH_EDITOR
void UWetClothingAsset::PreSave(FObjectPreSaveContext SaveContext)
{
    Super::PreSave(SaveContext);

    if (bSkipNextPreSaveRuntimeDataRebuild)
    {
        bSkipNextPreSaveRuntimeDataRebuild = false;
        return;
    }

    FString RuntimePreparationMessage;
    if (CanPrepareRuntimeDataForEditorSave(&RuntimePreparationMessage))
    {
        if (!RebuildRuntimeDataForSave(&RuntimePreparationMessage))
        {
            UE_LOG(
                LogDWC,
                Error,
                TEXT("WetClothingAsset: Failed to prepare runtime data before saving '%s'. %s"),
                *GetNameSafe(this),
                *RuntimePreparationMessage);
        }
    }
    else if (!RuntimePreparationMessage.IsEmpty())
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("WetClothingAsset: Runtime data preparation skipped before saving '%s'. %s"),
            *GetNameSafe(this),
            *RuntimePreparationMessage);
    }
}

bool UWetClothingAsset::InitializeNewAsset(
    USkeletalMesh* InSourceMesh,
    const FDWCWetClothingAssetSetupSettings& InSettings,
    FString* OutErrorMessage)
{
    // Initialization is write-once. Reusing this API on an existing WCA would otherwise
    // provide a back door for replacing the locked Original UV and island identity.
    const bool bAlreadyInitialized =
        Metadata.SourceSkeletalMesh != nullptr ||
        Metadata.DWCSkeletalMesh != nullptr ||
        Metadata.bDataUVLayoutSealed ||
        Metadata.DWCDataUVChannelIndex != INDEX_NONE ||
        !Derived.Inline.DataUVMetadata.IsEmpty();
#if WITH_EDITORONLY_DATA
    if (bAlreadyInitialized || !Derived.Inline.OriginalUVTopologies.IsEmpty())
#else
    if (bAlreadyInitialized)
#endif
    {
        DWC::Error::SetMessage(
            OutErrorMessage,
            TEXT("This Wet Clothing Asset is already initialized. Create a new WCA instead of replacing its Original UV or DWC UV Channel layout."));
        return false;
    }

    if (InSourceMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No source skeletal mesh is assigned."));
        return false;
    }

    FDWCWetClothingAssetSetupSettings NormalizedSettings = InSettings;
    NormalizedSettings.NormalizeMapResolutions();
    ClampSetupLODRangeToMesh(InSourceMesh, NormalizedSettings);
    if (!ValidateSetupUVChannels(InSourceMesh, NormalizedSettings, OutErrorMessage))
    {
        return false;
    }

    Metadata.SourceSkeletalMesh = InSourceMesh;
    Metadata.SetupSettings = NormalizedSettings;
    // The source mesh is immutable input. A dedicated prepared mesh is created by the DWC UV Channel build service.
    Metadata.DWCSkeletalMesh = nullptr;
    Authored.WrinkleData.BakeSettings.DefaultResolution = Metadata.SetupSettings.GetWrinkleMapResolution();
    Authored.TransparencyData.TransparencyBakeResolution = Metadata.SetupSettings.GetTransparencyMapResolution();
    Metadata.OriginalUVChannelIndex = Metadata.SetupSettings.OriginalUVChannelIndex;
    Metadata.SetupSettings.SimulationLODIndex = RuntimeSimulationLODIndex;
    Metadata.SimulationLODIndex = RuntimeSimulationLODIndex;
    Metadata.DWCDataUVChannelIndex = INDEX_NONE;
    Metadata.bDataUVLayoutSealed = false;
    Derived.Inline.DataUVMetadata.Reset();
    Metadata.AssetDataVersion = CurrentAssetDataVersion;
    Derived.Inline.SourceMeshSignature = BuildMeshContentSignature(InSourceMesh, GetSimulationLODIndex(), Metadata.OriginalUVChannelIndex);

    FWetClothingEditableWetPartData& EditableWetPartData = Authored.PartData.EditableWetPartData;
    EditableWetPartData.MaterialSlots.Reset();
    EditableWetPartData.Profiles.Reset();
    EditableWetPartData.EnsureDefaultProfile();
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < InSourceMesh->GetMaterials().Num(); ++MaterialSlotIndex)
    {
        FWetClothingAuthoredMaterialSlot& Slot = EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
        Slot.MaterialSlotIndex = MaterialSlotIndex;
        Slot.bIsWettableSlot = false;

        Slot.SurfaceWater.SurfaceWaterNormalUVChannel = Metadata.OriginalUVChannelIndex;

        FWetClothingWetPartEntry& DefaultPart = Slot.WetPartEntries.AddDefaulted_GetRef();
        DefaultPart.WetPartID = 0;
        DefaultPart.DisplayName = TEXT("Part Default");
        DefaultPart.Color = FLinearColor::White;
        DefaultPart.bViewEnabled = true;
        DefaultPart.ProfileIndex = 0;
    }

    RefreshBakeState(false);
    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}

bool UWetClothingAsset::ApplySetupSettings(
    const FDWCWetClothingAssetSetupSettings& InSettings,
    FString* OutChangeSummary)
{
    FDWCWetClothingAssetSetupSettings NewSettings = InSettings;
    NewSettings.NormalizeMapResolutions();
    ClampSetupLODRangeToMesh(GetSourceSkeletalMesh(), NewSettings);

    // Original UV defines persistent WCA island identity. It is selected only during asset creation
    // and is immutable afterwards, including for callers that bypass the Asset Setup dialog.
    if (NewSettings.OriginalUVChannelIndex != Metadata.OriginalUVChannelIndex)
    {
        DWC::Error::SetMessage(
            OutChangeSummary,
            FString::Printf(
                TEXT("Original UV is locked to UV%d for this Wet Clothing Asset. Create a new WCA to use a different Original UV channel."),
                Metadata.OriginalUVChannelIndex));
        return false;
    }
    NewSettings.OriginalUVChannelIndex = Metadata.OriginalUVChannelIndex;

    if (!ValidateSetupUVChannels(GetSourceSkeletalMesh(), NewSettings, OutChangeSummary))
    {
        return false;
    }

    const FDWCWetClothingAssetSetupSettings PreviousSettings = Metadata.SetupSettings;
    const bool bDataUVTargetChanged =
        PreviousSettings.PreferredDWCDataUVChannelIndex != NewSettings.PreferredDWCDataUVChannelIndex;
    const bool bLODRangeChanged =
        PreviousSettings.FirstGeneratedLODIndex != NewSettings.FirstGeneratedLODIndex ||
        PreviousSettings.LastGeneratedLODIndex != NewSettings.LastGeneratedLODIndex;

    // A sealed WCA may select only LODs whose UV layout/topology was created during the initial commit.
    // Expanding into an ungenerated LOD would require rebuilding island information, which is forbidden.
    if (HasLockedDataUVLayout() && bLODRangeChanged)
    {
        for (int32 LODIndex = NewSettings.FirstGeneratedLODIndex;
             LODIndex <= NewSettings.LastGeneratedLODIndex;
             ++LODIndex)
        {
            if (FindDataUVMetadataForLOD(LODIndex) == nullptr || FindOriginalUVTopologyForLOD(LODIndex) == nullptr)
            {
                DWC::Error::SetMessage(
                    OutChangeSummary,
                    FString::Printf(
                        TEXT("LOD%d has no sealed DWC UV Channel/island payload. Create a new WCA to generate a different LOD mapping range."),
                        LODIndex));
                return false;
            }
        }
    }

    const bool bCPUSimulationSettingChanged =
        PreviousSettings.bBuildCPUVertexSimulationData != NewSettings.bBuildCPUVertexSimulationData;
    const bool bGPUSimulationSettingChanged =
        PreviousSettings.bBuildGPUWetnessMapSimulationData != NewSettings.bBuildGPUWetnessMapSimulationData;
    const bool bGPUResolutionChanged =
        PreviousSettings.GetGPUSimulationMapResolution() != NewSettings.GetGPUSimulationMapResolution();
    const bool bWrinkleResolutionChanged =
        PreviousSettings.GetWrinkleMapResolution() != NewSettings.GetWrinkleMapResolution();
    const bool bTransparencyResolutionChanged =
        PreviousSettings.GetTransparencyMapResolution() != NewSettings.GetTransparencyMapResolution();
    const bool bSurfaceWaterRTResolutionChanged =
        PreviousSettings.GetSurfaceWaterRTResolution() != NewSettings.GetSurfaceWaterRTResolution();

    Metadata.SetupSettings = NewSettings;
    Metadata.SetupSettings.OriginalUVChannelIndex = Metadata.OriginalUVChannelIndex;
    Metadata.SetupSettings.SimulationLODIndex = RuntimeSimulationLODIndex;
    Metadata.SimulationLODIndex = RuntimeSimulationLODIndex;
    Authored.WrinkleData.BakeSettings.DefaultResolution = Metadata.SetupSettings.GetWrinkleMapResolution();
    Authored.TransparencyData.TransparencyBakeResolution = Metadata.SetupSettings.GetTransparencyMapResolution();

    if (bDataUVTargetChanged)
    {
        // The packed UV layout and Original-UV island records remain sealed. Only the destination
        // channel is stale until the editor copies the existing values verbatim to the new channel.
        Derived.Inline.BakeState.GeneratedDataUV = Derived.Inline.DataUVMetadata.IsEmpty()
            ? EDWCBakeStatus::Required
            : EDWCBakeStatus::OutOfDate;
    }

    const bool bSimulationStructureChanged =
        bDataUVTargetChanged ||
        bLODRangeChanged ||
        bCPUSimulationSettingChanged ||
        bGPUSimulationSettingChanged;

    if (bSimulationStructureChanged)
    {
        MarkSimulationBakeOutOfDate();
    }
    else if (bGPUResolutionChanged || (bSurfaceWaterRTResolutionChanged && UsesSurfaceWater()))
    {
        // Resolution-only changes do not invalidate CPU/GPU runtime topology.
        // They invalidate only the generated GPU simulation maps that own those RT lookups.
        Derived.Inline.BakeState.GPUMaps = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
            ? (HasGPUMapDataPayload() ? EDWCBakeStatus::OutOfDate : EDWCBakeStatus::Required)
            : EDWCBakeStatus::Disabled;
    }

    if (bDataUVTargetChanged)
    {
        MarkVisualBakeOutOfDate();
    }
    else
    {
        if (bWrinkleResolutionChanged)
        {
            Derived.Inline.BakeState.WrinkleMaps = HasWrinkleBakeContent()
                ? (HasSavedBakeOutput(DWCBakeOutput::WrinkleMaps)
                    ? EDWCBakeStatus::OutOfDate
                    : EDWCBakeStatus::Required)
                : EDWCBakeStatus::Disabled;
        }
        if (bTransparencyResolutionChanged)
        {
            Derived.Inline.BakeState.TransparencyMaps = HasTransparencyBakeContent()
                ? (HasSavedBakeOutput(DWCBakeOutput::TransparencyMaps)
                    ? EDWCBakeStatus::OutOfDate
                    : EDWCBakeStatus::Required)
                : EDWCBakeStatus::Disabled;
        }
    }

    TArray<FString> Changes;
    if (bDataUVTargetChanged)
    {
        Changes.Add(FString::Printf(
            TEXT("DWC UV Channel changed: UV%d -> UV%d. The existing packed layout will be copied without rebuilding island topology."),
            Metadata.DWCDataUVChannelIndex,
            Metadata.SetupSettings.PreferredDWCDataUVChannelIndex));
    }
    if (bLODRangeChanged)
    {
        Changes.Add(FString::Printf(
            TEXT("LOD mapping range changed: LOD%d-LOD%d -> LOD%d-LOD%d. Runtime mapping rebuild is required."),
            PreviousSettings.FirstGeneratedLODIndex,
            PreviousSettings.LastGeneratedLODIndex,
            Metadata.SetupSettings.FirstGeneratedLODIndex,
            Metadata.SetupSettings.LastGeneratedLODIndex));
    }
    if (bCPUSimulationSettingChanged)
    {
        Changes.Add(FString::Printf(
            TEXT("CPU vertex simulation data: %s -> %s."),
            PreviousSettings.bBuildCPUVertexSimulationData ? TEXT("Enabled") : TEXT("Disabled"),
            Metadata.SetupSettings.bBuildCPUVertexSimulationData ? TEXT("Enabled") : TEXT("Disabled")));
    }
    if (bGPUSimulationSettingChanged)
    {
        Changes.Add(FString::Printf(
            TEXT("GPU wetness-map simulation data: %s -> %s."),
            PreviousSettings.bBuildGPUWetnessMapSimulationData ? TEXT("Enabled") : TEXT("Disabled"),
            Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData ? TEXT("Enabled") : TEXT("Disabled")));
    }
    if (bGPUResolutionChanged)
    {
        Changes.Add(FString::Printf(TEXT("GPU Simulation Map resolution: %d -> %d."), PreviousSettings.GetGPUSimulationMapResolution(), Metadata.SetupSettings.GetGPUSimulationMapResolution()));
    }
    if (bWrinkleResolutionChanged)
    {
        Changes.Add(FString::Printf(TEXT("Wrinkle Map resolution: %d -> %d."), PreviousSettings.GetWrinkleMapResolution(), Metadata.SetupSettings.GetWrinkleMapResolution()));
    }
    if (bTransparencyResolutionChanged)
    {
        Changes.Add(FString::Printf(TEXT("Transparency Map resolution: %d -> %d."), PreviousSettings.GetTransparencyMapResolution(), Metadata.SetupSettings.GetTransparencyMapResolution()));
    }
    if (bSurfaceWaterRTResolutionChanged)
    {
        Changes.Add(FString::Printf(TEXT("Surface Water RT resolution: %d -> %d."), PreviousSettings.GetSurfaceWaterRTResolution(), Metadata.SetupSettings.GetSurfaceWaterRTResolution()));
    }

    DWC::Error::SetMessage(
        OutChangeSummary,
        Changes.IsEmpty()
            ? TEXT("Wet Clothing setup settings are unchanged.")
            : FString::Join(Changes, TEXT("\n")));
    return true;
}

bool UWetClothingAsset::HasLockedDataUVLayout() const
{
#if WITH_EDITORONLY_DATA
    return Metadata.bDataUVLayoutSealed ||
           (Metadata.DWCSkeletalMesh != nullptr &&
            Metadata.DWCDataUVChannelIndex != INDEX_NONE &&
            !Derived.Inline.DataUVMetadata.IsEmpty() &&
            !Derived.Inline.OriginalUVTopologies.IsEmpty());
#else
    return false;
#endif
}

bool UWetClothingAsset::CommitInitialDataUVLayout(
    USkeletalMesh* InRuntimeMesh,
    const int32 InDWCDataUVChannelIndex,
    TArray<FDWCDataUVLODMetadata>&& InMetadata,
    TArray<FDWCEditorUVTopologyData>&& InTopologies,
    FString* OutErrorMessage)
{
#if WITH_EDITORONLY_DATA
    const bool bHasAnyExistingLayoutPayload =
        Metadata.bDataUVLayoutSealed ||
        Metadata.DWCSkeletalMesh != nullptr ||
        Metadata.DWCDataUVChannelIndex != INDEX_NONE ||
        !Derived.Inline.DataUVMetadata.IsEmpty() ||
        !Derived.Inline.OriginalUVTopologies.IsEmpty();
    if (bHasAnyExistingLayoutPayload)
    {
        DWC::Error::SetMessage(
            OutErrorMessage,
            TEXT("DWC UV Channel and Original UV island topology are write-once. Create a new WCA instead of rebuilding this layout."));
        return false;
    }
    if (InRuntimeMesh == nullptr || InDWCDataUVChannelIndex < 0 || InDWCDataUVChannelIndex >= 8)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The initial DWC UV Channel target is invalid."));
        return false;
    }
    if (InDWCDataUVChannelIndex == Metadata.OriginalUVChannelIndex)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC UV Channel cannot use the locked Original UV channel."));
        return false;
    }
    if (InMetadata.IsEmpty() || InTopologies.IsEmpty())
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The initial DWC UV Channel commit requires both metadata and Original UV topology payloads."));
        return false;
    }

    Metadata.DWCSkeletalMesh = InRuntimeMesh;
    Metadata.DWCDataUVChannelIndex = InDWCDataUVChannelIndex;
    Metadata.SetupSettings.PreferredDWCDataUVChannelIndex = InDWCDataUVChannelIndex;
    Derived.Inline.DataUVMetadata = MoveTemp(InMetadata);
    Derived.Inline.OriginalUVTopologies = MoveTemp(InTopologies);
    Metadata.bDataUVLayoutSealed = true;

    Derived.Inline.BakeState.GeneratedDataUV = EDWCBakeStatus::Valid;
    Derived.Inline.BakeState.OriginalUVTopology = EDWCBakeStatus::Valid;
    MarkBakeOutputGenerated(DWCBakeOutput::GeneratedDataUV | DWCBakeOutput::OriginalUVTopology);
    MarkSimulationBakeOutOfDate();
    Derived.Inline.BakeState.LastFailure.Reset();
    MarkPackageDirty();
    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
#else
    DWC::Error::SetMessage(OutErrorMessage, TEXT("Initial DWC UV Channel commit is editor-only."));
    return false;
#endif
}

bool UWetClothingAsset::ReplaceDataUVLayout(
    USkeletalMesh* InRuntimeMesh,
    const int32 InDWCDataUVChannelIndex,
    TArray<FDWCDataUVLODMetadata>&& InMetadata,
    TArray<FDWCEditorUVTopologyData>&& InTopologies,
    FString* OutErrorMessage)
{
#if WITH_EDITORONLY_DATA
    if (!HasLockedDataUVLayout())
    {
        DWC::Error::SetMessage(
            OutErrorMessage,
            TEXT("The WCA does not contain a DWC UV Channel layout to rebuild."));
        return false;
    }
    if (InRuntimeMesh == nullptr || InDWCDataUVChannelIndex < 0 || InDWCDataUVChannelIndex >= 8)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The rebuilt DWC UV Channel target is invalid."));
        return false;
    }
    if (InDWCDataUVChannelIndex == Metadata.OriginalUVChannelIndex)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC UV Channel cannot use the locked Original UV channel."));
        return false;
    }
    if (InMetadata.IsEmpty() || InTopologies.IsEmpty())
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The rebuilt DWC UV Channel commit requires both metadata and Original UV topology payloads."));
        return false;
    }

    Metadata.DWCSkeletalMesh = InRuntimeMesh;
    Metadata.DWCDataUVChannelIndex = InDWCDataUVChannelIndex;
    Metadata.SetupSettings.PreferredDWCDataUVChannelIndex = InDWCDataUVChannelIndex;
    Derived.Inline.DataUVMetadata = MoveTemp(InMetadata);
    Derived.Inline.OriginalUVTopologies = MoveTemp(InTopologies);
    Metadata.bDataUVLayoutSealed = true;

    Derived.Inline.BakeState.GeneratedDataUV = EDWCBakeStatus::Valid;
    Derived.Inline.BakeState.OriginalUVTopology = EDWCBakeStatus::Valid;
    MarkBakeOutputGenerated(DWCBakeOutput::GeneratedDataUV | DWCBakeOutput::OriginalUVTopology);
    MarkSimulationBakeOutOfDate();
    MarkVisualBakeOutOfDate();
    Derived.Inline.BakeState.LastFailure.Reset();
    MarkPackageDirty();
    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
#else
    DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC UV Channel replacement is editor-only."));
    return false;
#endif
}

bool UWetClothingAsset::CommitDataUVChannelRelocation(
    const int32 InDWCDataUVChannelIndex,
    FString* OutErrorMessage)
{
#if WITH_EDITORONLY_DATA
    if (!HasLockedDataUVLayout())
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The WCA does not contain a sealed DWC UV Channel layout to relocate."));
        return false;
    }
    if (InDWCDataUVChannelIndex < 0 || InDWCDataUVChannelIndex >= 8)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The destination DWC UV Channel is outside the supported UV0-UV7 range."));
        return false;
    }
    if (InDWCDataUVChannelIndex == Metadata.OriginalUVChannelIndex)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC UV Channel cannot use the locked Original UV channel."));
        return false;
    }

    TArray<FString> NewOutputSignatures;
    NewOutputSignatures.Reserve(Derived.Inline.DataUVMetadata.Num());
    for (const FDWCDataUVLODMetadata& LODMetadata : Derived.Inline.DataUVMetadata)
    {
        const FString Signature = BuildMeshContentSignature(
            Metadata.DWCSkeletalMesh,
            LODMetadata.LODIndex,
            InDWCDataUVChannelIndex);
        if (Signature.IsEmpty())
        {
            DWC::Error::SetMessage(
                OutErrorMessage,
                FString::Printf(TEXT("Could not validate relocated DWC UV Channel on LOD%d."), LODMetadata.LODIndex));
            return false;
        }
        NewOutputSignatures.Add(Signature);
    }

    Metadata.DWCDataUVChannelIndex = InDWCDataUVChannelIndex;
    Metadata.SetupSettings.PreferredDWCDataUVChannelIndex = InDWCDataUVChannelIndex;
    Metadata.bDataUVLayoutSealed = true;
    for (int32 MetadataIndex = 0; MetadataIndex < Derived.Inline.DataUVMetadata.Num(); ++MetadataIndex)
    {
        FDWCDataUVLODMetadata& LODMetadata = Derived.Inline.DataUVMetadata[MetadataIndex];
        LODMetadata.UVChannelIndex = InDWCDataUVChannelIndex;
        LODMetadata.DataUVOutputSignature = NewOutputSignatures[MetadataIndex];
    }

    Derived.Inline.BakeState.GeneratedDataUV = EDWCBakeStatus::Valid;
    MarkBakeOutputGenerated(DWCBakeOutput::GeneratedDataUV);
    MarkSimulationBakeOutOfDate();
    MarkVisualBakeOutOfDate();
    Derived.Inline.BakeState.LastFailure.Reset();
    MarkPackageDirty();
    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
#else
    DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC UV Channel relocation is editor-only."));
    return false;
#endif
}

void UWetClothingAsset::MarkGeneratedDataUVOutOfDate()
{
    Derived.Inline.BakeState.GeneratedDataUV = EDWCBakeStatus::OutOfDate;
    MarkSimulationBakeOutOfDate();
}

void UWetClothingAsset::MarkSimulationBakeOutOfDate()
{
    ClearMeshContentSignatureCache();
    if (!HasAnyWettableMaterialSlot())
    {
        Derived.Inline.BakeState.CPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Disabled;
        return;
    }

    Derived.Inline.BakeState.CPURuntimeData = Metadata.SetupSettings.bBuildCPUVertexSimulationData
                                    ? (HasCPURuntimeDataPayload()
                                           ? EDWCBakeStatus::OutOfDate
                                           : EDWCBakeStatus::Required)
                                    : EDWCBakeStatus::Disabled;
    Derived.Inline.BakeState.GPURuntimeData = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
                                    ? (HasGPURuntimeDataPayload()
                                           ? EDWCBakeStatus::OutOfDate
                                           : EDWCBakeStatus::Required)
                                    : EDWCBakeStatus::Disabled;
    Derived.Inline.BakeState.GPUMaps = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
                            ? (HasGPUMapDataPayload()
                                   ? EDWCBakeStatus::OutOfDate
                                   : EDWCBakeStatus::Required)
                            : EDWCBakeStatus::Disabled;
}

void UWetClothingAsset::MarkWrinkleBakeOutOfDate()
{
    Derived.Inline.BakeState.WrinkleMaps = HasWrinkleBakeContent()
                                ? (HasSavedBakeOutput(DWCBakeOutput::WrinkleMaps)
                                       ? EDWCBakeStatus::OutOfDate
                                       : EDWCBakeStatus::Required)
                                : EDWCBakeStatus::Disabled;
}

void UWetClothingAsset::MarkVisualBakeOutOfDate()
{
    MarkWrinkleBakeOutOfDate();
    Derived.Inline.BakeState.TransparencyMaps = HasTransparencyBakeContent()
                                     ? (HasSavedBakeOutput(DWCBakeOutput::TransparencyMaps)
                                            ? EDWCBakeStatus::OutOfDate
                                            : EDWCBakeStatus::Required)
                                     : EDWCBakeStatus::Disabled;
}

void UWetClothingAsset::SetLastBakeFailure(const FString& InFailure)
{
    Derived.Inline.BakeState.LastFailure = InFailure;
}

void UWetClothingAsset::SetCPURuntimeDataStatus(const EDWCBakeStatus InStatus, const FString& InFailure)
{
    Derived.Inline.BakeState.CPURuntimeData = InStatus;
    if (DWCBuildStatus::IsUsable(InStatus))
    {
        MarkBakeOutputGenerated(DWCBakeOutput::CPURuntimeData);
    }
    Derived.Inline.BakeState.LastFailure = InFailure;
}

void UWetClothingAsset::SetGPURuntimeDataStatus(const EDWCBakeStatus InStatus, const FString& InFailure)
{
    Derived.Inline.BakeState.GPURuntimeData = InStatus;
    if (DWCBuildStatus::IsUsable(InStatus))
    {
        MarkBakeOutputGenerated(DWCBakeOutput::GPURuntimeData);
    }
    Derived.Inline.BakeState.LastFailure = InFailure;
}

void UWetClothingAsset::SetGPUMapBakeStatus(const EDWCBakeStatus InStatus, const FString& InFailure)
{
    Derived.Inline.BakeState.GPUMaps = InStatus;
    if (DWCBuildStatus::IsUsable(InStatus))
    {
        MarkBakeOutputGenerated(DWCBakeOutput::GPUMaps);
    }
    Derived.Inline.BakeState.LastFailure = InFailure;
}

void UWetClothingAsset::SetWrinkleBakeStatus(const EDWCBakeStatus InStatus, const FString& InFailure)
{
    Derived.Inline.BakeState.WrinkleMaps = InStatus;
    if (DWCBuildStatus::IsUsable(InStatus))
    {
        MarkBakeOutputGenerated(DWCBakeOutput::WrinkleMaps);
    }
    Derived.Inline.BakeState.LastFailure = InFailure;
}

void UWetClothingAsset::SetTransparencyBakeStatus(const EDWCBakeStatus InStatus, const FString& InFailure)
{
    Derived.Inline.BakeState.TransparencyMaps = InStatus;
    if (DWCBuildStatus::IsUsable(InStatus))
    {
        MarkBakeOutputGenerated(DWCBakeOutput::TransparencyMaps);
    }
    Derived.Inline.BakeState.LastFailure = InFailure;
}

void UWetClothingAsset::MarkBakeOutputGenerated(const int32 OutputMask)
{
    Derived.Inline.BakeState.GeneratedOutputMask |= OutputMask;
}

void UWetClothingAsset::MarkBakeOutputsSaved(const int32 OutputMask)
{
    Derived.Inline.BakeState.SavedOutputMask |= OutputMask;
}

bool UWetClothingAsset::HasGeneratedBakeOutput(const int32 OutputMask) const
{
    return DWCBakeOutput::Has(Derived.Inline.BakeState.GeneratedOutputMask, OutputMask);
}

bool UWetClothingAsset::HasSavedBakeOutput(const int32 OutputMask) const
{
    return DWCBakeOutput::Has(Derived.Inline.BakeState.SavedOutputMask, OutputMask);
}

void UWetClothingAsset::RefreshBakeState(const bool bRunDeepValidation)
{
    bRunDeepValidation ? RefreshBakeStateDeep() : RefreshBakeStateFast();
}

void UWetClothingAsset::RefreshBakeStateFast()
{
    RefreshBakeStateInternal(false);
}

void UWetClothingAsset::RefreshBakeStateDeep()
{
    RefreshBakeStateInternal(true);
}

void UWetClothingAsset::RefreshBakeStateInternal(const bool bRunDeepValidation)
{
    const EDWCBakeStatus PreviousCPUStatus = Derived.Inline.BakeState.CPURuntimeData;
    const EDWCBakeStatus PreviousGPUStatus = Derived.Inline.BakeState.GPURuntimeData;
    const EDWCBakeStatus PreviousGPUMapStatus = Derived.Inline.BakeState.GPUMaps;
    const EDWCBakeStatus PreviousWrinkleMapStatus = Derived.Inline.BakeState.WrinkleMaps;
    const EDWCBakeStatus PreviousTransparencyMapStatus = Derived.Inline.BakeState.TransparencyMaps;
    auto PreserveFailureStatus = [this](const EDWCBakeStatus PreviousStatus, const EDWCBakeStatus NewStatus)
    {
        return PreviousStatus == EDWCBakeStatus::Failed &&
               NewStatus != EDWCBakeStatus::Valid &&
               !Derived.Inline.BakeState.LastFailure.IsEmpty()
                   ? EDWCBakeStatus::Failed
                   : NewStatus;
    };

    const bool bHasWettableSlots = HasAnyWettableMaterialSlot();
    USkeletalMesh* RuntimeMesh = GetRuntimeSkeletalMesh();
    auto IsDataUVMetadataCurrent = [this, RuntimeMesh, bRunDeepValidation](const int32 LODIndex)
    {
        const FDWCDataUVLODMetadata* DataUVMetadata = FindDataUVMetadataForLOD(LODIndex);
        bool bCurrent =
            DataUVMetadata != nullptr && DataUVMetadata->bIsValid &&
            DataUVMetadata->UVChannelIndex == Metadata.DWCDataUVChannelIndex &&
            DataUVMetadata->GeneratorVersion == DWCGeneratedDataVersion::DataUV;
        if (bCurrent && bRunDeepValidation)
        {
            const FString CurrentOriginalUVSignature = BuildMeshContentSignature(
                RuntimeMesh, LODIndex, Metadata.OriginalUVChannelIndex);
            const FString CurrentDataUVSignature = BuildMeshContentSignature(
                RuntimeMesh, LODIndex, Metadata.DWCDataUVChannelIndex);
            bCurrent =
                !CurrentOriginalUVSignature.IsEmpty() &&
                !CurrentDataUVSignature.IsEmpty() &&
                DataUVMetadata->MeshInputSignature == CurrentOriginalUVSignature &&
                DataUVMetadata->DataUVOutputSignature == CurrentDataUVSignature;
        }
        return bCurrent;
    };
    const bool bDataUVMetadataValid = bRunDeepValidation
        ? DoesMappedLODRangeHavePayload(RuntimeMesh, Metadata.SetupSettings, IsDataUVMetadataCurrent)
        : DoesSavedDataUVLODRangeHavePayload(Metadata.SetupSettings, Derived.Inline.DataUVMetadata, IsDataUVMetadataCurrent);
    Derived.Inline.BakeState.GeneratedDataUV = bDataUVMetadataValid
                                     ? EDWCBakeStatus::Valid
                                     : (!Derived.Inline.DataUVMetadata.IsEmpty() ? EDWCBakeStatus::OutOfDate : EDWCBakeStatus::Required);

    const int32 CanonicalTopologyLODIndex = RuntimeSimulationLODIndex;
    bool bTopologyValid = false;
    if (RuntimeMesh != nullptr && (!bRunDeepValidation || GetLODCount(RuntimeMesh) > CanonicalTopologyLODIndex))
    {
        const FDWCEditorUVTopologyData* Topology = FindOriginalUVTopologyForLOD(CanonicalTopologyLODIndex);
        bTopologyValid =
            Topology != nullptr && Topology->bIsValid &&
            Topology->LODIndex == CanonicalTopologyLODIndex &&
            Topology->UVChannelIndex == Metadata.OriginalUVChannelIndex &&
            Topology->GeneratorVersion == DWCGeneratedDataVersion::OriginalUVTopology;
        if (bTopologyValid && bRunDeepValidation)
        {
            const FString CurrentTopologySignature = BuildMeshContentSignature(
                RuntimeMesh, CanonicalTopologyLODIndex, Metadata.OriginalUVChannelIndex);
            bTopologyValid = !CurrentTopologySignature.IsEmpty() && Topology->BuildSignature == CurrentTopologySignature;
        }
    }
    Derived.Inline.BakeState.OriginalUVTopology = bTopologyValid
                                       ? EDWCBakeStatus::Valid
                                       : (!Derived.Inline.OriginalUVTopologies.IsEmpty() ? EDWCBakeStatus::OutOfDate : EDWCBakeStatus::Required);

    if (!bHasWettableSlots)
    {
        Derived.Inline.BakeState.CPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.WrinkleMaps = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.TransparencyMaps = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.LastFailure.Reset();
        return;
    }

    const bool bCPUDataValid = bRunDeepValidation
                                   ? IsPrecomputedSimulationDataValidForMesh(RuntimeMesh)
                                   : IsPrecomputedSimulationDataMetadataValidForMesh(RuntimeMesh);
    const int32 RuntimeLODIndex = RuntimeSimulationLODIndex;
    const bool bGPUDataValid = bRunDeepValidation
        ? IsGPURuntimeDataValidForMesh(RuntimeMesh, RuntimeLODIndex)
        : IsGPURuntimeDataMetadataValidForMesh(RuntimeMesh, RuntimeLODIndex);
    const EDWCBakeStatus NewCPUStatus = Metadata.SetupSettings.bBuildCPUVertexSimulationData
                                            ? (bCPUDataValid
                                                   ? EDWCBakeStatus::Valid
                                                   : (HasCPURuntimeDataPayload()
                                                          ? EDWCBakeStatus::OutOfDate
                                                          : EDWCBakeStatus::Required))
                                            : EDWCBakeStatus::Disabled;
    const EDWCBakeStatus NewGPUStatus = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
                                            ? (bGPUDataValid
                                                   ? EDWCBakeStatus::Valid
                                                   : (HasGPURuntimeDataPayload()
                                                          ? EDWCBakeStatus::OutOfDate
                                                          : EDWCBakeStatus::Required))
                                            : EDWCBakeStatus::Disabled;
    Derived.Inline.BakeState.CPURuntimeData = PreserveFailureStatus(PreviousCPUStatus, NewCPUStatus);
    Derived.Inline.BakeState.GPURuntimeData = PreserveFailureStatus(PreviousGPUStatus, NewGPUStatus);
    const bool bGPUMapDataValid = bRunDeepValidation
        ? IsGPUWetMapDataValidForMesh(RuntimeMesh, RuntimeLODIndex)
        : IsGPUWetMapDataMetadataValidForMesh(RuntimeMesh, RuntimeLODIndex);
    const EDWCBakeStatus NewGPUMapStatus = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
                                               ? (bGPUMapDataValid
                                                      ? EDWCBakeStatus::Valid
                                                      : (HasGPUMapDataPayload()
                                                             ? EDWCBakeStatus::OutOfDate
                                                             : EDWCBakeStatus::Required))
                                               : EDWCBakeStatus::Disabled;
    Derived.Inline.BakeState.GPUMaps = PreserveFailureStatus(PreviousGPUMapStatus, NewGPUMapStatus);

    if (!HasWrinkleBakeContent())
    {
        Derived.Inline.BakeState.WrinkleMaps = EDWCBakeStatus::Disabled;
    }
    else if (!AreRequiredWrinkleBakeAssetReferencesValid(*this))
    {
        const EDWCBakeStatus NewWrinkleMapStatus =
            HasGeneratedBakeOutput(DWCBakeOutput::WrinkleMaps) ||
                    HasSavedBakeOutput(DWCBakeOutput::WrinkleMaps) ||
                    !Authored.WrinkleData.BakedWrinkleMaps.IsEmpty()
                ? EDWCBakeStatus::OutOfDate
                : EDWCBakeStatus::Required;
        Derived.Inline.BakeState.WrinkleMaps =
            PreserveFailureStatus(PreviousWrinkleMapStatus, NewWrinkleMapStatus);
    }
    else if (Derived.Inline.BakeState.WrinkleMaps == EDWCBakeStatus::Disabled)
    {
        Derived.Inline.BakeState.WrinkleMaps = HasSavedBakeOutput(DWCBakeOutput::WrinkleMaps)
                                    ? EDWCBakeStatus::OutOfDate
                                    : EDWCBakeStatus::Required;
    }

    if (!HasTransparencyBakeContent())
    {
        Derived.Inline.BakeState.TransparencyMaps = EDWCBakeStatus::Disabled;
    }
    else if (!AreRequiredTransparencyBakeAssetReferencesValid(*this))
    {
        const bool bHasAnyBakedTransparencyMap = Authored.TransparencyData.TransparencyLayers.ContainsByPredicate(
            [](const FWetClothingTransparencyLayerData& Layer)
            {
                return !Layer.BakedMaps.IsEmpty();
            });
        const EDWCBakeStatus NewTransparencyMapStatus =
            HasGeneratedBakeOutput(DWCBakeOutput::TransparencyMaps) ||
                    HasSavedBakeOutput(DWCBakeOutput::TransparencyMaps) ||
                    bHasAnyBakedTransparencyMap
                ? EDWCBakeStatus::OutOfDate
                : EDWCBakeStatus::Required;
        Derived.Inline.BakeState.TransparencyMaps =
            PreserveFailureStatus(PreviousTransparencyMapStatus, NewTransparencyMapStatus);
    }
    else if (Derived.Inline.BakeState.TransparencyMaps == EDWCBakeStatus::Disabled)
    {
        Derived.Inline.BakeState.TransparencyMaps = HasSavedBakeOutput(DWCBakeOutput::TransparencyMaps)
                                         ? EDWCBakeStatus::OutOfDate
                                         : EDWCBakeStatus::Required;
    }
}

bool UWetClothingAsset::RebuildGPURuntimeData(FString* OutErrorMessage)
{
    if (!LoadRuntimeBulkData(true))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Existing WCA runtime bulk data could not be loaded."));
        return false;
    }

    if (!HasAnyWettableMaterialSlot())
    {
        ClearGPUWetMapData();
        Derived.Inline.BakeState.GPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Disabled;
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
    }

    ResolveWetnessProfilesForDerivedInline(
        Authored.PartData.EditableWetPartData,
        Derived.Inline.ResolvedWetnessProfileParameters);

    USkeletalMesh* RuntimeMesh = GetRuntimeSkeletalMesh();
    const int32 LODIndex = RuntimeSimulationLODIndex;
    if (RuntimeMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No DWC Skeletal Mesh is assigned."));
        return false;
    }

    FScopedSlowTask SlowTask(
        6.0f,
        FText::FromString(FString::Printf(TEXT("Rebuilding DWC GPU runtime data for LOD%d..."), LODIndex)));
    SlowTask.MakeDialog(false);

    RemoveNonSimulationGPULODData(Derived.Bulk.GPURuntimeData);
    SlowTask.EnterProgressFrame(
        0.5f,
        FText::FromString(FString::Printf(TEXT("Checking generated DWC UV Channel for LOD%d before GPU runtime rebuild..."), LODIndex)));

    if (!HasValidDataUVForLOD(LODIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, FString::Printf(TEXT("Generate valid DWC UV Channel payloads for LOD%d before rebuilding GPU runtime data."), LODIndex));
        return false;
    }

    if (!FWetGPUMapBakeBuilder::BuildRuntimeLOD(*this, LODIndex, OutErrorMessage, &SlowTask))
    {
        return false;
    }
    RemoveNonSimulationGPULODData(Derived.Bulk.GPURuntimeData);

    SlowTask.EnterProgressFrame(
        0.5f,
        FText::FromString(FString::Printf(TEXT("Finalizing LOD%d GPU runtime triangles and vertex incident tables..."), LODIndex)));

    MarkBakeOutputGenerated(DWCBakeOutput::GPURuntimeData);
    MarkRuntimeBulkDataDirty(DWCBakeOutput::GPURuntimeData);
    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}

bool UWetClothingAsset::BakeGPUWetnessMaps(FString* OutErrorMessage)
{
    if (!HasAnyWettableMaterialSlot())
    {
        ClearGPUMapData();
        Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Disabled;
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
    }

    ResolveWetnessProfilesForDerivedInline(
        Authored.PartData.EditableWetPartData,
        Derived.Inline.ResolvedWetnessProfileParameters);

    USkeletalMesh* RuntimeMesh = GetRuntimeSkeletalMesh();
    const int32 LODIndex = RuntimeSimulationLODIndex;
    if (RuntimeMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No DWC Skeletal Mesh is assigned."));
        return false;
    }

    constexpr float PrepareWork = 0.5f;
    constexpr float SaveWork = 0.5f;
    const float MapBakeWork = FWetGPUMapBakeBuilder::GetLODMapBakeProgressWork(*this);

    FScopedSlowTask SlowTask(
        PrepareWork + MapBakeWork + SaveWork,
        FText::FromString(FString::Printf(TEXT("Baking DWC GPU simulation maps for LOD%d..."), LODIndex)));
    SlowTask.MakeDialog(false);

    RemoveNonSimulationGPULODData(Derived.Bulk.GPURuntimeData);
    SlowTask.EnterProgressFrame(
        PrepareWork,
        FText::FromString(FString::Printf(TEXT("Preparing LOD%d GPU simulation map bake inputs..."), LODIndex)));

    if (!FWetGPUMapBakeBuilder::BuildLODMaps(*this, LODIndex, OutErrorMessage, &SlowTask))
    {
        Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Failed;
        Derived.Inline.BakeState.LastFailure = OutErrorMessage != nullptr ? *OutErrorMessage : FString();
        return false;
    }
    RemoveNonSimulationGPULODData(Derived.Bulk.GPURuntimeData);

    SlowTask.EnterProgressFrame(
        SaveWork,
        FText::FromString(FString::Printf(TEXT("Saving LOD%d GPU simulation map data into the WCA runtime payload..."), LODIndex)));

    Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Valid;
    Derived.Inline.BakeState.LastFailure.Reset();
    MarkBakeOutputGenerated(DWCBakeOutput::GPUMaps);
    MarkRuntimeBulkDataDirty(DWCBakeOutput::GPUMaps);
    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}

bool UWetClothingAsset::RebuildRuntimeDataForSave(FString* OutErrorMessage)
{
    if (bRuntimeDataRebuildInProgress)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
    }

    USkeletalMesh* RuntimeMesh = GetRuntimeSkeletalMesh();
    const bool bHasWettableSlots = HasAnyWettableMaterialSlot();
    TArray<FWetnessProfileParameters> CurrentResolvedWetnessProfileParameters;
    bool bResolvedWetnessProfilesCurrent = true;
    if (bHasWettableSlots)
    {
        ResolveWetnessProfilesForDerivedInline(
            Authored.PartData.EditableWetPartData,
            CurrentResolvedWetnessProfileParameters);
        bResolvedWetnessProfilesCurrent = AreResolvedWetnessProfileTablesEquivalent(
            CurrentResolvedWetnessProfileParameters,
            Derived.Inline.ResolvedWetnessProfileParameters);
    }

    constexpr int32 RuntimeOutputMask =
        DWCBakeOutput::CPURuntimeData |
        DWCBakeOutput::GPURuntimeData |
        DWCBakeOutput::GPUMaps;
    bool bCPUDataCurrent = Metadata.SetupSettings.bBuildCPUVertexSimulationData
        ? IsPrecomputedSimulationDataValidForMesh(RuntimeMesh)
        : (Derived.Inline.BakeState.CPURuntimeData == EDWCBakeStatus::Disabled &&
           !HasCPURuntimeDataPayload());
    bool bGPUDataCurrent = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
        ? IsGPURuntimeDataValidForMesh(RuntimeMesh, RuntimeSimulationLODIndex)
        : (Derived.Inline.BakeState.GPURuntimeData == EDWCBakeStatus::Disabled &&
           Derived.Inline.BakeState.GPUMaps == EDWCBakeStatus::Disabled &&
           !HasGPURuntimeDataPayload() &&
           !HasGPUMapDataPayload());
    const bool bLODVertexColorDataCurrent = FWCALODVertexColorBuilder::IsCurrent(
        RuntimeMesh,
        Metadata.SetupSettings.FirstGeneratedLODIndex,
        Metadata.SetupSettings.LastGeneratedLODIndex,
        Derived.Bulk.LODVertexColorRuntimeData);
    const bool bHasPendingRuntimeOutput = (PendingRuntimeSaveOutputMask & RuntimeOutputMask) != 0;
    auto RecoverUnreadableRuntimeBulkData = [this, RuntimeOutputMask, &bCPUDataCurrent, &bGPUDataCurrent]()
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("WetClothingAsset: Existing runtime bulk data for '%s' could not be loaded. Discarding the unreadable payload and rebuilding runtime data from authored inputs."),
            *GetNameSafe(this));

        Derived.Bulk.NeighborRuntimeData = FWetClothingPrecomputedSimulationData();
        Derived.Bulk.GPURuntimeData.Reset();
        ClearRuntimeBulkData();
        Derived.Inline.BakeState.CPURuntimeData = Metadata.SetupSettings.bBuildCPUVertexSimulationData
                                       ? EDWCBakeStatus::Required
                                       : EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPURuntimeData = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
                                       ? EDWCBakeStatus::Required
                                       : EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPUMaps = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
                                ? EDWCBakeStatus::Required
                                : EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GeneratedOutputMask &= ~RuntimeOutputMask;
        Derived.Inline.BakeState.SavedOutputMask &= ~RuntimeOutputMask;
        PendingRuntimeSaveOutputMask &= ~RuntimeOutputMask;
        Derived.Inline.BakeState.LastFailure.Reset();
        bCPUDataCurrent = false;
        bGPUDataCurrent = false;
    };
    if (bHasWettableSlots &&
        !bRuntimeBulkDataDirty &&
        !bHasPendingRuntimeOutput &&
        bResolvedWetnessProfilesCurrent &&
        bCPUDataCurrent &&
        bGPUDataCurrent &&
        bLODVertexColorDataCurrent)
    {
        if (HasRuntimeBulkPayload() && !bRuntimeBulkDataLoaded && !LoadRuntimeBulkData(true))
        {
            RecoverUnreadableRuntimeBulkData();
        }
        else
        {
            if (Metadata.SetupSettings.bBuildCPUVertexSimulationData)
            {
                Derived.Inline.BakeState.CPURuntimeData = EDWCBakeStatus::Valid;
                MarkBakeOutputGenerated(DWCBakeOutput::CPURuntimeData);
            }
            if (Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData)
            {
                Derived.Inline.BakeState.GPURuntimeData = EDWCBakeStatus::Valid;
                MarkBakeOutputGenerated(DWCBakeOutput::GPURuntimeData);
            }
            DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
            return true;
        }
    }

    const bool bCPUBackendEnabled = Metadata.SetupSettings.bBuildCPUVertexSimulationData;
    const bool bGPUBackendEnabled = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData;
    if (!bCPUBackendEnabled && !bGPUBackendEnabled)
    {
        // No runtime backend needs either segment. Clear the aggregate payload directly;
        // loading a stale or corrupt payload first would only prevent a valid cleanup.
        Derived.Bulk.NeighborRuntimeData = FWetClothingPrecomputedSimulationData();
        Derived.Bulk.GPURuntimeData.Reset();
        ClearRuntimeBulkData();
        Derived.Inline.BakeState.CPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GeneratedOutputMask &= ~RuntimeOutputMask;
        Derived.Inline.BakeState.SavedOutputMask &= ~RuntimeOutputMask;
        PendingRuntimeSaveOutputMask &= ~RuntimeOutputMask;
        Derived.Inline.BakeState.LastFailure.Reset();
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
    }

    if (!LoadRuntimeBulkData(true))
    {
        RecoverUnreadableRuntimeBulkData();
    }

    TGuardValue<bool> RebuildGuard(bRuntimeDataRebuildInProgress, true);
    FScopedSlowTask SlowTask(
        5.0f,
        NSLOCTEXT("WetClothingAsset", "RebuildRuntimeDataForSave", "Preparing DWC runtime data for save..."));
    SlowTask.MakeDialog(false);
    SlowTask.EnterProgressFrame(
        1.0f,
        NSLOCTEXT("WetClothingAsset", "RefreshBakeStateForSave", "Checking mesh signatures and current DWC bake state..."));

    bool bCPUSucceeded = true;
    bool bGPUSucceeded = true;
    const int32 RuntimeLODIndex = GetSimulationLODIndex();

    if (!bHasWettableSlots)
    {
        SlowTask.EnterProgressFrame(3.75f, NSLOCTEXT("WetClothingAsset", "ClearUnusedRuntimeDataForSave", "Clearing runtime data because no material slot is wettable..."));
        ClearPrecomputedSimulationData();
        ClearGPUWetMapData();
        Derived.Inline.ResolvedWetnessProfileParameters.Reset();
        Derived.Inline.GeneratedWetMaterialOverrides.Reset();
        Derived.Inline.BakedWetPartData = FWetClothingBakedWetPartData();
        Authored.WrinkleData.BakedWrinkleMaps.Reset();
        for (FWetClothingTransparencyLayerData& Layer : Authored.TransparencyData.TransparencyLayers)
        {
            Layer.BakedMaps.Reset();
            Layer.AutoBakeMetadata = FWetClothingTransparencyAutoBakeMetadata();
        }
        Derived.Inline.BakeState.CPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.WrinkleMaps = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.TransparencyMaps = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GeneratedOutputMask &= ~(DWCBakeOutput::CPURuntimeData | DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps | DWCBakeOutput::WrinkleMaps | DWCBakeOutput::TransparencyMaps);
        Derived.Inline.BakeState.SavedOutputMask &= ~(DWCBakeOutput::CPURuntimeData | DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps | DWCBakeOutput::WrinkleMaps | DWCBakeOutput::TransparencyMaps);
        PendingRuntimeSaveOutputMask = 0;
        Derived.Inline.BakeState.LastFailure.Reset();
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
    }

    Derived.Inline.ResolvedWetnessProfileParameters = MoveTemp(CurrentResolvedWetnessProfileParameters);

    TArray<FString> FailureMessages;

    if (!bLODVertexColorDataCurrent)
    {
        TArray<FWCALODVertexColorRuntimeData> RebuiltLODVertexColorRuntimeData;
        FString LODVertexColorError;
        if (FWCALODVertexColorBuilder::Build(
                RuntimeMesh,
                Metadata.SetupSettings.FirstGeneratedLODIndex,
                Metadata.SetupSettings.LastGeneratedLODIndex,
                RebuiltLODVertexColorRuntimeData,
                &LODVertexColorError))
        {
            Derived.Bulk.LODVertexColorRuntimeData = MoveTemp(RebuiltLODVertexColorRuntimeData);
        }
        else
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("WetClothingAsset: Failed to build LOD vertex color runtime data for %s. %s"),
                *GetNameSafe(this),
                *LODVertexColorError);
        }
    }

    if (Metadata.SetupSettings.bBuildCPUVertexSimulationData)
    {
        if (bCPUDataCurrent)
        {
            SlowTask.EnterProgressFrame(
                1.25f,
                FText::FromString(FString::Printf(
                    TEXT("CPU vertex simulation data for LOD%d is already current..."),
                    RuntimeLODIndex)));
            Derived.Inline.BakeState.CPURuntimeData = EDWCBakeStatus::Valid;
            MarkBakeOutputGenerated(DWCBakeOutput::CPURuntimeData);
        }
        else
        {
            SlowTask.EnterProgressFrame(
                1.25f,
                FText::FromString(FString::Printf(
                    TEXT("Building CPU vertex simulation data for LOD%d..."),
                    RuntimeLODIndex)));
            FString CPUError;
            bCPUSucceeded = RebuildPrecomputedSimulationData(&CPUError);
            Derived.Inline.BakeState.CPURuntimeData = bCPUSucceeded ? EDWCBakeStatus::Valid : EDWCBakeStatus::Failed;
            if (bCPUSucceeded)
            {
                MarkBakeOutputGenerated(DWCBakeOutput::CPURuntimeData);
            }
            if (!bCPUSucceeded)
            {
                FailureMessages.Add(FString::Printf(TEXT("CPU Runtime Data: %s"), *CPUError));
            }
        }
    }
    else
    {
        SlowTask.EnterProgressFrame(
            1.25f,
            NSLOCTEXT("WetClothingAsset", "ClearCPURuntimeDataForSave", "Clearing disabled CPU runtime data..."));
        ClearPrecomputedSimulationData();
        Derived.Inline.BakeState.CPURuntimeData = EDWCBakeStatus::Disabled;
    }

    if (Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData)
    {
        if (bGPUDataCurrent)
        {
            SlowTask.EnterProgressFrame(
                1.5f,
                FText::FromString(FString::Printf(
                    TEXT("GPU wetness-map runtime data for LOD%d is already current..."),
                    RuntimeLODIndex)));
            Derived.Inline.BakeState.GPURuntimeData = EDWCBakeStatus::Valid;
            MarkBakeOutputGenerated(DWCBakeOutput::GPURuntimeData);
        }
        else
        {
            const bool bHadGPUMapsBeforeRuntimeRebuild =
                HasGPUMapDataPayload() ||
                HasGeneratedBakeOutput(DWCBakeOutput::GPUMaps) ||
                HasSavedBakeOutput(DWCBakeOutput::GPUMaps);
            SlowTask.EnterProgressFrame(
                1.5f,
                FText::FromString(FString::Printf(
                    TEXT("Building GPU wetness-map runtime data for LOD%d..."),
                    RuntimeLODIndex)));
            FString GPUError;
            bGPUSucceeded = RebuildGPURuntimeData(&GPUError);
            Derived.Inline.BakeState.GPURuntimeData = bGPUSucceeded ? EDWCBakeStatus::Valid : EDWCBakeStatus::Failed;
            if (bGPUSucceeded)
            {
                Derived.Inline.BakeState.GPUMaps = HasGPUMapDataPayload() || bHadGPUMapsBeforeRuntimeRebuild
                                        ? EDWCBakeStatus::OutOfDate
                                        : EDWCBakeStatus::Required;
            }
            if (!bGPUSucceeded)
            {
                FailureMessages.Add(FString::Printf(TEXT("GPU Runtime Data: %s"), *GPUError));
            }
        }
    }
    else
    {
        SlowTask.EnterProgressFrame(
            1.5f,
            NSLOCTEXT("WetClothingAsset", "ClearGPURuntimeDataForSave", "Clearing disabled GPU runtime data..."));
        ClearGPUWetMapData();
        Derived.Inline.BakeState.GPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Disabled;
    }

    SlowTask.EnterProgressFrame(
        1.25f,
        NSLOCTEXT("WetClothingAsset", "RuntimeDataReadyForSave", "Runtime metadata and bulk payload are ready to be written to disk..."));
    const bool bSucceeded = bCPUSucceeded && bGPUSucceeded;
    if (bSucceeded)
    {
        Derived.Inline.BakeState.LastFailure.Reset();
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    }
    else
    {
        const FString CombinedFailure = FString::Join(FailureMessages, TEXT("\n"));
        Derived.Inline.BakeState.LastFailure = CombinedFailure;
        DWC::Error::SetMessage(OutErrorMessage, CombinedFailure.IsEmpty() ? TEXT("Runtime data rebuild failed.") : *CombinedFailure);
    }
    return bSucceeded;
}

bool UWetClothingAsset::CanPrepareRuntimeDataForEditorSave(FString* OutSkipReason) const
{
    if (!HasAnyWettableMaterialSlot())
    {
        DWC::Error::SetMessage(OutSkipReason, TEXT(""));
        return true;
    }

    if (GetRuntimeSkeletalMesh() == nullptr)
    {
        DWC::Error::SetMessage(OutSkipReason, TEXT("No runtime skeletal mesh is assigned."));
        return false;
    }

    const bool bRequiresDWCDataUV = HasAnyWettableMaterialSlot() &&
        Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData;
    if (bRequiresDWCDataUV)
    {
        const bool bHasMappedDataUV = DoesMappedLODRangeHavePayload(
            GetRuntimeSkeletalMesh(),
            Metadata.SetupSettings,
            [this](const int32 LODIndex)
            {
                return HasValidDataUVForLOD(LODIndex);
            });
        if (!bHasMappedDataUV)
        {
            DWC::Error::SetMessage(OutSkipReason, TEXT("DWC UV Channel has not been generated for every mapped LOD yet."));
            return false;
        }
    }

    DWC::Error::SetMessage(OutSkipReason, TEXT(""));
    return true;
}

bool UWetClothingAsset::PrepareRuntimeDataForEditorSave(FString* OutErrorMessage)
{
    if (!CanPrepareRuntimeDataForEditorSave(OutErrorMessage))
    {
        return false;
    }

    const bool bSucceeded = RebuildRuntimeDataForSave(OutErrorMessage);
    bSkipNextPreSaveRuntimeDataRebuild = true;
    if (!bSucceeded)
    {
        // Do not overwrite an existing runtime bulk payload with a partial CPU-only or GPU-only rebuild.
        bRuntimeBulkDataDirty = false;
    }
    return bSucceeded;
}

void UWetClothingAsset::ClearRuntimeDataEditorSavePreparation()
{
    bSkipNextPreSaveRuntimeDataRebuild = false;
}

void UWetClothingAsset::SkipNextRuntimeDataPreSaveRebuild()
{
    bSkipNextPreSaveRuntimeDataRebuild = true;
}

void UWetClothingAsset::BeginRuntimeDataEditorSaveAttempt()
{
    if (bRuntimeDataEditorSaveAttemptActive)
    {
        return;
    }

    bRuntimeDataEditorSaveAttemptActive = true;
    EditorSavePendingOutputMaskSnapshot = PendingRuntimeSaveOutputMask;
    EditorSaveSavedOutputMaskSnapshot = Derived.Inline.BakeState.SavedOutputMask;
}

void UWetClothingAsset::CompleteRuntimeDataEditorSaveAttempt(const bool bSaveSucceeded)
{
    bSkipNextPreSaveRuntimeDataRebuild = false;

    if (!bRuntimeDataEditorSaveAttemptActive)
    {
        return;
    }

    if (!bSaveSucceeded)
    {
        Derived.Inline.BakeState.SavedOutputMask = EditorSaveSavedOutputMaskSnapshot;
        PendingRuntimeSaveOutputMask |= EditorSavePendingOutputMaskSnapshot;
        if (PendingRuntimeSaveOutputMask != 0)
        {
            bRuntimeBulkDataDirty = true;
            MarkPackageDirty();
        }
    }

    EditorSavePendingOutputMaskSnapshot = 0;
    EditorSaveSavedOutputMaskSnapshot = 0;
    bRuntimeDataEditorSaveAttemptActive = false;
}

bool UWetClothingAsset::IsBakeOutputSavePending(const int32 OutputMask) const
{
    return DWCBakeOutput::Has(PendingRuntimeSaveOutputMask, OutputMask);
}

void UWetClothingAsset::MarkRuntimeBakeOutputsDirty(const int32 OutputMask)
{
    ClearMeshContentSignatureCache();

    const auto InvalidateOutput = [this, OutputMask](
        const int32 Output,
        EDWCBakeStatus& Status,
        const bool bEnabled,
        const bool bHasPayload)
    {
        if (!DWCBakeOutput::Has(OutputMask, Output))
        {
            return;
        }

        if (!bEnabled || !HasAnyWettableMaterialSlot())
        {
            Status = EDWCBakeStatus::Disabled;
        }
        else
        {
            const bool bHasPriorOutput =
                bHasPayload ||
                HasGeneratedBakeOutput(Output) ||
                HasSavedBakeOutput(Output);
            Status = bHasPriorOutput ? EDWCBakeStatus::OutOfDate : EDWCBakeStatus::Required;
        }

        // The previous payload is stale, not newly generated. Do not present it as
        // "Save Required"; it must be rebuilt (or explicitly baked) first.
        PendingRuntimeSaveOutputMask &= ~Output;
    };

    InvalidateOutput(
        DWCBakeOutput::CPURuntimeData,
        Derived.Inline.BakeState.CPURuntimeData,
        Metadata.SetupSettings.bBuildCPUVertexSimulationData,
        HasCPURuntimeDataPayload());
    InvalidateOutput(
        DWCBakeOutput::GPURuntimeData,
        Derived.Inline.BakeState.GPURuntimeData,
        Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData,
        HasGPURuntimeDataPayload());
    InvalidateOutput(
        DWCBakeOutput::GPUMaps,
        Derived.Inline.BakeState.GPUMaps,
        Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData,
        HasGPUMapDataPayload());

    Derived.Inline.BakeState.LastFailure.Reset();
    MarkPackageDirty();
}


#endif // WITH_EDITOR

FString UWetClothingAsset::BuildMeshContentSignature(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex,
    const int32 UVChannelIndex)
{
    const FSkeletalMeshRenderData* RenderData = SkeletalMesh != nullptr ? SkeletalMesh->GetResourceForRendering() : nullptr;
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return FString();
    }

    return FDWCMeshContentSignature::BuildUVContent(
        SkeletalMesh,
        LODIndex,
        UVChannelIndex);
}

#if WITH_EDITOR
void UWetClothingAsset::ClearMeshContentSignatureCache()
{
    FWetGPUMapBakeBuilder::ClearSignatureCache();
}

const FDWCEditorUVTopologyData* UWetClothingAsset::FindOriginalUVTopologyForLOD(const int32 LODIndex) const
{
    return Derived.Inline.OriginalUVTopologies.FindByPredicate(
        [LODIndex](const FDWCEditorUVTopologyData& Data)
        {
            return Data.LODIndex == LODIndex;
        });
}

void UWetClothingAsset::LogOriginalUVTopologyMemoryStats() const
{
    int32 IslandCount = 0;
    int64 TriangleIndexCount = 0;
    int64 EstimatedBytes = Derived.Inline.OriginalUVTopologies.GetAllocatedSize();

    for (const FDWCEditorUVTopologyData& Topology : Derived.Inline.OriginalUVTopologies)
    {
        IslandCount += Topology.Islands.Num();
        EstimatedBytes += Topology.BuildSignature.GetAllocatedSize();
        EstimatedBytes += Topology.Islands.GetAllocatedSize();
        for (const FDWCOriginalUVIslandTopology& Island : Topology.Islands)
        {
            TriangleIndexCount += Island.TriangleIndices.Num();
            EstimatedBytes += Island.TriangleIndices.GetAllocatedSize();
        }
    }


    UE_LOG(
        LogDWC,
        Display,
        TEXT("WetClothingAsset topology memory: '%s' LODs=%d, islands=%d, triangle indices=%lld, estimated allocations=%.2f MB."),
        *GetNameSafe(this),
        Derived.Inline.OriginalUVTopologies.Num(),
        IslandCount,
        TriangleIndexCount,
        static_cast<double>(EstimatedBytes) / (1024.0 * 1024.0));
}

void UWetClothingAsset::SetValidationSummary(const FDWCTriangleValidationSummary& InSummary)
{
    Derived.Inline.ValidationSummary = InSummary;
}
#endif // WITH_EDITOR

bool UWetClothingAsset::IsPrecomputedSimulationDataMetadataValidForMesh(
    const USkeletalMesh* SkeletalMesh) const
{
    const int32 LODIndex = RuntimeSimulationLODIndex;
    const FWetClothingPrecomputedSimulationData& Data = Derived.Bulk.NeighborRuntimeData;
    if (!Data.bIsValid || SkeletalMesh == nullptr || SkeletalMesh != GetRuntimeSkeletalMesh())
    {
        return false;
    }

    // Fast metadata validation must not initialize Skeletal Mesh render data,
    // load runtime bulk payloads, or recompute signatures.
    return Data.DataVersion == CurrentPrecomputedSimulationDataVersion &&
           Data.LODIndex == LODIndex &&
           Data.VertexCount > 0 &&
           !Data.MeshSignature.IsEmpty() &&
           !Data.SourceDataSignature.IsEmpty() &&
           HasCPURuntimeDataPayload();
}

bool UWetClothingAsset::IsPrecomputedSimulationDataValidForMesh(const USkeletalMesh* SkeletalMesh) const
{
    const int32 LODIndex = RuntimeSimulationLODIndex;
    const FWetClothingPrecomputedSimulationData& Data = Derived.Bulk.NeighborRuntimeData;
    if (!Data.bIsValid || SkeletalMesh == nullptr)
    {
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    const bool bPayloadAvailable = bRuntimeBulkDataLoaded
        ? Data.Vertices.Num() == LODData.GetNumVertices() && Data.NeighborGraph.Num() == LODData.GetNumVertices()
        : HasRuntimeBulkPayload();

    const FString ExpectedSourceSignature = MakeSourceDataSignature(
        Authored.PartData.EditableWetPartData);
    return Data.DataVersion == CurrentPrecomputedSimulationDataVersion &&
           Data.LODIndex == LODIndex &&
           Data.VertexCount == LODData.GetNumVertices() &&
           bPayloadAvailable &&
           Data.MeshSignature == FDWCMeshContentSignature::BuildStructure(SkeletalMesh, LODData, LODIndex) &&
           Data.SourceDataSignature == ExpectedSourceSignature;
}

FString UWetClothingAsset::GetPrecomputedSimulationDataValidationSummary(const USkeletalMesh* SkeletalMesh) const
{
    const int32 LODIndex = RuntimeSimulationLODIndex;
    EnsureRuntimeBulkDataLoaded();

    const FWetClothingPrecomputedSimulationData& Data = Derived.Bulk.NeighborRuntimeData;
    const FSkeletalMeshRenderData* RenderData = SkeletalMesh != nullptr ? SkeletalMesh->GetResourceForRendering() : nullptr;
    const bool bHasLODData = RenderData != nullptr && RenderData->LODRenderData.IsValidIndex(LODIndex);
    const int32 ExpectedVertexCount = bHasLODData ? RenderData->LODRenderData[LODIndex].GetNumVertices() : INDEX_NONE;
    const FString ExpectedMeshSignature = bHasLODData
                                               ? FDWCMeshContentSignature::BuildStructure(SkeletalMesh, RenderData->LODRenderData[LODIndex], LODIndex)
                                               : FString();
    const FString ExpectedSourceSignature = MakeSourceDataSignature(
        Authored.PartData.EditableWetPartData);

    const bool bVersionMatches = Data.DataVersion == CurrentPrecomputedSimulationDataVersion;
    const bool bLODMatches = Data.LODIndex == LODIndex;
    const bool bVertexCountMatches = ExpectedVertexCount != INDEX_NONE && Data.VertexCount == ExpectedVertexCount;
    const bool bVertexPayloadMatches = ExpectedVertexCount != INDEX_NONE && Data.Vertices.Num() == ExpectedVertexCount;
    const bool bNeighborPayloadMatches = ExpectedVertexCount != INDEX_NONE && Data.NeighborGraph.Num() == ExpectedVertexCount;
    const bool bHasRuntimePayload = bVertexPayloadMatches && bNeighborPayloadMatches;
    const bool bMeshSignatureMatches = !ExpectedMeshSignature.IsEmpty() && Data.MeshSignature == ExpectedMeshSignature;
    const bool bSourceSignatureMatches =
        Data.SourceDataSignature == ExpectedSourceSignature;

    return FString::Printf(
        TEXT("CPUPrecomputed{validFlag=%s, hasMesh=%s, hasLOD=%s, dataVersion=%d/%d:%s, lod=%d/%d:%s, vertexCount=%d/%d:%s, vertices=%d:%s, neighborGraph=%d:%s, boneCache=%s, hasBulk=%s, bulkLoaded=%s, bulkLoadFailed=%s, hasPayload=%s, meshSignature=%s, sourceSignature=%s}"),
        Data.bIsValid ? TEXT("true") : TEXT("false"),
        SkeletalMesh != nullptr ? TEXT("true") : TEXT("false"),
        bHasLODData ? TEXT("true") : TEXT("false"),
        Data.DataVersion,
        CurrentPrecomputedSimulationDataVersion,
        bVersionMatches ? TEXT("match") : TEXT("mismatch"),
        Data.LODIndex,
        LODIndex,
        bLODMatches ? TEXT("match") : TEXT("mismatch"),
        Data.VertexCount,
        ExpectedVertexCount,
        bVertexCountMatches ? TEXT("match") : TEXT("mismatch"),
        Data.Vertices.Num(),
        bVertexPayloadMatches ? TEXT("match") : TEXT("mismatch"),
        Data.NeighborGraph.Num(),
        bNeighborPayloadMatches ? TEXT("match") : TEXT("mismatch"),
        Data.BoneOptimizationCache.bIsValid ? TEXT("valid") : TEXT("invalid"),
        HasRuntimeBulkPayload() ? TEXT("true") : TEXT("false"),
        bRuntimeBulkDataLoaded ? TEXT("true") : TEXT("false"),
        bRuntimeBulkDataLoadFailed ? TEXT("true") : TEXT("false"),
        bHasRuntimePayload ? TEXT("true") : TEXT("false"),
        bMeshSignatureMatches ? TEXT("match") : TEXT("mismatch"),
        bSourceSignatureMatches ? TEXT("match") : TEXT("mismatch"));
}

bool UWetClothingAsset::IsGPURuntimeDataMetadataValidForMesh(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex) const
{
    const FDWCGPULODBakeData* Data = FindGPULODData(Derived.Bulk.GPURuntimeData, LODIndex);
    if (Data == nullptr || !Data->bRuntimeDataValid || SkeletalMesh == nullptr || SkeletalMesh != GetRuntimeSkeletalMesh())
    {
        return false;
    }

    // Fast metadata validation must not initialize Skeletal Mesh render data,
    // load runtime bulk payloads, or recompute signatures.
    return Data->RuntimeDataVersion == FDWCGPULODBakeData::CurrentRuntimeDataVersion &&
           Data->BulkDataVersion == FDWCGPULODBakeData::CurrentBulkDataVersion &&
           Data->LODIndex == LODIndex &&
           !Data->MeshSignature.IsEmpty() &&
           !Data->RuntimeSignature.IsEmpty() &&
           Data->ProfileCount > 0 &&
           Data->TriangleCount > 0 &&
           Data->VertexIncidentRecordCount > 0 &&
           HasGPURuntimeDataPayload();
}

bool UWetClothingAsset::IsGPURuntimeDataValidForMesh(const USkeletalMesh* SkeletalMesh, const int32 LODIndex) const
{
    const FDWCGPULODBakeData* Data = FindGPULODData(Derived.Bulk.GPURuntimeData, LODIndex);
    if (Data == nullptr || !Data->bRuntimeDataValid || SkeletalMesh == nullptr)
    {
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return false;
    }

    const FString ExpectedDataUVMeshSignature = BuildMeshContentSignature(
        SkeletalMesh,
        LODIndex,
        GetDWCDataUVChannelIndex());
    const bool bRuntimePayloadAvailable = bRuntimeBulkDataLoaded
        ? Data->Profiles.Num() == Data->ProfileCount &&
          Data->Triangles.Num() == Data->TriangleCount &&
          Data->VertexIncidentTriangles.Num() == Data->VertexIncidentRecordCount
        : (!bRuntimeBulkDataLoadFailed && HasRuntimeBulkPayload());
    if (Data->RuntimeDataVersion != FDWCGPULODBakeData::CurrentRuntimeDataVersion ||
        Data->BulkDataVersion != FDWCGPULODBakeData::CurrentBulkDataVersion ||
        Data->LODIndex != LODIndex ||
        ExpectedDataUVMeshSignature.IsEmpty() ||
        Data->MeshSignature != ExpectedDataUVMeshSignature ||
        Data->ProfileCount <= 0 ||
        Data->TriangleCount <= 0 ||
        Data->VertexIncidentRecordCount <= 0 ||
        !bRuntimePayloadAvailable)
    {
        return false;
    }

#if WITH_EDITOR
    FString ExpectedRuntimeSignature;
    if (!FWetGPUMapBakeBuilder::BuildLODRuntimeSignature(
            *this,
            LODIndex,
            ExpectedRuntimeSignature) ||
        ExpectedRuntimeSignature.IsEmpty() ||
        Data->RuntimeSignature != ExpectedRuntimeSignature)
    {
        return false;
    }
#endif

    return true;
}

bool UWetClothingAsset::IsGPUWetMapDataMetadataValidForMesh(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex) const
{
    const FDWCGPULODBakeData* Data = FindGPULODData(Derived.Bulk.GPURuntimeData, LODIndex);
    return Data != nullptr &&
           Data->bMapDataValid &&
           IsGPURuntimeDataMetadataValidForMesh(SkeletalMesh, LODIndex) &&
           Data->MapBakeVersion == FDWCGPULODBakeData::CurrentMapBakeVersion &&
           IsGPUMapPayloadCompatibleWithCurrentSetup(*this, *Data) &&
           !Data->MapSignature.IsEmpty() &&
           HasGPUMapDataPayload();
}

bool UWetClothingAsset::IsGPUWetMapDataValidForMesh(const USkeletalMesh* SkeletalMesh, const int32 LODIndex) const
{
    const FDWCGPULODBakeData* Data = FindGPULODData(Derived.Bulk.GPURuntimeData, LODIndex);
    if (Data == nullptr || !Data->bMapDataValid || !IsGPURuntimeDataValidForMesh(SkeletalMesh, LODIndex))
    {
        return false;
    }

    const bool bMapPayloadAvailable = bRuntimeBulkDataLoaded
        ? Data->MaterialSlots.Num() == Data->MaterialSlotMapCount
        : (!bRuntimeBulkDataLoadFailed && HasRuntimeBulkPayload());
    if (Data->MapBakeVersion != FDWCGPULODBakeData::CurrentMapBakeVersion ||
        Data->MaterialSlotMapCount <= 0 ||
        !IsGPUMapPayloadCompatibleWithCurrentSetup(*this, *Data) ||
        !bMapPayloadAvailable)
    {
        return false;
    }

#if WITH_EDITOR
    FString ExpectedMapSignature;
    if (!FWetGPUMapBakeBuilder::BuildLODMapSignature(
            *this,
            LODIndex,
            ExpectedMapSignature) ||
        ExpectedMapSignature.IsEmpty() ||
        Data->MapSignature != ExpectedMapSignature)
    {
        return false;
    }
#endif

    return true;
}

#if WITH_EDITOR
bool UWetClothingAsset::RebuildPrecomputedSimulationData(FString* OutErrorMessage)
{
    if (!LoadRuntimeBulkData(true))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Existing WCA runtime bulk data could not be loaded."));
        return false;
    }

    const int32 LODIndex = RuntimeSimulationLODIndex;
    ClearPrecomputedSimulationData();

    ResolveWetnessProfilesForDerivedInline(
        Authored.PartData.EditableWetPartData,
        Derived.Inline.ResolvedWetnessProfileParameters);

    USkeletalMesh* RuntimeMesh = GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No DWC Skeletal Mesh is assigned."));
        return false;
    }

    Metadata.DWCSkeletalMesh = RuntimeMesh;

    const FSkeletalMeshRenderData* RenderData = RuntimeMesh->GetResourceForRendering();
    if (RenderData == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Skeletal Mesh render data is unavailable."));
        return false;
    }

    if (!RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The requested LOD render data is unavailable."));
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    TArray<uint32>                    IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

    if (LODData.GetNumVertices() <= 0 || IndexBuffer.Num() == 0)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Skeletal Mesh render data is empty."));
        return false;
    }

    Derived.Inline.GeneratedWetMaterialOverrides.RemoveAll(
        [this](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
        {
            return MaterialOverride.MaterialSlotIndex != INDEX_NONE &&
                   !IsWettableMaterialSlot(Authored.PartData.EditableWetPartData, MaterialOverride.MaterialSlotIndex);
        });

    Derived.Inline.BakedWetPartData.SlotTextures.RemoveAll(
        [this](const FWetClothingBakedWetPartDataSlotTexture& SlotTexture)
        {
            return !IsWettableMaterialSlot(
                Authored.PartData.EditableWetPartData,
                SlotTexture.MaterialSlotIndex);
        });
    if (Derived.Inline.BakedWetPartData.SlotTextures.IsEmpty())
    {
        Derived.Inline.BakedWetPartData = FWetClothingBakedWetPartData();
    }

    Derived.Bulk.NeighborRuntimeData.bIsValid = true;
    Derived.Bulk.NeighborRuntimeData.LODIndex = LODIndex;
    Derived.Bulk.NeighborRuntimeData.VertexCount = LODData.GetNumVertices();
    Derived.Bulk.NeighborRuntimeData.MeshSignature = FDWCMeshContentSignature::BuildStructure(RuntimeMesh, LODData, LODIndex);
    Derived.Bulk.NeighborRuntimeData.SourceDataSignature = MakeSourceDataSignature(
        Authored.PartData.EditableWetPartData);
    Derived.Bulk.NeighborRuntimeData.DataVersion = CurrentPrecomputedSimulationDataVersion;
    Derived.Bulk.NeighborRuntimeData.Vertices.SetNum(Derived.Bulk.NeighborRuntimeData.VertexCount);

    const FDWCEditorUVTopologyData* OriginalUVTopology = FindOriginalUVTopologyForLOD(LODIndex);
    const FString CurrentTopologySignature = BuildMeshContentSignature(
        RuntimeMesh,
        LODIndex,
        Metadata.OriginalUVChannelIndex);
    if (OriginalUVTopology == nullptr ||
        !OriginalUVTopology->bIsValid ||
        OriginalUVTopology->UVChannelIndex != Metadata.OriginalUVChannelIndex ||
        OriginalUVTopology->GeneratorVersion != DWCGeneratedDataVersion::OriginalUVTopology ||
        OriginalUVTopology->BuildSignature.IsEmpty() ||
        OriginalUVTopology->BuildSignature != CurrentTopologySignature)
    {
        DWC::Error::SetMessage(
            OutErrorMessage,
            TEXT("Original UV topology is missing or out of date. The sealed layout cannot be rebuilt; create a new WCA if its source topology changed."));
        ClearPrecomputedSimulationData();
        return false;
    }

    const int32 OriginalUVChannelIndex = Metadata.OriginalUVChannelIndex;
    for (const FWetClothingAuthoredMaterialSlot& Slot : Authored.PartData.EditableWetPartData.MaterialSlots)
    {
        if (!Slot.bIsWettableSlot || Slot.MaterialSlotIndex == INDEX_NONE)
        {
            continue;
        }

        TArray<FDWCRuntimeTopologyTriangle> RawTriangles;
        if (!FDWCOriginalUVRuntimeTopologyAdapter::ReadMaterialSlotTriangles(
                RuntimeMesh,
                LODData,
                IndexBuffer,
                OriginalUVChannelIndex,
                Slot.MaterialSlotIndex,
                RawTriangles,
                OutErrorMessage))
        {
            ClearPrecomputedSimulationData();
            return false;
        }

        TArray<FDWCRuntimeOriginalUVIsland> Islands;
        if (!FDWCOriginalUVRuntimeTopologyAdapter::BuildIslands(
                RawTriangles,
                *OriginalUVTopology,
                Slot.MaterialSlotIndex,
                Islands,
                OutErrorMessage))
        {
            ClearPrecomputedSimulationData();
            return false;
        }

        TMap<int32, int32> AssignedUVIslandToEntryIndex;
        for (int32 EntryIndex = 0; EntryIndex < Slot.WetPartEntries.Num(); ++EntryIndex)
        {
            const FWetClothingWetPartEntry& Entry = Slot.WetPartEntries[EntryIndex];
            if (Entry.WetPartID == 0)
            {
                continue;
            }

            for (const int32 UVIslandID : Entry.AssignedUVIslandIDs)
            {
                AssignedUVIslandToEntryIndex.FindOrAdd(UVIslandID) = EntryIndex;
            }
        }

        for (const FDWCRuntimeOriginalUVIsland& Island : Islands)
        {
            const int32* AssignedEntryIndex = AssignedUVIslandToEntryIndex.Find(Island.UVIslandID);
            if (AssignedEntryIndex == nullptr || !Slot.WetPartEntries.IsValidIndex(*AssignedEntryIndex))
            {
                continue;
            }

            const FWetClothingWetPartEntry& Entry = Slot.WetPartEntries[*AssignedEntryIndex];
            const int32 EffectiveProfileIndex = Authored.PartData.EditableWetPartData.Profiles.IsValidIndex(Entry.ProfileIndex)
                ? Entry.ProfileIndex
                : 0;
            for (const int32 VertexIndex : Island.VertexIndices)
            {
                if (!Derived.Bulk.NeighborRuntimeData.Vertices.IsValidIndex(VertexIndex))
                {
                    continue;
                }

                FWetClothingPrecomputedVertexData& VertexData = Derived.Bulk.NeighborRuntimeData.Vertices[VertexIndex];
                // Vertex-only contacts have no section context. Use the lowest slot as a deterministic primary binding.
                if (VertexData.MaterialSlotIndex != INDEX_NONE && VertexData.MaterialSlotIndex <= Slot.MaterialSlotIndex)
                {
                    continue;
                }
                VertexData.WetPartID = Entry.WetPartID;
                VertexData.ProfileIndex = EffectiveProfileIndex;
                VertexData.MaterialSlotIndex = Slot.MaterialSlotIndex;
            }
        }
    }

    BuildNeighborGraph(
        LODData,
        IndexBuffer,
        Derived.Bulk.NeighborRuntimeData.Vertices,
        Derived.Bulk.NeighborRuntimeData.NeighborGraph);

    FWetBoneOptimizationCache   RuntimeBoneOptimizationCache;
    FString                     BoneCacheErrorMessage;
    if (FWetBoneOptimizationCacheBuilder::Build(
            RuntimeMesh,
            LODIndex,
            RuntimeBoneOptimizationCache,
            &BoneCacheErrorMessage))
    {
        FilterBoneOptimizationCacheByWettableVertices(
            RuntimeBoneOptimizationCache,
            Derived.Bulk.NeighborRuntimeData.Vertices);

        Derived.Bulk.NeighborRuntimeData.BoneOptimizationCache.BuildFromRuntimeCache(
            RuntimeMesh,
            RuntimeBoneOptimizationCache,
            Derived.Bulk.NeighborRuntimeData.MeshSignature,
            nullptr);
    }
    else
    {
        Derived.Bulk.NeighborRuntimeData.BoneOptimizationCache.Reset();
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("WetClothingAsset: Failed to precompute bone optimization cache for %s. %s"),
            *GetNameSafe(RuntimeMesh),
            *BoneCacheErrorMessage);
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    MarkBakeOutputGenerated(DWCBakeOutput::CPURuntimeData);
    MarkRuntimeBulkDataDirty(DWCBakeOutput::CPURuntimeData);
    return true;
}
#endif // WITH_EDITOR
