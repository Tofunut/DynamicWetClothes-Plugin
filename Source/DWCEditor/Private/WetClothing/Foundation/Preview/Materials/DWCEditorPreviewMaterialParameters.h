#pragma once

#include "CoreMinimal.h"

namespace DWCEditorPreviewMaterialParameters
{
    inline const FName& PreviewWetness()
    {
        static const FName Name(TEXT("DWC_PreviewWetness"));
        return Name;
    }
}
