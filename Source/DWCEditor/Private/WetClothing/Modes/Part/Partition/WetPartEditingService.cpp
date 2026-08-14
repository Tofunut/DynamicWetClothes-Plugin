// Copyright 2026 Team Tofunut. All Rights Reserved.

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
bool FWetPartEditingService::EnsureDefaultWetPartForScope(
    FWetClothingEditableWetPartData& EditableData,
    const FWetPartScope& Scope)
{
    if (!Scope.IsValid())
    {
        return false;
    }

    FWetClothingAuthoredMaterialSlot& SlotData = EditableData.FindOrAddMaterialSlot(Scope.MaterialSlotIndex);
    if (SlotData.FindPart(0) != nullptr)
    {
        return false;
    }

    EditableData.EnsureDefaultProfile();

    FWetClothingWetPartEntry& NewEntry = SlotData.WetPartEntries.AddDefaulted_GetRef();
    NewEntry.WetPartID = 0;
    NewEntry.DisplayName = GetDefaultWetPartName(0);
    NewEntry.Color = GetDefaultWetPartColor(0);
    NewEntry.bViewEnabled = true;
    NewEntry.ProfileIndex = 0;

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
