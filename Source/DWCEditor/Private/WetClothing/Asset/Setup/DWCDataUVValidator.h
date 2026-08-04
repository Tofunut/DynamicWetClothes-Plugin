#pragma once

#include "CoreMinimal.h"
#include "DWCDataUVGenerationTypes.h"

/** Final validation for packed DWC UV Channel coordinates before they are committed to the mesh. */
class FDWCDataUVValidator
{
public:
    static bool Validate(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const TArray<FDWCDataUVChart>& Charts,
        const TMap<int32, FVector2f>& PackedUVByVertexInstance,
        int32 OutputResolution,
        TSet<int32>& OutProblemMaterialSlots,
        FString& OutError,
        FDWCDataUVValidationFailure* OutFailure = nullptr);
};
