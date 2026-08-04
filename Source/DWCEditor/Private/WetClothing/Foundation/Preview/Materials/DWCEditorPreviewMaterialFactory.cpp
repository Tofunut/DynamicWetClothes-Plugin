#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialFactory.h"

#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
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

    TArray<FString> CompileTransientMaterial(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return { TEXT("Cannot compile a null editor preview material.") };
        }

        Material->SetMaterialUsage(MATUSAGE_SkeletalMesh);
        Material->UpdateCachedExpressionData();
        {
            FMaterialUpdateContext UpdateContext(FMaterialUpdateContext::EOptions::SyncWithRenderingThread);
            UpdateContext.AddMaterial(Material);
            Material->PreEditChange(nullptr);
            Material->PostEditChange();
        }

        if (FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
        {
            if (!Resource->IsGameThreadShaderMapComplete())
            {
                Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
            }
            Resource->FinishCompilation();
            return Resource->GetCompileErrors();
        }
        return {};
    }
}

UMaterial* FDWCEditorPreviewMaterialFactory::BuildTransientBaseMaterial(
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

    const TArray<FString> CompileErrors = CompileTransientMaterial(PreviewMaterial);
    if (CompileErrors.Num() > 0)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Editor preview material compilation failed for '%s':\n%s"),
            *GetNameSafe(Request.SourceMaterial),
            *FString::Join(CompileErrors, TEXT("\n")));
        PreviewMaterial->MarkAsGarbage();
        return nullptr;
    }
    return PreviewMaterial;
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
