#include "DataAssets/WetClothingAsset.h"

#include "CoreGlobals.h"
#include "Engine/SkeletalMesh.h"
#include "DerivedData/DWCMeshContentSignature.h"
#include "Misc/ScopedSlowTask.h"
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
#include "Utility/DWCDataUVBufferView.h"

namespace
{
    static constexpr float CoincidentVertexNeighborTolerance = 0.001f;
    static constexpr int32 DWCRuntimeBulkPayloadMagic = 0x44574342; // DWCB
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

    bool IsWettableMaterialSlot(
        const FWetClothingEditableWetPartData& WetPartData,
        const int32 MaterialSlotIndex)
    {
        const FWetClothingWettableMaterialSlotState* State =
            WetPartData.WettableMaterialSlots.FindByPredicate(
                [MaterialSlotIndex](const FWetClothingWettableMaterialSlotState& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex;
                });
        return State != nullptr && State->bIsWettableSlot;
    }

    FString MakeSourceDataSignature(
        const FWetClothingEditableWetPartData& WetPartData,
        const FSurfaceWaterSimulationSettings& SurfaceWaterSettings)
    {
    FString Signature = FString::Printf(
        TEXT("DWC_SourceData_v1|SurfaceWater=%d|RT=%d"),
        SurfaceWaterSettings.bEnabled ? 1 : 0,
        SurfaceWaterSettings.RenderTargetResolution);

        TArray<int32> SlotIndices;
        for (int32 SlotIndex = 0; SlotIndex < WetPartData.WettableMaterialSlots.Num(); ++SlotIndex)
        {
            SlotIndices.Add(SlotIndex);
        }
        SlotIndices.Sort(
            [&WetPartData](const int32 A, const int32 B)
            {
                const FWetClothingWettableMaterialSlotState& Left = WetPartData.WettableMaterialSlots[A];
                const FWetClothingWettableMaterialSlotState& Right = WetPartData.WettableMaterialSlots[B];
                if (Left.MaterialSlotIndex != Right.MaterialSlotIndex)
                {
                    return Left.MaterialSlotIndex < Right.MaterialSlotIndex;
                }
                return Left.ComponentPath < Right.ComponentPath;
            });
        Signature += FString::Printf(TEXT("|WettableSlots=%d"), SlotIndices.Num());
        for (const int32 SlotIndex : SlotIndices)
        {
            const FWetClothingWettableMaterialSlotState& Slot = WetPartData.WettableMaterialSlots[SlotIndex];
            Signature += FString::Printf(
                TEXT("|Slot{%s,%d,%d}"),
                *Slot.ComponentPath,
                Slot.MaterialSlotIndex,
                Slot.bIsWettableSlot ? 1 : 0);
        }

        TArray<int32> EntryIndices;
        for (int32 EntryIndex = 0; EntryIndex < WetPartData.WetPartEntries.Num(); ++EntryIndex)
        {
            EntryIndices.Add(EntryIndex);
        }
        EntryIndices.Sort(
            [&WetPartData](const int32 A, const int32 B)
            {
                const FWetClothingWetPartEntry& Left = WetPartData.WetPartEntries[A];
                const FWetClothingWetPartEntry& Right = WetPartData.WetPartEntries[B];
                if (Left.MaterialSlotIndex != Right.MaterialSlotIndex)
                {
                    return Left.MaterialSlotIndex < Right.MaterialSlotIndex;
                }
                if (Left.UVChannelIndex != Right.UVChannelIndex)
                {
                    return Left.UVChannelIndex < Right.UVChannelIndex;
                }
                if (Left.WetPartID != Right.WetPartID)
                {
                    return Left.WetPartID < Right.WetPartID;
                }
                return Left.ComponentPath < Right.ComponentPath;
            });
        Signature += FString::Printf(TEXT("|WetPartEntries=%d"), EntryIndices.Num());
        for (const int32 EntryIndex : EntryIndices)
        {
            const FWetClothingWetPartEntry& Entry = WetPartData.WetPartEntries[EntryIndex];
            TArray<int32> AssignedIslandIDs = Entry.AssignedUVIslandIDs;
            AssignedIslandIDs.Sort();

            Signature += FString::Printf(
                TEXT("|Entry{%s,%d,%d,%d,%s,%d,%s"),
                *Entry.ComponentPath,
                Entry.MaterialSlotIndex,
                Entry.UVChannelIndex,
                Entry.WetPartID,
                *Entry.DisplayName,
                Entry.bViewEnabled ? 1 : 0,
                *Entry.ProfileAssignment.SourceProfileName);
            Signature += FString::Printf(TEXT(",Profile=%s"), *Entry.ProfileAssignment.SourceProfile.ToString());
            Signature += FString::Printf(TEXT(",Blend=%d"), static_cast<int32>(Entry.ProfileAssignment.BlendMode));
            Signature += FString::Printf(TEXT(",Islands=%d"), AssignedIslandIDs.Num());
            for (const int32 IslandID : AssignedIslandIDs)
            {
                Signature += FString::Printf(TEXT(",%d"), IslandID);
            }
            Signature += TEXT("}");
        }

        TArray<int32> SurfaceSlotIndices;
        for (int32 SurfaceSlotIndex = 0; SurfaceSlotIndex < SurfaceWaterSettings.SurfaceWaterMaterialSlots.Num(); ++SurfaceSlotIndex)
        {
            SurfaceSlotIndices.Add(SurfaceSlotIndex);
        }
        SurfaceSlotIndices.Sort(
            [&SurfaceWaterSettings](const int32 A, const int32 B)
            {
                return SurfaceWaterSettings.SurfaceWaterMaterialSlots[A].MaterialSlotIndex <
                       SurfaceWaterSettings.SurfaceWaterMaterialSlots[B].MaterialSlotIndex;
            });
        Signature += FString::Printf(TEXT("|SurfaceSlots=%d"), SurfaceSlotIndices.Num());
        for (const int32 SurfaceSlotIndex : SurfaceSlotIndices)
        {
            const FSurfaceWaterMaterialSlotData& Slot = SurfaceWaterSettings.SurfaceWaterMaterialSlots[SurfaceSlotIndex];
            Signature += FString::Printf(
                TEXT("|SurfaceSlot{%d,%d,%d,%d,%d,%s}"),
                Slot.MaterialSlotIndex,
                Slot.bEnabled ? 1 : 0,
                Slot.BakedFlowMap.bEnabled ? 1 : 0,
                Slot.BakedFlowMap.bIsValid ? 1 : 0,
                Slot.BakedFlowMap.Resolution,
                *Slot.BakedFlowMap.BuildSignature);
        }

        return Signature;
    }

