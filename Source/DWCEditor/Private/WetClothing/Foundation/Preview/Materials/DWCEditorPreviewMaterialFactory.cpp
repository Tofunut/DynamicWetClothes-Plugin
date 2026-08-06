#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialFactory.h"

#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "WetClothing/Foundation/MaterialGraph/DWCSurfaceGraphBuilder.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialParameters.h"

namespace
{
    constexpr int32 MaxGpuSkinUVChannelCount = 4;
    constexpr const TCHAR* PreviewEvaluateSurfaceAppearanceFunctionName = TEXT("MF_DWC_EvaluateSurfaceAppearance");

    bool ContainsGeneratedDwcSurfaceGraph(const UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return false;
        }

        for (const UMaterialExpression* Expression : Material->GetExpressions())
        {
            const UMaterialExpressionMaterialFunctionCall* FunctionCall =
                Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
            if (FunctionCall != nullptr && FunctionCall->MaterialFunction != nullptr &&
                FunctionCall->MaterialFunction->GetName().Equals(
                    PreviewEvaluateSurfaceAppearanceFunctionName,
                    ESearchCase::CaseSensitive))
            {
                return true;
            }
        }
        return false;
    }

    bool ConnectMaterialProperty(
        const FDWCMaterialGraphPin& Pin,
        const EMaterialProperty Property,
        const TCHAR* PropertyName,
        FString& OutErrorMessage)
    {
        if (!Pin.IsValid() ||
            !UMaterialEditingLibrary::ConnectMaterialProperty(Pin.Expression, Pin.OutputName, Property))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Failed to connect the common DWC preview %s output."), PropertyName);
            return false;
        }
        return true;
    }

}

