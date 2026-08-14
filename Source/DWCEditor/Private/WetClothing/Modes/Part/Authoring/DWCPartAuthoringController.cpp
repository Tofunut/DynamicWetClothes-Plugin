// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Part/Authoring/DWCPartAuthoringController.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "DataAssets/WetnessProfile.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Modes/Part/Partition/WetPartEditingService.h"

namespace
{
    constexpr int32 RuntimePartOutputs =
        DWCBakeOutput::CPURuntimeData |
        DWCBakeOutput::GPURuntimeData |
        DWCBakeOutput::GPUMaps;
    constexpr int32 VisualPartOutputs =
        DWCBakeOutput::WrinkleMaps |
        DWCBakeOutput::TransparencyMaps |
        DWCBakeOutput::RenderProfileData;
    constexpr int32 AllPartDerivedOutputs = RuntimePartOutputs | VisualPartOutputs;

    bool AreSurfaceWaterSettingsEqual(
        const FWetPartSurfaceWaterSettings& A,
        const FWetPartSurfaceWaterSettings& B)
    {
        return A.bOverrideDropletStampSize == B.bOverrideDropletStampSize &&
            A.bOverrideDropletFlowStampSize == B.bOverrideDropletFlowStampSize &&
            FMath::IsNearlyEqual(A.DropletRadiusScale, B.DropletRadiusScale) &&
            FMath::IsNearlyEqual(A.DropletFlowSizeScale, B.DropletFlowSizeScale) &&
            FMath::IsNearlyEqual(A.DropletDetailSize, B.DropletDetailSize) &&
            FMath::IsNearlyEqual(A.DropletFlowDetailSize, B.DropletFlowDetailSize);
    }

    bool DoStampSettingsMatch(
        const FWetPartSurfaceWaterSettings& A,
        const FWetPartSurfaceWaterSettings& B)
    {
        return A.bOverrideDropletStampSize == B.bOverrideDropletStampSize &&
            A.bOverrideDropletFlowStampSize == B.bOverrideDropletFlowStampSize &&
            FMath::IsNearlyEqual(A.DropletRadiusScale, B.DropletRadiusScale) &&
            FMath::IsNearlyEqual(A.DropletFlowSizeScale, B.DropletFlowSizeScale);
    }

    FWetClothingAuthoredMaterialSlot& EnsureEditableSlot(
        UWetClothingAsset& MutableAsset,
        const int32 MaterialSlotIndex)
    {
        FWetClothingEditableWetPartData& EditableData =
            MutableAsset.Authored.PartData.EditableWetPartData;
        FWetClothingAuthoredMaterialSlot& Slot = EditableData.FindOrAddMaterialSlot(MaterialSlotIndex);
        Slot.bIsWettableSlot = true;
        FWetPartEditingService::EnsureDefaultWetPartForScope(
            EditableData,
            FWetPartEditingService::MakeScope(MaterialSlotIndex));
        return Slot;
    }
}

FDWCPartAuthoringController::FDWCPartAuthoringController(
    UWetClothingAsset* InAsset,
    TSharedPtr<FDWCEditorAuthoringDocument> InAuthoringDocument)
    : Asset(InAsset)
    , AuthoringDocument(MoveTemp(InAuthoringDocument))
{
}

