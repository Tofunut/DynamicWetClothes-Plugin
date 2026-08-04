#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialTypes.h"

class UMaterial;
class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;

/** Stateless construction helpers used by a preview-session material cache. */
class FDWCEditorPreviewMaterialFactory
{
  public:
    static constexpr uint32 CommonGraphSchemaVersion = 1;

    static UMaterial* BuildTransientBaseMaterial(
        const FDWCEditorPreviewMaterialRequest& Request,
        FString& OutErrorMessage);

    static UMaterialInstanceConstant* BuildTransientParent(
        UMaterialInterface* SourceMaterial,
        UMaterial* TransientBaseMaterial,
        FString& OutErrorMessage);

    static UMaterialInstanceDynamic* BuildSlotMID(
        UMaterialInterface* TransientParent,
        UObject* Outer,
        FString& OutErrorMessage);
};