    void ResolveWetnessProfilesForDerivedInline(
        const FWetClothingEditableWetPartData& WetPartData,
        TArray<FWetnessProfileParameters>& OutResolvedParameters)
    {
        OutResolvedParameters.Reset();
        OutResolvedParameters.Reserve(WetPartData.WetPartEntries.Num());

        for (const FWetClothingWetPartEntry& Entry : WetPartData.WetPartEntries)
        {
            FWetnessProfileParameters Parameters = Entry.ProfileAssignment.Parameters;
            if (Entry.ProfileAssignment.SourceProfile.IsValid())
            {
                if (const UWetnessProfile* SourceProfile =
                        Cast<UWetnessProfile>(Entry.ProfileAssignment.SourceProfile.TryLoad()))
                {
                    Parameters = SourceProfile->GetParameters();
                }
            }
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
        if (Settings.PreferredDWCDataUVChannelIndex < 0 || Settings.PreferredDWCDataUVChannelIndex > 7)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Data UV Channel must be between UV0 and UV7."));
            return false;
        }
        if (Settings.PreferredDWCDataUVChannelIndex == Settings.OriginalUVChannelIndex)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Data UV Channel cannot be the same as Original UV Channel."));
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
        Ar << Vertex.WetPartEntryIndex;
        Ar << Vertex.MaterialSlotIndex;
        Ar << Vertex.UVChannelIndex;
        Ar << Vertex.UVIslandID;
        Ar << Vertex.SurfaceWaterUV;
        Ar << Vertex.bHasSurfaceWaterUV;
        Ar << Vertex.bIsWettable;
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

    void SerializeGPUProfile(FArchive& Ar, FDWCGPUProfileParameters& Profile)
    {
        Ar << Profile.AbsorptionMultiplier;
        Ar << Profile.SpreadRatePerSecond;
        Ar << Profile.DryRatePerSecond;
        Ar << Profile.GravityFlowStrength;
    }

    void SerializeGPUTriangle(FArchive& Ar, FDWCGPUBakedTriangle& Triangle)
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
        Ar << Triangle.RestSurfaceArea;
        Ar << Triangle.ProfileIndex;
    }

    void SerializeGPUIncident(FArchive& Ar, FDWCGPUVertexIncidentTriangles& Incident)
    {
        Ar << Incident.SourceVertexIndex;
        Ar << Incident.TriangleIDs;
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

    void SerializeGPUMaterialSlot(FArchive& Ar, FDWCGPUMaterialSlotBakeData& Slot)
    {
        Ar << Slot.MaterialSlotIndex;
        Ar << Slot.UVChannelIndex;
        Ar << Slot.Resolution;
        Ar << Slot.TexelTriangleIDs;
        Ar << Slot.PackedTexelBarycentricXY;
        Ar << Slot.RestTexelAreas;
        Ar << Slot.ValidMask;
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

        SerializeArrayWithProgress<FDWCGPUProfileParameters>(
            Ar,
            Data.Profiles,
            [](FArchive& InnerAr, FDWCGPUProfileParameters& Profile)
            {
                SerializeGPUProfile(InnerAr, Profile);
            },
            SlowTask,
            Work * ProfileRatio,
            TEXT("Loading GPU wetness profiles"),
            TEXT("Serializing GPU wetness profiles"));
        SerializeArrayWithProgress<FDWCGPUBakedTriangle>(
            Ar,
            Data.Triangles,
            [](FArchive& InnerAr, FDWCGPUBakedTriangle& Triangle)
            {
                SerializeGPUTriangle(InnerAr, Triangle);
            },
            SlowTask,
            Work * TriangleRatio,
            TEXT("Loading GPU runtime triangles"),
            TEXT("Serializing GPU runtime triangles"));
        SerializeArrayWithProgress<FDWCGPUVertexIncidentTriangles>(
            Ar,
            Data.VertexIncidentTriangles,
            [](FArchive& InnerAr, FDWCGPUVertexIncidentTriangles& Incident)
            {
                SerializeGPUIncident(InnerAr, Incident);
            },
            SlowTask,
            Work * IncidentRatio,
            TEXT("Loading GPU vertex incident records"),
            TEXT("Serializing GPU vertex incident records"));
        SerializeArrayWithProgress<FDWCGPUMaterialSlotBakeData>(
            Ar,
            Data.MaterialSlots,
            [](FArchive& InnerAr, FDWCGPUMaterialSlotBakeData& Slot)
            {
                SerializeGPUMaterialSlot(InnerAr, Slot);
            },
            SlowTask,
            Work * MaterialSlotRatio,
            TEXT("Loading GPU material-slot map payloads"),
            TEXT("Serializing GPU material-slot map payloads"));
    }

    void SerializeRuntimeBulkPayload(
        FArchive& Ar,
        FWetClothingPrecomputedSimulationData& CPUData,
        TArray<FDWCGPULODBakeData>& GPUData,
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
        if (Ar.IsLoading() && (Magic != DWCRuntimeBulkPayloadMagic || Version < 1 || Version > UWetClothingAsset::CurrentRuntimeBulkDataVersion))
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
                SlowTask,
                GPUProgressPerLOD * 0.95f,
                GPUIndex,
                GPUData.Num());
        }
    }

    struct FWetPartScopeKey
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 UVChannelIndex = INDEX_NONE;

        bool operator==(const FWetPartScopeKey& Other) const
        {
            return MaterialSlotIndex == Other.MaterialSlotIndex && UVChannelIndex == Other.UVChannelIndex;
        }
    };

    uint32 GetTypeHash(const FWetPartScopeKey& Key)
    {
        return HashCombine(::GetTypeHash(Key.MaterialSlotIndex), ::GetTypeHash(Key.UVChannelIndex));
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
            if (!VertexData.IsValidIndex(VertexIndex) || !VertexData[VertexIndex].bIsWettable)
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

                const bool b0 = VertexData.IsValidIndex(Index0) && VertexData[Index0].bIsWettable;
                const bool b1 = VertexData.IsValidIndex(Index1) && VertexData[Index1].bIsWettable;
                const bool b2 = VertexData.IsValidIndex(Index2) && VertexData[Index2].bIsWettable;

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
                if (VertexData.IsValidIndex(VertexIndex) && VertexData[VertexIndex].bIsWettable)
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
                if (VertexData.IsValidIndex(VertexIndex) && VertexData[VertexIndex].bIsWettable)
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

} // namespace

