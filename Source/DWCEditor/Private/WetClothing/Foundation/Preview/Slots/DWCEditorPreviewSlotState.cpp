//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Preview/Slots/DWCEditorPreviewSlotState.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetPartDataTextureBaker.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"

#define LOCTEXT_NAMESPACE "DWCEditorPreviewSlotState"

namespace
{
    uint32 AddStateHash(uint32 Hash, const FDWCEditorPreviewSlotState& State)
    {
        Hash = HashCombine(Hash, static_cast<uint32>(State.MaterialSlotIndex));
        Hash = HashCombine(Hash, FCrc::StrCrc32(*State.MaterialSlotName.ToString()));
        Hash = HashCombine(Hash, PointerHash(State.SourceMaterial.Get()));
        Hash = HashCombine(Hash, PointerHash(State.WetPartDataTexture.Get()));
        Hash = HashCombine(Hash, State.bWettable ? 1u : 0u);
        Hash = HashCombine(Hash, State.bPreviewReady ? 1u : 0u);
        return HashCombine(Hash, static_cast<uint32>(State.Issue));
    }
}

const FDWCEditorPreviewSlotState* FDWCEditorPreviewSlotCollection::Find(const int32 MaterialSlotIndex) const
{
    if (!Slots.IsValidIndex(MaterialSlotIndex))
    {
        return nullptr;
    }

    const FDWCEditorPreviewSlotState& State = Slots[MaterialSlotIndex];
    return State.MaterialSlotIndex == MaterialSlotIndex ? &State : nullptr;
}

bool FDWCEditorPreviewSlotCollection::IsReady(const int32 MaterialSlotIndex) const
{
    const FDWCEditorPreviewSlotState* State = Find(MaterialSlotIndex);
    return State != nullptr && State->bPreviewReady;
}

FDWCEditorPreviewSlotCollection FDWCEditorPreviewSlotResolver::Resolve(
    const UWetClothingAsset* WetClothingAsset)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorPreviewSlotResolver_Resolve);
    FDWCEditorPreviewSlotCollection Result;
    if (WetClothingAsset == nullptr)
    {
        return Result;
    }

    const USkeletalMesh* Mesh = WetClothingAsset->GetDWCSkeletalMesh();
    if (Mesh == nullptr)
    {
        return Result;
    }

    const bool bHasValidDataUV = WetClothingAsset->HasValidDataUVForLOD(0);
    const FWetClothingBakedWetPartData& Baked = WetClothingAsset->Derived.Inline.BakedWetPartData;
    const bool bHasBakedHeader = Baked.DataUVChannelIndex != INDEX_NONE &&
        !Baked.BuildSignature.IsEmpty() &&
        Baked.NormalizedNeutralSurfaceNormal != nullptr &&
        Baked.LocalProfiles.Num() <= DWCWetPartDataTextureBake::MaxLocalProfileCount;
    const bool bBakedSettingsCurrent =
        Baked.Resolution == DWCWetPartDataTextureBake::Resolution &&
        Baked.PaddingPixels == DWCWetPartDataTextureBake::PaddingPixels &&
        Baked.SurfaceTextureResolution == DWCSurfaceTextureNormalization::Resolution;
    const FString ExpectedGlobalSignature = bHasValidDataUV
        ? FWetClothingWetPartDataTextureBaker::MakeBuildSignature(WetClothingAsset)
        : FString();
    const bool bBakedDataCurrent = bHasBakedHeader && bBakedSettingsCurrent &&
        Baked.DataUVChannelIndex == WetClothingAsset->GetDWCDataUVChannelIndex() &&
        !ExpectedGlobalSignature.IsEmpty() &&
        Baked.BuildSignature == ExpectedGlobalSignature;

    const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
    Result.Slots.Reserve(Materials.Num());
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Materials.Num(); ++MaterialSlotIndex)
    {
        const FSkeletalMaterial& SkeletalMaterial = Materials[MaterialSlotIndex];
        FDWCEditorPreviewSlotState& State = Result.Slots.AddDefaulted_GetRef();
        State.MaterialSlotIndex = MaterialSlotIndex;
        State.MaterialSlotName = SkeletalMaterial.MaterialSlotName;
        State.SourceMaterial = FWCAMaterialGenerator::ResolveGeneratedMaterialSource(
            WetClothingAsset,
            MaterialSlotIndex,
            SkeletalMaterial.MaterialInterface);
        State.bWettable = WetClothingAsset->IsMaterialSlotWettable(MaterialSlotIndex);

        if (!State.bWettable)
        {
            State.Issue = EDWCEditorPreviewSlotIssue::NotWettable;
        }
        else
        {
            ++Result.WettableSlotCount;
            UMaterial* SourceBaseMaterial = State.SourceMaterial.IsValid()
                ? State.SourceMaterial->GetMaterial()
                : nullptr;
            if (SourceBaseMaterial == nullptr)
            {
                State.Issue = EDWCEditorPreviewSlotIssue::MissingSourceMaterial;
            }
            else if (SourceBaseMaterial->bUseMaterialAttributes)
            {
                State.Issue = EDWCEditorPreviewSlotIssue::UnsupportedMaterialGraph;
            }
            else if (!bHasValidDataUV)
            {
                State.Issue = EDWCEditorPreviewSlotIssue::MissingDataUV;
            }
            else if (!bHasBakedHeader)
            {
                State.Issue = EDWCEditorPreviewSlotIssue::WetPartDataMissing;
            }
            else if (!bBakedDataCurrent)
            {
                State.Issue = EDWCEditorPreviewSlotIssue::WetPartDataOutOfDate;
            }
            else if (const FWetClothingBakedWetPartDataSlotTexture* BakedSlot = Baked.FindSlot(MaterialSlotIndex);
                     BakedSlot == nullptr || BakedSlot->WetPartDataTexture == nullptr)
            {
                State.Issue = EDWCEditorPreviewSlotIssue::SlotTextureMissing;
            }
            else
            {
                State.WetPartDataTexture = BakedSlot->WetPartDataTexture;
                const FString ExpectedSlotSignature =
                    FWetClothingWetPartDataTextureBaker::MakeSlotBuildSignature(
                        ExpectedGlobalSignature,
                        MaterialSlotIndex);
                if (BakedSlot->BuildSignature != ExpectedSlotSignature)
                {
                    State.Issue = EDWCEditorPreviewSlotIssue::SlotTextureOutOfDate;
                }
                else
                {
                    State.Issue = EDWCEditorPreviewSlotIssue::None;
                    State.bPreviewReady = true;
                    Result.ReadyWettableSlotIndices.Add(MaterialSlotIndex);
                }
            }

            if (!State.bPreviewReady)
            {
                ++Result.SkippedWettableSlotCount;
            }
        }

        Result.StateSignature = AddStateHash(Result.StateSignature, State);
    }

    Result.StateSignature = HashCombine(
        Result.StateSignature,
        FCrc::MemCrc32(&Baked.BakeGuid, sizeof(Baked.BakeGuid)));
    Result.StateSignature = HashCombine(
        Result.StateSignature,
        FCrc::StrCrc32(*Baked.BuildSignature));
    Result.StateSignature = HashCombine(
        Result.StateSignature,
        static_cast<uint32>(WetClothingAsset->GetDWCDataUVChannelIndex()));
    return Result;
}

