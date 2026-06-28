#pragma once

#include "CoreMinimal.h"

class UDynamicWetSourceComponent;

DECLARE_DELEGATE_RetVal_OneParam(bool, FDWCSourceAutoBindingProvider, UDynamicWetSourceComponent&);

class DYNAMICWETCLOTHES_API FDynamicWetSourceAutoBindingRegistry
{
public:
	static FDelegateHandle RegisterProvider(FDWCSourceAutoBindingProvider Provider);
	static void UnregisterProvider(FDelegateHandle ProviderHandle);
	static bool ApplyAutoBindings(UDynamicWetSourceComponent& SourceComponent);
};
