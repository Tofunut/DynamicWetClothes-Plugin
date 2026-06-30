#include "WetClothingProfileMeshAnalyzer.h"

void FWetClothingProfileMeshAnalyzer::SetError(FString* OutErrorMessage, const TCHAR* InMessage)
{
    FDynamicWetMeshAnalysis::SetError(OutErrorMessage, InMessage);
}

int32 FWetClothingProfileMeshAnalyzer::GetNumUVChannels(const USkeletalMesh* SkeletalMesh, int32 LODIndex)
{
    return FDynamicWetMeshAnalysis::GetNumUVChannels(SkeletalMesh, LODIndex);
}

bool FWetClothingProfileMeshAnalyzer::BuildMaterialSlotUVIslands(
    const USkeletalMesh* SkeletalMesh,
    int32 LODIndex,
    int32 UVChannelIndex,
    int32 MaterialSlotIndex,
    TArray<FWetClothingProfileUVIsland>& OutIslands,
    FString* OutErrorMessage)
{
    return FDynamicWetMeshAnalysis::BuildMaterialSlotUVIslands(
        SkeletalMesh,
        LODIndex,
        UVChannelIndex,
        MaterialSlotIndex,
        OutIslands,
        OutErrorMessage);
}
