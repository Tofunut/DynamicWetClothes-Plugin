// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FWCAEditorValidationSnapshot;
struct FWCAValidationReport;

class FDWCEditorValidationReportAdapter
{
  public:
    static FWCAValidationReport BuildReport(const FWCAEditorValidationSnapshot& Snapshot);
};

