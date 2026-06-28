#include "DynamicWet/DynamicWetSourceComponent.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DynamicWet/DynamicWetReceiverComponent.h"
#include "DynamicWet/DynamicWetSourceAutoBindingRegistry.h"
#include "NiagaraComponent.h"

UDynamicWetSourceComponent::UDynamicWetSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	ManualSourceData.SourceBinding = EDWCSourceBindingType::Manual;
	ManualSourceData.InfluenceType = EDWCInfluenceType::Volume;
	ManualSourceData.InfluenceShape = EDWCInfluenceShape::Box;
	ManualSourceData.Intensity = 0.5f;
	ManualSourceData.bIsValid = true;
}

void UDynamicWetSourceComponent::SetOverlapComponent(UPrimitiveComponent* InOverlapComponent)
{
	if (ExplicitOverlapComponent == InOverlapComponent)
	{
		return;
	}

	UnbindOverlapComponent();
	ExplicitOverlapComponent = InOverlapComponent;

	if (HasBegunPlay())
	{
		BindOverlapComponent();

		if (bApplyToExistingOverlapsOnBeginPlay)
		{
			RefreshExistingOverlaps();
		}
	}
}

void UDynamicWetSourceComponent::SetExternalSourceBinding(FDWCExternalSourceBinding InBinding)
{
	ClearExternalSourceBinding();
	ExternalSourceBinding = MoveTemp(InBinding);

	if (UPrimitiveComponent* ExternalOverlapComponent = ExternalSourceBinding.OverlapComponent.Get())
	{
		if (bApplyingAutoSourceBinding)
		{
			UnbindOverlapComponent();
			ExplicitOverlapComponent = ExternalOverlapComponent;
		}
		else
		{
			SetOverlapComponent(ExternalOverlapComponent);
		}
	}
}

void UDynamicWetSourceComponent::ClearExternalSourceBinding()
{
	if (ExternalSourceBinding.Cleanup.IsBound())
	{
		ExternalSourceBinding.Cleanup.Execute();
	}

	ExternalSourceBinding = FDWCExternalSourceBinding();
}

void UDynamicWetSourceComponent::InitializeWaterVolume(UPrimitiveComponent* InWaterBounds)
{
	ClearExternalSourceBinding();

	ManualSourceData.SourceBinding = EDWCSourceBindingType::Auto;
	ManualSourceData.InfluenceType = EDWCInfluenceType::Volume;
	ManualSourceData.InfluenceShape = EDWCInfluenceShape::Box;
	ManualSourceData.bIsValid = true;

	SetOverlapComponent(InWaterBounds);
}

void UDynamicWetSourceComponent::InitializeRainVolume(
	UPrimitiveComponent* InRainBounds,
	UNiagaraComponent* InRainNiagara
)
{
	ClearExternalSourceBinding();

	ManualSourceData.SourceBinding = EDWCSourceBindingType::Auto;
	ManualSourceData.InfluenceType = EDWCInfluenceType::Directional;
	ManualSourceData.InfluenceShape = EDWCInfluenceShape::Box;
	ManualSourceData.bIsValid = true;
	SourceNiagara = InRainNiagara;

	SetOverlapComponent(InRainBounds);
	ApplyRainNiagaraParameters();
}

FVector UDynamicWetSourceComponent::GetWorldRainDirection() const
{
	const FVector SafeRainDirection =
		ManualSourceData.Direction.IsNearlyZero()
		? FVector(0.0f, 0.0f, -1.0f)
		: ManualSourceData.Direction.GetSafeNormal();

	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorTransform().TransformVectorNoScale(SafeRainDirection).GetSafeNormal();
	}

	return SafeRainDirection;
}

