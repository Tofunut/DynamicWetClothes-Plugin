#pragma once

#include "CoreTypes.h"

class FString;

namespace DWC::Error
{
    DWC_API void SetMessage(FString* OutErrorMessage, const TCHAR* Message);
}
