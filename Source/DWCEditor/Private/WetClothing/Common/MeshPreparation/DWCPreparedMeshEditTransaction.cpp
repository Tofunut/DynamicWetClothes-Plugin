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

bool FDWCPreparedMeshEditTransaction::CaptureAllEditableLODs(FString* OutErrorMessage)
{
    Backups.Reset();
    bCommitted = false;
    bRolledBack = false;

    const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
    if (Mesh == nullptr || RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The Prepared Mesh has no editable LOD data to capture.");
        return false;
    }

    Backups.Reserve(RenderData->LODRenderData.Num());
    for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
    {
        FMeshDescription* MeshDescription = Mesh->GetMeshDescription(LODIndex);
        if (MeshDescription == nullptr)
        {
            if (RenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
            {
                if (OutErrorMessage)
                {
                    *OutErrorMessage = FString::Printf(
                        TEXT("LOD%d does not expose editable MeshDescription data."),
                        LODIndex);
                }
                Backups.Reset();
                return false;
            }
            continue;
        }

        FLODBackup& Backup = Backups.AddDefaulted_GetRef();
        Backup.LODIndex = LODIndex;
        Backup.MeshDescription = *MeshDescription;
    }

    if (Backups.IsEmpty())
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The Prepared Mesh produced no editable LOD backups.");
        return false;
    }

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
