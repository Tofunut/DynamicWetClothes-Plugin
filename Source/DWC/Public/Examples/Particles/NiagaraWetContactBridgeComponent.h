#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WetInputSystem/WetContactTypes.h"
#include "NiagaraDataInterfaceExport.h"
#include "NiagaraWetContactBridgeComponent.generated.h"

class UDynamicWetClothesComponent;
class UNiagaraComponent;

UCLASS(ClassGroup = (Wetness), DisplayName = "Niagara Wet Contact Bridge", meta = (BlueprintSpawnableComponent))
class DWC_API UNiagaraWetContactBridgeComponent
    : public UActorComponent,
      public INiagaraParticleCallbackHandler
{
    GENERATED_BODY()

  public:
    UNiagaraWetContactBridgeComponent();

    virtual void BeginPlay() override;

    virtual void ReceiveParticleData_Implementation(
        const TArray<FBasicParticleData>& Data,
        UNiagaraSystem*                   NiagaraSystem,
        const FVector&                    SimulationPositionOffset) override;

  private:
    UDynamicWetClothesComponent* ResolveReceiver() const;
    UNiagaraComponent*           ResolveNiagaraComponent() const;
    void                         BindCallbackUserParameter();
    bool                         BuildContactsFromParticle(
                                const FBasicParticleData& Particle,
                                const FVector&            SimulationPositionOffset,
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact")
    bool bRequireBoneNameForContact = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Trace")
    bool bTraceForWaterSurfaceData = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Trace", meta = (EditCondition = "bTraceForWaterSurfaceData"))
    TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Trace", meta = (ClampMin = "0.0", EditCondition = "bTraceForWaterSurfaceData"))
    float TraceDistance = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Trace", meta = (EditCondition = "bTraceForWaterSurfaceData"))
    bool bTraceComplex = false;

  private:
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> NiagaraComponent = nullptr;
};
