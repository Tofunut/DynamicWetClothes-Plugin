// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Diagnostics/DWCEditorAuthoringPayloadDiagnostics.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCEditorAuthoringPayload, Log, All);

namespace DWCEditorAuthoringPayloadDiagnosticsPrivate
{
    constexpr int32 MaxRecentOperations = 64;
    constexpr int32 MaxRecentLoads = 256;

    struct FAuthoringObservedLoad
    {
        FString ObjectPath;
        FString ClassName;
        FString Context;
        uint64 OperationId = 0;
        bool bGameThread = false;
    };

    struct FAuthoringActiveOperation
    {
        FDWCEditorAuthoringOperationRecord Record;
        double StartSeconds = 0.0;
        TSet<FString> ObservedObjectPaths;
    };

    struct FAuthoringPayloadState
    {
        FCriticalSection Mutex;
        FDelegateHandle AssetLoadedHandle;
        TMap<uint64, FAuthoringActiveOperation> ActiveOperations;
        TArray<uint64> ActiveOperationStack;
        TArray<FDWCEditorAuthoringOperationRecord> RecentOperations;
        TArray<FAuthoringObservedLoad> RecentLoads;
        uint64 NextOperationId = 1;
    };

    FAuthoringPayloadState& GetAuthoringPayloadState()
    {
        static FAuthoringPayloadState* State = new FAuthoringPayloadState();
        return *State;
    }

