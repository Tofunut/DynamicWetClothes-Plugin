/*
 *  Wet Part별 색상 정보를 바탕으로 3D Wet Part Overlay Mesh 데이터를 생성합니다.
 */

#include "WetClothingWetPartOverlayMeshBuilder.h"

#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Modes/Part/Viewport/DWCOverlayNormal.h"

void FWetClothingWetPartOverlayMeshBuilder::BuildMeshData(
    const TArray<FWetClothingAssetUVIsland>& Islands,
    const TMap<int32, int32>&                UVIslandToWetPartID,
    const TMap<int32, FLinearColor>&         IslandColors,
    float                                    NormalOffset,
    FDWCOverlayMeshData&             OutMeshData)
{
    OutMeshData.Reset();

    for (const FWetClothingAssetUVIsland& Island : Islands)
    {
        const int32*        WetPartID = UVIslandToWetPartID.Find(Island.UVIslandID);
        const FLinearColor* IslandColor = IslandColors.Find(Island.UVIslandID);
        if (WetPartID == nullptr || *WetPartID == 0 || IslandColor == nullptr)
        {
            continue;
        }

        for (const FWetClothingAssetUVTriangle& UVTriangle : Island.UVTriangles)
        {
            const FVector Normal = DWCOverlayNormal::MakeWetPartOverlayNormal(
                UVTriangle.LocalPositions[0], UVTriangle.LocalPositions[1], UVTriangle.LocalPositions[2]);

            for (float OffsetSign : { 1.0f, -1.0f })
            {
                const FVector OffsetNormal = Normal * OffsetSign;
                const int32   BaseVertexIndex = OutMeshData.Vertices.Num();

                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    OutMeshData.Vertices.Add(UVTriangle.LocalPositions[CornerIndex] + OffsetNormal * NormalOffset);
                    OutMeshData.Normals.Add(OffsetNormal);
                    OutMeshData.UVs.Add(UVTriangle.UVs[CornerIndex]);
                    OutMeshData.VertexColors.Add(*IslandColor);
                }

                OutMeshData.Indices.Add(BaseVertexIndex);
                OutMeshData.Indices.Add(BaseVertexIndex + 1);
                OutMeshData.Indices.Add(BaseVertexIndex + 2);

                OutMeshData.Indices.Add(BaseVertexIndex + 2);
                OutMeshData.Indices.Add(BaseVertexIndex + 1);
                OutMeshData.Indices.Add(BaseVertexIndex);
            }
        }
    }
}
