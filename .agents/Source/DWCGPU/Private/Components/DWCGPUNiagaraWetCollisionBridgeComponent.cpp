#include "Components/DWCGPUNiagaraWetCollisionBridgeComponent.h"

#include "Components/DynamicWetClothesComponent.h"
#include "GameFramework/Actor.h"
#include "Niagara/DWCGPUNiagaraWetCollisionBridge.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstanceController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UDWCGPUNiagaraWetCollisionBridgeComponent::UDWCGPUNiagaraWetCollisionBridgeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UDWCGPUNiagaraWetCollisionBridgeComponent::BeginPlay()
{
    Super::BeginPlay();

    RefreshBridge();
}

void UDWCGPUNiagaraWetCollisionBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (LastAppliedSystemInstanceID != 0)
    {
        DWCGPUNiagaraWetCollisionBridge::ClearTargetReceiverGPUIds_GameThread(LastAppliedSystemInstanceID);
    }

    LastAppliedSystemInstanceID = 0;
    LastAppliedTargetReceiverGPUIds.Reset();

    Super::EndPlay(EndPlayReason);
}

void UDWCGPUNiagaraWetCollisionBridgeComponent::TickComponent(
    const float DeltaTime,
    const ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bRefreshBridgeOnTick)
    {
        RefreshBridge();
    }
}

bool UDWCGPUNiagaraWetCollisionBridgeComponent::RefreshBridge()
{
    UNiagaraComponent* TargetNiagaraComponent = NiagaraComponent;
    if (!TargetNiagaraComponent && bFindNiagaraComponentOnOwner)
    {
        TargetNiagaraComponent = ResolveNiagaraComponent();
        NiagaraComponent = TargetNiagaraComponent;
    }

    if (!IsValid(TargetNiagaraComponent))
    {
        return false;
    }

    ApplyWetContactUserParameters(*TargetNiagaraComponent);

    FNiagaraSystemInstanceControllerPtr SystemInstanceController =
        TargetNiagaraComponent->GetSystemInstanceController();
    if (!SystemInstanceController.IsValid() || !SystemInstanceController->IsValid())
    {
        return false;
    }

    TArray<int32> ReceiverGPUIds;
    if (bRestrictToAllowedReceivers)
    {
        BuildTargetReceiverGPUIds(ReceiverGPUIds);
    }

    const bool bApplyTargetRestriction =
        bRestrictToAllowedReceivers && !ReceiverGPUIds.IsEmpty();

    return ApplyTargetReceivers(
        SystemInstanceController->GetSystemInstanceID(),
        bApplyTargetRestriction,
        ReceiverGPUIds);
}

void UDWCGPUNiagaraWetCollisionBridgeComponent::SetAllowedReceivers(
    const TArray<UDynamicWetClothesComponent*>& InAllowedReceivers)
{
    RuntimeAllowedReceivers.Reset();
    RuntimeAllowedReceivers.Reserve(InAllowedReceivers.Num());

    for (UDynamicWetClothesComponent* Receiver : InAllowedReceivers)
    {
        if (IsValid(Receiver))
        {
            RuntimeAllowedReceivers.AddUnique(Receiver);
        }
    }

    bRestrictToAllowedReceivers = true;
    bFindAllowedReceiversInWorld = false;
    RefreshBridge();
}

void UDWCGPUNiagaraWetCollisionBridgeComponent::ClearAllowedReceivers()
{
    RuntimeAllowedReceivers.Reset();
    AllowedReceivers.Reset();
    bRestrictToAllowedReceivers = false;
    bFindAllowedReceiversInWorld = false;
    RefreshBridge();
}

void UDWCGPUNiagaraWetCollisionBridgeComponent::SetAllowedReceiversFromWorld()
{
    RuntimeAllowedReceivers.Reset();
    AllowedReceivers.Reset();
    bRestrictToAllowedReceivers = true;
    bFindAllowedReceiversInWorld = true;
    RefreshBridge();
}

void UDWCGPUNiagaraWetCollisionBridgeComponent::SetWetContactUserParameters(
    const float InWetAmount,
    const float InWetRadius)
{
    WetAmount = FMath::Max(0.0f, InWetAmount);
    WetRadius = FMath::Max(0.0f, InWetRadius);
    RefreshBridge();
}

