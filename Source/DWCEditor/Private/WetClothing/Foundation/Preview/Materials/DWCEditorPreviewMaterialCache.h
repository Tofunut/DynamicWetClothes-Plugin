#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectKey.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialTypes.h"

/** Per-preview-session hierarchy cache. It deliberately has no global singleton. */
class FDWCEditorPreviewMaterialCache final : public FGCObject
{
  public:
    FDWCEditorPreviewMaterialResult GetOrCreate(const FDWCEditorPreviewMaterialRequest& Request);

    void InvalidateSource(UMaterialInterface* SourceMaterial, bool bIncludeSharedBaseMaterial);
    void InvalidateSlot(UObject* SlotOwner, int32 MaterialSlotIndex);
    /** Drops parent and graph entries that no longer have a retained slot MID. */
    void PruneUnusedHierarchies();
    void Reset();
    void ResetDiagnosticCounters();

    FDWCEditorPreviewMaterialCacheStats GetStats() const;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override;

  private:
    struct FGraphKey
    {
        FObjectKey SourceBaseMaterial;
        FGuid SourceStateId;
        int32 DWCDataUVChannelIndex = INDEX_NONE;
        int32 SurfaceWaterNormalUVChannelIndex = INDEX_NONE;
        uint8 FeatureMask = 0;
        uint32 FeatureSchemaVersion = 0;

        bool operator==(const FGraphKey& Other) const;
        friend uint32 GetTypeHash(const FGraphKey& Key)
        {
            uint32 Hash = GetTypeHash(Key.SourceBaseMaterial);
            Hash = HashCombine(Hash, GetTypeHash(Key.SourceStateId));
            Hash = HashCombine(Hash, GetTypeHash(Key.DWCDataUVChannelIndex));
            Hash = HashCombine(Hash, GetTypeHash(Key.SurfaceWaterNormalUVChannelIndex));
            Hash = HashCombine(Hash, GetTypeHash(Key.FeatureMask));
            return HashCombine(Hash, GetTypeHash(Key.FeatureSchemaVersion));
        }
    };

    struct FParentKey
    {
        FObjectKey SourceMaterial;
        FObjectKey TransientBaseMaterial;
        uint32 SourceParameterRevision = 0;

        bool operator==(const FParentKey& Other) const;
        friend uint32 GetTypeHash(const FParentKey& Key)
        {
            uint32 Hash = HashCombine(GetTypeHash(Key.SourceMaterial), GetTypeHash(Key.TransientBaseMaterial));
            return HashCombine(Hash, GetTypeHash(Key.SourceParameterRevision));
        }
    };

    struct FSlotKey
    {
        FObjectKey SlotOwner;
        int32 MaterialSlotIndex = INDEX_NONE;
        FObjectKey TransientParent;

        bool operator==(const FSlotKey& Other) const;
        friend uint32 GetTypeHash(const FSlotKey& Key)
        {
            uint32 Hash = GetTypeHash(Key.SlotOwner);
            Hash = HashCombine(Hash, GetTypeHash(Key.MaterialSlotIndex));
            return HashCombine(Hash, GetTypeHash(Key.TransientParent));
        }
    };

    struct FGraphEntry
    {
        TObjectPtr<UMaterial> Material = nullptr;
        TObjectPtr<UMaterialInterface> SourceMaterial = nullptr;
        EDWCEditorPreviewMaterialState State = EDWCEditorPreviewMaterialState::Failed;
        FString FailureMessage;
    };

    struct FParentEntry
    {
        TObjectPtr<UMaterialInstanceConstant> Parent = nullptr;
        TObjectPtr<UMaterialInterface> SourceMaterial = nullptr;
        FString FailureMessage;
    };

    struct FSlotEntry
    {
        TObjectPtr<UMaterialInstanceDynamic> MID = nullptr;
        TObjectPtr<UObject> SlotOwner = nullptr;
        TObjectPtr<UMaterialInterface> SourceMaterial = nullptr;
    };

    TMap<FGraphKey, FGraphEntry> GraphEntries;
    TMap<FParentKey, FParentEntry> ParentEntries;
    TMap<FSlotKey, FSlotEntry> SlotEntries;
    FDWCEditorPreviewMaterialCacheStats LifetimeStats;
};
