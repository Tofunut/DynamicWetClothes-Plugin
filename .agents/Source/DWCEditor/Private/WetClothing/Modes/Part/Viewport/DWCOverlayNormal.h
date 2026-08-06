#pragma once

#include "CoreMinimal.h"

namespace DWCOverlayNormal
{
    inline FVector MakeWetPartOverlayNormal(const FVector& A, const FVector& B, const FVector& C)
    {
        FVector Normal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
        if (Normal.IsNearlyZero())
        {
            Normal = FVector::UpVector;
        }
        return Normal;
    }
} // namespace DWCOverlayNormal