FText FDWCEditorPreviewSlotResolver::GetIssueText(const EDWCEditorPreviewSlotIssue Issue)
{
    switch (Issue)
    {
    case EDWCEditorPreviewSlotIssue::None:
        return LOCTEXT("Ready", "Ready");
    case EDWCEditorPreviewSlotIssue::MissingPreparedMesh:
        return LOCTEXT("MissingPreparedMesh", "The prepared DWC Skeletal Mesh is missing.");
    case EDWCEditorPreviewSlotIssue::InvalidMaterialSlot:
        return LOCTEXT("InvalidMaterialSlot", "The material slot is no longer valid on LOD 0.");
    case EDWCEditorPreviewSlotIssue::MissingSourceMaterial:
        return LOCTEXT("MissingSourceMaterial", "The source material is missing.");
    case EDWCEditorPreviewSlotIssue::UnsupportedMaterialGraph:
        return LOCTEXT(
            "UnsupportedMaterialGraph",
            "The source material uses Material Attributes, which the common DWC editor preview does not support yet.");
    case EDWCEditorPreviewSlotIssue::NotWettable:
        return LOCTEXT("NotWettable", "The material slot is not Wettable.");
    case EDWCEditorPreviewSlotIssue::MissingDataUV:
        return LOCTEXT("MissingDataUV", "Generate the WCA Data UV for LOD 0 before editing this slot.");
    case EDWCEditorPreviewSlotIssue::WetPartDataMissing:
        return LOCTEXT("WetPartDataMissing", "Bake Wet Part Data for this WCA before editing this slot.");
    case EDWCEditorPreviewSlotIssue::WetPartDataOutOfDate:
        return LOCTEXT("WetPartDataOutOfDate", "The baked Wet Part Data is out of date.");
    case EDWCEditorPreviewSlotIssue::SlotTextureMissing:
        return LOCTEXT("SlotTextureMissing", "The Wet Part Data Texture for this slot is missing.");
    case EDWCEditorPreviewSlotIssue::SlotTextureOutOfDate:
        return LOCTEXT("SlotTextureOutOfDate", "The Wet Part Data Texture for this slot is out of date.");
    case EDWCEditorPreviewSlotIssue::InvalidAsset:
    default:
        return LOCTEXT("InvalidAsset", "The Wet Clothing Asset is unavailable.");
    }
}

FText FDWCEditorPreviewSlotResolver::GetAggregateTooltip(
    const FDWCEditorPreviewSlotCollection& Collection)
{
    if (Collection.ReadyWettableSlotIndices.IsEmpty())
    {
        return LOCTEXT("NoReadyWettableSlots", "No Wettable slots are ready for preview.");
    }

    if (Collection.SkippedWettableSlotCount == 0)
    {
        return FText::Format(
            LOCTEXT("AllWettableSlotsReady", "Previewing all {0} Wettable slots."),
            FText::AsNumber(Collection.ReadyWettableSlotIndices.Num()));
    }

    return FText::Format(
        LOCTEXT("SomeWettableSlotsSkipped", "Previewing {0} Wettable slots. {1} slots were skipped because their preview data is missing or out of date."),
        FText::AsNumber(Collection.ReadyWettableSlotIndices.Num()),
        FText::AsNumber(Collection.SkippedWettableSlotCount));
}

#undef LOCTEXT_NAMESPACE
