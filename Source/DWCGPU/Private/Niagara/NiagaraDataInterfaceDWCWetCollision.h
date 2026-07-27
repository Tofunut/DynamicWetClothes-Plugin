#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraRenderGraphUtils.h"

#include "NiagaraDataInterfaceDWCWetCollision.generated.h"

class FNiagaraSystemInstance;

struct FDWCWetCollisionInstanceData_GT
{
    int32 MaxContacts = 4096;
};

struct FDWCWetCollisionInstanceData_RT
{
    void ResizeBuffers(FRDGBuilder& GraphBuilder);

    int32 MaxContacts = 4096;
    bool bNeedsRealloc = false;

    FNiagaraPooledRWBuffer ContactBuffer;
    FNiagaraPooledRWBuffer ContactCountBuffer;
    uint64 LastClearedRenderFrame = 0;
};

struct FDWCNiagaraDataInterfaceProxyWetCollision final : public FNiagaraDataInterfaceProxy
{
    virtual void ResetData(const FNDIGpuComputeResetContext& Context) override;
    virtual void PreStage(const FNDIGpuComputePreStageContext& Context) override;
    virtual void PostSimulate(const FNDIGpuComputePostSimulateContext& Context) override;
    virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override {}
    virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return 0; }

    TMap<FNiagaraSystemInstanceID, FDWCWetCollisionInstanceData_RT> SystemInstancesToProxyData_RT;
};

UCLASS(EditInlineNew, Category = "DWC", CollapseCategories, meta = (DisplayName = "DWC Wet Collision"))
class DWCGPU_API UNiagaraDataInterfaceDWCWetCollision final : public UNiagaraDataInterface
{
    GENERATED_BODY()

    BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
        SHADER_PARAMETER(int32, MaxContacts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, Contacts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<int>, ContactCount)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, OutputContacts)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, OutputContactCount)
    END_SHADER_PARAMETER_STRUCT()

public:
    UNiagaraDataInterfaceDWCWetCollision(FObjectInitializer const& ObjectInitializer = FObjectInitializer::Get());

    UPROPERTY(EditAnywhere, Category = "DWC", meta = (ClampMin = "1", ClampMax = "1048576"))
    int32 MaxContacts = 4096;

    virtual void PostInitProperties() override;
    virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
    virtual bool Equals(const UNiagaraDataInterface* Other) const override;
    virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
    virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
    virtual int32 PerInstanceDataSize() const override { return sizeof(FDWCWetCollisionInstanceData_GT); }
    virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;

#if WITH_EDITORONLY_DATA
    virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
    virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
    virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif

    virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
    virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

protected:
#if WITH_EDITORONLY_DATA
    virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const override;
#endif
};
