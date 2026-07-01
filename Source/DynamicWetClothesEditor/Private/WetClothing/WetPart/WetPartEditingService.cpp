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

bool FWetPartEditingService::EnsureDefaultWetPartForScope(UWetClothingAsset* Profile, const FWetPartScope& Scope)
{
    if (Profile == nullptr || !Scope.IsValid())
    {
        return false;
    }

    for (FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (MatchesScope(Entry, Scope) && Entry.WetPartID == 0)
        {
            Profile->Modify();
            Entry.Name = GetDefaultWetPartName(0);
            Entry.Color = GetDefaultWetPartColor(0);
            Entry.bViewEnabled = true;
            Profile->MarkPackageDirty();
            return true;
        }
    }

    Profile->Modify();

    FWetClothingAssetWetPartEntry NewEntry;
    NewEntry.MaterialSlotIndex = Scope.MaterialSlotIndex;
    NewEntry.UVChannelIndex = Scope.UVChannelIndex;
    NewEntry.WetPartID = 0;
    NewEntry.Name = GetDefaultWetPartName(NewEntry.WetPartID);
    NewEntry.Color = GetDefaultWetPartColor(NewEntry.WetPartID);
    NewEntry.bViewEnabled = true;

    Profile->WetPartEntries.Add(NewEntry);
    Profile->MarkPackageDirty();
    return true;
}

int32 FWetPartEditingService::FindNextWetPartIDForScope(const UWetClothingAsset* Profile, const FWetPartScope& Scope)
{
    int32 MaxWetPartID = 0;
    if (Profile != nullptr && Scope.IsValid())
    {
        for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
        {
            if (MatchesScope(Entry, Scope))
            {
                MaxWetPartID = FMath::Max(MaxWetPartID, Entry.WetPartID);
            }
        }
    }

    return MaxWetPartID + 1;
}

FWetClothingAssetWetPartEntry* FWetPartEditingService::FindMutableEntry(UWetClothingAsset* Profile, const FWetPartScope& Scope, int32 WetPartID)
{
    if (Profile == nullptr || !Scope.IsValid())
    {
        return nullptr;
    }

    for (FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (MatchesScope(Entry, Scope) && Entry.WetPartID == WetPartID)
        {
            return &Entry;
        }
    }

    return nullptr;
}

const FWetClothingAssetWetPartEntry* FWetPartEditingService::FindEntry(const UWetClothingAsset* Profile, const FWetPartScope& Scope, int32 WetPartID)
{
    if (Profile == nullptr || !Scope.IsValid())
    {
        return nullptr;
    }

    for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (MatchesScope(Entry, Scope) && Entry.WetPartID == WetPartID)
        {
            return &Entry;
        }
    }

    return nullptr;
}

const FWetClothingAssetWetPartEntry* FWetPartEditingService::FindEntryForIsland(const UWetClothingAsset* Profile, const FWetPartScope& Scope, int32 IslandID)
{
    if (Profile == nullptr || !Scope.IsValid())
    {
        return nullptr;
    }

    for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (MatchesScope(Entry, Scope) && Entry.AssignedIslandIDs.Contains(IslandID))
        {
            return &Entry;
        }
    }

    return nullptr;
}

const FWetClothingAssetWetPartEntry* FWetPartEditingService::FindEffectiveEntryForIsland(const UWetClothingAsset* Profile, const FWetPartScope& Scope, int32 IslandID)
{
    if (const FWetClothingAssetWetPartEntry* AssignedEntry = FindEntryForIsland(Profile, Scope, IslandID))
    {
        return AssignedEntry;
    }

    return FindEntry(Profile, Scope, 0);
}

void FWetPartEditingService::BuildWetPartItemsForScope(
    const UWetClothingAsset*                           Profile,
    const FWetPartScope&                      Scope,
    TArray<TSharedPtr<FWetClothingAssetWetPartEntry>>& OutItems)
{
    OutItems.Reset();

    if (Profile == nullptr || !Scope.IsValid())
    {
        return;
    }

    for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (MatchesScope(Entry, Scope))
        {
            OutItems.Add(MakeShared<FWetClothingAssetWetPartEntry>(Entry));
        }
    }

    OutItems.Sort([](const TSharedPtr<FWetClothingAssetWetPartEntry>& A, const TSharedPtr<FWetClothingAssetWetPartEntry>& B)
                  { return A.IsValid() && B.IsValid() ? A->WetPartID < B->WetPartID : A.IsValid(); });
}

TSet<int32> FWetPartEditingService::GetIslandIDsForWetPart(
    const UWetClothingAsset*                             Profile,
    const FWetPartScope&                        Scope,
    const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands,
    int32                                                  WetPartID)
{
    TSet<int32> Result;

    for (const TSharedPtr<FWetClothingAssetUVIsland>& IslandItem : Islands)
    {
        if (IslandItem.IsValid() && GetEffectiveWetPartIDForIsland(Profile, Scope, IslandItem->IslandID) == WetPartID)
        {
            Result.Add(IslandItem->IslandID);
        }
    }

    return Result;
}

int32 FWetPartEditingService::GetEffectiveWetPartIDForIsland(const UWetClothingAsset* Profile, const FWetPartScope& Scope, int32 IslandID)
{
    if (const FWetClothingAssetWetPartEntry* EffectiveEntry = FindEffectiveEntryForIsland(Profile, Scope, IslandID))
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

TMap<int32, int32> FWetPartEditingService::BuildIslandWetPartIDMap(
    const UWetClothingAsset*                             Profile,
    const FWetPartScope&                        Scope,
    const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands)
{
    TMap<int32, int32> Result;

    for (const TSharedPtr<FWetClothingAssetUVIsland>& IslandItem : Islands)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        const int32 EffectiveWetPartID = GetEffectiveWetPartIDForIsland(Profile, Scope, IslandItem->IslandID);
        // if (EffectiveWetPartID == 0)
        // {
        //     continue;
        // }

        if (FindEntry(Profile, Scope, EffectiveWetPartID) != nullptr)
        {
            Result.Add(IslandItem->IslandID, EffectiveWetPartID);
        }
    }

    return Result;
}

TMap<int32, FLinearColor> FWetPartEditingService::BuildIslandColorMap(
    const UWetClothingAsset*                             Profile,
    const FWetPartScope&                        Scope,
    const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands)
{
    TMap<int32, FLinearColor> Result;

    for (const TSharedPtr<FWetClothingAssetUVIsland>& IslandItem : Islands)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        if (const FWetClothingAssetWetPartEntry* Entry = FindEffectiveEntryForIsland(Profile, Scope, IslandItem->IslandID))
        {
            if (Entry->WetPartID != 0 && !Entry->bViewEnabled)
            {
                continue;
            }

            FLinearColor Color = Entry->WetPartID == 0 ? FLinearColor::White : Entry->Color;
            Color.A = 1.0f;
            Result.Add(IslandItem->IslandID, Color);
        }
    }

    return Result;
}