void UWetClothingAsset::ClearPrecomputedSimulationData()
{
#if WITH_EDITOR
    ClearMeshContentSignatureCache();
#endif
    Derived.Bulk.NeighborRuntimeData = FWetClothingPrecomputedSimulationData();
    MarkRuntimeBulkDataDirty(DWCBakeOutput::CPURuntimeData);
}

void UWetClothingAsset::ClearGPUWetMapData()
{
#if WITH_EDITOR
    ClearMeshContentSignatureCache();
#endif
    Derived.Bulk.GPURuntimeData.Reset();
    MarkRuntimeBulkDataDirty(DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps);
}

void UWetClothingAsset::ClearGPUMapData()
{
#if WITH_EDITOR
    ClearMeshContentSignatureCache();
#endif
    EnsureRuntimeBulkDataLoaded();
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
    return Authored.PartData.EditableWetPartData.WettableMaterialSlots.ContainsByPredicate(
        [](const FWetClothingWettableMaterialSlotState& SlotState)
        {
            return SlotState.bIsWettableSlot && SlotState.MaterialSlotIndex != INDEX_NONE;
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

    if (!Authored.TransparencyData.SourceBlueprintClass.IsNull())
    {
        return true;
    }

    return Authored.TransparencyData.TransparencyLayers.ContainsByPredicate(
        [this](const FWetClothingTransparencyLayerData& Layer)
        {
            return Layer.TargetSurface.OuterMaterialSlotIndex != INDEX_NONE &&
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
    if (DataUVMetadata == nullptr || !DataUVMetadata->bIsValid || DataUVMetadata->UVChannelIndex != Metadata.DWCDataUVChannelIndex)
    {
        return false;
    }

    FDWCDataUVBufferView View;
    if (!View.Initialize(GetDWCSkeletalMesh(), LODIndex, Metadata.DWCDataUVChannelIndex))
    {
        return false;
    }

    return DataUVMetadata->RenderVertexCount == View.NumVertices();
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
    if (Ar.IsSaving() || Metadata.AssetDataVersion >= FirstAssetVersionWithSerializedRuntimeBulkData)
    {
        Ar << bHasSerializedRuntimeBulkData;
        if (bHasSerializedRuntimeBulkData)
        {
            RuntimeBulkData.Serialize(Ar, this, INDEX_NONE, false, EFileRegionType::None);
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
    Metadata.SetupSettings.NormalizeMapResolutions();
    ClampSetupLODRangeToMesh(
        Metadata.DWCSkeletalMesh != nullptr ? Metadata.DWCSkeletalMesh.Get() : Metadata.SourceSkeletalMesh.Get(),
        Metadata.SetupSettings);
    // Surface Water RT resolution is mirrored into setup settings so the
    // consolidated Map Resolutions panel reflects the runtime value for old assets.
    Metadata.SetupSettings.SurfaceWaterRTResolution = DWCMapResolution::ToInt(
        DWCMapResolution::FromInt(Authored.SurfaceWaterSettings.RenderTargetResolution));
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
    if (Metadata.AssetDataVersion != CurrentAssetDataVersion)
    {
        // Legacy WCA layouts are intentionally unsupported. Do not allow an old saved
        // Data-UV status to look valid after the persistent UV-coordinate array was removed.
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
            TEXT("WetClothingAsset: '%s' uses unsupported asset schema version %d (current: %d). Recreate or regenerate its DWC Data UV and runtime outputs."),
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
        const bool bMetadataMatchesReferences = DoesMappedLODRangeHavePayload(
            Metadata.DWCSkeletalMesh.Get(),
            Metadata.SetupSettings,
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

    const FName PropertyName = PropertyChangedEvent.GetPropertyName();
    if (PropertyName == GET_MEMBER_NAME_CHECKED(FSurfaceWaterSimulationSettings, RenderTargetResolution) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(FWCAAuthoredData, SurfaceWaterSettings))
    {
        Metadata.SetupSettings.SurfaceWaterRTResolution = DWCMapResolution::ToInt(
            DWCMapResolution::FromInt(Authored.SurfaceWaterSettings.RenderTargetResolution));
    }
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
        return true;
    }

    if (RuntimeBulkData.GetBulkDataSize() <= 0)
    {
        bRuntimeBulkDataLoaded = true;
        bRuntimeBulkDataLoadFailed = false;
        return false;
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

    bRuntimeBulkDataLoaded = true;
    bRuntimeBulkDataLoadFailed = false;
    return true;
}

#if WITH_EDITOR
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
    Derived.Inline.BakeState.SavedOutputMask &= ~RuntimeSavedOutputMask;
    Derived.Inline.BakeState.SavedOutputMask |= MetadataOutputsBeingSaved;
#endif

    if (!bRuntimeBulkDataDirty)
    {
#if WITH_EDITORONLY_DATA
        Derived.Inline.BakeState.SavedOutputMask |= RuntimeOutputsBeingSaved;
#endif
        return;
    }

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
    SerializeRuntimeBulkPayload(Writer, Derived.Bulk.NeighborRuntimeData, Derived.Bulk.GPURuntimeData, SlowTask.Get());
    if (Writer.IsError() || Bytes.IsEmpty())
    {
        return;
    }

#if WITH_EDITORONLY_DATA
    Derived.Inline.BakeState.SavedOutputMask |= RuntimeOutputsBeingSaved;
#endif

    if (SlowTask.IsValid())
    {
        SlowTask->EnterProgressFrame(
            1.0f,
            FText::FromString(FString::Printf(
                TEXT("Writing %.1f MB of WCA runtime data to the asset package..."),
                static_cast<double>(Bytes.Num()) / (1024.0 * 1024.0))));
    }

    RuntimeBulkData.RemoveBulkData();
    RuntimeBulkData.SetBulkDataFlags(BULKDATA_PayloadAtEndOfFile | BULKDATA_LazyLoadable);
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
    bRuntimeBulkDataLoaded = true;
    bRuntimeBulkDataLoadFailed = false;
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
    }
}

bool UWetClothingAsset::InitializeNewAsset(
    USkeletalMesh* InSourceMesh,
    const FDWCWetClothingAssetSetupSettings& InSettings,
    FString* OutErrorMessage)
{
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
    // The source mesh is immutable input. A dedicated prepared mesh is created by the Data UV build service.
    Metadata.DWCSkeletalMesh = nullptr;
    Authored.WrinkleData.BakeSettings.DefaultResolution = Metadata.SetupSettings.GetWrinkleMapResolution();
    Authored.TransparencyData.TransparencyBakeResolution = Metadata.SetupSettings.GetTransparencyMapResolution();
    Authored.TransparencyData.RevealBakeResolution = Metadata.SetupSettings.GetTransparencyMapResolution();
    Authored.SurfaceWaterSettings.RenderTargetResolution = Metadata.SetupSettings.GetSurfaceWaterRTResolution();
    Metadata.OriginalUVChannelIndex = Metadata.SetupSettings.OriginalUVChannelIndex;
    Metadata.SetupSettings.SimulationLODIndex = RuntimeSimulationLODIndex;
    Metadata.SimulationLODIndex = RuntimeSimulationLODIndex;
    Metadata.DWCDataUVChannelIndex = INDEX_NONE;
    Derived.Inline.DataUVMetadata.Reset();
    Metadata.AssetDataVersion = CurrentAssetDataVersion;
    Derived.Inline.SourceMeshSignature = BuildMeshContentSignature(InSourceMesh, GetSimulationLODIndex(), Metadata.OriginalUVChannelIndex);

    Authored.PartData.EditableWetPartData.WettableMaterialSlots.Reset();
    Authored.PartData.EditableWetPartData.WetPartEntries.Reset();
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < InSourceMesh->GetMaterials().Num(); ++MaterialSlotIndex)
    {
        FWetClothingWettableMaterialSlotState& SlotState =
            Authored.PartData.EditableWetPartData.WettableMaterialSlots.AddDefaulted_GetRef();
        SlotState.MaterialSlotIndex = MaterialSlotIndex;
        SlotState.bIsWettableSlot = false;

        FWetClothingWetPartEntry& DefaultPart =
            Authored.PartData.EditableWetPartData.WetPartEntries.AddDefaulted_GetRef();
        DefaultPart.MaterialSlotIndex = MaterialSlotIndex;
        DefaultPart.UVChannelIndex = Metadata.OriginalUVChannelIndex;
        DefaultPart.WetPartID = 0;
        DefaultPart.DisplayName = TEXT("Part Default");
        DefaultPart.Color = FLinearColor::White;
        DefaultPart.bViewEnabled = true;
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
    if (!ValidateSetupUVChannels(GetSourceSkeletalMesh(), NewSettings, OutChangeSummary))
    {
        return false;
    }

    const FDWCWetClothingAssetSetupSettings PreviousSettings = Metadata.SetupSettings;
    const int32 PreviousOriginalUVChannelIndex = Metadata.OriginalUVChannelIndex;
    const bool bOriginalUVChanged = PreviousOriginalUVChannelIndex != NewSettings.OriginalUVChannelIndex;
    const bool bDataUVTargetChanged =
        PreviousSettings.PreferredDWCDataUVChannelIndex != NewSettings.PreferredDWCDataUVChannelIndex;
    const bool bLODRangeChanged =
        PreviousSettings.FirstGeneratedLODIndex != NewSettings.FirstGeneratedLODIndex ||
        PreviousSettings.LastGeneratedLODIndex != NewSettings.LastGeneratedLODIndex;
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
    Metadata.OriginalUVChannelIndex = Metadata.SetupSettings.OriginalUVChannelIndex;
    Metadata.SetupSettings.SimulationLODIndex = RuntimeSimulationLODIndex;
    Metadata.SimulationLODIndex = RuntimeSimulationLODIndex;
    Authored.WrinkleData.BakeSettings.DefaultResolution = Metadata.SetupSettings.GetWrinkleMapResolution();
    Authored.TransparencyData.TransparencyBakeResolution = Metadata.SetupSettings.GetTransparencyMapResolution();
    Authored.TransparencyData.RevealBakeResolution = Metadata.SetupSettings.GetTransparencyMapResolution();
    Authored.SurfaceWaterSettings.RenderTargetResolution = Metadata.SetupSettings.GetSurfaceWaterRTResolution();

    if (bOriginalUVChanged)
    {
        // Preserve user-authored part/profile properties, but discard island IDs because they
        // belong to the previous Original UV topology.
        for (FWetClothingWetPartEntry& Entry : Authored.PartData.EditableWetPartData.WetPartEntries)
        {
            if (Entry.UVChannelIndex == PreviousOriginalUVChannelIndex)
            {
                Entry.UVChannelIndex = Metadata.OriginalUVChannelIndex;
                Entry.AssignedUVIslandIDs.Reset();
            }
        }
        Authored.PartData.EditableWetPartData.SourceTextureSelections.RemoveAll(
            [PreviousOriginalUVChannelIndex](const FWetClothingSourceTextureSelection& Selection)
            {
                return Selection.UVChannelIndex == PreviousOriginalUVChannelIndex;
            });
    }

    if (bOriginalUVChanged || bDataUVTargetChanged)
    {
        Derived.Inline.BakeState.GeneratedDataUV = Derived.Inline.DataUVMetadata.IsEmpty()
            ? EDWCBakeStatus::Required
            : EDWCBakeStatus::OutOfDate;
        Derived.Inline.BakeState.OriginalUVTopology = Derived.Inline.OriginalUVTopologies.IsEmpty()
            ? EDWCBakeStatus::Required
            : EDWCBakeStatus::OutOfDate;
    }

    const bool bSimulationStructureChanged =
        bOriginalUVChanged ||
        bDataUVTargetChanged ||
        bLODRangeChanged ||
        bCPUSimulationSettingChanged ||
        bGPUSimulationSettingChanged;

    if (bSimulationStructureChanged)
    {
        MarkSimulationBakeOutOfDate();
    }
    else if (bGPUResolutionChanged)
    {
        // A resolution-only change does not invalidate CPU/GPU runtime topology.
        // It invalidates only the generated GPU simulation maps.
        Derived.Inline.BakeState.GPUMaps = Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData
            ? (HasGPUMapDataPayload() ? EDWCBakeStatus::OutOfDate : EDWCBakeStatus::Required)
            : EDWCBakeStatus::Disabled;
    }

    if (bOriginalUVChanged || bDataUVTargetChanged)
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
    if (bOriginalUVChanged)
    {
        Changes.Add(FString::Printf(TEXT("Original UV changed: UV%d -> UV%d. Existing island assignments were cleared."), PreviousOriginalUVChannelIndex, Metadata.OriginalUVChannelIndex));
    }
    if (bDataUVTargetChanged)
    {
        Changes.Add(TEXT("DWC Data UV target changed. Rebuild is required."));
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

void UWetClothingAsset::SetGeneratedDataUVTarget(USkeletalMesh* InRuntimeMesh, const int32 InDWCDataUVChannelIndex)
{
    Metadata.DWCSkeletalMesh = InRuntimeMesh;
    Metadata.DWCDataUVChannelIndex = InDWCDataUVChannelIndex;
    MarkSimulationBakeOutOfDate();
}

void UWetClothingAsset::SetDataUVMetadata(TArray<FDWCDataUVLODMetadata>&& InMetadata)
{
    Derived.Inline.DataUVMetadata = MoveTemp(InMetadata);
    Derived.Inline.BakeState.GeneratedDataUV = Derived.Inline.DataUVMetadata.IsEmpty() ? EDWCBakeStatus::Required : EDWCBakeStatus::Valid;
    if (!Derived.Inline.DataUVMetadata.IsEmpty())
    {
        MarkBakeOutputGenerated(DWCBakeOutput::GeneratedDataUV);
    }
    MarkSimulationBakeOutOfDate();
}

void UWetClothingAsset::SetOriginalUVTopologies(TArray<FDWCEditorUVTopologyData>&& InTopologies)
{
    Derived.Inline.OriginalUVTopologies = MoveTemp(InTopologies);
    Derived.Inline.BakeState.OriginalUVTopology = Derived.Inline.OriginalUVTopologies.IsEmpty() ? EDWCBakeStatus::Required : EDWCBakeStatus::Valid;
    if (!Derived.Inline.OriginalUVTopologies.IsEmpty())
    {
        MarkBakeOutputGenerated(DWCBakeOutput::OriginalUVTopology);
    }
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
        Derived.Inline.BakeState.GeneratedDataUV = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.OriginalUVTopology = EDWCBakeStatus::Disabled;
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
    auto PreserveFailureStatus = [this](const EDWCBakeStatus PreviousStatus, const EDWCBakeStatus NewStatus)
    {
        return PreviousStatus == EDWCBakeStatus::Failed &&
               NewStatus != EDWCBakeStatus::Valid &&
               !Derived.Inline.BakeState.LastFailure.IsEmpty()
                   ? EDWCBakeStatus::Failed
                   : NewStatus;
    };

    const bool bHasWettableSlots = HasAnyWettableMaterialSlot();
    if (!bHasWettableSlots)
    {
        Derived.Inline.BakeState.GeneratedDataUV = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.OriginalUVTopology = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.CPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPURuntimeData = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.WrinkleMaps = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.TransparencyMaps = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.LastFailure.Reset();
        return;
    }

    USkeletalMesh* RuntimeMesh = GetRuntimeSkeletalMesh();
    const bool bDataUVMetadataValid = DoesMappedLODRangeHavePayload(
        RuntimeMesh,
        Metadata.SetupSettings,
        [this, RuntimeMesh, bRunDeepValidation](const int32 LODIndex)
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
        });
    Derived.Inline.BakeState.GeneratedDataUV = bDataUVMetadataValid
                                     ? EDWCBakeStatus::Valid
                                     : (!Derived.Inline.DataUVMetadata.IsEmpty() ? EDWCBakeStatus::OutOfDate : EDWCBakeStatus::Required);

    const int32 CanonicalTopologyLODIndex = RuntimeSimulationLODIndex;
    bool bTopologyValid = false;
    if (RuntimeMesh != nullptr && GetLODCount(RuntimeMesh) > CanonicalTopologyLODIndex)
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

    const bool bCPUDataValid = bRunDeepValidation
                                   ? IsPrecomputedSimulationDataValidForMesh(RuntimeMesh)
                                   : IsPrecomputedSimulationDataMetadataValidForMesh(RuntimeMesh);
    const bool bGPUDataValid = DoesMappedLODRangeHavePayload(
        RuntimeMesh,
        Metadata.SetupSettings,
        [this, RuntimeMesh, bRunDeepValidation](const int32 LODIndex)
        {
            return bRunDeepValidation
                ? IsGPURuntimeDataValidForMesh(RuntimeMesh, LODIndex)
                : IsGPURuntimeDataMetadataValidForMesh(RuntimeMesh, LODIndex);
        });
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
    const bool bGPUMapDataValid = DoesMappedLODRangeHavePayload(
        RuntimeMesh,
        Metadata.SetupSettings,
        [this, RuntimeMesh, bRunDeepValidation](const int32 LODIndex)
        {
            return bRunDeepValidation
                ? IsGPUWetMapDataValidForMesh(RuntimeMesh, LODIndex)
                : IsGPUWetMapDataMetadataValidForMesh(RuntimeMesh, LODIndex);
        });
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
    else if (Derived.Inline.BakeState.TransparencyMaps == EDWCBakeStatus::Disabled)
    {
        Derived.Inline.BakeState.TransparencyMaps = HasSavedBakeOutput(DWCBakeOutput::TransparencyMaps)
                                         ? EDWCBakeStatus::OutOfDate
                                         : EDWCBakeStatus::Required;
    }
}

bool UWetClothingAsset::RebuildGPURuntimeData(FString* OutErrorMessage)
{
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
    int32 FirstLODIndex = 0;
    int32 LastLODIndex = 0;
    if (!ResolveSetupLODRangeForMesh(RuntimeMesh, Metadata.SetupSettings, FirstLODIndex, LastLODIndex, OutErrorMessage))
    {
        return false;
    }

    FScopedSlowTask SlowTask(
        6.0f * FMath::Max(1, LastLODIndex - FirstLODIndex + 1),
        FText::FromString(FString::Printf(TEXT("Rebuilding DWC GPU runtime data for LOD%d-LOD%d..."), FirstLODIndex, LastLODIndex)));
    SlowTask.MakeDialog(false);

    for (int32 LODIndex = FirstLODIndex; LODIndex <= LastLODIndex; ++LODIndex)
    {
        SlowTask.EnterProgressFrame(
            0.5f,
            FText::FromString(FString::Printf(TEXT("Checking generated DWC Data UV for LOD%d before GPU runtime rebuild..."), LODIndex)));

        if (!HasValidDataUVForLOD(LODIndex))
        {
            DWC::Error::SetMessage(OutErrorMessage, FString::Printf(TEXT("Generate valid DWC Data UV payloads for LOD%d before rebuilding GPU runtime data."), LODIndex));
            return false;
        }

        if (!FWetGPUMapBakeBuilder::BuildRuntimeLOD(*this, LODIndex, OutErrorMessage, &SlowTask))
        {
            return false;
        }

        SlowTask.EnterProgressFrame(
            0.5f,
            FText::FromString(FString::Printf(TEXT("Finalizing LOD%d GPU runtime triangles and vertex incident tables..."), LODIndex)));
    }

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

    USkeletalMesh* RuntimeMesh = GetRuntimeSkeletalMesh();
    int32 FirstLODIndex = 0;
    int32 LastLODIndex = 0;
    if (!ResolveSetupLODRangeForMesh(RuntimeMesh, Metadata.SetupSettings, FirstLODIndex, LastLODIndex, OutErrorMessage))
    {
        return false;
    }

    FScopedSlowTask SlowTask(
        7.25f * FMath::Max(1, LastLODIndex - FirstLODIndex + 1),
        FText::FromString(FString::Printf(TEXT("Baking DWC GPU simulation maps for LOD%d-LOD%d..."), FirstLODIndex, LastLODIndex)));
    SlowTask.MakeDialog(false);

    for (int32 LODIndex = FirstLODIndex; LODIndex <= LastLODIndex; ++LODIndex)
    {
        SlowTask.EnterProgressFrame(
            0.5f,
            FText::FromString(FString::Printf(TEXT("Preparing LOD%d GPU simulation map bake inputs..."), LODIndex)));

        if (!FWetGPUMapBakeBuilder::BuildLODMaps(*this, LODIndex, OutErrorMessage, &SlowTask))
        {
            Derived.Inline.BakeState.GPUMaps = EDWCBakeStatus::Failed;
            Derived.Inline.BakeState.LastFailure = OutErrorMessage != nullptr ? *OutErrorMessage : FString();
            return false;
        }

        SlowTask.EnterProgressFrame(
            0.5f,
            FText::FromString(FString::Printf(TEXT("Saving LOD%d GPU simulation map data into the WCA runtime payload..."), LODIndex)));
    }

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
    constexpr int32 RuntimeOutputMask =
        DWCBakeOutput::CPURuntimeData |
        DWCBakeOutput::GPURuntimeData |
        DWCBakeOutput::GPUMaps;
    const bool bCPUDataCurrent =
        !Metadata.SetupSettings.bBuildCPUVertexSimulationData ||
        DWCBuildStatus::IsUsable(Derived.Inline.BakeState.CPURuntimeData);
    const bool bGPUDataCurrent =
        !Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData ||
        DWCBuildStatus::IsUsable(Derived.Inline.BakeState.GPURuntimeData);
    const bool bLODVertexColorDataCurrent = FWCALODVertexColorBuilder::IsCurrent(
        RuntimeMesh,
        Metadata.SetupSettings.FirstGeneratedLODIndex,
        Metadata.SetupSettings.LastGeneratedLODIndex,
        Derived.Bulk.LODVertexColorRuntimeData);
    const bool bHasPendingRuntimeOutput = (PendingRuntimeSaveOutputMask & RuntimeOutputMask) != 0;
    if (HasAnyWettableMaterialSlot() &&
        !bRuntimeBulkDataDirty &&
        !bHasPendingRuntimeOutput &&
        bCPUDataCurrent &&
        bGPUDataCurrent &&
        bLODVertexColorDataCurrent)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
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
    int32 FirstMappedLODIndex = RuntimeLODIndex;
    int32 LastMappedLODIndex = RuntimeLODIndex;
    ResolveSetupLODRangeForMesh(RuntimeMesh, Metadata.SetupSettings, FirstMappedLODIndex, LastMappedLODIndex);

    if (!HasAnyWettableMaterialSlot())
    {
        SlowTask.EnterProgressFrame(3.75f, NSLOCTEXT("WetClothingAsset", "ClearUnusedRuntimeDataForSave", "Clearing runtime data because no material slot is wettable..."));
        ClearPrecomputedSimulationData();
        ClearGPUWetMapData();
        Derived.Inline.ResolvedWetnessProfileParameters.Reset();
        Derived.Inline.GeneratedWetMaterialOverrides.Reset();
        Derived.Inline.BakedWetnessProfileMaps.Reset();
        Authored.WrinkleData.BakedWrinkleMaps.Reset();
        Authored.TransparencyData.BakedRevealLayers.Reset();
        for (FWetClothingTransparencyLayerData& Layer : Authored.TransparencyData.TransparencyLayers)
        {
            Layer.BakedMaps.Reset();
            Layer.AutoBakeMetadata = FWetClothingTransparencyAutoBakeMetadata();
        }
        Derived.Inline.BakeState.GeneratedDataUV = EDWCBakeStatus::Disabled;
        Derived.Inline.BakeState.OriginalUVTopology = EDWCBakeStatus::Disabled;
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

    ResolveWetnessProfilesForDerivedInline(
        Authored.PartData.EditableWetPartData,
        Derived.Inline.ResolvedWetnessProfileParameters);

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
        if (IsPrecomputedSimulationDataValidForMesh(RuntimeMesh))
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
        const bool bMappedGPUDataValid = DoesMappedLODRangeHavePayload(
            RuntimeMesh,
            Metadata.SetupSettings,
            [this, RuntimeMesh](const int32 LODIndex)
            {
                return IsGPURuntimeDataValidForMesh(RuntimeMesh, LODIndex);
            });
        if (bMappedGPUDataValid)
        {
            SlowTask.EnterProgressFrame(
                1.5f,
                FText::FromString(FString::Printf(
                    TEXT("GPU wetness-map runtime data for LOD%d-LOD%d is already current..."),
                    FirstMappedLODIndex,
                    LastMappedLODIndex)));
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
                    TEXT("Building GPU wetness-map runtime data for LOD%d-LOD%d..."),
                    FirstMappedLODIndex,
                    LastMappedLODIndex)));
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

    if (!Metadata.SetupSettings.bBuildCPUVertexSimulationData && !Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData)
    {
        DWC::Error::SetMessage(OutSkipReason, TEXT("No DWC runtime data backend is enabled."));
        return false;
    }

    if (GetRuntimeSkeletalMesh() == nullptr)
    {
        DWC::Error::SetMessage(OutSkipReason, TEXT("No runtime skeletal mesh is assigned."));
        return false;
    }

    const bool bRequiresDWCDataUV = HasAnyWettableMaterialSlot() &&
        (Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData || Authored.SurfaceWaterSettings.bEnabled);
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
            DWC::Error::SetMessage(OutSkipReason, TEXT("DWC Data UV has not been generated for every mapped LOD yet."));
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

    return Data.DataVersion == CurrentPrecomputedSimulationDataVersion &&
           Data.LODIndex == LODIndex &&
           Data.VertexCount == LODData.GetNumVertices() &&
           bPayloadAvailable &&
           Data.MeshSignature == FDWCMeshContentSignature::BuildStructure(SkeletalMesh, LODData, LODIndex) &&
           Data.SourceDataSignature == MakeSourceDataSignature(Authored.PartData.EditableWetPartData, Authored.SurfaceWaterSettings);
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
    const FString ExpectedSourceSignature = MakeSourceDataSignature(Authored.PartData.EditableWetPartData, Authored.SurfaceWaterSettings);

    const bool bVersionMatches = Data.DataVersion == CurrentPrecomputedSimulationDataVersion;
    const bool bLODMatches = Data.LODIndex == LODIndex;
    const bool bVertexCountMatches = ExpectedVertexCount != INDEX_NONE && Data.VertexCount == ExpectedVertexCount;
    const bool bVertexPayloadMatches = ExpectedVertexCount != INDEX_NONE && Data.Vertices.Num() == ExpectedVertexCount;
    const bool bNeighborPayloadMatches = ExpectedVertexCount != INDEX_NONE && Data.NeighborGraph.Num() == ExpectedVertexCount;
    const bool bHasRuntimePayload = bVertexPayloadMatches && bNeighborPayloadMatches;
    const bool bMeshSignatureMatches = !ExpectedMeshSignature.IsEmpty() && Data.MeshSignature == ExpectedMeshSignature;
    const bool bSourceSignatureMatches = Data.SourceDataSignature == ExpectedSourceSignature;

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
    if (Data->RuntimeDataVersion != FDWCGPULODBakeData::CurrentRuntimeDataVersion ||
        Data->BulkDataVersion != FDWCGPULODBakeData::CurrentBulkDataVersion ||
        Data->LODIndex != LODIndex ||
        ExpectedDataUVMeshSignature.IsEmpty() ||
        Data->MeshSignature != ExpectedDataUVMeshSignature ||
        Data->ProfileCount <= 0 ||
        Data->TriangleCount <= 0 ||
        Data->VertexIncidentRecordCount <= 0 ||
        (Data->Triangles.Num() == 0 && !HasRuntimeBulkPayload()))
    {
        return false;
    }

#if WITH_EDITOR
    FString RuntimeSignature;
    if (!FWetGPUMapBakeBuilder::BuildLODRuntimeSignature(*this, LODIndex, RuntimeSignature, nullptr))
    {
        return false;
    }

    return Data->RuntimeSignature == RuntimeSignature;
#else
    return true;
#endif
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
           Data->MaterialSlotMapCount > 0 &&
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

    if (Data->MapBakeVersion != FDWCGPULODBakeData::CurrentMapBakeVersion ||
        Data->MaterialSlotMapCount <= 0 ||
        (Data->MaterialSlots.Num() == 0 && !HasRuntimeBulkPayload()))
    {
        return false;
    }

#if WITH_EDITOR
    FString MapSignature;
    if (!FWetGPUMapBakeBuilder::BuildLODMapSignature(*this, LODIndex, MapSignature, nullptr))
    {
        return false;
    }

    return Data->MapSignature == MapSignature;
#else
    return true;
#endif
}

#if WITH_EDITOR
bool UWetClothingAsset::RebuildPrecomputedSimulationData(FString* OutErrorMessage)
{
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

    Derived.Inline.BakedWetnessProfileMaps.RemoveAll(
        [this](const FWetClothingBakedWetnessProfileMap& BakedMap)
        {
            for (const int32 MaterialSlotIndex : BakedMap.MaterialSlotIndices)
            {
                if (IsWettableMaterialSlot(Authored.PartData.EditableWetPartData, MaterialSlotIndex))
                {
                    return false;
                }
            }
            return true;
        });

    Derived.Bulk.NeighborRuntimeData.bIsValid = true;
    Derived.Bulk.NeighborRuntimeData.LODIndex = LODIndex;
    Derived.Bulk.NeighborRuntimeData.VertexCount = LODData.GetNumVertices();
    Derived.Bulk.NeighborRuntimeData.MeshSignature = FDWCMeshContentSignature::BuildStructure(RuntimeMesh, LODData, LODIndex);
    Derived.Bulk.NeighborRuntimeData.SourceDataSignature = MakeSourceDataSignature(Authored.PartData.EditableWetPartData, Authored.SurfaceWaterSettings);
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
            TEXT("Original UV topology is missing or out of date. Rebuild DWC Data UV before rebuilding runtime data."));
        ClearPrecomputedSimulationData();
        return false;
    }

    TMap<FWetPartScopeKey, TArray<int32>> EntryIndicesByScope;
    for (int32 EntryIndex = 0; EntryIndex < Authored.PartData.EditableWetPartData.WetPartEntries.Num(); ++EntryIndex)
    {
        const FWetClothingWetPartEntry& Entry = Authored.PartData.EditableWetPartData.WetPartEntries[EntryIndex];
        if (Entry.MaterialSlotIndex == INDEX_NONE ||
            Entry.UVChannelIndex == INDEX_NONE ||
            !IsWettableMaterialSlot(Authored.PartData.EditableWetPartData, Entry.MaterialSlotIndex))
        {
            continue;
        }

        FWetPartScopeKey ScopeKey;
        ScopeKey.MaterialSlotIndex = Entry.MaterialSlotIndex;
        ScopeKey.UVChannelIndex = Entry.UVChannelIndex;
        EntryIndicesByScope.FindOrAdd(ScopeKey).Add(EntryIndex);
    }

    for (const TPair<FWetPartScopeKey, TArray<int32>>& ScopePair : EntryIndicesByScope)
    {
        TArray<FDWCRuntimeTopologyTriangle> RawTriangles;
        if (!FDWCOriginalUVRuntimeTopologyAdapter::ReadMaterialSlotTriangles(
                RuntimeMesh,
                LODData,
                IndexBuffer,
                ScopePair.Key.UVChannelIndex,
                ScopePair.Key.MaterialSlotIndex,
                RawTriangles,
                OutErrorMessage))
        {
            ClearPrecomputedSimulationData();
            return false;
        }

        if (ScopePair.Key.UVChannelIndex != OriginalUVTopology->UVChannelIndex)
        {
            DWC::Error::SetMessage(
                OutErrorMessage,
                TEXT("Wet Part data references a UV channel that is not the WCA Original UV channel."));
            ClearPrecomputedSimulationData();
            return false;
        }

        TArray<FDWCRuntimeOriginalUVIsland> Islands;
        if (!FDWCOriginalUVRuntimeTopologyAdapter::BuildIslands(
                RawTriangles,
                *OriginalUVTopology,
                ScopePair.Key.MaterialSlotIndex,
                Islands,
                OutErrorMessage))
        {
            ClearPrecomputedSimulationData();
            return false;
        }

        int32              DefaultEntryIndex = INDEX_NONE;
        TMap<int32, int32> AssignedUVIslandToEntryIndex;

        for (int32 EntryIndex : ScopePair.Value)
        {
            const FWetClothingWetPartEntry& Entry = Authored.PartData.EditableWetPartData.WetPartEntries[EntryIndex];
            if (Entry.WetPartID == 0)
            {
                DefaultEntryIndex = EntryIndex;
                continue;
            }

            for (int32 UVIslandID : Entry.AssignedUVIslandIDs)
            {
                AssignedUVIslandToEntryIndex.FindOrAdd(UVIslandID) = EntryIndex;
            }
        }

        for (const FDWCRuntimeOriginalUVIsland& Island : Islands)
        {
            const int32* AssignedEntryIndex = AssignedUVIslandToEntryIndex.Find(Island.UVIslandID);
            const int32  EffectiveEntryIndex = AssignedEntryIndex != nullptr ? *AssignedEntryIndex : DefaultEntryIndex;

            if (!Authored.PartData.EditableWetPartData.WetPartEntries.IsValidIndex(EffectiveEntryIndex))
            {
                continue;
            }

            const FWetClothingWetPartEntry& Entry = Authored.PartData.EditableWetPartData.WetPartEntries[EffectiveEntryIndex];
            for (int32 VertexIndex : Island.VertexIndices)
            {
                if (!Derived.Bulk.NeighborRuntimeData.Vertices.IsValidIndex(VertexIndex))
                {
                    continue;
                }

                FWetClothingPrecomputedVertexData& VertexData = Derived.Bulk.NeighborRuntimeData.Vertices[VertexIndex];
                // Vertex-only contacts have no section context. Use the lowest slot as a deterministic primary binding.
                if (VertexData.MaterialSlotIndex != INDEX_NONE && VertexData.MaterialSlotIndex <= ScopePair.Key.MaterialSlotIndex)
                {
                    continue;
                }
                VertexData.WetPartID = Entry.WetPartID;
                VertexData.WetPartEntryIndex = EffectiveEntryIndex;
                VertexData.MaterialSlotIndex = ScopePair.Key.MaterialSlotIndex;
                VertexData.UVChannelIndex = ScopePair.Key.UVChannelIndex;
                VertexData.UVIslandID = Island.UVIslandID;
                VertexData.bIsWettable = true;
            }
        }
    }

    if (Authored.SurfaceWaterSettings.bEnabled)
    {
        FDWCDataUVBufferView DataUVView;
        FString DataUVError;
        if (!DataUVView.Initialize(GetDWCSkeletalMesh(), LODIndex, Metadata.DWCDataUVChannelIndex, &DataUVError) ||
            DataUVView.NumVertices() != static_cast<int32>(LODData.GetNumVertices()))
        {
            const FString ErrorMessage = FString::Printf(
                TEXT("Surface Water requires valid DWC Data UV. Rebuild DWC Data UV before rebuilding runtime data. %s"),
                *DataUVError);
            DWC::Error::SetMessage(OutErrorMessage, *ErrorMessage);
            ClearPrecomputedSimulationData();
            return false;
        }

        for (int32 VertexIndex = 0; VertexIndex < Derived.Bulk.NeighborRuntimeData.Vertices.Num(); ++VertexIndex)
        {
            FWetClothingPrecomputedVertexData& Vertex = Derived.Bulk.NeighborRuntimeData.Vertices[VertexIndex];
            if (!Vertex.bIsWettable || Vertex.MaterialSlotIndex == INDEX_NONE) continue;
            const FSurfaceWaterMaterialSlotData* SlotData = Authored.SurfaceWaterSettings.FindMaterialSlot(Vertex.MaterialSlotIndex);
            if (SlotData && !SlotData->bEnabled) continue;
            if (!DataUVView.IsValidVertexIndex(VertexIndex)) continue;
            const FVector2f UV = DataUVView.GetUV(VertexIndex);
            if (UV.ContainsNaN() || !FMath::IsFinite(UV.X) || !FMath::IsFinite(UV.Y) || UV.X < 0.f || UV.X > 1.f || UV.Y < 0.f || UV.Y > 1.f) continue;
            Vertex.SurfaceWaterUV = FVector2D(UV);
            Vertex.bHasSurfaceWaterUV = true;
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
