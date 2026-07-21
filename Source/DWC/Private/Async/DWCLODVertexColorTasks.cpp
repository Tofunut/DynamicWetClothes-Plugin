#include "Async/DWCLODVertexColorTasks.h"

#include "Components/DynamicWetClothesComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Math/GenericOctree.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Utility/DWCProfiling.h"

namespace
{
    bool GetLODVertexColorLODRenderData(const USkeletalMeshComponent* TargetSkeletalMesh, const int32 LODIndex, FSkeletalMeshLODRenderData*& OutLODData)
    {
        OutLODData = nullptr;
        if (!TargetSkeletalMesh)
        {
            return false;
        }

        const USkeletalMesh* SkeletalMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
        if (!SkeletalMesh)
        {
            return false;
        }

        FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
        if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            return false;
        }

        OutLODData = &RenderData->LODRenderData[LODIndex];
        return true;
    }

    struct FDWCLODVertexOctreeElement
    {
        int32 VertexIndex = INDEX_NONE;
        FBoxCenterAndExtent Bounds;
    };

    struct FDWCLODVertexOctreeSemantics
    {
        enum { MaxElementsPerLeaf = 16 };
        enum { MinInclusiveElementsPerNode = 7 };
        enum { MaxNodeDepth = 12 };

        typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

        static const FBoxCenterAndExtent& GetBoundingBox(const FDWCLODVertexOctreeElement& Element)
        {
            return Element.Bounds;
        }

        static bool AreElementsEqual(const FDWCLODVertexOctreeElement& A, const FDWCLODVertexOctreeElement& B)
        {
            return A.VertexIndex == B.VertexIndex;
        }

        static void SetElementId(const FDWCLODVertexOctreeElement& Element, FOctreeElementId2 Id)
        {
        }

        static void ApplyOffset(FDWCLODVertexOctreeElement& Element, FVector Offset)
        {
            Element.Bounds.Center += Offset;
        }
    };

    using FDWCLODVertexOctree = TOctree2<FDWCLODVertexOctreeElement, FDWCLODVertexOctreeSemantics>;
}

static int32 FindBestSourceVertex(
    const FDWCLODVertexStaticData&            Source,
    const TArray<FColor>&                     SourceColors,
    const FDWCLODVertexOctree&                SourceOctree,
    const FVector3f&                          TargetPos,
    const FVector3f&                          TargetNormal,
    const FDWCLODVertexColorTransferSettings& Settings)
{
    int32 BestIndex = INDEX_NONE;
    float BestDistSq = TNumericLimits<float>::Max();
    float BestNormalDot = -1.0f;

    auto ConsiderSourceVertex = [&Source, &SourceColors, &TargetPos, &TargetNormal, &Settings, &BestIndex, &BestDistSq, &BestNormalDot](const int32 SourceIndex)
    {
        if (!SourceColors.IsValidIndex(SourceIndex) || !Source.Geometry.LocalPositions.IsValidIndex(SourceIndex))
        {
            return;
        }

        float NormalDot = 1.0f;
        if (Source.Geometry.LocalNormals.IsValidIndex(SourceIndex))
        {
            NormalDot = FVector3f::DotProduct(Source.Geometry.LocalNormals[SourceIndex], TargetNormal);
            if (NormalDot < Settings.MaxNormalAngleDot)
            {
                return;
            }
        }

        const float DistSq = FVector3f::DistSquared(Source.Geometry.LocalPositions[SourceIndex], TargetPos);
        const float TieToleranceSq = FMath::Square(FMath::Max(Settings.DistanceTieTolerance, 0.0f));

        if (BestIndex == INDEX_NONE ||
            DistSq < BestDistSq - TieToleranceSq ||
            (FMath::Abs(DistSq - BestDistSq) <= TieToleranceSq && NormalDot > BestNormalDot))
        {
            BestIndex = SourceIndex;
            BestDistSq = DistSq;
            BestNormalDot = NormalDot;
        }
    };

    const float InitialRadius = FMath::Max(Settings.InitialSearchRadius, Settings.DistanceTieTolerance);
    const float MaxRadius = FMath::Max(Settings.MaxSearchRadius, InitialRadius);

    for (float SearchRadius = InitialRadius; SearchRadius <= MaxRadius && BestIndex == INDEX_NONE; SearchRadius *= 2.0f)
    {
        const FVector QueryCenter(TargetPos);
        const FVector QueryExtent(SearchRadius, SearchRadius, SearchRadius);
        SourceOctree.FindElementsWithBoundsTest(
            FBoxCenterAndExtent(QueryCenter, QueryExtent),
            [&ConsiderSourceVertex](const FDWCLODVertexOctreeElement& Element)
            {
                ConsiderSourceVertex(Element.VertexIndex);
            });
    }

    return BestIndex;
}

TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> BuildDWCLODVertexStaticData(
    USkeletalMeshComponent* TargetSkeletalMesh,
    const int32 LODIndex)
{
    DWC_PROFILE_SCOPE(DWC_BuildLODVertexStaticData);

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODVertexColorLODRenderData(TargetSkeletalMesh, LODIndex, LODData))
    {
        return nullptr;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    if (VertexCount <= 0)
    {
        return nullptr;
    }

    TSharedRef<FDWCLODVertexStaticData, ESPMode::ThreadSafe> StaticData =
        MakeShared<FDWCLODVertexStaticData, ESPMode::ThreadSafe>();
    StaticData->Geometry.SkeletalMeshIdentity = reinterpret_cast<UPTRINT>(TargetSkeletalMesh->GetSkeletalMeshAsset());
    StaticData->Geometry.VertexDataIdentity = reinterpret_cast<UPTRINT>(LODData);
    StaticData->Geometry.VertexCount = VertexCount;
    StaticData->LODIndex = LODIndex;
    StaticData->Geometry.LocalPositions.SetNumZeroed(VertexCount);
    StaticData->Geometry.LocalNormals.SetNumZeroed(VertexCount);

    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        int32 SectionIndex = INDEX_NONE;
        int32 SectionVertexIndex = INDEX_NONE;
        LODData->GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);
        if (!LODData->RenderSections.IsValidIndex(SectionIndex) || SectionVertexIndex < 0)
        {
            continue;
        }

        const FSkelMeshRenderSection& Section = LODData->RenderSections[SectionIndex];
        const int32 BufferVertexIndex = Section.GetVertexBufferIndex() + SectionVertexIndex;
        if (BufferVertexIndex < 0 || BufferVertexIndex >= VertexCount)
        {
            continue;
        }

        StaticData->Geometry.LocalPositions[VertexIndex] =
            LODData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(BufferVertexIndex);
        StaticData->Geometry.LocalNormals[VertexIndex] =
            LODData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(BufferVertexIndex).GetSafeNormal();
    }

    if (!StaticData->IsValid())
    {
        return nullptr;
    }

    return StaticData;
}

static void ApplyTransferMap(
    const TArray<FColor>& SourceColors,
    const TArray<int32>&  TargetToSourceVertex,
    TArray<FColor>&       OutTargetColors)
{
    OutTargetColors.SetNumUninitialized(TargetToSourceVertex.Num());

    for (int32 TargetVertexIndex = 0; TargetVertexIndex < TargetToSourceVertex.Num(); ++TargetVertexIndex)
    {
        const int32 SourceVertexIndex = TargetToSourceVertex[TargetVertexIndex];
        OutTargetColors[TargetVertexIndex] = SourceColors.IsValidIndex(SourceVertexIndex)
                                                 ? SourceColors[SourceVertexIndex]
                                                 : FColor::Black;
    }
}

static void ApplyDirtyTransferMap(
    const TArray<FColor>& SourceColors,
    const TArray<int32>&  DirtySourceVertices,
    const TArray<int32>&  TargetToSourceVertex,
    const TArray<FColor>& CachedTargetColors,
    TArray<FColor>&       OutTargetColors)
{
    OutTargetColors = CachedTargetColors;

    TSet<int32> DirtySourceSet;
    DirtySourceSet.Reserve(DirtySourceVertices.Num());
    for (const int32 SourceVertexIndex : DirtySourceVertices)
    {
        if (SourceColors.IsValidIndex(SourceVertexIndex))
        {
            DirtySourceSet.Add(SourceVertexIndex);
        }
    }

    if (DirtySourceSet.IsEmpty())
    {
        return;
    }

    for (int32 TargetVertexIndex = 0; TargetVertexIndex < TargetToSourceVertex.Num(); ++TargetVertexIndex)
    {
        const int32 SourceVertexIndex = TargetToSourceVertex[TargetVertexIndex];
        if (DirtySourceSet.Contains(SourceVertexIndex) &&
            SourceColors.IsValidIndex(SourceVertexIndex) &&
            OutTargetColors.IsValidIndex(TargetVertexIndex))
        {
            OutTargetColors[TargetVertexIndex] = SourceColors[SourceVertexIndex];
        }
    }
}

FDWCLODVertexColorTransferTask::FDWCLODVertexColorTransferTask(
    TWeakObjectPtr<UDynamicWetClothesComponent> InOwner,
    FDWCLODVertexColorTransferSnapshot&&        InSnapshot)
    : Owner(InOwner), Snapshot(MoveTemp(InSnapshot))
{
}


