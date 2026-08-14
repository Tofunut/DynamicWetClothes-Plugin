// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Part/Presentation/DWCPartPresentationModel.h"

#include "Algo/Unique.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "WetClothing/Modes/Part/Partition/WetPartEditingService.h"
#include "WetClothing/Foundation/UV/DWCEditorUVTopologyCache.h"
#include "Engine/SkeletalMesh.h"

namespace DWCPartPresentationModelLocal
{
    uint32 HashColor(const FLinearColor& Color)
    {
        uint32 Hash = GetTypeHash(Color.R);
        Hash = HashCombine(Hash, GetTypeHash(Color.G));
        Hash = HashCombine(Hash, GetTypeHash(Color.B));
        return HashCombine(Hash, GetTypeHash(Color.A));
    }

    uint32 HashItem(const FDWCPartPresentationItem& Item)
    {
        uint32 Hash = GetTypeHash(Item.WetPartID);
        Hash = HashCombine(Hash, GetTypeHash(Item.DisplayName));
        Hash = HashCombine(Hash, HashColor(Item.Color));
        Hash = HashCombine(Hash, GetTypeHash(Item.bViewEnabled));
        Hash = HashCombine(Hash, GetTypeHash(Item.ProfileIndex));
        Hash = HashCombine(Hash, GetTypeHash(Item.ProfilePath));
        Hash = HashCombine(Hash, GetTypeHash(Item.ProfileLabel));
        Hash = HashCombine(Hash, GetTypeHash(Item.bSurfaceWaterEnabled));
        Hash = HashCombine(Hash, GetTypeHash(Item.bSyntheticDefault));
        Hash = HashCombine(Hash, GetTypeHash(Item.SurfaceWater.bOverrideDropletStampSize));
        Hash = HashCombine(Hash, GetTypeHash(Item.SurfaceWater.DropletRadiusScale));
        Hash = HashCombine(Hash, GetTypeHash(Item.SurfaceWater.bOverrideDropletFlowStampSize));
        Hash = HashCombine(Hash, GetTypeHash(Item.SurfaceWater.DropletFlowSizeScale));
        Hash = HashCombine(Hash, GetTypeHash(Item.SurfaceWater.DropletDetailSize));
        Hash = HashCombine(Hash, GetTypeHash(Item.SurfaceWater.DropletFlowDetailSize));
        return Hash;
    }

    TSharedPtr<FDWCPartPresentationItem> MakeItem(
        const FWetClothingWetPartEntry& Entry,
        const FWetClothingEditableWetPartData& EditableData)
    {
        TSharedPtr<FDWCPartPresentationItem> Item = MakeShared<FDWCPartPresentationItem>();
        Item->WetPartID = Entry.WetPartID;
        Item->DisplayName = FWetPartEditingService::GetWetPartDisplayName(Entry);
        Item->Color = Entry.WetPartID == 0
                          ? FWetPartEditingService::GetDefaultWetPartColor(0)
                          : Entry.Color;
        Item->Color.A = 1.0f;
        Item->bViewEnabled = Entry.bViewEnabled;
        Item->ProfileIndex = Entry.ProfileIndex;
        Item->SurfaceWater = Entry.SurfaceWater;

        if (const FWetPartProfileAssignment* Profile = EditableData.FindProfile(Entry))
        {
            Item->ProfilePath = Profile->GetSourceProfilePath();
            Item->ProfileLabel = Profile->GetDisplayName().TrimStartAndEnd();
            const FWetnessProfileParameters* Parameters = &Profile->Parameters;
            if (const UWetnessProfile* LoadedProfile =
                    Cast<UWetnessProfile>(Item->ProfilePath.ResolveObject()))
            {
                Parameters = &LoadedProfile->GetParameters();
            }
            Item->bSurfaceWaterEnabled = Parameters->SurfaceWater.bEnabled;
        }
        if (Item->ProfileLabel.IsEmpty())
        {
            Item->ProfileLabel = TEXT("Select Profile");
        }
        return Item;
    }

