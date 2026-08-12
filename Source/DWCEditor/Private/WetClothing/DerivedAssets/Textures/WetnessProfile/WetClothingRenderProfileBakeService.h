// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;

enum class EDWCRenderProfileIssueResolution : uint8
{
    BakeRenderProfile,
    GenerateMaterials,
    Manual
};

/** Structured pending state shared by validation, Build Status, and the bake UI. */
struct FDWCRenderProfileValidationIssue
{
    FName Code;
    int32 MaterialSlotIndex = INDEX_NONE;
    FString ProfileStableKey;
    FString Detail;
    EDWCRenderProfileIssueResolution Resolution = EDWCRenderProfileIssueResolution::BakeRenderProfile;
    bool bFailed = false;
};

struct FDWCRenderProfileValidationSnapshot
{
    TArray<FDWCRenderProfileValidationIssue> Issues;
    bool bRequired = false;

    bool HasPendingTasks() const { return !Issues.IsEmpty(); }
};

class FWetClothingRenderProfileBakeService
{
  public:
    static FDWCRenderProfileValidationSnapshot EvaluateVisualBakeState(
        const UWetClothingAsset* WetClothingAsset);
    static bool HasPendingVisualBakeTasks(const UWetClothingAsset* WetClothingAsset, FString* OutSummary = nullptr);
    static bool BakeRenderProfileDataAndUpdateMaterials(UWetClothingAsset* WetClothingAsset, FString& OutSummary, bool* OutHadWarnings = nullptr);
    static bool SaveBakedRenderProfileAssets(UWetClothingAsset* WetClothingAsset);
};
