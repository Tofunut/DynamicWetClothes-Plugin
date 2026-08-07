//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

class FString;

namespace DWC::Error
{
    DWC_API void SetMessage(FString* OutErrorMessage, const TCHAR* Message);
    DWC_API void SetMessage(FString* OutErrorMessage, const FString& Message);
}
