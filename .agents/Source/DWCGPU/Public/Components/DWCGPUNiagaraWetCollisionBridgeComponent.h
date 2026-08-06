#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWCGPUNiagaraWetCollisionBridgeComponent.generated.h"

class UDynamicWetClothesComponent;
class UNiagaraComponent;

UCLASS(ClassGroup = (Wetness), DisplayName = "DWC GPU Niagara Wet Collision Bridge", meta = (BlueprintSpawnableComponent))
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

    UFUNCTION(BlueprintCallable, Category = "DWC|Niagara Wet Collision")
    bool RefreshBridge();

    UFUNCTION(BlueprintCallable, Category = "DWC|Niagara Wet Collision")
    void SetAllowedReceivers(const TArray<UDynamicWetClothesComponent*>& InAllowedReceivers);

    UFUNCTION(BlueprintCallable, Category = "DWC|Niagara Wet Collision")
    void ClearAllowedReceivers();

    UFUNCTION(BlueprintCallable, Category = "DWC|Niagara Wet Collision")
    void SetAllowedReceiversFromWorld();

    UFUNCTION(BlueprintCallable, Category = "DWC|Niagara Wet Collision")
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision")
    TObjectPtr<UNiagaraComponent> NiagaraComponent = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision")
    bool bFindNiagaraComponentOnOwner = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision")
    bool bRestrictToAllowedReceivers = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision", meta = (EditCondition = "bRestrictToAllowedReceivers"))
    bool bFindAllowedReceiversInWorld = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision", meta = (EditCondition = "bRestrictToAllowedReceivers"))
    TArray<TObjectPtr<UDynamicWetClothesComponent>> AllowedReceivers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision")
    bool bSetWetContactUserParameters = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision", meta = (EditCondition = "bSetWetContactUserParameters"))
    FName WetAmountUserParameterName = TEXT("User.DWCWetAmount");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision", meta = (EditCondition = "bSetWetContactUserParameters"))
    FName WetRadiusUserParameterName = TEXT("User.DWCWetRadius");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision", meta = (ClampMin = "0.0", EditCondition = "bSetWetContactUserParameters"))
    float WetAmount = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision", meta = (ClampMin = "0.0", EditCondition = "bSetWetContactUserParameters"))
    float WetRadius = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC|Niagara Wet Collision")
    bool bRefreshBridgeOnTick = true;

  private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<UDynamicWetClothesComponent>> RuntimeAllowedReceivers;

    int32 LastAppliedSystemInstanceID = 0;
    bool bLastAppliedTargetRestriction = false;
    TArray<int32> LastAppliedTargetReceiverGPUIds;
};
