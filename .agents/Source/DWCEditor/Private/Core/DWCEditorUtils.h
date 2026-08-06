#pragma once

#include "CoreMinimal.h"

class UObject;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnDWCEditorAssetSaved, UObject*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDWCEditorAssetSaveAttemptFinished, UObject*, bool);

namespace DWCEditorUtils
{
    bool SaveAsset(UObject* Asset, bool bPrepareRuntimeData = true);
    FOnDWCEditorAssetSaved& OnAssetSaved();
    FOnDWCEditorAssetSaveAttemptFinished& OnAssetSaveAttemptFinished();
} // namespace DWCEditorUtils