UMaterial* FDWCEditorPreviewMaterialFactory::BuildTransientBaseMaterialGraph(
    const FDWCEditorPreviewMaterialRequest& Request,
    FString& OutErrorMessage)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorPreviewMaterialFactory_BuildTransientBaseMaterial);

    OutErrorMessage.Reset();
    if (Request.SourceMaterial == nullptr)
    {
        OutErrorMessage = TEXT("No source material was supplied for editor preview generation.");
        return nullptr;
    }
    if (Request.DWCDataUVChannelIndex < 0 || Request.DWCDataUVChannelIndex >= MaxGpuSkinUVChannelCount ||
        Request.SurfaceWaterNormalUVChannelIndex < 0 ||
        Request.SurfaceWaterNormalUVChannelIndex >= MaxGpuSkinUVChannelCount)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Editor preview UV channels must be in the GPUSkin-supported range 0-%d."),
            MaxGpuSkinUVChannelCount - 1);
        return nullptr;
    }

    UMaterial* SourceBaseMaterial = const_cast<UMaterial*>(Request.SourceMaterial->GetMaterial());
    if (SourceBaseMaterial == nullptr)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Preview source '%s' has no editable base material."), *GetNameSafe(Request.SourceMaterial));
        return nullptr;
    }
    if (SourceBaseMaterial->bUseMaterialAttributes)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Preview source '%s' uses Material Attributes, which the common DWC preview graph does not support yet."),
            *GetNameSafe(Request.SourceMaterial));
        return nullptr;
    }
    if (ContainsGeneratedDwcSurfaceGraph(SourceBaseMaterial))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Preview source '%s' already contains %s. Resolve the original source material before building the editor preview graph."),
            *GetNameSafe(Request.SourceMaterial),
            PreviewEvaluateSurfaceAppearanceFunctionName);
        return nullptr;
    }

    UMaterial* PreviewMaterial = DuplicateObject<UMaterial>(
        SourceBaseMaterial,
        GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass(), TEXT("DWC_EditorPreviewMaterial")));
    if (PreviewMaterial == nullptr)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to duplicate '%s' for editor preview."), *GetNameSafe(SourceBaseMaterial));
        return nullptr;
    }
    PreviewMaterial->SetFlags(RF_Transient);
    PreviewMaterial->ClearFlags(RF_Public | RF_Standalone);

    UMaterialExpressionScalarParameter* PreviewWetness =
        Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(
            PreviewMaterial,
            UMaterialExpressionScalarParameter::StaticClass(),
            -2280,
            -720));
    if (PreviewWetness == nullptr)
    {
        OutErrorMessage = TEXT("Failed to create DWC_PreviewWetness in the transient preview material.");
        PreviewMaterial->MarkAsGarbage();
        return nullptr;
    }
    PreviewWetness->ParameterName = DWCEditorPreviewMaterialParameters::PreviewWetness();
    PreviewWetness->DefaultValue = 1.0f;
    PreviewWetness->Desc = TEXT("Editor-only representative wetness. Updated through the slot preview MID.");

    FDWCSurfaceGraphBuildRequest SurfaceRequest;
    SurfaceRequest.Material = PreviewMaterial;
    SurfaceRequest.DWCDataUVChannelIndex = Request.DWCDataUVChannelIndex;
    SurfaceRequest.SurfaceWaterNormalUVChannelIndex = Request.SurfaceWaterNormalUVChannelIndex;
    SurfaceRequest.WetnessInput = { PreviewWetness, FString() };

    const FDWCSurfaceGraphBuildResult SurfaceResult = FDWCSurfaceGraphBuilder::Build(SurfaceRequest);
    if (!SurfaceResult.bSucceeded)
    {
        OutErrorMessage = TEXT("Failed to build the common DWC editor preview surface graph.\n") +
                          FString::Join(SurfaceResult.FailureReasons, TEXT("\n"));
        PreviewMaterial->MarkAsGarbage();
        return nullptr;
    }

    bool bConnected = true;
    bConnected &= ConnectMaterialProperty(
        SurfaceResult.Outputs.BaseColor, MP_BaseColor, TEXT("Base Color"), OutErrorMessage);
    bConnected &= ConnectMaterialProperty(
        SurfaceResult.Outputs.Roughness, MP_Roughness, TEXT("Roughness"), OutErrorMessage);
    bConnected &= ConnectMaterialProperty(
        SurfaceResult.Outputs.Specular, MP_Specular, TEXT("Specular"), OutErrorMessage);
    bConnected &= ConnectMaterialProperty(
        SurfaceResult.Outputs.Normal, MP_Normal, TEXT("Normal"), OutErrorMessage);
    if (!bConnected)
    {
        PreviewMaterial->MarkAsGarbage();
        return nullptr;
    }

    if (Request.ExtendGraph && !Request.ExtendGraph(PreviewMaterial, SurfaceResult, OutErrorMessage))
    {
        if (OutErrorMessage.IsEmpty())
        {
            OutErrorMessage = TEXT("The mode-specific editor preview graph extension failed.");
        }
        PreviewMaterial->MarkAsGarbage();
        return nullptr;
    }

    // Graph edits are still performed on the game thread. PostEditChange
    // schedules the engine shader compile; the cache polls that work later
    // instead of blocking this call with FinishCompilation().
    PreviewMaterial->SetMaterialUsage(MATUSAGE_SkeletalMesh);
    PreviewMaterial->UpdateCachedExpressionData();
    {
        FMaterialUpdateContext UpdateContext(FMaterialUpdateContext::EOptions::SyncWithRenderingThread);
        UpdateContext.AddMaterial(PreviewMaterial);
        PreviewMaterial->PreEditChange(nullptr);
        PreviewMaterial->PostEditChange();
    }
    return PreviewMaterial;
}

bool FDWCEditorPreviewMaterialFactory::BeginTransientBaseMaterialCompilation(
    UMaterial* TransientBaseMaterial,
    FString& OutErrorMessage)
{
    OutErrorMessage.Reset();
    if (TransientBaseMaterial == nullptr)
    {
        OutErrorMessage = TEXT("Cannot compile a null editor preview material.");
        return false;
    }

    FMaterialResource* Resource = TransientBaseMaterial->GetMaterialResource(GMaxRHIShaderPlatform);
    if (Resource == nullptr)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Preview material '%s' did not create a shader resource."),
            *GetNameSafe(TransientBaseMaterial));
        return false;
    }

    if (!Resource->IsGameThreadShaderMapComplete())
    {
        Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
    }
    return true;
}