void UDynamicWetSourceComponent::ApplyRainNiagaraParameters() const
{
	const UPrimitiveComponent* OverlapComponent =
		IsValid(BoundOverlapComponent)
		? BoundOverlapComponent.Get()
		: ResolveOverlapComponent();

	if (!IsValid(SourceNiagara) || !IsValid(OverlapComponent))
	{
		return;
	}

	SourceNiagara->SetVariableVec3(
		RainDirectionParameterName,
		ManualSourceData.Direction
	);

	if (const UBoxComponent* BoxComponent = Cast<UBoxComponent>(OverlapComponent))
	{
		SourceNiagara->SetVariableVec3(
			RainBoundsExtentParameterName,
			BoxComponent->GetScaledBoxExtent() * 2.0f
		);
	}
	else
	{
		SourceNiagara->SetVariableVec3(
			RainBoundsExtentParameterName,
			OverlapComponent->Bounds.BoxExtent * 2.0f
		);
	}

	SourceNiagara->SetVariableFloat(
		RainIntensityParameterName,
		ManualSourceData.Intensity
	);
}

void UDynamicWetSourceComponent::BeginPlay()
{
	Super::BeginPlay();

	ApplyAutoSourceBindings();
	BindOverlapComponent();

	if (bApplyToExistingOverlapsOnBeginPlay)
	{
		RefreshExistingOverlaps();
	}
}

void UDynamicWetSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearActiveWetSources();
	UnbindOverlapComponent();
	ClearExternalSourceBinding();

	Super::EndPlay(EndPlayReason);
}

bool UDynamicWetSourceComponent::BuildWetSourceData(FDWCWetSourceData& OutSourceData) const
{
	const bool bUseExternalBinding = ExternalSourceBinding.SourceData.bIsValid;
	OutSourceData = bUseExternalBinding ? ExternalSourceBinding.SourceData : ManualSourceData;

	if (!bUseExternalBinding)
	{
		OutSourceData.SourceBinding = EDWCSourceBindingType::Manual;
	}

	OutSourceData.bIsValid = OutSourceData.bIsValid && OutSourceData.Intensity > 0.0f;

	const AActor* Owner = GetOwner();
	if (Owner)
	{
		OutSourceData.WorldLocation = Owner->GetActorLocation();

		if (OutSourceData.Direction.IsNearlyZero())
		{
			OutSourceData.Direction = -Owner->GetActorUpVector();
		}
		else
		{
			OutSourceData.Direction = Owner->GetActorTransform()
				.TransformVectorNoScale(OutSourceData.Direction)
				.GetSafeNormal();
		}
	}

	if (const UPrimitiveComponent* OverlapComponent = BoundOverlapComponent.Get())
	{
		OutSourceData.WorldBounds = OverlapComponent->Bounds.GetBox();
	}

	if (OutSourceData.InfluenceType == EDWCInfluenceType::Volume)
	{
		if (!bUseExternalBinding)
		{
			OutSourceData.bUseSourceSurfaceHeightQuery = false;
		}

		QueryWetSurfaceZ(OutSourceData.WorldLocation, OutSourceData.WaterLevel);
	}
	else if (OutSourceData.InfluenceType == EDWCInfluenceType::Directional)
	{
		OutSourceData.Direction = GetWorldRainDirection();
	}

	return OutSourceData.bIsValid;
}

bool UDynamicWetSourceComponent::QueryWetSurfaceZ(const FVector& WorldPosition, float& OutSurfaceZ) const
{
	if (ExternalSourceBinding.SurfaceQuery.IsBound() &&
		ExternalSourceBinding.SurfaceQuery.Execute(WorldPosition, OutSurfaceZ))
	{
		return true;
	}

	if (const UPrimitiveComponent* OverlapComponent = BoundOverlapComponent.Get())
	{
		OutSurfaceZ =
			OverlapComponent->GetComponentLocation().Z +
			OverlapComponent->Bounds.BoxExtent.Z;

		return true;
	}

	if (const AActor* Owner = GetOwner())
	{
		OutSurfaceZ = Owner->GetActorLocation().Z;
		return true;
	}

	return false;
}

void UDynamicWetSourceComponent::ApplyAutoSourceBindings()
{
	if (!bEnableAutoSourceBinding)
	{
		return;
	}

	TGuardValue<bool> ApplyingAutoSourceBindingGuard(bApplyingAutoSourceBinding, true);
	FDynamicWetSourceAutoBindingRegistry::ApplyAutoBindings(*this);
}

