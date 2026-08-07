//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "DWCPreparedMeshEditTransaction.h"

#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"

FDWCPreparedMeshEditTransaction::FDWCPreparedMeshEditTransaction(USkeletalMesh* InMesh)
    : Mesh(InMesh)
{
}

FDWCPreparedMeshEditTransaction::~FDWCPreparedMeshEditTransaction()
{
    if (!bCommitted)
    {
        Rollback();
    }
}

bool FDWCPreparedMeshEditTransaction::CaptureEditableLOD(const int32 LODIndex, FString* OutErrorMessage)
{
    bCommitted = false;
    bRolledBack = false;

    if (Mesh == nullptr)
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The Prepared Mesh is unavailable.");
        return false;
    }

    FMeshDescription* MeshDescription = Mesh->GetMeshDescription(LODIndex);
    const bool bHadMeshDescriptionBeforeCapture = MeshDescription != nullptr;
    if (MeshDescription == nullptr)
    {
        const FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
        if (ImportedModel != nullptr && ImportedModel->LODModels.IsValidIndex(LODIndex))
        {
            FMeshDescription RecoveredMeshDescription;
            ImportedModel->LODModels[LODIndex].GetMeshDescription(Mesh, LODIndex, RecoveredMeshDescription);
            if (!RecoveredMeshDescription.IsEmpty())
            {
                MeshDescription = Mesh->CreateMeshDescription(LODIndex, MoveTemp(RecoveredMeshDescription));
                Mesh->CommitMeshDescription(LODIndex);
                MeshDescription = Mesh->GetMeshDescription(LODIndex);
            }
        }

        if (MeshDescription == nullptr)
        {
            if (OutErrorMessage)
            {
                *OutErrorMessage = FString::Printf(
                    TEXT("LOD%d does not expose editable MeshDescription data, and DWC could not recover one from the skeletal mesh LOD model."),
                    LODIndex);
            }
            return false;
        }
    }

    if (Backups.ContainsByPredicate(
            [LODIndex](const FLODBackup& Existing)
            {
                return Existing.LODIndex == LODIndex;
            }))
    {
        if (OutErrorMessage) OutErrorMessage->Reset();
        return true;
    }

    FLODBackup& Backup = Backups.AddDefaulted_GetRef();
    Backup.LODIndex = LODIndex;
    Backup.MeshDescription = *MeshDescription;
    Backup.bHadMeshDescriptionBeforeCapture = bHadMeshDescriptionBeforeCapture;
    if (OutErrorMessage) OutErrorMessage->Reset();
    return true;
}

void FDWCPreparedMeshEditTransaction::Commit()
{
    bCommitted = true;
    Backups.Reset();
}

void FDWCPreparedMeshEditTransaction::Rollback(const bool bDeferMeshCommit)
{
    if (bCommitted || bRolledBack || Mesh == nullptr || Backups.IsEmpty())
    {
        return;
    }

    bRolledBack = true;
    Mesh->Modify();
    for (const FLODBackup& Backup : Backups)
    {
        FMeshDescription* MeshDescription = Mesh->GetMeshDescription(Backup.LODIndex);
        if (!Backup.bHadMeshDescriptionBeforeCapture)
        {
            Mesh->ClearMeshDescriptionAndBulkData(Backup.LODIndex);
            continue;
        }

        if (MeshDescription == nullptr)
        {
            continue;
        }

        *MeshDescription = Backup.MeshDescription;
        if (!bDeferMeshCommit)
        {
            Mesh->CommitMeshDescription(Backup.LODIndex);
        }
    }

    if (!bDeferMeshCommit)
    {
        Mesh->PostEditChange();
        Mesh->MarkPackageDirty();
    }
    Backups.Reset();
}