    TSharedPtr<FDWCPartPresentationItem> MakeSyntheticDefault()
    {
        TSharedPtr<FDWCPartPresentationItem> Item = MakeShared<FDWCPartPresentationItem>();
        Item->WetPartID = 0;
        Item->DisplayName = FWetPartEditingService::GetDefaultWetPartName(0);
        Item->Color = FWetPartEditingService::GetDefaultWetPartColor(0);
        Item->ProfileLabel = TEXT("Select Profile");
        Item->bSyntheticDefault = true;
        return Item;
    }
}

FDWCPartPresentationItemPtr FDWCPartPresentationSnapshot::FindItem(const int32 WetPartID) const
{
    const FDWCPartPresentationItemPtr* Item = ItemByPartID.Find(WetPartID);
    return Item != nullptr ? *Item : nullptr;
}

TSet<int32> FDWCPartPresentationSnapshot::GetIslandIDsForPart(const int32 WetPartID) const
{
    TSet<int32> Result;
    if (const TArray<int32>* IslandIDs = IslandIDsByPartID.Find(WetPartID))
    {
        for (const int32 IslandID : *IslandIDs)
        {
            Result.Add(IslandID);
        }
    }
    return Result;
}

int32 FDWCPartPresentationSnapshot::GetEffectivePartID(const int32 UVIslandID) const
{
    const int32* WetPartID = IslandToPartID.Find(UVIslandID);
    return WetPartID != nullptr ? *WetPartID : 0;
}

bool FDWCPartPresentationSnapshot::IsEquivalentTo(const FDWCPartPresentationSnapshot& Other) const
{
    return MaterialSlotIndex == Other.MaterialSlotIndex &&
           bIsWettableSlot == Other.bIsWettableSlot &&
           SemanticHash == Other.SemanticHash &&
           Items.Num() == Other.Items.Num() &&
           IslandToPartID.Num() == Other.IslandToPartID.Num();
}

