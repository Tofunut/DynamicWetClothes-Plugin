//Copyright 2026 Team Tofunut. All Rights Reserved.
/*
 * Implements Wet Part editing operations, including default-part creation, island mapping, and display metadata updates.
 */

#include "WetClothing/Modes/Part/Partition/WetPartEditingService.h"

FWetPartScope FWetPartEditingService::MakeScope(int32 MaterialSlotIndex)
{
    FWetPartScope Scope;
    Scope.MaterialSlotIndex = MaterialSlotIndex;
    return Scope;
}

bool FWetPartEditingService::EnsureDefaultWetPartForScope(UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope)
{
    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return false;
    }

    FWetClothingEditableWetPartData& EditableData = WetClothingAsset->Authored.PartData.EditableWetPartData;
    FWetClothingAuthoredMaterialSlot& SlotData = EditableData.FindOrAddMaterialSlot(Scope.MaterialSlotIndex);
    if (SlotData.FindPart(0) != nullptr)
    {
        return true;
    }

    WetClothingAsset->Modify();
    EditableData.EnsureDefaultProfile();

    FWetClothingWetPartEntry& NewEntry = SlotData.WetPartEntries.AddDefaulted_GetRef();
    NewEntry.WetPartID = 0;
    NewEntry.DisplayName = GetDefaultWetPartName(0);
    NewEntry.Color = GetDefaultWetPartColor(0);
    NewEntry.bViewEnabled = true;
    NewEntry.ProfileIndex = 0;

    WetClothingAsset->MarkPackageDirty();
    return true;
}

int32 FWetPartEditingService::FindNextWetPartIDForScope(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope)
{
    int32 MaxWetPartID = 0;
    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return MaxWetPartID + 1;
    }

    const FWetClothingAuthoredMaterialSlot* SlotData = WetClothingAsset->Authored.PartData.EditableWetPartData.FindMaterialSlot(Scope.MaterialSlotIndex);
    if (SlotData != nullptr)
    {
        for (const FWetClothingWetPartEntry& Entry : SlotData->WetPartEntries)
        {
            MaxWetPartID = FMath::Max(MaxWetPartID, Entry.WetPartID);
        }
    }
    return MaxWetPartID + 1;
}

FWetClothingWetPartEntry* FWetPartEditingService::FindMutableEntry(UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 WetPartID)
{
    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return nullptr;
    }

    FWetClothingAuthoredMaterialSlot* SlotData = WetClothingAsset->Authored.PartData.EditableWetPartData.FindMaterialSlot(Scope.MaterialSlotIndex);
    return SlotData != nullptr ? SlotData->FindPart(WetPartID) : nullptr;
}

const FWetClothingWetPartEntry* FWetPartEditingService::FindEntry(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 WetPartID)
{
    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return nullptr;
    }

    const FWetClothingAuthoredMaterialSlot* SlotData = WetClothingAsset->Authored.PartData.EditableWetPartData.FindMaterialSlot(Scope.MaterialSlotIndex);
    return SlotData != nullptr ? SlotData->FindPart(WetPartID) : nullptr;
}

const FWetClothingWetPartEntry* FWetPartEditingService::FindEntryForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID)
{
    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return nullptr;
    }

    const FWetClothingAuthoredMaterialSlot* SlotData = WetClothingAsset->Authored.PartData.EditableWetPartData.FindMaterialSlot(Scope.MaterialSlotIndex);
    return SlotData != nullptr
        ? SlotData->WetPartEntries.FindByPredicate(
              [UVIslandID](const FWetClothingWetPartEntry& Entry)
              {
                  return Entry.AssignedUVIslandIDs.Contains(UVIslandID);
              })
        : nullptr;
}

const FWetClothingWetPartEntry* FWetPartEditingService::FindEffectiveEntryForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID)
{
    if (const FWetClothingWetPartEntry* AssignedEntry = FindEntryForUVIsland(WetClothingAsset, Scope, UVIslandID))
    {
        return AssignedEntry;
    }
    return FindEntry(WetClothingAsset, Scope, 0);
}

void FWetPartEditingService::BuildWetPartItemsForScope(
    const UWetClothingAsset* WetClothingAsset,
    const FWetPartScope& Scope,
    TArray<TSharedPtr<FWetClothingWetPartEntry>>& OutItems)
{
    OutItems.Reset();
    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return;
    }

    const FWetClothingAuthoredMaterialSlot* SlotData = WetClothingAsset->Authored.PartData.EditableWetPartData.FindMaterialSlot(Scope.MaterialSlotIndex);
    if (SlotData != nullptr)
    {
        for (const FWetClothingWetPartEntry& Entry : SlotData->WetPartEntries)
        {
            OutItems.Add(MakeShared<FWetClothingWetPartEntry>(Entry));
        }
    }

    OutItems.Sort([](const TSharedPtr<FWetClothingWetPartEntry>& A, const TSharedPtr<FWetClothingWetPartEntry>& B)
    {
        return A.IsValid() && B.IsValid() ? A->WetPartID < B->WetPartID : A.IsValid();
    });
}

TSet<int32> FWetPartEditingService::GetUVIslandIDsForWetPart(
    const UWetClothingAsset*                             WetClothingAsset,
    const FWetPartScope&                                 Scope,
    const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands,
    int32                                                WetPartID)
{
    TSet<int32> Result;

    for (const TSharedPtr<FWetClothingAssetUVIsland>& IslandItem : Islands)
    {
        if (IslandItem.IsValid() && GetEffectiveWetPartIDForUVIsland(WetClothingAsset, Scope, IslandItem->UVIslandID) == WetPartID)
        {
            Result.Add(IslandItem->UVIslandID);
        }
    }

    return Result;
}