FDWCPartAuthoringResult FDWCPartAuthoringController::Edit(
    const FText& TransactionText,
    const int32 MaterialSlotIndex,
    const int32 WetPartID,
    const int32 InvalidatedBakeOutputMask,
    const EDWCEditorAuthoringImpact Impact,
    TFunctionRef<bool(UWetClothingAsset&)> Mutation) const
{
    FDWCPartAuthoringResult Result;
    Result.WetPartID = WetPartID;
    if (!AuthoringDocument.IsValid() || !Asset.IsValid())
    {
        Result.Error = TEXT("The Wet Part authoring session is unavailable.");
        return Result;
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Part;
    Change.Impact = Impact | EDWCEditorAuthoringImpact::AssetDirty;
    Change.MaterialSlotIndex = MaterialSlotIndex;
    Change.WetPartID = WetPartID;
    Change.InvalidatedBakeOutputMask = InvalidatedBakeOutputMask;
    const FDWCEditorAuthoringResult DocumentResult =
        AuthoringDocument->Edit(TransactionText, Change, Mutation);
    Result.bChanged = DocumentResult.bChanged;
    Result.Error = DocumentResult.Error;
    return Result;
}

FDWCPartAuthoringResult FDWCPartAuthoringController::SetMaterialSlotWettable(
    const int32 MaterialSlotIndex,
    const bool bWettable) const
{
    FDWCPartAuthoringResult NoChange;
    if (MaterialSlotIndex == INDEX_NONE || !Asset.IsValid())
    {
        NoChange.Error = TEXT("A valid material slot is required.");
        return NoChange;
    }

    const FWetClothingEditableWetPartData& EditableData =
        Asset->Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* Existing = EditableData.FindMaterialSlot(MaterialSlotIndex);
    const bool bHasGeneratedOverride =
        Asset->Derived.Inline.GeneratedWetMaterialOverrides.ContainsByPredicate(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& Override)
            {
                return Override.MaterialSlotIndex == MaterialSlotIndex;
            });
    const bool bAlreadySet = Existing != nullptr && Existing->bIsWettableSlot == bWettable;
    const bool bHasDefaultPart = Existing != nullptr && Existing->FindPart(0) != nullptr;
    if (!bWettable && Existing == nullptr && !bHasGeneratedOverride)
    {
        return NoChange;
    }
    if (bAlreadySet && (bWettable ? bHasDefaultPart : !bHasGeneratedOverride))
    {
        return NoChange;
    }

    return Edit(
        FText::FromString(bWettable ? TEXT("Enable Wettable Material Slot") : TEXT("Disable Wettable Material Slot")),
        MaterialSlotIndex,
        INDEX_NONE,
        AllPartDerivedOutputs,
        EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::PartSlotPresentation,
        [MaterialSlotIndex, bWettable](UWetClothingAsset& MutableAsset)
        {
            FWetClothingEditableWetPartData& MutableData =
                MutableAsset.Authored.PartData.EditableWetPartData;
            FWetClothingAuthoredMaterialSlot& Slot = MutableData.FindOrAddMaterialSlot(MaterialSlotIndex);
            Slot.bIsWettableSlot = bWettable;
            if (bWettable)
            {
                FWetPartEditingService::EnsureDefaultWetPartForScope(
                    MutableData,
                    FWetPartEditingService::MakeScope(MaterialSlotIndex));
            }
            else
            {
                MutableAsset.Derived.Inline.GeneratedWetMaterialOverrides.RemoveAll(
                    [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& Override)
                    {
                        return Override.MaterialSlotIndex == MaterialSlotIndex;
                    });
            }
            return true;
        });
}

FDWCPartAuthoringResult FDWCPartAuthoringController::RenamePart(
    const int32 MaterialSlotIndex,
    const int32 WetPartID,
    const FString& DisplayName) const
{
    const FWetPartScope Scope = FWetPartEditingService::MakeScope(MaterialSlotIndex);
    const FWetClothingWetPartEntry* Entry =
        FWetPartEditingService::FindEntry(Asset.Get(), Scope, WetPartID);
    const FString Trimmed = DisplayName.TrimStartAndEnd();
    const FString ResolvedName = Trimmed.IsEmpty()
        ? FWetPartEditingService::GetDefaultWetPartName(WetPartID)
        : Trimmed;
    if (Entry == nullptr || WetPartID == 0 || Entry->DisplayName == ResolvedName)
    {
        return {};
    }

    return Edit(
        FText::FromString(TEXT("Rename Wet Part")), MaterialSlotIndex, WetPartID, 0,
        EDWCEditorAuthoringImpact::ElementList,
        [MaterialSlotIndex, WetPartID, ResolvedName](UWetClothingAsset& MutableAsset)
        {
            FWetClothingWetPartEntry* MutableEntry = FWetPartEditingService::FindMutableEntry(
                &MutableAsset, FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID);
            if (MutableEntry == nullptr)
            {
                return false;
            }
            MutableEntry->DisplayName = ResolvedName;
            return true;
        });
}

