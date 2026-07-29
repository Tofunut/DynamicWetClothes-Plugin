#pragma once

#include "CoreMinimal.h"
#include "DWCDataUVGenerationTypes.h"

/** Result of making logical Data UV chart boundaries into real mesh seams. */
struct FDWCDataUVSeamSplitResult
{
    bool bSucceeded = false;
    int32 SplitVertexInstanceCount = 0;
    int32 AffectedPolygonCount = 0;
    FString Message;
};

/**
 * Splits VertexInstances shared by triangles assigned to different packed Data UV charts.
 * The caller must pass the same triangle array used to build the charts because its corner
 * VertexInstance IDs are updated to match the edited MeshDescription.
 */
class FDWCDataUVSeamSplitter
{
public:
    static FDWCDataUVSeamSplitResult SplitChartBoundaries(
        FMeshDescription& MeshDescription,
        TArray<FDWCDataUVTriangle>& Triangles,
        const TArray<FDWCDataUVChart>& Charts);
};
