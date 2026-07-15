#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "DWCMaterialSetupEditorLibrary.generated.h"

class UWetClothingAsset;

/** Scriptable entry points that reuse the production Material Setup repair path. */
UCLASS()
class DWCEDITOR_API UDWCMaterialSetupEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

  public:
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "DWC|Material Setup")
    static bool RepairGeneratedWetMaterials(UWetClothingAsset* WetClothingAsset, FString& OutReport);

};