FDWCPartAuthoringResult FDWCPartAuthoringController::SetPartColor(
    const int32 MaterialSlotIndex,
    const int32 WetPartID,
    FLinearColor Color) const
{
    Color.A = 1.0f;
    const FWetClothingWetPartEntry* Entry = FWetPartEditingService::FindEntry(
        Asset.Get(), FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID);
    if (Entry == nullptr || WetPartID == 0 || Entry->Color.Equals(Color))
    {
        return {};
    }
    return Edit(
        FText::FromString(TEXT("Set Wet Part Color")), MaterialSlotIndex, WetPartID, 0,
        EDWCEditorAuthoringImpact::ElementList | EDWCEditorAuthoringImpact::Preview,
        [MaterialSlotIndex, WetPartID, Color](UWetClothingAsset& MutableAsset)
        {
            FWetClothingWetPartEntry* MutableEntry = FWetPartEditingService::FindMutableEntry(
                &MutableAsset, FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID);
            if (MutableEntry == nullptr)
            {
                return false;
            }
            MutableEntry->Color = Color;
            return true;
        });
}

FDWCPartAuthoringResult FDWCPartAuthoringController::SetPartVisibility(
    const int32 MaterialSlotIndex,
    const int32 WetPartID,
    const bool bVisible) const
{
    const FWetClothingWetPartEntry* Entry = FWetPartEditingService::FindEntry(
        Asset.Get(), FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID);
    if (Entry == nullptr || WetPartID == 0 || Entry->bViewEnabled == bVisible)
    {
        return {};
    }
    return Edit(
        FText::FromString(TEXT("Set Wet Part Visibility")), MaterialSlotIndex, WetPartID, 0,
        EDWCEditorAuthoringImpact::ElementList | EDWCEditorAuthoringImpact::Preview,
        [MaterialSlotIndex, WetPartID, bVisible](UWetClothingAsset& MutableAsset)
        {
            FWetClothingWetPartEntry* MutableEntry = FWetPartEditingService::FindMutableEntry(
                &MutableAsset, FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID);
            if (MutableEntry == nullptr)
            {
                return false;
            }
            MutableEntry->bViewEnabled = bVisible;
            return true;
        });
}

FDWCPartAuthoringResult FDWCPartAuthoringController::SetPartProfile(
    const int32 MaterialSlotIndex,
    const int32 WetPartID,
    const FSoftObjectPath& SourceProfilePath,
    const FWetnessProfileParameters* ProfileParameters) const
{
    if (WetPartID == 0 || (SourceProfilePath.IsValid() && ProfileParameters == nullptr))
    {
        return {};
    }
    const FWetClothingWetPartEntry* CurrentEntry = FWetPartEditingService::FindEntry(
        Asset.Get(), FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID);
    if (CurrentEntry == nullptr)
    {
        return {};
    }
    const FWetClothingEditableWetPartData& CurrentData =
        Asset->Authored.PartData.EditableWetPartData;
    const FWetPartProfileAssignment* CurrentProfile = CurrentData.FindProfile(*CurrentEntry);
    if (!SourceProfilePath.IsValid() && CurrentEntry->ProfileIndex == 0)
    {
        return {};
    }
    if (SourceProfilePath.IsValid() && CurrentProfile != nullptr &&
        CurrentProfile->GetSourceProfilePath() == SourceProfilePath &&
        FWetnessProfileParameters::StaticStruct()->CompareScriptStruct(
            &CurrentProfile->Parameters, ProfileParameters, 0))
    {
        return {};
    }
    const TOptional<FWetnessProfileParameters> Parameters =
        ProfileParameters != nullptr
            ? TOptional<FWetnessProfileParameters>(*ProfileParameters)
            : TOptional<FWetnessProfileParameters>();
    return Edit(
        FText::FromString(TEXT("Set Wet Part Profile")), MaterialSlotIndex, WetPartID,
        AllPartDerivedOutputs,
        EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::PartSlotPresentation,
        [MaterialSlotIndex, WetPartID, SourceProfilePath, Parameters](UWetClothingAsset& MutableAsset)
        {
            FWetClothingEditableWetPartData& EditableData =
                MutableAsset.Authored.PartData.EditableWetPartData;
            EnsureEditableSlot(MutableAsset, MaterialSlotIndex);
            FWetClothingWetPartEntry* Entry = EditableData.FindMaterialSlot(MaterialSlotIndex)->FindPart(WetPartID);
            if (Entry == nullptr)
            {
                return false;
            }
            Entry->ProfileIndex = SourceProfilePath.IsValid()
                ? EditableData.FindOrAddProfile(SourceProfilePath, Parameters.GetValue())
                : 0;
            EditableData.CompactProfiles();
            return true;
        });
}