FDWCPartPresentationSnapshot FDWCPartPresentationModel::Build(
    const UWetClothingAsset* WetClothingAsset,
    const int32              MaterialSlotIndex,
    const TConstArrayView<int32> UVIslandIDs)
{
    FDWCPartPresentationSnapshot Snapshot;
    Snapshot.MaterialSlotIndex = MaterialSlotIndex;

    const FWetClothingEditableWetPartData* EditableData = nullptr;
    const FWetClothingAuthoredMaterialSlot* SlotData = nullptr;
    if (WetClothingAsset != nullptr && MaterialSlotIndex != INDEX_NONE)
    {
        EditableData = &WetClothingAsset->Authored.PartData.EditableWetPartData;
        SlotData = EditableData->FindMaterialSlot(MaterialSlotIndex);
        Snapshot.bIsWettableSlot = SlotData != nullptr && SlotData->bIsWettableSlot;
    }

    TMap<int32, int32> ExplicitPartByIslandID;
    if (SlotData != nullptr && EditableData != nullptr)
    {
        Snapshot.Items.Reserve(SlotData->WetPartEntries.Num() + 1);
        for (const FWetClothingWetPartEntry& Entry : SlotData->WetPartEntries)
        {
            FDWCPartPresentationItemPtr Item =
                DWCPartPresentationModelLocal::MakeItem(Entry, *EditableData);
            Snapshot.Items.Add(Item);
            if (!Snapshot.ItemByPartID.Contains(Item->WetPartID))
            {
                Snapshot.ItemByPartID.Add(Item->WetPartID, Item);
            }

            for (const int32 UVIslandID : Entry.AssignedUVIslandIDs)
            {
                if (!ExplicitPartByIslandID.Contains(UVIslandID))
                {
                    ExplicitPartByIslandID.Add(UVIslandID, Entry.WetPartID);
                }
            }
        }
    }

    if (!Snapshot.ItemByPartID.Contains(0))
    {
        FDWCPartPresentationItemPtr DefaultItem =
            DWCPartPresentationModelLocal::MakeSyntheticDefault();
        Snapshot.Items.Add(DefaultItem);
        Snapshot.ItemByPartID.Add(0, DefaultItem);
    }

    Snapshot.Items.StableSort(
        [](const FDWCPartPresentationItemPtr& A,
           const FDWCPartPresentationItemPtr& B)
        {
            return A.IsValid() && B.IsValid() ? A->WetPartID < B->WetPartID : A.IsValid();
        });

    TArray<int32> SortedIslandIDs;
    SortedIslandIDs.Append(UVIslandIDs.GetData(), UVIslandIDs.Num());
    SortedIslandIDs.Sort();
    SortedIslandIDs.SetNum(Algo::Unique(SortedIslandIDs));
    for (const int32 UVIslandID : SortedIslandIDs)
    {
        int32 EffectivePartID = ExplicitPartByIslandID.FindRef(UVIslandID);
        FDWCPartPresentationItemPtr Item = Snapshot.FindItem(EffectivePartID);
        if (!Item.IsValid())
        {
            EffectivePartID = 0;
            Item = Snapshot.FindItem(0);
        }

        Snapshot.IslandToPartID.Add(UVIslandID, EffectivePartID);
        Snapshot.IslandIDsByPartID.FindOrAdd(EffectivePartID).Add(UVIslandID);
        if (!Item.IsValid())
        {
            continue;
        }
        if (EffectivePartID != 0 && !Item->bViewEnabled)
        {
            Snapshot.HiddenIslandIDs.Add(UVIslandID);
            continue;
        }

        FLinearColor Color = EffectivePartID == 0 ? GetUnassignedUVViewColor() : Item->Color;
        Color.A = 1.0f;
        Snapshot.UVIslandColors.Add(UVIslandID, Color);
        if (EffectivePartID != 0)
        {
            Snapshot.PreviewIslandColors.Add(UVIslandID, Color);
        }
    }

    uint32 Hash = GetTypeHash(MaterialSlotIndex);
    Hash = HashCombine(Hash, GetTypeHash(Snapshot.bIsWettableSlot));
    for (const FDWCPartPresentationItemPtr& Item : Snapshot.Items)
    {
        if (Item.IsValid())
        {
            Hash = HashCombine(Hash, DWCPartPresentationModelLocal::HashItem(*Item));
        }
    }
    for (const int32 UVIslandID : SortedIslandIDs)
    {
        Hash = HashCombine(Hash, GetTypeHash(UVIslandID));
        Hash = HashCombine(Hash, GetTypeHash(Snapshot.GetEffectivePartID(UVIslandID)));
    }
    Snapshot.SemanticHash = Hash;
    return Snapshot;
}

FLinearColor FDWCPartPresentationModel::GetUnassignedUVViewColor()
{
    return FLinearColor(0.62f, 0.62f, 0.62f, 1.0f);
}

bool FDWCPartSlotPresentationItem::IsEquivalentTo(
    const FDWCPartSlotPresentationItem& Other) const
{
    return MaterialSlotIndex == Other.MaterialSlotIndex &&
           SemanticHash == Other.SemanticHash;
}

const FDWCPartSlotPresentationItem* FDWCPartSlotPresentationSnapshot::Find(
    const int32 MaterialSlotIndex) const
{
    return ItemsByMaterialSlot.Find(MaterialSlotIndex);
}

bool FDWCPartSlotPresentationSnapshot::Update(FDWCPartSlotPresentationItem Item)
{
    const FDWCPartSlotPresentationItem* Existing = Find(Item.MaterialSlotIndex);
    if (Existing != nullptr && Existing->IsEquivalentTo(Item))
    {
        return false;
    }

    ItemsByMaterialSlot.Add(Item.MaterialSlotIndex, MoveTemp(Item));
    RebuildSemanticHash();
    return true;
}

void FDWCPartSlotPresentationSnapshot::RebuildSemanticHash()
{
    TArray<int32> SortedMaterialSlotIndices;
    ItemsByMaterialSlot.GetKeys(SortedMaterialSlotIndices);
    SortedMaterialSlotIndices.Sort();

    uint32 Hash = GetTypeHash(SortedMaterialSlotIndices.Num());
    for (const int32 MaterialSlotIndex : SortedMaterialSlotIndices)
    {
        if (const FDWCPartSlotPresentationItem* Item = Find(MaterialSlotIndex))
        {
            Hash = HashCombine(Hash, Item->SemanticHash);
        }
    }
    SemanticHash = Hash;
}

