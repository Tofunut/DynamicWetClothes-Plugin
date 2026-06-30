#pragma once

#include "CoreMinimal.h"
#include "DynamicWet/DynamicWetReceiverSettings.h"
#include "WetClothingAsset.h"
#include "WetnessProfile.h"

class FDynamicWetReceiverMeshSampler;
class FDynamicWetReceiverRenderApplier;
class FDynamicWetReceiverRuntimeData;
class FDynamicWetReceiverRuntimeDataBuilder;
class FDynamicWetReceiverSimulationSolver;
class FDynamicWetReceiverSimulationState;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;

struct DYNAMICWETCLOTHES_API FDynamicWetReceiverContext
{
    UObject* OwnerForLogs = nullptr;
    TObjectPtr<USkeletalMeshComponent>& TargetSkeletalMesh;
    TArray<UWetnessProfile*>& MaterialProfiles;
    TObjectPtr<UWetClothingAsset>& WetClothingProfile;
    FDynamicWetReceiverSettings& WetnessSettings;
    FLinearColor& FallbackUnderColor;
    float& WetUnderColorBlendStrength;
    bool& bEnableWetPartDebugVertexColors;
    bool& bWetPartDebugUseWetnessMask;
    FLinearColor& UnassignedWetPartDebugColor;
    FName& WetPartDebugStrengthParameterName;
    FName& WetPartDebugUseWetnessMaskParameterName;
    FName& ProfileMap0ParameterName;
    FName& UseProfileMap0ParameterName;
    TArray<TObjectPtr<UMaterialInstanceDynamic>>& WetMaterialInstances;
    FDynamicWetReceiverRuntimeData& RuntimeData;
    FDynamicWetReceiverRuntimeDataBuilder& RuntimeDataBuilder;
    FDynamicWetReceiverSimulationState& SimulationState;
    FDynamicWetReceiverSimulationSolver& SimulationSolver;
    FDynamicWetReceiverMeshSampler& MeshSampler;
    FDynamicWetReceiverRenderApplier& RenderApplier;

    FDynamicWetReceiverContext(
        UObject* InOwnerForLogs,
        TObjectPtr<USkeletalMeshComponent>& InTargetSkeletalMesh,
        TArray<UWetnessProfile*>& InMaterialProfiles,
        TObjectPtr<UWetClothingAsset>& InWetClothingProfile,
        FDynamicWetReceiverSettings& InWetnessSettings,
        FLinearColor& InFallbackUnderColor,
        float& InWetUnderColorBlendStrength,
        bool& bInEnableWetPartDebugVertexColors,
        bool& bInWetPartDebugUseWetnessMask,
        FLinearColor& InUnassignedWetPartDebugColor,
        FName& InWetPartDebugStrengthParameterName,
        FName& InWetPartDebugUseWetnessMaskParameterName,
        FName& InProfileMap0ParameterName,
        FName& InUseProfileMap0ParameterName,
        TArray<TObjectPtr<UMaterialInstanceDynamic>>& InWetMaterialInstances,
        FDynamicWetReceiverRuntimeData& InRuntimeData,
        FDynamicWetReceiverRuntimeDataBuilder& InRuntimeDataBuilder,
        FDynamicWetReceiverSimulationState& InSimulationState,
        FDynamicWetReceiverSimulationSolver& InSimulationSolver,
        FDynamicWetReceiverMeshSampler& InMeshSampler,
        FDynamicWetReceiverRenderApplier& InRenderApplier)
        : OwnerForLogs(InOwnerForLogs)
        , TargetSkeletalMesh(InTargetSkeletalMesh)
        , MaterialProfiles(InMaterialProfiles)
        , WetClothingProfile(InWetClothingProfile)
        , WetnessSettings(InWetnessSettings)
        , FallbackUnderColor(InFallbackUnderColor)
        , WetUnderColorBlendStrength(InWetUnderColorBlendStrength)
        , bEnableWetPartDebugVertexColors(bInEnableWetPartDebugVertexColors)
        , bWetPartDebugUseWetnessMask(bInWetPartDebugUseWetnessMask)
        , UnassignedWetPartDebugColor(InUnassignedWetPartDebugColor)
        , WetPartDebugStrengthParameterName(InWetPartDebugStrengthParameterName)
        , WetPartDebugUseWetnessMaskParameterName(InWetPartDebugUseWetnessMaskParameterName)
        , ProfileMap0ParameterName(InProfileMap0ParameterName)
        , UseProfileMap0ParameterName(InUseProfileMap0ParameterName)
        , WetMaterialInstances(InWetMaterialInstances)
        , RuntimeData(InRuntimeData)
        , RuntimeDataBuilder(InRuntimeDataBuilder)
        , SimulationState(InSimulationState)
        , SimulationSolver(InSimulationSolver)
        , MeshSampler(InMeshSampler)
        , RenderApplier(InRenderApplier)
    {
    }

    const UWetnessProfile* GetActiveMaterialProfile() const
    {
        for (const UWetnessProfile* MaterialPreset : MaterialProfiles)
        {
            if (MaterialPreset)
            {
                return MaterialPreset;
            }
        }

        return nullptr;
    }

    float GetAbsorptionMultiplier() const
    {
        const UWetnessProfile* MaterialPreset = GetActiveMaterialProfile();
        return MaterialPreset ? MaterialPreset->GetAbsorptionMultiplier() : 1.0f;
    }

    float GetDryRatePerSecond() const
    {
        const UWetnessProfile* MaterialPreset = GetActiveMaterialProfile();
        return MaterialPreset ? MaterialPreset->GetDryRatePerSecond() : 1.0f;
    }

    float GetSpreadRatePerSecond() const
    {
        const UWetnessProfile* MaterialPreset = GetActiveMaterialProfile();
        return MaterialPreset ? MaterialPreset->GetSpreadRatePerSecond() : 0.0f;
    }

    float GetGravityFlowStrength() const
    {
        const UWetnessProfile* MaterialPreset = GetActiveMaterialProfile();
        return MaterialPreset ? MaterialPreset->GetGravityFlowStrength() : 0.0f;
    }

    float GetAbsorptionMultiplierForVertex(int32 VertexIndex) const;
    float GetDryRatePerSecondForVertex(int32 VertexIndex) const;
    float GetSpreadRatePerSecondForVertex(int32 VertexIndex) const;
    float GetGravityFlowStrengthForVertex(int32 VertexIndex) const;
};
