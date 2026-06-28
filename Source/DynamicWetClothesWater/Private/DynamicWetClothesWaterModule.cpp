#include "DynamicWet/DynamicWetSourceComponent.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DynamicWet/DynamicWetSourceAutoBindingRegistry.h"
#include "DynamicWet/DynamicWetSourceBindingTypes.h"
#include "Modules/ModuleManager.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"

namespace
{
	bool GetWaterBodyProxyBounds(const UWaterBodyComponent* WaterBodyComponent, FBox& OutBounds)
	{
		if (!IsValid(WaterBodyComponent))
		{
			return false;
		}

		OutBounds = WaterBodyComponent->GetCollisionComponentBounds();
		if (OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero())
		{
			return true;
		}

		OutBounds.Init();
		for (UPrimitiveComponent* CollisionComponent : WaterBodyComponent->GetCollisionComponents(false))
		{
			if (!IsValid(CollisionComponent))
			{
				continue;
			}

			const FBox ComponentBounds = CollisionComponent->Bounds.GetBox();
			if (ComponentBounds.IsValid && !ComponentBounds.GetExtent().IsNearlyZero())
			{
				OutBounds += ComponentBounds;
			}
		}

		if (OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero())
		{
			return true;
		}

		OutBounds = WaterBodyComponent->Bounds.GetBox();
		return OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero();
	}

	bool TryBindWaterBodyWetSource(UDynamicWetSourceComponent& SourceComponent)
	{
		AActor* Owner = SourceComponent.GetOwner();
		if (!IsValid(Owner))
		{
			return false;
		}

		UWaterBodyComponent* WaterBodyComponent = Owner->FindComponentByClass<UWaterBodyComponent>();
		if (!IsValid(WaterBodyComponent))
		{
			return false;
		}

		USceneComponent* RootComponent = Owner->GetRootComponent();
		if (!IsValid(RootComponent))
		{
			return false;
		}

		FBox WaterBodyBounds(ForceInit);
		if (!GetWaterBodyProxyBounds(WaterBodyComponent, WaterBodyBounds))
		{
			UE_LOG(LogTemp, Warning, TEXT("DynamicWetClothesWater: Could not resolve water body bounds on %s."), *Owner->GetName());
			return false;
		}

		const float DesiredTopZ = WaterBodyBounds.Max.Z + WaterBodyComponent->GetMaxWaveHeight();
		const float DesiredBottomZ = WaterBodyBounds.Min.Z;

		const FVector ProxyOrigin(
			(WaterBodyBounds.Min.X + WaterBodyBounds.Max.X) * 0.5f,
			(WaterBodyBounds.Min.Y + WaterBodyBounds.Max.Y) * 0.5f,
			(DesiredTopZ + DesiredBottomZ) * 0.5f
		);

		const FVector ProxyExtent(
			FMath::Max((WaterBodyBounds.Max.X - WaterBodyBounds.Min.X) * 0.5f, 1.0f),
			FMath::Max((WaterBodyBounds.Max.Y - WaterBodyBounds.Min.Y) * 0.5f, 1.0f),
			FMath::Max((DesiredTopZ - DesiredBottomZ) * 0.5f, 1.0f)
		);

		UBoxComponent* WetnessOverlapProxy = NewObject<UBoxComponent>(Owner, TEXT("DynamicWetWaterOverlapProxy"));
		if (!IsValid(WetnessOverlapProxy))
		{
			return false;
		}

		Owner->AddInstanceComponent(WetnessOverlapProxy);
		WetnessOverlapProxy->SetupAttachment(RootComponent);
		WetnessOverlapProxy->SetBoxExtent(ProxyExtent, false);
		WetnessOverlapProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		WetnessOverlapProxy->SetCollisionObjectType(ECC_WorldDynamic);
		WetnessOverlapProxy->SetCollisionResponseToAllChannels(ECR_Overlap);
		WetnessOverlapProxy->SetGenerateOverlapEvents(true);
		WetnessOverlapProxy->RegisterComponent();
		WetnessOverlapProxy->SetWorldLocation(ProxyOrigin);

		FDWCWetSourceData SourceData = SourceComponent.GetManualSourceData();
		SourceData.SourceBinding = EDWCSourceBindingType::UnrealWater;
		SourceData.InfluenceType = EDWCInfluenceType::Volume;
		SourceData.InfluenceShape = EDWCInfluenceShape::Custom;
		SourceData.bUseSourceSurfaceHeightQuery = true;
		SourceData.bIsValid = SourceData.Intensity > 0.0f;
		SourceData.WorldLocation = Owner->GetActorLocation();
		SourceData.WorldBounds = WetnessOverlapProxy->Bounds.GetBox();

		TWeakObjectPtr<UWaterBodyComponent> WeakWaterBodyComponent(WaterBodyComponent);
		TWeakObjectPtr<UBoxComponent> WeakWetnessOverlapProxy(WetnessOverlapProxy);

		FDWCExternalSourceBinding ExternalBinding;
		ExternalBinding.OverlapComponent = WetnessOverlapProxy;
		ExternalBinding.SourceData = SourceData;
		ExternalBinding.SurfaceQuery.BindLambda(
			[WeakWaterBodyComponent](const FVector& WorldPosition, float& OutSurfaceZ)
			{
				UWaterBodyComponent* WaterBody = WeakWaterBodyComponent.Get();
				if (!IsValid(WaterBody))
				{
					return false;
				}

				const EWaterBodyQueryFlags QueryFlags =
					EWaterBodyQueryFlags::ComputeLocation |
					EWaterBodyQueryFlags::IncludeWaves;

				const auto QueryResult =
					WaterBody->TryQueryWaterInfoClosestToWorldLocation(
						WorldPosition,
						QueryFlags
					);

				if (!QueryResult.HasValue())
				{
					return false;
				}

				OutSurfaceZ = QueryResult.GetValue().GetWaterSurfaceLocation().Z;
				return true;
			}
		);
		ExternalBinding.Cleanup.BindLambda(
			[WeakWetnessOverlapProxy]()
			{
				if (UBoxComponent* Proxy = WeakWetnessOverlapProxy.Get())
				{
					Proxy->DestroyComponent();
				}
			}
		);

		SourceComponent.SetExternalSourceBinding(MoveTemp(ExternalBinding));
		return true;
	}
}

class FDynamicWetClothesWaterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		WaterBindingProviderHandle = FDynamicWetSourceAutoBindingRegistry::RegisterProvider(
			FDWCSourceAutoBindingProvider::CreateStatic(&TryBindWaterBodyWetSource)
		);
	}

	virtual void ShutdownModule() override
	{
		FDynamicWetSourceAutoBindingRegistry::UnregisterProvider(WaterBindingProviderHandle);
		WaterBindingProviderHandle.Reset();
	}

private:
	FDelegateHandle WaterBindingProviderHandle;
};

IMPLEMENT_MODULE(FDynamicWetClothesWaterModule, DynamicWetClothesWater)