bool FDWCPartSlotPresentationSnapshot::IsEquivalentTo(
    const FDWCPartSlotPresentationSnapshot& Other) const
{
    return SemanticHash == Other.SemanticHash &&
           ItemsByMaterialSlot.Num() == Other.ItemsByMaterialSlot.Num();
}

FDWCPartSlotPresentationItem FDWCPartSlotPresentationModel::BuildSlot(
    const UWetClothingAsset* WetClothingAsset,
    const int32              MaterialSlotIndex,
    const TSet<int32>&       FailedDataUVMaterialSlotIndices,
    const TSharedPtr<FDWCEditorCacheStore>& CacheStore)
{
    FDWCPartSlotPresentationItem Item;
    Item.MaterialSlotIndex = MaterialSlotIndex;
    if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return Item;
    }

    const FWetClothingEditableWetPartData& EditableData =
        WetClothingAsset->Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* SlotData =
        EditableData.FindMaterialSlot(MaterialSlotIndex);
    Item.bIsWettableSlot = SlotData != nullptr && SlotData->bIsWettableSlot;
    Item.bDataUVFailed = FailedDataUVMaterialSlotIndices.Contains(MaterialSlotIndex);

    bool bExpectedDiagnostic = false;
    bool bMissingDiagnostic = false;
    if (WetClothingAsset->HasLockedDataUVLayout())
    {
        for (const FDWCDataUVLODMetadata& Metadata : WetClothingAsset->GetDataUVMetadata())
        {
            const bool bSlotIncluded = Metadata.GeneratedMaterialSlotIndices.IsEmpty() ||
                                       Metadata.GeneratedMaterialSlotIndices.Contains(MaterialSlotIndex);
            if (!bSlotIncluded)
            {
                continue;
            }

            Item.bDataUVIncluded = true;
            if (!Metadata.bIsValid)
            {
                continue;
            }

            bExpectedDiagnostic = true;
            const FDWCDataUVSlotWarning* Diagnostic = Metadata.SlotWarnings.FindByPredicate(
                [MaterialSlotIndex](const FDWCDataUVSlotWarning& Warning)
                {
                    return Warning.MaterialSlotIndex == MaterialSlotIndex;
                });
            bMissingDiagnostic = bMissingDiagnostic || Diagnostic == nullptr;
            Item.bDataUVHasWarnings = Item.bDataUVHasWarnings ||
                                       (Diagnostic != nullptr && Diagnostic->HasWarnings());
        }
    }
    Item.bDataUVDiagnosticsComplete = bExpectedDiagnostic && !bMissingDiagnostic;

    const FDWCDataUVLODMetadata* LOD0Metadata =
        WetClothingAsset->FindDataUVMetadataForLOD(0);
    Item.bDataUVReady = !Item.bDataUVFailed &&
                        WetClothingAsset->GetRuntimeSkeletalMesh() != nullptr &&
                        Item.bDataUVIncluded &&
                        LOD0Metadata != nullptr &&
                        LOD0Metadata->bIsValid &&
                        LOD0Metadata->UVChannelIndex == WetClothingAsset->GetDWCDataUVChannelIndex() &&
                        LOD0Metadata->GeneratorVersion == DWCGeneratedDataVersion::DataUV;

    TSet<int32> AssignedRealPartIslandIDs;
    if (SlotData != nullptr)
    {
        for (const FWetClothingWetPartEntry& Entry : SlotData->WetPartEntries)
        {
            if (Entry.WetPartID == 0 || Entry.AssignedUVIslandIDs.IsEmpty())
            {
                continue;
            }
            const FWetPartProfileAssignment* Profile = EditableData.FindProfile(Entry);
            if (Profile == nullptr || !Profile->HasSourceProfile())
            {
                ++Item.MissingProfilePartCount;
            }
            for (const int32 UVIslandID : Entry.AssignedUVIslandIDs)
            {
                AssignedRealPartIslandIDs.Add(UVIslandID);
            }
        }
    }

    if (Item.bDataUVReady)
    {
        FDWCEditorCacheKey TopologyKey;
        FDWCEditorCacheLease TopologyLease;
        if (FDWCEditorUVTopologyCache::AcquireForAsset(
                CacheStore,
                WetClothingAsset,
                WetClothingAsset->GetOriginalUVChannelIndex(),
                MaterialSlotIndex,
                TopologyKey,
                TopologyLease))
        {
            const FDWCEditorUVTopologyCacheValue* Topology =
                TopologyLease.GetAs<FDWCEditorUVTopologyCacheValue>();
            Item.UVIslandCount = Topology != nullptr ? Topology->Islands.Num() : 0;
            if (Topology != nullptr)
            {
                for (const TSharedPtr<FWetClothingAssetUVIsland>& Island : Topology->Islands)
                {
                    if (!Island.IsValid() ||
                        !AssignedRealPartIslandIDs.Contains(Island->UVIslandID))
                    {
                        ++Item.UnassignedUVIslandCount;
                    }
                }
            }
        }
    }

    Item.bPartMapComplete = Item.bIsWettableSlot &&
                            Item.bDataUVReady &&
                            SlotData != nullptr &&
                            Item.UVIslandCount > 0 &&
                            Item.UnassignedUVIslandCount == 0 &&
                            Item.MissingProfilePartCount == 0;
    Item.bNeedsPartMapAttention = Item.bIsWettableSlot && !Item.bPartMapComplete;

    uint32 Hash = GetTypeHash(Item.MaterialSlotIndex);
    Hash = HashCombine(Hash, GetTypeHash(Item.bIsWettableSlot));
    Hash = HashCombine(Hash, GetTypeHash(Item.bDataUVIncluded));
    Hash = HashCombine(Hash, GetTypeHash(Item.bDataUVReady));
    Hash = HashCombine(Hash, GetTypeHash(Item.bDataUVFailed));
    Hash = HashCombine(Hash, GetTypeHash(Item.bDataUVHasWarnings));
    Hash = HashCombine(Hash, GetTypeHash(Item.bDataUVDiagnosticsComplete));
    Hash = HashCombine(Hash, GetTypeHash(Item.bPartMapComplete));
    Hash = HashCombine(Hash, GetTypeHash(Item.bNeedsPartMapAttention));
    Hash = HashCombine(Hash, GetTypeHash(Item.UVIslandCount));
    Hash = HashCombine(Hash, GetTypeHash(Item.UnassignedUVIslandCount));
    Hash = HashCombine(Hash, GetTypeHash(Item.MissingProfilePartCount));
    Item.SemanticHash = Hash;
    return Item;
}

