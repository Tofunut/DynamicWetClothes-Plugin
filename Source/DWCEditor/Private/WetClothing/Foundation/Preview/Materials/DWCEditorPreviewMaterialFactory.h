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

    static UMaterial* BuildTransientBaseMaterialGraph(
        const FDWCEditorPreviewMaterialRequest& Request,
        FString& OutErrorMessage);

    /** Starts shader compilation without blocking the editor thread. */
    static bool BeginTransientBaseMaterialCompilation(
        UMaterial* TransientBaseMaterial,
        FString& OutErrorMessage);

    /** Polls a previously submitted compile and never waits for completion. */
    static EDWCEditorPreviewMaterialState PollTransientBaseMaterialCompilation(
        UMaterial* TransientBaseMaterial,
        FString& OutErrorMessage);

    static void CancelTransientBaseMaterialCompilation(UMaterial* TransientBaseMaterial);

    static UMaterialInstanceConstant* BuildTransientParent(
        UMaterialInterface* SourceMaterial,
        UMaterial* TransientBaseMaterial,
        FString& OutErrorMessage);

    static UMaterialInstanceDynamic* BuildSlotMID(
        UMaterialInterface* TransientParent,
        UObject* Outer,
        FString& OutErrorMessage);
};