    TAutoConsoleVariable<int32> CVarEnable(
        TEXT("dwc.Editor.AuthoringPayload.Enable"),
        0,
        TEXT("Enables WCA authoring payload and scoped load diagnostics."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarTraceLoads(
        TEXT("dwc.Editor.AuthoringPayload.TraceLoads"),
        1,
        TEXT("Records UObject load notifications while WCA authoring diagnostics are enabled."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarTraceOperations(
        TEXT("dwc.Editor.AuthoringPayload.TraceOperations"),
        1,
        TEXT("Logs completed WCA editor operation scopes while authoring diagnostics are enabled."),
        ECVF_Default);

    FString FormatPayloadBytes(const uint64 Bytes)
    {
        return FString::Printf(TEXT("%.2f MiB"), static_cast<double>(Bytes) / (1024.0 * 1024.0));
    }

    void AddStringBytes(uint64& Bytes, const FString& Value)
    {
        Bytes += Value.GetAllocatedSize();
    }

    void AddHardReference(
        const UObject* Object,
        TSet<FString>& HardObjects,
        TSet<FString>& Textures)
    {
        if (Object == nullptr)
        {
            return;
        }
        const FString Path = Object->GetPathName();
        HardObjects.Add(Path);
        if (Object->IsA<UTexture>())
        {
            Textures.Add(Path);
        }
    }

    template <typename ObjectType>
    void AddSoftReference(
        const TSoftObjectPtr<ObjectType>& Reference,
        TSet<FString>& SoftObjects,
        TSet<FString>& Textures)
    {
        const FSoftObjectPath Path = Reference.ToSoftObjectPath();
        if (!Path.IsValid())
        {
            return;
        }
        const FString PathString = Path.ToString();
        SoftObjects.Add(PathString);
        if constexpr (TIsDerivedFrom<ObjectType, UTexture>::IsDerived)
        {
            Textures.Add(PathString);
        }
    }

    template <typename ObjectType>
    void AddSoftReference(
        const TSoftClassPtr<ObjectType>& Reference,
        TSet<FString>& SoftObjects,
        TSet<FString>&)
    {
        const FSoftObjectPath Path = Reference.ToSoftObjectPath();
        if (Path.IsValid())
        {
            SoftObjects.Add(Path.ToString());
        }
    }

    uint64 CountWetPartPayload(
        const FWetClothingPartData& PartData,
        FDWCEditorAuthoringPayloadSnapshot& Snapshot,
        TSet<FString>& HardObjects,
        TSet<FString>& SoftObjects,
        TSet<FString>& Textures)
    {
        const FWetClothingEditableWetPartData& Editable = PartData.EditableWetPartData;
        uint64 Bytes = Editable.MaterialSlots.GetAllocatedSize() + Editable.Profiles.GetAllocatedSize();
        for (const FWetClothingAuthoredMaterialSlot& Slot : Editable.MaterialSlots)
        {
#if WITH_EDITORONLY_DATA
            AddHardReference(Slot.SourceTexture, HardObjects, Textures);
#endif
            Bytes += Slot.WetPartEntries.GetAllocatedSize();
            Snapshot.WetPartCount += Slot.WetPartEntries.Num();
            for (const FWetClothingWetPartEntry& Part : Slot.WetPartEntries)
            {
                AddStringBytes(Bytes, Part.DisplayName);
                Bytes += Part.AssignedUVIslandIDs.GetAllocatedSize();
            }
        }
        for (const FWetPartProfileAssignment& Profile : Editable.Profiles)
        {
            AddSoftReference(Profile.SourceProfileAsset, SoftObjects, Textures);
            AddHardReference(Profile.Parameters.SurfaceWater.DropletNormalTexture, HardObjects, Textures);
            AddHardReference(Profile.Parameters.SurfaceWater.DropletMaskTexture, HardObjects, Textures);
            AddHardReference(Profile.Parameters.SurfaceWater.DropletFlowNormalTexture, HardObjects, Textures);
            AddHardReference(Profile.Parameters.SurfaceWater.DropletFlowMaskTexture, HardObjects, Textures);
        }
        return Bytes;
    }

    uint64 CountTopologyPayload(
        const TArray<FDWCEditorUVTopologyDescriptor>& Topologies,
        FDWCEditorAuthoringPayloadSnapshot& Snapshot)
    {
        uint64 Bytes = Topologies.GetAllocatedSize();
        Snapshot.OriginalUVTopologyCount = Topologies.Num();
        for (const FDWCEditorUVTopologyDescriptor& Topology : Topologies)
        {
            AddStringBytes(Bytes, Topology.BuildSignature);
            Snapshot.OriginalUVTriangleReferenceCount += Topology.TriangleReferenceCount;
        }
        return Bytes;
    }

    uint64 CountWrinklePayload(
        const FWetClothingWrinkleData& WrinkleData,
        FDWCEditorAuthoringPayloadSnapshot& Snapshot,
        TSet<FString>& HardObjects,
        TSet<FString>& SoftObjects,
        TSet<FString>& Textures)
    {
        uint64 Bytes = WrinkleData.EditablePatches.GetAllocatedSize() +
            WrinkleData.EditableProceduralRidgeStrokes.GetAllocatedSize() +
            WrinkleData.RuntimeNormalSources.GetAllocatedSize() +
            WrinkleData.BakedWrinkleMaps.GetAllocatedSize();
        Snapshot.WrinklePatchCount = WrinkleData.EditablePatches.Num();
        Snapshot.RidgeStrokeCount = WrinkleData.EditableProceduralRidgeStrokes.Num();

        for (const FWetWrinklePatchPlacement& Patch : WrinkleData.EditablePatches)
        {
            AddStringBytes(Bytes, Patch.DisplayName);
            AddSoftReference(Patch.WrinkleNormalTexture, SoftObjects, Textures);
        }
        for (const FWetProceduralRidgeStroke& Stroke : WrinkleData.EditableProceduralRidgeStrokes)
        {
            AddStringBytes(Bytes, Stroke.DisplayName);
            Bytes += Stroke.Points.GetAllocatedSize();
            Snapshot.RidgePointCount += Stroke.Points.Num();
        }
        for (const FWetWrinkleRuntimeNormalSource& Source : WrinkleData.RuntimeNormalSources)
        {
            AddHardReference(Source.CustomWrinkleNormalMap, HardObjects, Textures);
        }
        for (const FWetWrinkleBakedMapSet& Map : WrinkleData.BakedWrinkleMaps)
        {
            AddHardReference(Map.BakedWrinkleNormalMap, HardObjects, Textures);
#if WITH_EDITORONLY_DATA
            AddSoftReference(Map.BakedWrinkleMask, SoftObjects, Textures);
#endif
            AddStringBytes(Bytes, Map.BuildSignature);
        }
        return Bytes;
    }

    void CountStageArtifact(
        const FDWCTransparencyTempArtifactReference& Artifact,
        uint64& Bytes,
        FDWCEditorAuthoringPayloadSnapshot& Snapshot,
        TSet<FString>& SoftObjects,
        TSet<FString>& Textures)
    {
        AddStringBytes(Bytes, Artifact.BuildSignature);
        AddSoftReference(Artifact.Texture, SoftObjects, Textures);
        ++Snapshot.TransparencyStageArtifactCount;
    }

    uint64 CountTransparencyPayload(
        const FWetClothingTransparencyData& Data,
        FDWCEditorAuthoringPayloadSnapshot& Snapshot,
        TSet<FString>& HardObjects,
        TSet<FString>& SoftObjects,
        TSet<FString>& Textures)
    {
        uint64 Bytes = Data.TransparencyLayers.GetAllocatedSize();
        uint64 StageBytes = 0;
        Snapshot.TransparencyLayerCount = Data.TransparencyLayers.Num();

        AddSoftReference(Data.SourceBlueprintClass, SoftObjects, Textures);
        for (const FWetClothingTransparencyLayerData& Layer : Data.TransparencyLayers)
        {
            Bytes += Layer.SameMeshSource.InnerSlotPriority.GetAllocatedSize();
            Bytes += Layer.BlueprintSource.SourcePriority.GetAllocatedSize();
            Bytes += Layer.ExternalMeshSource.SourcePriority.GetAllocatedSize();
            Bytes += Layer.ExternalMeshSource.SourceSlotPriority.GetAllocatedSize();
            AddSoftReference(Layer.BlueprintSource.BlueprintClass, SoftObjects, Textures);
            AddSoftReference(Layer.BlueprintSource.TargetComponent.ExpectedSkeletalMesh, SoftObjects, Textures);
            for (const FWetClothingTransparencyBlueprintComponentBinding& Binding : Layer.BlueprintSource.SourcePriority)
            {
                AddSoftReference(Binding.ExpectedSkeletalMesh, SoftObjects, Textures);
            }
            for (const FWetClothingTransparencyExternalMeshEntry& Entry : Layer.ExternalMeshSource.SourcePriority)
            {
                AddHardReference(Entry.SkeletalMesh, HardObjects, Textures);
            }
            AddHardReference(Layer.ExternalMeshSource.SkeletalMesh, HardObjects, Textures);
            AddSoftReference(Layer.ManualColorSource.SampledColorTexture, SoftObjects, Textures);

            const TArray<FDWCTransparencyBrushStroke>& AlphaStrokes = Layer.GetEditableStrokes();
            Bytes += AlphaStrokes.GetAllocatedSize();
            Snapshot.TransparencyAlphaStrokeCount += AlphaStrokes.Num();
            for (const FDWCTransparencyBrushStroke& Stroke : AlphaStrokes)
            {
                AddStringBytes(Bytes, Stroke.DisplayName);
                Bytes += Stroke.GetSampleAllocatedSize();
                Snapshot.TransparencySampleCount += Stroke.GetSampleCount();
            }
            const TArray<FDWCTransparencyRevealColorStroke>& RevealStrokes = Layer.GetRevealColorPaintStrokes();
            Bytes += RevealStrokes.GetAllocatedSize();
            Snapshot.TransparencyRevealStrokeCount += RevealStrokes.Num();
            for (const FDWCTransparencyRevealColorStroke& Stroke : RevealStrokes)
            {
                Bytes += Stroke.GetSampleAllocatedSize();
                Snapshot.TransparencySampleCount += Stroke.GetSampleCount();
            }
            AddStringBytes(Bytes, Layer.AutoBakeMetadata.BuildSignature);
            Bytes += Layer.BakedMaps.GetAllocatedSize();
            for (const FWetClothingBakedTransparencyMap& Map : Layer.BakedMaps)
            {
                AddHardReference(Map.TransparencyMap, HardObjects, Textures);
                AddHardReference(Map.RevealNormalMap, HardObjects, Textures);
                AddStringBytes(Bytes, Map.BuildSignature);
                AddStringBytes(Bytes, Map.FinalAlphaBuildSignature);
                AddStringBytes(Bytes, Map.RevealNormalBuildSignature);
                AddStringBytes(Bytes, Map.SourceWrinkleMaskBuildSignature);
                AddStringBytes(Bytes, Map.WrinkleSuppressionSettingsSignature);
#if WITH_EDITORONLY_DATA
                AddHardReference(Map.RevealSurfaceMap, HardObjects, Textures);
                AddStringBytes(Bytes, Map.RevealSurfaceBuildSignature);
#endif
            }
#if WITH_EDITORONLY_DATA
            AddStringBytes(StageBytes, Layer.EditorStageCache.MaterialBakeSignature);
            AddStringBytes(StageBytes, Layer.EditorStageCache.SourceSignature);
            AddStringBytes(StageBytes, Layer.EditorStageCache.RevealSignature);
            StageBytes += Layer.EditorStageCache.Artifacts.GetAllocatedSize();
            for (const FDWCTransparencyTempArtifactReference& Artifact : Layer.EditorStageCache.Artifacts)
            {
                CountStageArtifact(Artifact, StageBytes, Snapshot, SoftObjects, Textures);
            }
#endif
        }

#if WITH_EDITORONLY_DATA
        StageBytes += Data.MaterialColorCache.GetAllocatedSize();
        Snapshot.MaterialSurfaceCacheEntryCount = Data.MaterialColorCache.Num();
        for (const FDWCTransparencyMaterialColorCacheReference& Entry : Data.MaterialColorCache)
        {
            AddSoftReference(Entry.SourceMesh, SoftObjects, Textures);
            AddSoftReference(Entry.Texture, SoftObjects, Textures);
            AddSoftReference(Entry.NormalTexture, SoftObjects, Textures);
            AddSoftReference(Entry.MetallicTexture, SoftObjects, Textures);
            AddStringBytes(StageBytes, Entry.MaterialBakeSignature);
            AddStringBytes(StageBytes, Entry.CacheIdentity);
            AddStringBytes(StageBytes, Entry.SourceMeshContentSignature);
            AddStringBytes(StageBytes, Entry.EffectiveMaterialSignature);
            AddStringBytes(StageBytes, Entry.PlacementSignature);
        }
#endif
        Snapshot.TransparencyStageMetadataBytes = StageBytes;
        return Bytes + StageBytes;
    }

    void AddObservedLoad(
        const UObject* Object,
        const FString& Context,
        const uint64 ExplicitOperationId = 0)
    {
        if (!FDWCEditorAuthoringPayloadDiagnostics::IsEnabled() ||
            CVarTraceLoads.GetValueOnAnyThread() == 0 || Object == nullptr)
        {
            return;
        }

        FAuthoringPayloadState& State = GetAuthoringPayloadState();
        FScopeLock Lock(&State.Mutex);
        const uint64 OperationId = ExplicitOperationId != 0
            ? ExplicitOperationId
            : (State.ActiveOperationStack.IsEmpty() ? 0 : State.ActiveOperationStack.Last());
        const FString ObjectPath = Object->GetPathName();
        if (FAuthoringActiveOperation* Operation = State.ActiveOperations.Find(OperationId))
        {
            bool bAlreadyObserved = false;
            Operation->ObservedObjectPaths.Add(ObjectPath, &bAlreadyObserved);
            if (!bAlreadyObserved)
            {
                ++Operation->Record.ObservedLoadCount;
                if (IsInGameThread())
                {
                    ++Operation->Record.ObservedGameThreadLoadCount;
                }
            }
        }

        FAuthoringObservedLoad& Load = State.RecentLoads.AddDefaulted_GetRef();
        Load.ObjectPath = ObjectPath;
        Load.ClassName = Object->GetClass()->GetName();
        Load.Context = Context;
        Load.OperationId = OperationId;
        Load.bGameThread = IsInGameThread();
        if (State.RecentLoads.Num() > MaxRecentLoads)
        {
            State.RecentLoads.RemoveAt(0, State.RecentLoads.Num() - MaxRecentLoads, EAllowShrinking::No);
        }
    }

    FAutoConsoleCommand DumpPayloadCommand(
        TEXT("dwc.Editor.AuthoringPayload.Dump"),
        TEXT("Dumps direct WCA authoring payload, scoped operations, and observed loads without loading assets."),
        FConsoleCommandDelegate::CreateStatic(&FDWCEditorAuthoringPayloadDiagnostics::DumpLoadedAssets));

    FAutoConsoleCommand ResetPayloadCommand(
        TEXT("dwc.Editor.AuthoringPayload.Reset"),
        TEXT("Clears WCA authoring operation and load diagnostic history."),
        FConsoleCommandDelegate::CreateStatic(&FDWCEditorAuthoringPayloadDiagnostics::Reset));
}

namespace PayloadDiagnostics = DWCEditorAuthoringPayloadDiagnosticsPrivate;

uint64 FDWCEditorAuthoringPayloadSnapshot::GetDirectPayloadBytes() const
{
    return WetPartBytes + OriginalUVTopologyBytes + WrinkleBytes + TransparencyBytes + GeneratedManifestBytes;
}

void FDWCEditorAuthoringPayloadDiagnostics::Initialize()
{
    PayloadDiagnostics::FAuthoringPayloadState& State = PayloadDiagnostics::GetAuthoringPayloadState();
    FScopeLock Lock(&State.Mutex);
    if (!State.AssetLoadedHandle.IsValid())
    {
        State.AssetLoadedHandle = FCoreUObjectDelegates::OnAssetLoaded.AddStatic(
            &FDWCEditorAuthoringPayloadDiagnostics::HandleAssetLoaded);
    }
}

void FDWCEditorAuthoringPayloadDiagnostics::Shutdown()
{
    PayloadDiagnostics::FAuthoringPayloadState& State = PayloadDiagnostics::GetAuthoringPayloadState();
    FScopeLock Lock(&State.Mutex);
    if (State.AssetLoadedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnAssetLoaded.Remove(State.AssetLoadedHandle);
        State.AssetLoadedHandle.Reset();
    }
    State.ActiveOperations.Reset();
    State.ActiveOperationStack.Reset();
}

bool FDWCEditorAuthoringPayloadDiagnostics::IsEnabled()
{
    return PayloadDiagnostics::CVarEnable.GetValueOnAnyThread() != 0;
}

FDWCEditorAuthoringPayloadSnapshot FDWCEditorAuthoringPayloadDiagnostics::CaptureAssetSnapshot(
    const UWetClothingAsset& Asset)
{
    FDWCEditorAuthoringPayloadSnapshot Snapshot;
    Snapshot.AssetPath = Asset.GetPathName();
    TSet<FString> HardObjects;
    TSet<FString> SoftObjects;
    TSet<FString> Textures;

    PayloadDiagnostics::AddHardReference(Asset.Metadata.SourceSkeletalMesh, HardObjects, Textures);
    PayloadDiagnostics::AddHardReference(Asset.Metadata.DWCSkeletalMesh, HardObjects, Textures);
    Snapshot.WetPartBytes = PayloadDiagnostics::CountWetPartPayload(
        Asset.Authored.PartData, Snapshot, HardObjects, SoftObjects, Textures);
#if WITH_EDITORONLY_DATA
    Snapshot.OriginalUVTopologyBytes = PayloadDiagnostics::CountTopologyPayload(
        Asset.Derived.Inline.OriginalUVTopologyDescriptors, Snapshot);
    Snapshot.SerializedOriginalUVTopologyBulkBytes =
        static_cast<uint64>(FMath::Max<int64>(Asset.GetSerializedOriginalUVTopologyBytesForEditor(), 0));
    Snapshot.ResidentOriginalUVTopologyBytes =
        Asset.GetResidentOriginalUVTopologyBytesForEditor();
#endif
    Snapshot.WrinkleBytes = PayloadDiagnostics::CountWrinklePayload(
        Asset.Authored.WrinkleData, Snapshot, HardObjects, SoftObjects, Textures);
    Snapshot.TransparencyBytes = PayloadDiagnostics::CountTransparencyPayload(
        Asset.Authored.TransparencyData, Snapshot, HardObjects, SoftObjects, Textures);
#if WITH_EDITORONLY_DATA
    Snapshot.GeneratedManifestBytes = Asset.Metadata.GeneratedAssetManifest.GetAllocatedSize();
    for (const TSoftObjectPtr<UObject>& Entry : Asset.Metadata.GeneratedAssetManifest)
    {
        PayloadDiagnostics::AddSoftReference(Entry, SoftObjects, Textures);
    }
#endif
    Snapshot.EstimatedTransactionalBytes = Snapshot.GetDirectPayloadBytes() +
        sizeof(FWCAAuthoredData) + sizeof(FWCADerivedInlineData) + sizeof(FWCAMetadata);
    Snapshot.ResidentRuntimeBulkBytes = Asset.GetResidentRuntimeBulkPayloadBytesForEditor();
    Snapshot.UniqueHardObjectReferenceCount = HardObjects.Num();
    Snapshot.UniqueSoftObjectReferenceCount = SoftObjects.Num();
    Snapshot.UniqueTextureReferenceCount = Textures.Num();
    return Snapshot;
}

void FDWCEditorAuthoringPayloadDiagnostics::DumpLoadedAssets()
{
    int32 AssetCount = 0;
    uint64 TotalDirectBytes = 0;
    uint64 TotalRuntimeBulkBytes = 0;
    UE_LOG(LogDWCEditorAuthoringPayload, Display, TEXT("WCA authoring payload dump (loaded assets only):"));
    for (TObjectIterator<UWetClothingAsset> It; It; ++It)
    {
        UWetClothingAsset* Asset = *It;
        if (Asset == nullptr || Asset->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
        {
            continue;
        }
        const FDWCEditorAuthoringPayloadSnapshot Snapshot = CaptureAssetSnapshot(*Asset);
        ++AssetCount;
        TotalDirectBytes += Snapshot.GetDirectPayloadBytes();
        TotalRuntimeBulkBytes += Snapshot.ResidentRuntimeBulkBytes;
        UE_LOG(
            LogDWCEditorAuthoringPayload,
            Display,
            TEXT("  asset='%s' direct=%s transactionEstimate=%s runtimeBulk=%s sections={wetPart=%s topologyMetadata=%s topologySerializedBulk=%s topologyResident=%s wrinkle=%s transparencyTotal=%s stageMetadataSubset=%s manifest=%s} counts={parts=%d topology=%d triangleRefs=%d patches=%d ridges=%d ridgePoints=%d layers=%d alphaStrokes=%d revealStrokes=%d samples=%d artifacts=%d materialSurfaces=%d refs=hard:%d/soft:%d/textures:%d}"),
            *Snapshot.AssetPath,
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.GetDirectPayloadBytes()),
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.EstimatedTransactionalBytes),
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.ResidentRuntimeBulkBytes),
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.WetPartBytes),
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.OriginalUVTopologyBytes),
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.SerializedOriginalUVTopologyBulkBytes),
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.ResidentOriginalUVTopologyBytes),
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.WrinkleBytes),
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.TransparencyBytes),
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.TransparencyStageMetadataBytes),
            *PayloadDiagnostics::FormatPayloadBytes(Snapshot.GeneratedManifestBytes),
            Snapshot.WetPartCount,
            Snapshot.OriginalUVTopologyCount,
            Snapshot.OriginalUVTriangleReferenceCount,
            Snapshot.WrinklePatchCount,
            Snapshot.RidgeStrokeCount,
            Snapshot.RidgePointCount,
            Snapshot.TransparencyLayerCount,
            Snapshot.TransparencyAlphaStrokeCount,
            Snapshot.TransparencyRevealStrokeCount,
            Snapshot.TransparencySampleCount,
            Snapshot.TransparencyStageArtifactCount,
            Snapshot.MaterialSurfaceCacheEntryCount,
            Snapshot.UniqueHardObjectReferenceCount,
            Snapshot.UniqueSoftObjectReferenceCount,
            Snapshot.UniqueTextureReferenceCount);
    }

    TArray<FDWCEditorAuthoringOperationRecord> Operations;
    TArray<PayloadDiagnostics::FAuthoringObservedLoad> Loads;
    {
        PayloadDiagnostics::FAuthoringPayloadState& State = PayloadDiagnostics::GetAuthoringPayloadState();
        FScopeLock Lock(&State.Mutex);
        Operations = State.RecentOperations;
        Loads = State.RecentLoads;
    }
    UE_LOG(
        LogDWCEditorAuthoringPayload,
        Display,
        TEXT("WCA authoring totals: assets=%d direct=%s runtimeBulk=%s recentOperations=%d recentLoads=%d."),
        AssetCount,
        *PayloadDiagnostics::FormatPayloadBytes(TotalDirectBytes),
        *PayloadDiagnostics::FormatPayloadBytes(TotalRuntimeBulkBytes),
        Operations.Num(),
        Loads.Num());
    for (const FDWCEditorAuthoringOperationRecord& Operation : Operations)
    {
        UE_LOG(
            LogDWCEditorAuthoringPayload,
            Display,
            TEXT("  operation=%llu name='%s' asset='%s' duration=%.2fms loads=%d gameThreadLoads=%d payload=%s->%s runtimeBulk=%s->%s"),
            Operation.OperationId,
            *Operation.Name,
            *Operation.AssetPath,
            Operation.DurationMilliseconds,
            Operation.ObservedLoadCount,
            Operation.ObservedGameThreadLoadCount,
            *PayloadDiagnostics::FormatPayloadBytes(Operation.PayloadBytesBefore),
            *PayloadDiagnostics::FormatPayloadBytes(Operation.PayloadBytesAfter),
            *PayloadDiagnostics::FormatPayloadBytes(Operation.RuntimeBulkBytesBefore),
            *PayloadDiagnostics::FormatPayloadBytes(Operation.RuntimeBulkBytesAfter));
    }
    for (const PayloadDiagnostics::FAuthoringObservedLoad& Load : Loads)
    {
        UE_LOG(
            LogDWCEditorAuthoringPayload,
            Verbose,
            TEXT("  load operation=%llu thread=%s class='%s' object='%s' context='%s'"),
            Load.OperationId,
            Load.bGameThread ? TEXT("game") : TEXT("other"),
            *Load.ClassName,
            *Load.ObjectPath,
            *Load.Context);
    }
}