FDWCPartSlotPresentationSnapshot FDWCPartSlotPresentationModel::BuildAll(
    const UWetClothingAsset* WetClothingAsset,
    const TSet<int32>&       FailedDataUVMaterialSlotIndices,
    const TSharedPtr<FDWCEditorCacheStore>& CacheStore)
{
    FDWCPartSlotPresentationSnapshot Snapshot;
    const USkeletalMesh* SlotIdentityMesh = WetClothingAsset != nullptr
        ? WetClothingAsset->GetSourceSkeletalMesh()
        : nullptr;
    if (SlotIdentityMesh == nullptr && WetClothingAsset != nullptr)
    {
        SlotIdentityMesh = WetClothingAsset->GetRuntimeSkeletalMesh();
    }
    if (SlotIdentityMesh == nullptr)
    {
        return Snapshot;
    }

    for (int32 MaterialSlotIndex = 0;
         MaterialSlotIndex < SlotIdentityMesh->GetMaterials().Num();
         ++MaterialSlotIndex)
    {
        FDWCPartSlotPresentationItem Item = BuildSlot(
            WetClothingAsset,
            MaterialSlotIndex,
            FailedDataUVMaterialSlotIndices,
            CacheStore);
        Snapshot.ItemsByMaterialSlot.Add(MaterialSlotIndex, MoveTemp(Item));
    }
    Snapshot.RebuildSemanticHash();
    return Snapshot;
}
