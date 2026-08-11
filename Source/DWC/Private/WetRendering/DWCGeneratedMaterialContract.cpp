// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetRendering/DWCGeneratedMaterialContract.h"

bool DWCGeneratedMaterialContract::IsRuntimeCompatible(
    const int32    GeneratorVersion,
    const FString& GenerationSignature,
    const FString& SourceMaterialSignature)
{
    return GeneratorVersion == CurrentGeneratorVersion &&
           !GenerationSignature.IsEmpty() &&
           !SourceMaterialSignature.IsEmpty();
}