void FDWCEditorAuthoringPayloadDiagnostics::Reset()
{
    PayloadDiagnostics::FAuthoringPayloadState& State = PayloadDiagnostics::GetAuthoringPayloadState();
    FScopeLock Lock(&State.Mutex);
    State.RecentOperations.Reset();
    State.RecentLoads.Reset();
    UE_LOG(LogDWCEditorAuthoringPayload, Display, TEXT("WCA authoring diagnostic history reset."));
}

uint64 FDWCEditorAuthoringPayloadDiagnostics::BeginOperation(
    const FString& Name,
    const UWetClothingAsset* Asset)
{
    if (!IsEnabled())
    {
        return 0;
    }

    PayloadDiagnostics::FAuthoringActiveOperation Operation;
    Operation.Record.Name = Name;
    Operation.Record.AssetPath = Asset != nullptr ? Asset->GetPathName() : FString();
    if (Asset != nullptr)
    {
        const FDWCEditorAuthoringPayloadSnapshot Snapshot = CaptureAssetSnapshot(*Asset);
        Operation.Record.PayloadBytesBefore = Snapshot.GetDirectPayloadBytes();
        Operation.Record.RuntimeBulkBytesBefore = Snapshot.ResidentRuntimeBulkBytes;
    }
    Operation.StartSeconds = FPlatformTime::Seconds();

    PayloadDiagnostics::FAuthoringPayloadState& State = PayloadDiagnostics::GetAuthoringPayloadState();
    FScopeLock Lock(&State.Mutex);
    const uint64 OperationId = State.NextOperationId++;
    Operation.Record.OperationId = OperationId;
    State.ActiveOperations.Add(OperationId, MoveTemp(Operation));
    State.ActiveOperationStack.Add(OperationId);
    return OperationId;
}

