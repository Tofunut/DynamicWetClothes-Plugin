// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace DWCGeneratedMaterialContract
{
    inline constexpr int32 CurrentGeneratorVersion = 8;

    /** Runtime may only bind generated materials whose editor-authored contract is complete and current. */
    DWC_API bool IsRuntimeCompatible(
        int32          GeneratorVersion,
        const FString& GenerationSignature,
        const FString& SourceMaterialSignature);
} // namespace DWCGeneratedMaterialContract