FDWCPartAuthoringResult FDWCPartAuthoringController::AddPart(const int32 MaterialSlotIndex) const
{
    const int32 NewWetPartID = FWetPartEditingService::FindNextWetPartIDForScope(
        Asset.Get(), FWetPartEditingService::MakeScope(MaterialSlotIndex));
    return Edit(
        FText::FromString(TEXT("Add Wet Part")), MaterialSlotIndex, NewWetPartID,
        AllPartDerivedOutputs,
        EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::PartSlotPresentation,
        [MaterialSlotIndex, NewWetPartID](UWetClothingAsset& MutableAsset)
        {
            FWetClothingAuthoredMaterialSlot& Slot = EnsureEditableSlot(MutableAsset, MaterialSlotIndex);
            if (Slot.FindPart(NewWetPartID) != nullptr)
            {
                return false;
            }
            FWetClothingWetPartEntry& NewEntry = Slot.WetPartEntries.AddDefaulted_GetRef();
            NewEntry.WetPartID = NewWetPartID;
            NewEntry.DisplayName = FWetPartEditingService::GetDefaultWetPartName(NewWetPartID);
            NewEntry.Color = FWetPartEditingService::GetDefaultWetPartColor(NewWetPartID);
            NewEntry.bViewEnabled = true;
            NewEntry.ProfileIndex = 0;
            return true;
        });
}

FDWCPartAuthoringResult FDWCPartAuthoringController::RemovePart(
    const int32 MaterialSlotIndex,
    const int32 WetPartID) const
{
    if (WetPartID <= 0 || FWetPartEditingService::FindEntry(
            Asset.Get(), FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID) == nullptr)
    {
        return {};
    }
    return Edit(
        FText::FromString(TEXT("Remove Wet Part")), MaterialSlotIndex, WetPartID,
        AllPartDerivedOutputs,
        EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::PartSlotPresentation,
        [MaterialSlotIndex, WetPartID](UWetClothingAsset& MutableAsset)
        {
            FWetClothingEditableWetPartData& EditableData =
                MutableAsset.Authored.PartData.EditableWetPartData;
            FWetClothingAuthoredMaterialSlot* Slot = EditableData.FindMaterialSlot(MaterialSlotIndex);
            if (Slot == nullptr || Slot->WetPartEntries.RemoveAll(
                    [WetPartID](const FWetClothingWetPartEntry& Entry)
                    {
                        return Entry.WetPartID == WetPartID;
                    }) == 0)
            {
                return false;
            }
            EditableData.CompactProfiles();
            FWetPartEditingService::EnsureDefaultWetPartForScope(
                EditableData, FWetPartEditingService::MakeScope(MaterialSlotIndex));
            return true;
        });
}

