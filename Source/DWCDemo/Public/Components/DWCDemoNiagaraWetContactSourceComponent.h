//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WetInputSystem/WetContactTypes.h"
#include "NiagaraDataInterfaceExport.h"
#include "DWCDemoNiagaraWetContactSourceComponent.generated.h"

class UDynamicWetClothesComponent;
class UNiagaraComponent;

UCLASS(ClassGroup = (DWC), DisplayName = "DWC Demo Niagara Wet Contact Source", meta = (BlueprintSpawnableComponent))
class DWCDEMO_API UDWCDemoNiagaraWetContactSourceComponent
    : public UActorComponent,
      public INiagaraParticleCallbackHandler
{
    GENERATED_BODY()

  public:
    UDWCDemoNiagaraWetContactSourceComponent();

    virtual void BeginPlay() override;

    virtual void ReceiveParticleData_Implementation(
        const TArray<FBasicParticleData>& Data,
        UNiagaraSystem*                   NiagaraSystem,
        const FVector&                    SimulationPositionOffset) override;

  private:
    UDynamicWetClothesComponent* ResolveReceiver() const;
    UNiagaraComponent*           ResolveNiagaraComponent() const;
    void                         BindCallbackUserParameter();
    bool                         ShouldLogDebug() const;
    UDynamicWetClothesComponent* FindNearestReceiver(const FVector& Location, float MaxDistance) const;
    bool                         BuildContactsFromParticle(
                                const FBasicParticleData& Particle,
                                const FVector&            SimulationPositionOffset,
                                UDynamicWetClothesComponent*& InOutReceiver,
                                TArray<FDWCWetContact>&   OutContacts) const;

  public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Receiver")
    TObjectPtr<UDynamicWetClothesComponent> Receiver = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Receiver")
    bool bFindReceiverOnOwner = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Niagara")
    bool bFindNiagaraComponentOnOwner = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Niagara")
    FName CallbackUserParameterName = TEXT("User.WetContactCallbackHandler");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Niagara")
    bool bBindCallbackUserParameterOnBeginPlay = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (ClampMin = "0.0"))
    float ContactAmount = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (ClampMin = "0.0"))
    float ContactRadius = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact")
    bool bUseParticleSizeAsRadius = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (ClampMin = "0.0", EditCondition = "bUseParticleSizeAsRadius"))
    float ParticleSizeRadiusScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (ClampMin = "0"))
    int32 MaxParticlesPerCallback = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (ClampMin = "0.0"))
    float MinVelocityForContact = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Trace")
    bool bTraceForContactData = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Trace", meta = (EditCondition = "bTraceForContactData"))
    TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Trace", meta = (ClampMin = "0.0", EditCondition = "bTraceForContactData"))
    float TraceDistance = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Trace", meta = (EditCondition = "bTraceForContactData"))
    bool bTraceComplex = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug")
    bool bEnableDebugLogging = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug", meta = (ClampMin = "0.1"))
    float DebugLogInterval = 1.0f;

  private:
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> NiagaraComponent = nullptr;

    mutable double LastDebugLogTime = -1000000.0;
};
