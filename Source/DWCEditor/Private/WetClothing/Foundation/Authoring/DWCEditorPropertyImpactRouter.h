// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"

struct FPropertyChangedEvent;
class UWetClothingAsset;

/** Typed result for a WCA property edit observed outside an authoring controller. */
struct FDWCEditorPropertyImpactRoute
{
    FDWCEditorAuthoringChange Change;
    bool bRelevant = false;
    bool bApplyCommittedImpact = false;
    bool bRequestFullRefresh = false;
    bool bKnownProperty = false;
};

/**
 * Maps reflected WCA property edits to the same impact contract used by
 * authoring commands. The router is read-only and must never load payloads.
 */
class FDWCEditorPropertyImpactRouter final
{
  public:
    static FDWCEditorPropertyImpactRoute Route(
        const UWetClothingAsset& Asset,
        const FPropertyChangedEvent& Event);
};
