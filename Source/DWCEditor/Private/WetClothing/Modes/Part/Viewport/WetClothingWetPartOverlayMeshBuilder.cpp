//Copyright 2026 Team Tofunut. All Rights Reserved.
/*
 * Wet Part colors are emitted on the original reference-pose surface.
 * Camera-facing depth bias is applied by the dedicated overlay material.
 */

#include "WetClothingWetPartOverlayMeshBuilder.h"

#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Modes/Part/Viewport/DWCOverlayNormal.h"

void FWetClothingWetPartOverlayMeshBuilder::BuildMeshData(
    const TArray<FWetClothingAssetUVIsland>& Islands,
    const TMap<int32, int32>& UVIslandToWetPartID,
    const TMap<int32, FLinearColor>& IslandColors,
    const float /*NormalOffset*/,
    const float ColorIntensity,
    FDWCOverlayMeshData& OutMeshData)
{
    OutMeshData.Reset();
    const float ClampedIntensity = FMath::Clamp(ColorIntensity, 0.0f, 1.0f);

    for (const FWetClothingAssetUVIsland& Island : Islands)
    {
        const int32* WetPartID = UVIslandToWetPartID.Find(Island.UVIslandID);
        const FLinearColor* IslandColor = IslandColors.Find(Island.UVIslandID);
        if (WetPartID == nullptr || *WetPartID <= 0 || IslandColor == nullptr)
        {
            continue;
        }

        FLinearColor DisplayColor = FMath::Lerp(FLinearColor::White, *IslandColor, ClampedIntensity);
        DisplayColor.A = 1.0f;
        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            const FVector FaceNormal = DWCOverlayNormal::MakeWetPartOverlayNormal(
                Triangle.LocalPositions[0], Triangle.LocalPositions[1], Triangle.LocalPositions[2]);
            const int32 BaseVertexIndex = OutMeshData.Vertices.Num();
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                FVector VertexNormal = Triangle.LocalNormals[CornerIndex].GetSafeNormal();
                if (VertexNormal.IsNearlyZero())
                {
                    VertexNormal = FaceNormal;
                }
                if (FVector::DotProduct(VertexNormal, FaceNormal) < 0.0f)
                {
                    VertexNormal *= -1.0f;
                }

                OutMeshData.Vertices.Add(Triangle.LocalPositions[CornerIndex]);
                OutMeshData.Normals.Add(VertexNormal);
                OutMeshData.UVs.Add(Triangle.UVs[CornerIndex]);
                OutMeshData.VertexColors.Add(DisplayColor);
            }
            OutMeshData.Indices.Add(BaseVertexIndex);
            OutMeshData.Indices.Add(BaseVertexIndex + 1);
            OutMeshData.Indices.Add(BaseVertexIndex + 2);
        }
    }
}
