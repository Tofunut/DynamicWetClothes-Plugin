//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

using FDWCInstanceID = int32;
using FDWCFrameNumber = int32;

/*
Defines only the small common value types shared across DWC.

Current design rules:
- Do not introduce broad execution bundles such as ReceiverContext, RequestData, or task metadata here.
- Domain data such as RuntimeData, SimulationState, MaterialInstance, and SkeletalMeshComponent is passed
  directly to the stage functions that require it.
- Keep this file limited to IDs, enums, and small value types reused throughout the plugin.

If a multithreaded TaskQueue is expanded later, task-specific metadata should remain in the dedicated task layer.
*/
enum class EDWCResultCode : uint8
{
    Succeeded,
    Failed,
    Skipped
};