void FDWCLODVertexColorTransferTask::ExecuteWorker()
{
    DWC_PROFILE_SCOPE(DWC_LODVertexColorTransferTask_ExecuteWorker);

    SetStatus(EDWCTaskStatus::Running);

    Result.ReceiverId = Snapshot.ReceiverId;
    Result.Generation = Snapshot.Generation;
    Result.DirtySourceVertexCount = Snapshot.DirtySourceVertices.Num();

    if (!Snapshot.SourceLODData.IsValid() ||
        Snapshot.SourceLODData->Geometry.LocalPositions.Num() == 0 ||
        Snapshot.SourceLODData->Geometry.LocalPositions.Num() != Snapshot.SourceColors.Num())
    {
        SetStatus(EDWCTaskStatus::Failed);
        return;
    }

    bool bNeedsTransferMapBuild = false;
    for (const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>& TargetLODData : Snapshot.TargetLODData)
    {
        if (!TargetLODData.IsValid())
        {
            continue;
        }

        const TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>* CachedMap =
            Snapshot.CachedTargetToSourceVertexByLOD.Find(TargetLODData->LODIndex);
        if (CachedMap == nullptr || !CachedMap->IsValid() || (*CachedMap)->Num() != TargetLODData->Geometry.LocalPositions.Num())
        {
            bNeedsTransferMapBuild = true;
            break;
        }
    }

    TUniquePtr<FDWCLODVertexOctree> SourceOctree;
    if (bNeedsTransferMapBuild)
    {
        FBox SourceBounds(ForceInit);
        for (const FVector3f& SourcePosition : Snapshot.SourceLODData->Geometry.LocalPositions)
        {
            SourceBounds += FVector(SourcePosition);
        }

        if (!SourceBounds.IsValid)
        {
            SetStatus(EDWCTaskStatus::Failed);
            return;
        }

        const FVector SourceExtent = SourceBounds.GetExtent();
        const double OctreeExtent = FMath::Max3(SourceExtent.X, SourceExtent.Y, SourceExtent.Z) + FMath::Max(1.0f, Snapshot.Settings.MaxSearchRadius);
        SourceOctree = MakeUnique<FDWCLODVertexOctree>(SourceBounds.GetCenter(), OctreeExtent);

        {
            DWC_PROFILE_SCOPE(DWC_LODVertexColorTransferTask_BuildVertexOctree);

            for (int32 SourceIndex = 0; SourceIndex < Snapshot.SourceLODData->Geometry.LocalPositions.Num(); ++SourceIndex)
            {
                const FVector SourcePosition(Snapshot.SourceLODData->Geometry.LocalPositions[SourceIndex]);
                SourceOctree->AddElement({SourceIndex, FBoxCenterAndExtent(SourcePosition, FVector::ZeroVector)});
            }
        }
    }

    for (const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>& TargetLODData : Snapshot.TargetLODData)
    {
        DWC_PROFILE_SCOPE(DWC_LODVertexColorTransferTask_TransferTargetLOD);

        if (!TargetLODData.IsValid())
        {
            continue;
        }

        FDWCLODVertexColorTransferResult::FLODColors LODResult;
        LODResult.LODIndex = TargetLODData->LODIndex;

        const TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>* CachedMap =
            Snapshot.CachedTargetToSourceVertexByLOD.Find(LODResult.LODIndex);
        if (CachedMap != nullptr && CachedMap->IsValid() && (*CachedMap)->Num() == TargetLODData->Geometry.LocalPositions.Num())
        {
            const TSharedPtr<const TArray<FColor>, ESPMode::ThreadSafe>* CachedColors =
                Snapshot.CachedTargetColorsByLOD.Find(LODResult.LODIndex);
            if (CachedColors != nullptr &&
                CachedColors->IsValid() &&
                (*CachedColors)->Num() == TargetLODData->Geometry.LocalPositions.Num() &&
                !Snapshot.DirtySourceVertices.IsEmpty())
            {
                ApplyDirtyTransferMap(
                    Snapshot.SourceColors,
                    Snapshot.DirtySourceVertices,
                    **CachedMap,
                    **CachedColors,
                    LODResult.Colors);
            }
            else
            {
                ApplyTransferMap(Snapshot.SourceColors, **CachedMap, LODResult.Colors);
            }
        }
        else
        {
            if (!SourceOctree.IsValid())
            {
                continue;
            }

            LODResult.TargetToSourceVertex.SetNumUninitialized(TargetLODData->Geometry.LocalPositions.Num());
            for (int32 VertexIndex = 0; VertexIndex < TargetLODData->Geometry.LocalPositions.Num(); ++VertexIndex)
            {
                const FVector3f Normal = TargetLODData->Geometry.LocalNormals.IsValidIndex(VertexIndex) ? TargetLODData->Geometry.LocalNormals[VertexIndex] : FVector3f::UpVector;

                LODResult.TargetToSourceVertex[VertexIndex] = FindBestSourceVertex(
                    *Snapshot.SourceLODData,
                    Snapshot.SourceColors,
                    *SourceOctree,
                    TargetLODData->Geometry.LocalPositions[VertexIndex],
                    Normal,
                    Snapshot.Settings);
            }

            ApplyTransferMap(Snapshot.SourceColors, LODResult.TargetToSourceVertex, LODResult.Colors);
        }

        Result.LODResults.Add(MoveTemp(LODResult));
    }

    SetStatus(EDWCTaskStatus::Completed);
}

void FDWCLODVertexColorTransferTask::CommitGameThread()
{
    DWC_PROFILE_SCOPE(DWC_LODVertexColorTransferTask_CommitGameThread);

    if (!Owner.IsValid() || GetStatus() != EDWCTaskStatus::Completed)
    {
        return;
    }

    Owner->CommitLODVertexColorTransferResult(MoveTemp(Result));
}