void FDWCEditorAuthoringPayloadDiagnostics::EndOperation(
    const uint64 OperationId,
    const UWetClothingAsset* Asset)
{
    if (OperationId == 0)
    {
        return;
    }

    uint64 PayloadBytesAfter = 0;
    uint64 RuntimeBulkBytesAfter = 0;
    if (Asset != nullptr)
    {
        const FDWCEditorAuthoringPayloadSnapshot Snapshot = CaptureAssetSnapshot(*Asset);
        PayloadBytesAfter = Snapshot.GetDirectPayloadBytes();
        RuntimeBulkBytesAfter = Snapshot.ResidentRuntimeBulkBytes;
    }
    FDWCEditorAuthoringOperationRecord Completed;
    {
        PayloadDiagnostics::FAuthoringPayloadState& State = PayloadDiagnostics::GetAuthoringPayloadState();
        FScopeLock Lock(&State.Mutex);
        PayloadDiagnostics::FAuthoringActiveOperation* Operation = State.ActiveOperations.Find(OperationId);
        if (Operation == nullptr)
        {
            return;
        }
        Operation->Record.DurationMilliseconds =
            (FPlatformTime::Seconds() - Operation->StartSeconds) * 1000.0;
        Operation->Record.PayloadBytesAfter = PayloadBytesAfter;
        Operation->Record.RuntimeBulkBytesAfter = RuntimeBulkBytesAfter;
        Completed = MoveTemp(Operation->Record);
        State.ActiveOperations.Remove(OperationId);
        State.ActiveOperationStack.RemoveSingle(OperationId);
        State.RecentOperations.Add(Completed);
        if (State.RecentOperations.Num() > PayloadDiagnostics::MaxRecentOperations)
        {
            State.RecentOperations.RemoveAt(
                0,
                State.RecentOperations.Num() - PayloadDiagnostics::MaxRecentOperations,
                EAllowShrinking::No);
        }
    }

    if (PayloadDiagnostics::CVarTraceOperations.GetValueOnAnyThread() != 0)
    {
        UE_LOG(
            LogDWCEditorAuthoringPayload,
            Display,
            TEXT("WCA authoring operation: id=%llu name='%s' asset='%s' duration=%.2fms loads=%d gameThreadLoads=%d payload=%s->%s runtimeBulk=%s->%s."),
            Completed.OperationId,
            *Completed.Name,
            *Completed.AssetPath,
            Completed.DurationMilliseconds,
            Completed.ObservedLoadCount,
            Completed.ObservedGameThreadLoadCount,
            *PayloadDiagnostics::FormatPayloadBytes(Completed.PayloadBytesBefore),
            *PayloadDiagnostics::FormatPayloadBytes(Completed.PayloadBytesAfter),
            *PayloadDiagnostics::FormatPayloadBytes(Completed.RuntimeBulkBytesBefore),
            *PayloadDiagnostics::FormatPayloadBytes(Completed.RuntimeBulkBytesAfter));
    }
}

