// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "RuntimeState/DWCOriginalUVRuntimeTopology.h"

#if WITH_EDITOR

#include "DataAssets/WetClothingAssetSetupData.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Utility/DWCError.h"

bool FDWCOriginalUVRuntimeTopologyAdapter::ReadMaterialSlotTriangles(
    const USkeletalMesh*                 SkeletalMesh,
    const FSkeletalMeshLODRenderData&    LODData,
    const TArray<uint32>&                IndexBuffer,
    const int32                          UVChannelIndex,
    const int32                          MaterialSlotIndex,
    TArray<FDWCRuntimeTopologyTriangle>& OutTriangles,
    FString*                             OutErrorMessage)
{
    OutTriangles.Reset();

    if (SkeletalMesh == nullptr || !SkeletalMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("A wet part references an invalid material slot."));
        return false;
    }

    const int32 NumUVChannels = static_cast<int32>(LODData.GetNumTexCoords());
    if (UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("A wet part references a UV channel that is not available on the mesh."));
        return false;
    }

    const int32 VertexCount = LODData.GetNumVertices();
    for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
    {
        if (!Section.IsValid() || Section.MaterialIndex != MaterialSlotIndex)
        {
            continue;
        }

        const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
        const int32 LastIndex = FMath::Min(
            FirstIndex + static_cast<int32>(Section.NumTriangles * 3),
            IndexBuffer.Num());

        for (int32 TriangleIndex = FirstIndex; TriangleIndex + 2 < LastIndex; TriangleIndex += 3)
        {
            const uint32 Indices[3] = {
                IndexBuffer[TriangleIndex],
                IndexBuffer[TriangleIndex + 1],
                IndexBuffer[TriangleIndex + 2]
            };
            if (Indices[0] >= static_cast<uint32>(VertexCount) ||
                Indices[1] >= static_cast<uint32>(VertexCount) ||
                Indices[2] >= static_cast<uint32>(VertexCount))
            {
                continue;
            }

            FDWCRuntimeTopologyTriangle& Triangle = OutTriangles.AddDefaulted_GetRef();
            Triangle.TriangleID = TriangleIndex / 3;
            Triangle.VertexIndices[0] = static_cast<int32>(Indices[0]);
            Triangle.VertexIndices[1] = static_cast<int32>(Indices[1]);
            Triangle.VertexIndices[2] = static_cast<int32>(Indices[2]);
        }
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}

bool FDWCOriginalUVRuntimeTopologyAdapter::BuildIslands(
    const TArray<FDWCRuntimeTopologyTriangle>& RawTriangles,
    const FDWCEditorUVTopologyData&            Topology,
    const int32                                MaterialSlotIndex,
    TArray<FDWCRuntimeOriginalUVIsland>&       OutIslands,
    FString*                                   OutErrorMessage)
{
    OutIslands.Reset();

    TMap<int32, const FDWCRuntimeTopologyTriangle*> TriangleByID;
    TriangleByID.Reserve(RawTriangles.Num());
    for (const FDWCRuntimeTopologyTriangle& Triangle : RawTriangles)
    {
        TriangleByID.Add(Triangle.TriangleID, &Triangle);
    }

    for (const FDWCOriginalUVIslandTopology& TopologyIsland : Topology.Islands)
    {
        if (TopologyIsland.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        FDWCRuntimeOriginalUVIsland& Island = OutIslands.AddDefaulted_GetRef();
        Island.UVIslandID = TopologyIsland.IslandID;
        for (const int32 TriangleID : TopologyIsland.TriangleIndices)
        {
            const FDWCRuntimeTopologyTriangle* const* Triangle = TriangleByID.Find(TriangleID);
            if (Triangle == nullptr || *Triangle == nullptr)
            {
                continue;
            }

            Island.VertexIndices.Add((*Triangle)->VertexIndices[0]);
            Island.VertexIndices.Add((*Triangle)->VertexIndices[1]);
            Island.VertexIndices.Add((*Triangle)->VertexIndices[2]);
        }
    }

    OutIslands.RemoveAll([](const FDWCRuntimeOriginalUVIsland& Island)
                         { return Island.VertexIndices.IsEmpty(); });

    if (OutIslands.IsEmpty() && !RawTriangles.IsEmpty())
    {
        DWC::Error::SetMessage(
            OutErrorMessage,
            TEXT("Stored Original UV topology does not match the runtime mesh. The sealed topology cannot be rebuilt; create a new WCA for the changed mesh."));
        return false;
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}

#endif // WITH_EDITOR
