// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;
class UWetClothingAsset;

/** Heap-backed payload retained directly by one WCA. Referenced asset payloads are counted separately. */
struct FDWCEditorAuthoringPayloadSnapshot
{
    FString AssetPath;

    uint64 WetPartBytes = 0;
    /** Eager descriptor bytes only. The topology payload itself is editor bulk data. */
    uint64 OriginalUVTopologyBytes = 0;
    uint64 SerializedOriginalUVTopologyBulkBytes = 0;
    uint64 ResidentOriginalUVTopologyBytes = 0;
    uint64 WrinkleBytes = 0;
    uint64 TransparencyBytes = 0;
    uint64 TransparencyStageMetadataBytes = 0;
    uint64 GeneratedManifestBytes = 0;
    uint64 EstimatedTransactionalBytes = 0;
    uint64 ResidentRuntimeBulkBytes = 0;

    int32 WetPartCount = 0;
    int32 OriginalUVTopologyCount = 0;
    int32 OriginalUVTriangleReferenceCount = 0;
    int32 WrinklePatchCount = 0;
    int32 RidgeStrokeCount = 0;
    int32 RidgePointCount = 0;
    int32 TransparencyLayerCount = 0;
    int32 TransparencyAlphaStrokeCount = 0;
    int32 TransparencyRevealStrokeCount = 0;
    int32 TransparencySampleCount = 0;
    int32 TransparencyStageArtifactCount = 0;
    int32 MaterialSurfaceCacheEntryCount = 0;
    int32 UniqueHardObjectReferenceCount = 0;
    int32 UniqueSoftObjectReferenceCount = 0;
    int32 UniqueTextureReferenceCount = 0;

    uint64 GetDirectPayloadBytes() const;
};

struct FDWCEditorAuthoringOperationRecord
{
    uint64 OperationId = 0;
    FString Name;
    FString AssetPath;
    double DurationMilliseconds = 0.0;
    int32 ObservedLoadCount = 0;
    int32 ObservedGameThreadLoadCount = 0;
    uint64 PayloadBytesBefore = 0;
    uint64 PayloadBytesAfter = 0;
    uint64 RuntimeBulkBytesBefore = 0;
    uint64 RuntimeBulkBytesAfter = 0;
};

/**
 * Opt-in diagnostics for WCA serialized authoring payload and loads observed during editor operations.
 * It never loads an asset in order to inspect it and does not participate in resource admission.
 */
class FDWCEditorAuthoringPayloadDiagnostics final
{
public:
    static void Initialize();
    static void Shutdown();

    static bool IsEnabled();
    static FDWCEditorAuthoringPayloadSnapshot CaptureAssetSnapshot(const UWetClothingAsset& Asset);
    static void DumpLoadedAssets();
    static void Reset();

    static uint64 BeginOperation(const FString& Name, const UWetClothingAsset* Asset = nullptr);
    static void EndOperation(uint64 OperationId, const UWetClothingAsset* Asset = nullptr);
    static void RecordExplicitLoad(const UObject* Object, const TCHAR* Context);
    static TArray<FDWCEditorAuthoringOperationRecord> GetRecentOperations();

private:
    static void HandleAssetLoaded(UObject* Object);
};

/** Adds an operation boundary only while authoring diagnostics are enabled. */
class FDWCEditorAuthoringOperationScope final
{
public:
    FDWCEditorAuthoringOperationScope(FString InName, const UWetClothingAsset* InAsset = nullptr);
    ~FDWCEditorAuthoringOperationScope();

    FDWCEditorAuthoringOperationScope(const FDWCEditorAuthoringOperationScope&) = delete;
    FDWCEditorAuthoringOperationScope& operator=(const FDWCEditorAuthoringOperationScope&) = delete;

private:
    uint64 OperationId = 0;
    TWeakObjectPtr<const UWetClothingAsset> Asset;
};
