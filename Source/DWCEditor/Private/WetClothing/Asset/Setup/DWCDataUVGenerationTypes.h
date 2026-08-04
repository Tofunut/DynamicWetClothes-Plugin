#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"

/** Transient triangle data read from one editable Skeletal Mesh LOD. */
struct FDWCDataUVTriangle
{
    FTriangleID TriangleID;
    int32 MaterialSlotIndex = INDEX_NONE;
    FVertexInstanceID VertexInstances[3];
    FVertexID Vertices[3];
    FVector Positions[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
    FVector2D SourceUVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
};

/** Transient packing unit in generated DWC UV Channel space. Not the persistent Original UV island record. */
struct FDWCDataUVChart
{
    int32 MaterialSlotIndex = INDEX_NONE;
    TArray<int32> TriangleIndices;
    TMap<int32, FVector2D> RawUVByVertexInstance;
    FBox2D RawBounds = FBox2D(ForceInit);
    double RawArea = 0.0;
};

/** Structured details for a final packed DWC UV validation failure. */
struct FDWCDataUVValidationFailure
{
    bool bIsValid = false;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 MeshTriangleID = INDEX_NONE;
    int32 GeneratorTriangleIndex = INDEX_NONE;
    int32 ChartIndex = INDEX_NONE;
    double PackedArea = 0.0;
    FString Reason;
    FVector2D PackedUVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
};
