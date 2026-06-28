#pragma once

#include "CoreMinimal.h"
#include "DynamicWet/DynamicWetSourceTypes.h"

class UPrimitiveComponent;

DECLARE_DELEGATE_RetVal_TwoParams(bool, FDWCSourceSurfaceQuery, const FVector&, float&);
DECLARE_DELEGATE(FDWCExternalSourceBindingCleanup);

struct DYNAMICWETCLOTHES_API FDWCExternalSourceBinding
{
	TWeakObjectPtr<UPrimitiveComponent> OverlapComponent;
	FDWCWetSourceData SourceData;
	FDWCSourceSurfaceQuery SurfaceQuery;
	FDWCExternalSourceBindingCleanup Cleanup;

	bool HasBinding() const
	{
		return OverlapComponent.IsValid() ||
			SourceData.bIsValid ||
			SurfaceQuery.IsBound() ||
			Cleanup.IsBound();
	}
};
