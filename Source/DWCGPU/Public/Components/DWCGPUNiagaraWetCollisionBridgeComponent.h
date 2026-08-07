//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWCGPUNiagaraWetCollisionBridgeComponent.generated.h"

class UDynamicWetClothesComponent;
class UNiagaraComponent;

UCLASS(ClassGroup = (DWC), DisplayName = "DWC GPU Niagara Wet Collision Bridge", meta = (BlueprintSpawnableComponent))
class DWCGPU_API UDWCGPUNiagaraWetCollisionBridgeComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    UDWCGPUNiagaraWetCollisionBridgeComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // Internal refresh entry point used by the component lifecycle and C++ integrations.
    bool RefreshBridge();

    UFUNCTION(BlueprintCallable, Category = "DWC|Receiver Filter")
    void SetAllowedReceivers(const TArray<UDynamicWetClothesComponent*>& InAllowedReceivers);

    UFUNCTION(BlueprintCallable, Category = "DWC|Receiver Filter")
    void ClearAllowedReceivers();

    // C++ integration helper. Intentionally hidden from Blueprint.
    void SetAllowedReceiversFromWorld();

    UFUNCTION(BlueprintCallable, Category = "DWC|Wet Contact")
    void SetWetContactUserParameters(float InWetAmount, float InWetRadius);

  private:
    UNiagaraComponent* ResolveNiagaraComponent() const;
    void ApplyWetContactUserParameters(UNiagaraComponent& TargetNiagaraComponent) const;
    void BuildTargetReceiverGPUIds(TArray<int32>& OutReceiverGPUIds) const;
    bool ApplyTargetReceivers(
        int32 SystemInstanceID,
        bool bRestrict,
        const TArray<int32>& ReceiverGPUIds);

  public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara")
    TObjectPtr<UNiagaraComponent> NiagaraComponent = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Wet Contact", meta = (ClampMin = "0.0"))
    float WetAmount = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Wet Contact", meta = (ClampMin = "0.0"))
    float WetRadius = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Receiver Filter")
    bool bRestrictToAllowedReceivers = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Receiver Filter", meta = (EditCondition = "bRestrictToAllowedReceivers", EditConditionHides))
    TArray<TObjectPtr<UDynamicWetClothesComponent>> AllowedReceivers;

  private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<UDynamicWetClothesComponent>> RuntimeAllowedReceivers;

    int32 LastAppliedSystemInstanceID = 0;
    bool bLastAppliedTargetRestriction = false;
    TArray<int32> LastAppliedTargetReceiverGPUIds;
};