void FDWCEditorAuthoringPayloadDiagnostics::RecordExplicitLoad(
    const UObject* Object,
    const TCHAR* Context)
{
    PayloadDiagnostics::AddObservedLoad(Object, Context != nullptr ? Context : TEXT("explicit"));
}

TArray<FDWCEditorAuthoringOperationRecord> FDWCEditorAuthoringPayloadDiagnostics::GetRecentOperations()
{
    PayloadDiagnostics::FAuthoringPayloadState& State = PayloadDiagnostics::GetAuthoringPayloadState();
    FScopeLock Lock(&State.Mutex);
    return State.RecentOperations;
}

void FDWCEditorAuthoringPayloadDiagnostics::HandleAssetLoaded(UObject* Object)
{
    PayloadDiagnostics::AddObservedLoad(Object, TEXT("OnAssetLoaded"));
}

FDWCEditorAuthoringOperationScope::FDWCEditorAuthoringOperationScope(
    FString InName,
    const UWetClothingAsset* InAsset)
    : OperationId(FDWCEditorAuthoringPayloadDiagnostics::BeginOperation(InName, InAsset))
    , Asset(InAsset)
{
}

FDWCEditorAuthoringOperationScope::~FDWCEditorAuthoringOperationScope()
{
    FDWCEditorAuthoringPayloadDiagnostics::EndOperation(OperationId, Asset.Get());
}