UPrimitiveComponent* UDynamicWetSourceComponent::ResolveOverlapComponent() const
{
	if (IsValid(ExplicitOverlapComponent))
	{
		return ExplicitOverlapComponent;
	}

	if (!bAutoUseOwnerPrimitiveOverlap)
	{
		return nullptr;
	}

	AActor* Owner = GetOwner();
	return IsValid(Owner) ? Owner->FindComponentByClass<UPrimitiveComponent>() : nullptr;
}

void UDynamicWetSourceComponent::BindOverlapComponent()
{
	BoundOverlapComponent = ResolveOverlapComponent();
	if (!IsValid(BoundOverlapComponent))
	{
		return;
	}

	BoundOverlapComponent->OnComponentBeginOverlap.AddUniqueDynamic(
		this,
		&UDynamicWetSourceComponent::OnSourceBeginOverlap
	);

	BoundOverlapComponent->OnComponentEndOverlap.AddUniqueDynamic(
		this,
		&UDynamicWetSourceComponent::OnSourceEndOverlap
	);
}

void UDynamicWetSourceComponent::UnbindOverlapComponent()
{
	if (!IsValid(BoundOverlapComponent))
	{
		return;
	}

	BoundOverlapComponent->OnComponentBeginOverlap.RemoveDynamic(
		this,
		&UDynamicWetSourceComponent::OnSourceBeginOverlap
	);

	BoundOverlapComponent->OnComponentEndOverlap.RemoveDynamic(
		this,
		&UDynamicWetSourceComponent::OnSourceEndOverlap
	);

	BoundOverlapComponent = nullptr;
}

void UDynamicWetSourceComponent::RefreshExistingOverlaps()
{
	if (!IsValid(BoundOverlapComponent))
	{
		return;
	}

	BoundOverlapComponent->UpdateOverlaps();

	TArray<AActor*> OverlappingActors;
	BoundOverlapComponent->GetOverlappingActors(OverlappingActors);

	for (AActor* OverlappingActor : OverlappingActors)
	{
		ApplyWetSourceToActor(OverlappingActor);
	}
}

void UDynamicWetSourceComponent::ApplyWetSourceToActor(AActor* OtherActor)
{
	if (!IsValid(OtherActor) || OtherActor == GetOwner())
	{
		return;
	}

	UDynamicWetReceiverComponent* WetReceiver = OtherActor->FindComponentByClass<UDynamicWetReceiverComponent>();
	if (!IsValid(WetReceiver))
	{
		return;
	}

	FDWCWetSourceData SourceData;
	if (!BuildWetSourceData(SourceData))
	{
		return;
	}

	int32& OverlapCount = ActiveWetReceiverOverlapCounts.FindOrAdd(WetReceiver);
	++OverlapCount;

	WetReceiver->SetWetSourceData(this, SourceData);
}

void UDynamicWetSourceComponent::RemoveWetSourceFromActor(AActor* OtherActor)
{
	if (!IsValid(OtherActor))
	{
		return;
	}

	UDynamicWetReceiverComponent* WetReceiver = OtherActor->FindComponentByClass<UDynamicWetReceiverComponent>();
	if (!IsValid(WetReceiver))
	{
		return;
	}

	int32* OverlapCount = ActiveWetReceiverOverlapCounts.Find(WetReceiver);
	if (!OverlapCount)
	{
		return;
	}

	--(*OverlapCount);
	if (*OverlapCount > 0)
	{
		return;
	}

	ActiveWetReceiverOverlapCounts.Remove(WetReceiver);
	WetReceiver->ClearWetSource(this);
}

void UDynamicWetSourceComponent::ClearActiveWetSources()
{
	for (const TPair<TWeakObjectPtr<UDynamicWetReceiverComponent>, int32>& Pair : ActiveWetReceiverOverlapCounts)
	{
		if (UDynamicWetReceiverComponent* WetReceiver = Pair.Key.Get())
		{
			WetReceiver->ClearWetSource(this);
		}
	}

	ActiveWetReceiverOverlapCounts.Reset();
}

void UDynamicWetSourceComponent::OnSourceBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult
)
{
	ApplyWetSourceToActor(OtherActor);
}

void UDynamicWetSourceComponent::OnSourceEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex
)
{
	RemoveWetSourceFromActor(OtherActor);
}