FDWCPartAuthoringResult FDWCPartAuthoringController::ResetPart(
    const int32 MaterialSlotIndex,
    const int32 WetPartID) const
{
    const FWetClothingWetPartEntry* Entry = FWetPartEditingService::FindEntry(
        Asset.Get(), FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID);
    if (Entry == nullptr || WetPartID == 0)
    {
        return {};
    }
    const FWetPartSurfaceWaterSettings DefaultSurfaceWater;
    const bool bProfileChanged = Entry->ProfileIndex != 0;
    const bool bSurfaceChanged = !AreSurfaceWaterSettingsEqual(Entry->SurfaceWater, DefaultSurfaceWater);
    const bool bDisplayChanged =
        Entry->DisplayName != FWetPartEditingService::GetDefaultWetPartName(WetPartID) ||
        !Entry->Color.Equals(FWetPartEditingService::GetDefaultWetPartColor(WetPartID)) ||
        !Entry->bViewEnabled;
    if (!bProfileChanged && !bSurfaceChanged && !bDisplayChanged)
    {
        return {};
    }

    const int32 OutputMask = bProfileChanged
        ? AllPartDerivedOutputs
        : (bSurfaceChanged ? (DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps) : 0);
    return Edit(
        FText::FromString(TEXT("Reset Wet Part Settings")), MaterialSlotIndex, WetPartID,
        OutputMask,
        EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::PartSlotPresentation,
        [MaterialSlotIndex, WetPartID](UWetClothingAsset& MutableAsset)
        {
            FWetClothingWetPartEntry* MutableEntry = FWetPartEditingService::FindMutableEntry(
                &MutableAsset, FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID);
            if (MutableEntry == nullptr)
            {
                return false;
            }
            MutableEntry->DisplayName = FWetPartEditingService::GetDefaultWetPartName(WetPartID);
            MutableEntry->Color = FWetPartEditingService::GetDefaultWetPartColor(WetPartID);
            MutableEntry->bViewEnabled = true;
            MutableEntry->ProfileIndex = 0;
            MutableEntry->SurfaceWater = FWetPartSurfaceWaterSettings();
            MutableAsset.Authored.PartData.EditableWetPartData.CompactProfiles();
            return true;
        });
}

FDWCPartAuthoringResult FDWCPartAuthoringController::ReplaceWithAutoPartition(
    const int32 MaterialSlotIndex,
    const TArray<FWetPartAutoPartitionCluster>& Clusters) const
{
    return Edit(
        FText::FromString(TEXT("Apply Wet Part Auto Partition")), MaterialSlotIndex,
        Clusters.IsEmpty() ? 0 : 1, AllPartDerivedOutputs,
        EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::PartSlotPresentation,
        [MaterialSlotIndex, Clusters](UWetClothingAsset& MutableAsset)
        {
            FWetClothingEditableWetPartData& EditableData =
                MutableAsset.Authored.PartData.EditableWetPartData;
            FWetClothingAuthoredMaterialSlot& Slot = EnsureEditableSlot(MutableAsset, MaterialSlotIndex);
            Slot.WetPartEntries.RemoveAll(
                [](const FWetClothingWetPartEntry& Entry)
                {
                    return Entry.WetPartID != 0;
                });
            FWetClothingWetPartEntry* DefaultEntry = Slot.FindPart(0);
            check(DefaultEntry != nullptr);
            *DefaultEntry = FWetClothingWetPartEntry();
            DefaultEntry->WetPartID = 0;
            DefaultEntry->DisplayName = FWetPartEditingService::GetDefaultWetPartName(0);
            DefaultEntry->Color = FWetPartEditingService::GetDefaultWetPartColor(0);
            for (int32 ClusterIndex = 0; ClusterIndex < Clusters.Num(); ++ClusterIndex)
            {
                const int32 NewWetPartID = ClusterIndex + 1;
                FWetClothingWetPartEntry& NewEntry = Slot.WetPartEntries.AddDefaulted_GetRef();
                NewEntry.WetPartID = NewWetPartID;
                NewEntry.DisplayName = FWetPartEditingService::GetDefaultWetPartName(NewWetPartID);
                NewEntry.Color = FWetPartEditingService::GetDefaultWetPartColor(NewWetPartID);
                NewEntry.bViewEnabled = true;
                NewEntry.AssignedUVIslandIDs = Clusters[ClusterIndex].UVIslandIDs;
                NewEntry.AssignedUVIslandIDs.Sort();
                NewEntry.ProfileIndex = 0;
            }
            EditableData.CompactProfiles();
            return true;
        });
}

