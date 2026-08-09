//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageContracts.h"

bool FDWCTransparencyStageIdentity::IsValid() const
{
    return LayerGuid.IsValid() && MaterialSlotIndex != INDEX_NONE &&
        DataUVChannelIndex != INDEX_NONE && LODIndex == 0 &&
        Resolution.X > 0 && Resolution.Y > 0;
}
