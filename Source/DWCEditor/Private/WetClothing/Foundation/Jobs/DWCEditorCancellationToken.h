// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FDWCEditorCancellationToken final
{
  public:
    void Cancel() { bCanceled.Store(true); }
    bool IsCanceled() const { return bCanceled.Load(); }

  private:
    TAtomic<bool> bCanceled{ false };
};
