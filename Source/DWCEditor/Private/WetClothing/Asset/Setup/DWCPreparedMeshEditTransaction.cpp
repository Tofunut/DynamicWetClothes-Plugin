#include "DWCPreparedMeshEditTransaction.h"

#include "Engine/SkeletalMesh.h"
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
    Backups.Reset();
    bCommitted = false;
    bRolledBack = false;

    if (Mesh == nullptr)
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The Prepared Mesh is unavailable.");
        return false;
    }

    FMeshDescription* MeshDescription = Mesh->GetMeshDescription(LODIndex);
    if (MeshDescription == nullptr)
    {
        if (OutErrorMessage)
        {
            *OutErrorMessage = FString::Printf(
                TEXT("LOD%d does not expose editable MeshDescription data."),
                LODIndex);
        }
        return false;
    }

    FLODBackup& Backup = Backups.AddDefaulted_GetRef();
    Backup.LODIndex = LODIndex;
    Backup.MeshDescription = *MeshDescription;

    if (OutErrorMessage) OutErrorMessage->Reset();
    return true;
}

void FDWCPreparedMeshEditTransaction::Commit()
{
    bCommitted = true;
    Backups.Reset();
}

void FDWCPreparedMeshEditTransaction::Rollback()
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
        if (MeshDescription == nullptr)
        {
            continue;
        }

        *MeshDescription = Backup.MeshDescription;
        Mesh->CommitMeshDescription(Backup.LODIndex);
    }

    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    Backups.Reset();
}
