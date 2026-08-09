//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"

class FDWCEditorBuildActionRegistry
{
  public:
    static TConstArrayView<FDWCEditorBuildActionDescriptor> GetDescriptors();
    static const FDWCEditorBuildActionDescriptor* Find(EDWCEditorBuildAction Action);
    static bool Validate(FString& OutError);
};