int32 FWetPartEditingService::GetEffectiveWetPartIDForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID)
{
    if (const FWetClothingWetPartEntry* EffectiveEntry = FindEffectiveEntryForUVIsland(WetClothingAsset, Scope, UVIslandID))
    {
        return EffectiveEntry->WetPartID;
    }

    return 0;
}

FLinearColor FWetPartEditingService::GetDefaultWetPartColor(int32 WetPartID)
{
    if (WetPartID == 0)
    {
        return FLinearColor(0.32f, 0.32f, 0.32f, 1.0f);
    }

    static const FLinearColor Palette[] = {
        FLinearColor(1.00f, 0.00f, 1.00f, 1.0f),
        FLinearColor(0.00f, 0.25f, 1.00f, 1.0f),
        FLinearColor(0.00f, 1.00f, 0.05f, 1.0f),
        FLinearColor(1.00f, 1.00f, 0.00f, 1.0f),
        FLinearColor(1.00f, 0.35f, 0.00f, 1.0f),
        FLinearColor(0.55f, 0.00f, 1.00f, 1.0f),
        FLinearColor(0.00f, 1.00f, 1.00f, 1.0f),
        FLinearColor(1.00f, 0.00f, 0.20f, 1.0f),
        FLinearColor(0.35f, 1.00f, 0.00f, 1.0f),
        FLinearColor(1.00f, 0.00f, 0.65f, 1.0f),
        FLinearColor(0.00f, 0.65f, 1.00f, 1.0f),
        FLinearColor(0.75f, 1.00f, 0.00f, 1.0f),
        FLinearColor(1.00f, 0.60f, 0.00f, 1.0f),
        FLinearColor(0.35f, 0.00f, 1.00f, 1.0f),
        FLinearColor(0.00f, 1.00f, 0.55f, 1.0f),
        FLinearColor(1.00f, 0.00f, 0.00f, 1.0f)
    };

    const int32 PaletteIndex = FMath::Abs(WetPartID - 1) % UE_ARRAY_COUNT(Palette);
    return Palette[PaletteIndex];
}

FString FWetPartEditingService::GetDefaultWetPartName(int32 WetPartID)
{
    return WetPartID == 0 ? TEXT("Unassigned") : FString::Printf(TEXT("Part %d"), WetPartID);
}

FString FWetPartEditingService::GetWetPartDisplayName(const FWetClothingWetPartEntry& Entry)
{
    if (Entry.WetPartID == 0)
    {
        return TEXT("Unassigned");
    }

    const FString TrimmedName = Entry.DisplayName.TrimStartAndEnd();
    if (!TrimmedName.IsEmpty())
    {
        return TrimmedName;
    }

    return GetDefaultWetPartName(Entry.WetPartID);
}

FString FWetPartEditingService::GetAssignedProfileLabel(const UWetClothingAsset* WetClothingAsset, const FWetClothingWetPartEntry& Entry)
{
    if (WetClothingAsset == nullptr)
    {
        return TEXT("Select Profile");
    }

    const FWetPartProfileAssignment* Profile = WetClothingAsset->Authored.PartData.EditableWetPartData.FindProfile(Entry);
    if (Profile == nullptr)
    {
        return TEXT("Select Profile");
    }

    const FString TrimmedLabel = Profile->GetDisplayName().TrimStartAndEnd();
    return TrimmedLabel.IsEmpty() ? TEXT("Select Profile") : TrimmedLabel;
}

TMap<int32, int32> FWetPartEditingService::BuildUVIslandWetPartIDMap(
    const UWetClothingAsset*                             WetClothingAsset,
    const FWetPartScope&                                 Scope,
    const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands)
{
    TMap<int32, int32> Result;

    for (const TSharedPtr<FWetClothingAssetUVIsland>& IslandItem : Islands)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        const int32 EffectiveWetPartID = GetEffectiveWetPartIDForUVIsland(WetClothingAsset, Scope, IslandItem->UVIslandID);
        // if (EffectiveWetPartID == 0)
        // {
        //     continue;
        // }

        if (FindEntry(WetClothingAsset, Scope, EffectiveWetPartID) != nullptr)
        {
            Result.Add(IslandItem->UVIslandID, EffectiveWetPartID);
        }
    }

    return Result;
}

TMap<int32, FLinearColor> FWetPartEditingService::BuildUVIslandColorMap(
    const UWetClothingAsset*                             WetClothingAsset,
    const FWetPartScope&                                 Scope,
    const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands)
{
    TMap<int32, FLinearColor> Result;

    for (const TSharedPtr<FWetClothingAssetUVIsland>& IslandItem : Islands)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        if (const FWetClothingWetPartEntry* Entry = FindEffectiveEntryForUVIsland(WetClothingAsset, Scope, IslandItem->UVIslandID))
        {
            if (Entry->WetPartID != 0 && !Entry->bViewEnabled)
            {
                continue;
            }

            FLinearColor Color = Entry->WetPartID == 0 ? FLinearColor::White : Entry->Color;
            Color.A = 1.0f;
            Result.Add(IslandItem->UVIslandID, Color);
        }
    }

    return Result;
}