EDWCEditorPreviewMaterialState FDWCEditorPreviewMaterialFactory::PollTransientBaseMaterialCompilation(
    UMaterial* TransientBaseMaterial,
    FString& OutErrorMessage)
{
    OutErrorMessage.Reset();
    if (TransientBaseMaterial == nullptr)
    {
        OutErrorMessage = TEXT("The transient editor preview material no longer exists.");
        return EDWCEditorPreviewMaterialState::Failed;
    }

    FMaterialResource* Resource = TransientBaseMaterial->GetMaterialResource(GMaxRHIShaderPlatform);
    if (Resource == nullptr)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Preview material '%s' no longer has a shader resource."),
            *GetNameSafe(TransientBaseMaterial));
        return EDWCEditorPreviewMaterialState::Failed;
    }

    if (Resource->IsGameThreadShaderMapComplete())
    {
        const TArray<FString>& CompileErrors = Resource->GetCompileErrors();
        if (CompileErrors.Num() > 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Editor preview material compilation failed for '%s':\n%s"),
                *GetNameSafe(TransientBaseMaterial),
                *FString::Join(CompileErrors, TEXT("\n")));
            return EDWCEditorPreviewMaterialState::Failed;
        }
        return EDWCEditorPreviewMaterialState::Ready;
    }

    // IsCompilationFinished() also advances UE 5.8's deferred shader-cache
    // lookup. The first submit made immediately after PostEditChange() can be
    // a no-op while that lookup is pending, so submit again after polling.
    // SubmitCompileJobs_GameThread() is idempotent for an unchanged priority.
    Resource->IsCompilationFinished();
    if (!Resource->IsGameThreadShaderMapComplete())
    {
        Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
    }

    // Re-read after submission. A deferred DDC result can create the compile
    // jobs during the first poll, so the pre-submit value is not authoritative.
    if (!Resource->IsCompilationFinished())
    {
        return EDWCEditorPreviewMaterialState::Compiling;
    }

    const TArray<FString>& CompileErrors = Resource->GetCompileErrors();
    if (CompileErrors.Num() > 0 || !Resource->HasValidGameThreadShaderMap())
    {
        OutErrorMessage = FString::Printf(
            TEXT("Editor preview material compilation failed for '%s':\n%s"),
            *GetNameSafe(TransientBaseMaterial),
            CompileErrors.Num() > 0
                ? *FString::Join(CompileErrors, TEXT("\n"))
                : TEXT("The shader compiler produced no valid game-thread shader map."));
        return EDWCEditorPreviewMaterialState::Failed;
    }

    return EDWCEditorPreviewMaterialState::Ready;
}

void FDWCEditorPreviewMaterialFactory::CancelTransientBaseMaterialCompilation(
    UMaterial* TransientBaseMaterial)
{
    if (TransientBaseMaterial == nullptr)
    {
        return;
    }

    if (FMaterialResource* Resource = TransientBaseMaterial->GetMaterialResource(GMaxRHIShaderPlatform))
    {
        if (!Resource->IsCompilationFinished())
        {
            Resource->CancelCompilation();
        }
    }
}

UMaterialInstanceConstant* FDWCEditorPreviewMaterialFactory::BuildTransientParent(
    UMaterialInterface* SourceMaterial,
    UMaterial* TransientBaseMaterial,
    FString& OutErrorMessage)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorPreviewMaterialFactory_BuildTransientParent);

    OutErrorMessage.Reset();
    if (SourceMaterial == nullptr || TransientBaseMaterial == nullptr)
    {
        OutErrorMessage = TEXT("A source material and transient base material are required to build a preview parent.");
        return nullptr;
    }

    UMaterialInstanceConstant* Parent = NewObject<UMaterialInstanceConstant>(
        GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UMaterialInstanceConstant::StaticClass(), TEXT("DWC_EditorPreviewMIC")),
        RF_Transient);
    if (Parent == nullptr)
    {
        OutErrorMessage = TEXT("Failed to create the transient editor preview material instance.");
        return nullptr;
    }

    Parent->SetParentEditorOnly(TransientBaseMaterial, false);
    Parent->CopyMaterialUniformParametersEditorOnly(SourceMaterial, true);
    if (const UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(SourceMaterial))
    {
        FMaterialInstanceBasePropertyOverrides BasePropertyOverrides = SourceInstance->BasePropertyOverrides;
        Parent->UpdateStaticPermutation(SourceInstance->GetStaticParameters(), BasePropertyOverrides);
    }
    Parent->PostEditChange();
    return Parent;
}

UMaterialInstanceDynamic* FDWCEditorPreviewMaterialFactory::BuildSlotMID(
    UMaterialInterface* TransientParent,
    UObject* Outer,
    FString& OutErrorMessage)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorPreviewMaterialFactory_BuildSlotMID);

    OutErrorMessage.Reset();
    if (TransientParent == nullptr)
    {
        OutErrorMessage = TEXT("A transient preview parent is required to create a slot MID.");
        return nullptr;
    }

    UObject* EffectiveOuter = Outer != nullptr ? Outer : GetTransientPackage();
    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(TransientParent, EffectiveOuter);
    if (MID == nullptr)
    {
        OutErrorMessage = TEXT("Failed to create the editor preview slot MID.");
        return nullptr;
    }
    MID->SetFlags(RF_Transient);
    return MID;
}