FDWCPartAuthoringResult FDWCPartAuthoringController::AssignIslands(
    const int32 MaterialSlotIndex,
    const int32 WetPartID,
    const TSet<int32>& UVIslandIDs) const
{
    if (UVIslandIDs.IsEmpty())
    {
        return {};
    }
    bool bHasChange = false;
    const FWetPartScope Scope = FWetPartEditingService::MakeScope(MaterialSlotIndex);
    for (const int32 UVIslandID : UVIslandIDs)
    {
        if (FWetPartEditingService::GetEffectiveWetPartIDForUVIsland(Asset.Get(), Scope, UVIslandID) != WetPartID)
        {
            bHasChange = true;
            break;
        }
    }
    if (!bHasChange)
    {
        return {};
    }

    TArray<int32> SortedIslandIDs = UVIslandIDs.Array();
    SortedIslandIDs.Sort();
    return Edit(
        FText::FromString(TEXT("Assign UV Islands to Wet Part")), MaterialSlotIndex, WetPartID,
        AllPartDerivedOutputs,
        EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::Details |
            EDWCEditorAuthoringImpact::PartSlotPresentation,
        [MaterialSlotIndex, WetPartID, SortedIslandIDs](UWetClothingAsset& MutableAsset)
        {
            FWetClothingAuthoredMaterialSlot& Slot = EnsureEditableSlot(MutableAsset, MaterialSlotIndex);
            FWetClothingWetPartEntry* Target = Slot.FindPart(WetPartID);
            if (Target == nullptr)
            {
                return false;
            }
            for (FWetClothingWetPartEntry& Entry : Slot.WetPartEntries)
            {
                for (const int32 UVIslandID : SortedIslandIDs)
                {
                    Entry.AssignedUVIslandIDs.Remove(UVIslandID);
                }
            }
            if (WetPartID != 0)
            {
                for (const int32 UVIslandID : SortedIslandIDs)
                {
                    Target->AssignedUVIslandIDs.AddUnique(UVIslandID);
                }
                Target->AssignedUVIslandIDs.Sort();
            }
            return true;
        });
}

FDWCPartAuthoringResult FDWCPartAuthoringController::ApplySurfaceWaterSettings(
    const int32 MaterialSlotIndex,
    const int32 WetPartID,
    const FWetPartSurfaceWaterSettings& Settings) const
{
    const FWetClothingWetPartEntry* Entry = FWetPartEditingService::FindEntry(
        Asset.Get(), FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID);
    if (Entry == nullptr || AreSurfaceWaterSettingsEqual(Entry->SurfaceWater, Settings))
    {
        return {};
    }
    const int32 OutputMask = DoStampSettingsMatch(Entry->SurfaceWater, Settings)
        ? DWCBakeOutput::GPUMaps
        : (DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps);
    return Edit(
        FText::FromString(TEXT("Apply Surface Water Tiling Settings")),
        MaterialSlotIndex, WetPartID, OutputMask,
        EDWCEditorAuthoringImpact::Preview | EDWCEditorAuthoringImpact::Details,
        [MaterialSlotIndex, WetPartID, Settings](UWetClothingAsset& MutableAsset)
        {
            FWetClothingWetPartEntry* MutableEntry = FWetPartEditingService::FindMutableEntry(
                &MutableAsset, FWetPartEditingService::MakeScope(MaterialSlotIndex), WetPartID);
            if (MutableEntry == nullptr)
            {
                return false;
            }
            MutableEntry->SurfaceWater = Settings;
            return true;
        });
}