UNiagaraComponent* UDWCGPUNiagaraWetCollisionBridgeComponent::ResolveNiagaraComponent() const
{
    const AActor* Owner = GetOwner();
    if (!Owner)
    {
        return nullptr;
    }

    TArray<UNiagaraComponent*> NiagaraComponents;
    Owner->GetComponents<UNiagaraComponent>(NiagaraComponents);
    for (UNiagaraComponent* Candidate : NiagaraComponents)
    {
        if (IsValid(Candidate))
        {
            return Candidate;
        }
    }

    return nullptr;
}

void UDWCGPUNiagaraWetCollisionBridgeComponent::ApplyWetContactUserParameters(
    UNiagaraComponent& TargetNiagaraComponent) const
{
    if (!bSetWetContactUserParameters)
    {
        return;
    }

    if (!WetAmountUserParameterName.IsNone())
    {
        TargetNiagaraComponent.SetVariableFloat(WetAmountUserParameterName, WetAmount);
    }

    if (!WetRadiusUserParameterName.IsNone())
    {
        TargetNiagaraComponent.SetVariableFloat(WetRadiusUserParameterName, WetRadius);
    }

}

void UDWCGPUNiagaraWetCollisionBridgeComponent::BuildTargetReceiverGPUIds(
    TArray<int32>& OutReceiverGPUIds) const
{
    const auto AddReceiverGPUIds = [&OutReceiverGPUIds](const UDynamicWetClothesComponent* Receiver)
    {
        if (!IsValid(Receiver))
        {
            return;
        }

        Receiver->GetDWCReceiverGPUIds(OutReceiverGPUIds);
    };

    for (const TObjectPtr<UDynamicWetClothesComponent>& Receiver : AllowedReceivers)
    {
        AddReceiverGPUIds(Receiver);
    }

    for (const TObjectPtr<UDynamicWetClothesComponent>& Receiver : RuntimeAllowedReceivers)
    {
        AddReceiverGPUIds(Receiver);
    }

    if (bFindAllowedReceiversInWorld)
    {
        UWorld* World = GetWorld();
        if (World)
        {
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                const AActor* CandidateActor = *It;
                if (!IsValid(CandidateActor))
                {
                    continue;
                }

                TArray<UDynamicWetClothesComponent*> ReceiverComponents;
                CandidateActor->GetComponents<UDynamicWetClothesComponent>(ReceiverComponents);
                for (const UDynamicWetClothesComponent* ReceiverComponent : ReceiverComponents)
                {
                    AddReceiverGPUIds(ReceiverComponent);
                }
            }
        }
    }

    OutReceiverGPUIds.Sort();
}

bool UDWCGPUNiagaraWetCollisionBridgeComponent::ApplyTargetReceivers(
    const int32 SystemInstanceID,
    const bool bRestrict,
    const TArray<int32>& ReceiverGPUIds)
{
    if (SystemInstanceID == 0)
    {
        return false;
    }

    if (LastAppliedSystemInstanceID == SystemInstanceID &&
        bLastAppliedTargetRestriction == bRestrict &&
        LastAppliedTargetReceiverGPUIds == ReceiverGPUIds)
    {
        return true;
    }

    if (LastAppliedSystemInstanceID != 0 && LastAppliedSystemInstanceID != SystemInstanceID)
    {
        DWCGPUNiagaraWetCollisionBridge::ClearTargetReceiverGPUIds_GameThread(LastAppliedSystemInstanceID);
    }

    if (bRestrict)
    {
        DWCGPUNiagaraWetCollisionBridge::SetTargetReceiverGPUIds_GameThread(
            SystemInstanceID,
            ReceiverGPUIds);
    }
    else
    {
        DWCGPUNiagaraWetCollisionBridge::ClearTargetReceiverGPUIds_GameThread(SystemInstanceID);
    }

    LastAppliedSystemInstanceID = SystemInstanceID;
    bLastAppliedTargetRestriction = bRestrict;
    LastAppliedTargetReceiverGPUIds = ReceiverGPUIds;
    return true;
}
