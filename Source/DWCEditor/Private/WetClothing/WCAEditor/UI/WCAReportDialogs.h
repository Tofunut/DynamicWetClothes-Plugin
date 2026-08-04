#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
struct FDWCDataUVBuildResult;

namespace WCAReportDialogs
{
    void OpenDWCDataUVBuildResultDialog(
        const FDWCDataUVBuildResult& Result,
        const USkeletalMesh* PreparedMesh);

    void OpenDWCDataUVBuildFailureDialog(
        const FDWCDataUVBuildResult& Result,
        const USkeletalMesh* PreparedMesh);
}
