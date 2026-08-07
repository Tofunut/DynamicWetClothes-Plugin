//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"

class USkeletalMesh;

/**
 * Temporary edit transaction for a Prepared Mesh rebuild.
 * Restores the captured editable LOD automatically unless Commit() is called.
 */
class FDWCPreparedMeshEditTransaction
{
public:
    explicit FDWCPreparedMeshEditTransaction(USkeletalMesh* InMesh);
    ~FDWCPreparedMeshEditTransaction();

    bool CaptureEditableLOD(int32 LODIndex, FString* OutErrorMessage = nullptr);


    void Commit();
    void Rollback(bool bDeferMeshCommit = false);

private:
    struct FLODBackup
    {
        int32 LODIndex = INDEX_NONE;
        FMeshDescription MeshDescription;
        bool bHadMeshDescriptionBeforeCapture = false;
    };

    USkeletalMesh* Mesh = nullptr;
    TArray<FLODBackup> Backups;
    bool bCommitted = false;
    bool bRolledBack = false;
};
