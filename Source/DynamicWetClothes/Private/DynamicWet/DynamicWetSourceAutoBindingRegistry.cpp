#include "DynamicWet/DynamicWetSourceAutoBindingRegistry.h"

#include "DynamicWet/DynamicWetSourceComponent.h"

namespace
{
	TArray<TPair<FDelegateHandle, FDWCSourceAutoBindingProvider>>& GetDynamicWetSourceAutoBindingProviders()
	{
		static TArray<TPair<FDelegateHandle, FDWCSourceAutoBindingProvider>> Providers;
		return Providers;
	}
}

FDelegateHandle FDynamicWetSourceAutoBindingRegistry::RegisterProvider(FDWCSourceAutoBindingProvider Provider)
{
	if (!Provider.IsBound())
	{
		return FDelegateHandle();
	}

	FDelegateHandle ProviderHandle(FDelegateHandle::GenerateNewHandle);
	GetDynamicWetSourceAutoBindingProviders().Emplace(ProviderHandle, MoveTemp(Provider));
	return ProviderHandle;
}

void FDynamicWetSourceAutoBindingRegistry::UnregisterProvider(FDelegateHandle ProviderHandle)
{
	if (!ProviderHandle.IsValid())
	{
		return;
	}

	TArray<TPair<FDelegateHandle, FDWCSourceAutoBindingProvider>>& Providers =
		GetDynamicWetSourceAutoBindingProviders();

	Providers.RemoveAll(
		[ProviderHandle](const TPair<FDelegateHandle, FDWCSourceAutoBindingProvider>& Provider)
		{
			return Provider.Key == ProviderHandle;
		}
	);
}

bool FDynamicWetSourceAutoBindingRegistry::ApplyAutoBindings(UDynamicWetSourceComponent& SourceComponent)
{
	for (const TPair<FDelegateHandle, FDWCSourceAutoBindingProvider>& Provider : GetDynamicWetSourceAutoBindingProviders())
	{
		if (Provider.Value.IsBound() && Provider.Value.Execute(SourceComponent))
		{
			return true;
		}
	}

	return false;
}
