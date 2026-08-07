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

    UFUNCTION(BlueprintCallable, Category = "DWC|Receiver Filter", meta = (ToolTip = "Restricts this Niagara wet collision system to the specified Dynamic Wet Clothes receivers. Passing a non-empty list also enables receiver filtering."))
    void SetAllowedReceivers(const TArray<UDynamicWetClothesComponent*>& InAllowedReceivers);

    UFUNCTION(BlueprintCallable, Category = "DWC|Receiver Filter", meta = (ToolTip = "Clears the receiver filter so this Niagara system can wet any valid Dynamic Wet Clothes receiver."))
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Receiver Filter", meta = (ToolTip = "When enabled, Niagara wet collision contacts from this component are applied only to the receivers in Allowed Receivers. When disabled, contacts can affect any valid Dynamic Wet Clothes receiver."))
    bool bRestrictToAllowedReceivers = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Receiver Filter", meta = (EditCondition = "bRestrictToAllowedReceivers", EditConditionHides, ToolTip = "Dynamic Wet Clothes components that this Niagara wet collision bridge is allowed to wet. Leave empty only when you intentionally want the enabled filter to target no receivers."))
    TArray<TObjectPtr<UDynamicWetClothesComponent>> AllowedReceivers;

  private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<UDynamicWetClothesComponent>> RuntimeAllowedReceivers;

    int32 LastAppliedSystemInstanceID = 0;
    bool bLastAppliedTargetRestriction = false;
    TArray<int32> LastAppliedTargetReceiverGPUIds;
};
