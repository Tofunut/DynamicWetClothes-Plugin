/*
 *  Wet Part 검색, 기본 Part 생성, Island 매핑, 표시 이름/색상 계산 등 편집 서비스 로직을 구현합니다.
 */

#include "WetClothing/WetPart/WetPartEditingService.h"

FWetPartScope FWetPartEditingService::MakeScope(int32 MaterialSlotIndex, int32 UVChannelIndex)
{
    FWetPartScope Scope;
    Scope.MaterialSlotIndex = MaterialSlotIndex;
    Scope.UVChannelIndex = UVChannelIndex;
    return Scope;
}

bool FWetPartEditingService::MatchesScope(const FWetClothingAssetWetPartEntry& Entry, const FWetPartScope& Scope)
{
    return Entry.MaterialSlotIndex == Scope.MaterialSlotIndex && Entry.UVChannelIndex == Scope.UVChannelIndex;
}

bool FWetPartEditingService::EnsureDefaultWetPartForScope(UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope)
{
    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return false;
    }

    for (FWetClothingAssetWetPartEntry& Entry : WetClothingAsset->WetPartEntries)
    {
        if (MatchesScope(Entry, Scope) && Entry.WetPartID == 0)
        {
            WetClothingAsset->Modify();
            Entry.Name = GetDefaultWetPartName(0);
            Entry.Color = GetDefaultWetPartColor(0);
            Entry.bViewEnabled = true;
            WetClothingAsset->MarkPackageDirty();
            return true;
        }
    }

    WetClothingAsset->Modify();

    FWetClothingAssetWetPartEntry NewEntry;
    NewEntry.MaterialSlotIndex = Scope.MaterialSlotIndex;
    NewEntry.UVChannelIndex = Scope.UVChannelIndex;
    NewEntry.WetPartID = 0;
    NewEntry.Name = GetDefaultWetPartName(NewEntry.WetPartID);
    NewEntry.Color = GetDefaultWetPartColor(NewEntry.WetPartID);
    NewEntry.bViewEnabled = true;

    WetClothingAsset->WetPartEntries.Add(NewEntry);
    WetClothingAsset->MarkPackageDirty();
    return true;
}

int32 FWetPartEditingService::FindNextWetPartIDForScope(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope)
{
    int32 MaxWetPartID = 0;
    if (WetClothingAsset != nullptr && Scope.IsValid())
    {
        for (const FWetClothingAssetWetPartEntry& Entry : WetClothingAsset->WetPartEntries)
        {
            if (MatchesScope(Entry, Scope))
            {
                MaxWetPartID = FMath::Max(MaxWetPartID, Entry.WetPartID);
            }
        }
    }

    return MaxWetPartID + 1;
}

FWetClothingAssetWetPartEntry* FWetPartEditingService::FindMutableEntry(UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 WetPartID)
{
    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return nullptr;
    }

    for (FWetClothingAssetWetPartEntry& Entry : WetClothingAsset->WetPartEntries)
    {
        if (MatchesScope(Entry, Scope) && Entry.WetPartID == WetPartID)
        {
            return &Entry;
        }
    }

    return nullptr;
}

const FWetClothingAssetWetPartEntry* FWetPartEditingService::FindEntry(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 WetPartID)
{
    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return nullptr;
    }

    for (const FWetClothingAssetWetPartEntry& Entry : WetClothingAsset->WetPartEntries)
    {
        if (MatchesScope(Entry, Scope) && Entry.WetPartID == WetPartID)
        {
            return &Entry;
        }
    }

    return nullptr;
}

const FWetClothingAssetWetPartEntry* FWetPartEditingService::FindEntryForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID)
{
    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return nullptr;
    }

    for (const FWetClothingAssetWetPartEntry& Entry : WetClothingAsset->WetPartEntries)
    {
        if (MatchesScope(Entry, Scope) && Entry.AssignedUVIslandIDs.Contains(UVIslandID))
        {
            return &Entry;
        }
    }

    return nullptr;
}

const FWetClothingAssetWetPartEntry* FWetPartEditingService::FindEffectiveEntryForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID)
{
    if (const FWetClothingAssetWetPartEntry* AssignedEntry = FindEntryForUVIsland(WetClothingAsset, Scope, UVIslandID))
    {
        return AssignedEntry;
    }

    return FindEntry(WetClothingAsset, Scope, 0);
}

void FWetPartEditingService::BuildWetPartItemsForScope(
    const UWetClothingAsset*                           WetClothingAsset,
    const FWetPartScope&                               Scope,
    TArray<TSharedPtr<FWetClothingAssetWetPartEntry>>& OutItems)
{
    OutItems.Reset();

    if (WetClothingAsset == nullptr || !Scope.IsValid())
    {
        return;
    }

    for (const FWetClothingAssetWetPartEntry& Entry : WetClothingAsset->WetPartEntries)
    {
        if (MatchesScope(Entry, Scope))
        {
            OutItems.Add(MakeShared<FWetClothingAssetWetPartEntry>(Entry));
        }
    }

    OutItems.Sort([](const TSharedPtr<FWetClothingAssetWetPartEntry>& A, const TSharedPtr<FWetClothingAssetWetPartEntry>& B)
                  { return A.IsValid() && B.IsValid() ? A->WetPartID < B->WetPartID : A.IsValid(); });
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
    if (const FWetClothingAssetWetPartEntry* EffectiveEntry = FindEffectiveEntryForUVIsland(WetClothingAsset, Scope, UVIslandID))
    {
        return EffectiveEntry->WetPartID;
    }

    return 0;
}

FLinearColor FWetPartEditingService::GetDefaultWetPartColor(int32 WetPartID)
{
    if (WetPartID == 0)
    {
        return FLinearColor::White;
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
    return WetPartID == 0 ? TEXT("Part Default") : FString::Printf(TEXT("Part %d"), WetPartID);
}

FString FWetPartEditingService::GetWetPartDisplayName(const FWetClothingAssetWetPartEntry& Entry)
{
    const FString TrimmedName = Entry.Name.TrimStartAndEnd();
    if (!TrimmedName.IsEmpty())
    {
        return TrimmedName;
    }

    return GetDefaultWetPartName(Entry.WetPartID);
}

FString FWetPartEditingService::GetAssignedProfileLabel(const FWetClothingAssetWetPartEntry& Entry)
{
    const FString TrimmedLabel = Entry.ProfileAssignment.SourceProfileName.TrimStartAndEnd();
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

        if (const FWetClothingAssetWetPartEntry* Entry = FindEffectiveEntryForUVIsland(WetClothingAsset, Scope, IslandItem->UVIslandID))
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
