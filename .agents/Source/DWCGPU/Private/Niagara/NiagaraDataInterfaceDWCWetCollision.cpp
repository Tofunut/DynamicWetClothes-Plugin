#include "Niagara/NiagaraDataInterfaceDWCWetCollision.h"

#include "Niagara/DWCGPUNiagaraWetCollisionBridge.h"
#include "NiagaraCompileHashVisitor.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"
#include "RenderCore.h"
#include "RenderGraphUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraDataInterfaceDWCWetCollision)

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceDWCWetCollision"

namespace DWCNiagaraWetCollisionLocal
{
static const TCHAR* TemplateShaderFile = TEXT("/DWCGPU/NiagaraDataInterfaceDWCWetCollision.ush");

static const FString MaxContactsName(TEXT("_MaxContacts"));
static const FString ContactsName(TEXT("_Contacts"));
static const FString ContactCountName(TEXT("_ContactCount"));
static const FString OutputContactsName(TEXT("_OutputContacts"));
static const FString OutputContactCountName(TEXT("_OutputContactCount"));

static const FName WriteWetContactName(TEXT("WriteWetContact"));
static constexpr int32 ContactFloat4Count = 2;
}

void FDWCWetCollisionInstanceData_RT::ResizeBuffers(FRDGBuilder& GraphBuilder)
{
    bNeedsRealloc = false;

    const uint32 SafeMaxContacts = FMath::Max(MaxContacts, 1);
    ContactBuffer.Release();
    ContactCountBuffer.Release();
    ContactBuffer.Initialize(
        GraphBuilder,
        TEXT("DWC.NiagaraWetCollision.Contacts"),
        PF_A32B32G32R32F,
        sizeof(FVector4f),
        SafeMaxContacts * DWCNiagaraWetCollisionLocal::ContactFloat4Count,
        BUF_Static);
    ContactCountBuffer.Initialize(
        GraphBuilder,
        TEXT("DWC.NiagaraWetCollision.ContactCount"),
        PF_R32_SINT,
        sizeof(int32),
        1,
        BUF_Static);
    AddClearUAVPass(GraphBuilder, ContactCountBuffer.GetOrCreateUAV(GraphBuilder), 0);
}

UNiagaraDataInterfaceDWCWetCollision::UNiagaraDataInterfaceDWCWetCollision(FObjectInitializer const& ObjectInitializer)
    : Super(ObjectInitializer)
{
    Proxy.Reset(new FDWCNiagaraDataInterfaceProxyWetCollision());
}

void UNiagaraDataInterfaceDWCWetCollision::PostInitProperties()
{
    Super::PostInitProperties();
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        FNiagaraTypeRegistry::Register(
            FNiagaraTypeDefinition(GetClass()),
            ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter);
    }
}

bool UNiagaraDataInterfaceDWCWetCollision::Equals(const UNiagaraDataInterface* Other) const
{
    if (!Super::Equals(Other))
    {
        return false;
    }
    const UNiagaraDataInterfaceDWCWetCollision* OtherTyped =
        CastChecked<const UNiagaraDataInterfaceDWCWetCollision>(Other);
    return OtherTyped->MaxContacts == MaxContacts;
}

bool UNiagaraDataInterfaceDWCWetCollision::InitPerInstanceData(
    void* PerInstanceData,
    FNiagaraSystemInstance* SystemInstance)
{
    FDWCWetCollisionInstanceData_GT* InstanceData =
        new (PerInstanceData) FDWCWetCollisionInstanceData_GT();
    InstanceData->MaxContacts = FMath::Max(MaxContacts, 1);

    FDWCNiagaraDataInterfaceProxyWetCollision* RTProxy =
        GetProxyAs<FDWCNiagaraDataInterfaceProxyWetCollision>();
    const int32 RTMaxContacts = InstanceData->MaxContacts;
    ENQUEUE_RENDER_COMMAND(DWCWetCollisionDIInit)(
        [RTProxy, InstanceID = SystemInstance->GetId(), RTMaxContacts](FRHICommandListImmediate& RHICmdList)
        {
            FDWCWetCollisionInstanceData_RT& TargetData =
                RTProxy->SystemInstancesToProxyData_RT.FindOrAdd(InstanceID);
            TargetData.MaxContacts = RTMaxContacts;
            TargetData.bNeedsRealloc = true;
        });

    return true;
}

void UNiagaraDataInterfaceDWCWetCollision::DestroyPerInstanceData(
    void* PerInstanceData,
    FNiagaraSystemInstance* SystemInstance)
{
    FDWCWetCollisionInstanceData_GT* InstanceData =
        reinterpret_cast<FDWCWetCollisionInstanceData_GT*>(PerInstanceData);
    InstanceData->~FDWCWetCollisionInstanceData_GT();

    FDWCNiagaraDataInterfaceProxyWetCollision* RTProxy =
        GetProxyAs<FDWCNiagaraDataInterfaceProxyWetCollision>();
    ENQUEUE_RENDER_COMMAND(DWCWetCollisionDIDestroy)(
        [RTProxy, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
        {
            DWCGPUNiagaraWetCollisionBridge::UnregisterBuffer_RenderThread(InstanceID);
            RTProxy->SystemInstancesToProxyData_RT.Remove(InstanceID);
        });
}

void UNiagaraDataInterfaceDWCWetCollision::GetVMExternalFunction(
    const FVMExternalFunctionBindingInfo& BindingInfo,
    void* InstanceData,
    FVMExternalFunction& OutFunc)
{
    OutFunc = FVMExternalFunction::CreateLambda([](FVectorVMExternalFunctionContext& Context) {});
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceDWCWetCollision::GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const
{
    using namespace DWCNiagaraWetCollisionLocal;
    Super::GetFunctionsInternal(OutFunctions);

    FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
    Sig.Name = WriteWetContactName;
    NIAGARA_ADD_FUNCTION_SOURCE_INFO(Sig);
    Sig.SetDescription(LOCTEXT("WriteWetContactDesc", "Writes a GPU wet contact candidate for DWC wet-map collision consumption."));
    Sig.bMemberFunction = true;
    Sig.ModuleUsageBitmask = ENiagaraScriptUsageMask::Particle;
    Sig.bRequiresContext = false;
    Sig.bRequiresExecPin = true;
    Sig.bWriteFunction = true;
    Sig.bSupportsCPU = true;
    Sig.bSupportsGPU = true;
    Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(UNiagaraDataInterfaceDWCWetCollision::StaticClass()), TEXT("DWCWetCollision")));
    Sig.Inputs.Emplace_GetRef(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Execute")).SetValue(true);
    Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("WorldPosition")));
    Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Radius")));
    Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Amount")));
    Sig.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Success")));
}

bool UNiagaraDataInterfaceDWCWetCollision::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
    bool bSuccess = Super::AppendCompileHash(InVisitor);
    bSuccess &= InVisitor->UpdateShaderFile(DWCNiagaraWetCollisionLocal::TemplateShaderFile);
    bSuccess &= InVisitor->UpdateShaderParameters<FShaderParameters>();
    return bSuccess;
}

void UNiagaraDataInterfaceDWCWetCollision::GetParameterDefinitionHLSL(
    const FNiagaraDataInterfaceGPUParamInfo& ParamInfo,
    FString& OutHLSL)
{
    using namespace DWCNiagaraWetCollisionLocal;

    OutHLSL.Appendf(TEXT("int %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *MaxContactsName);
    OutHLSL.Appendf(TEXT("Buffer<float4> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *ContactsName);
    OutHLSL.Appendf(TEXT("Buffer<int> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *ContactCountName);
    OutHLSL.Appendf(TEXT("RWBuffer<float4> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *OutputContactsName);
    OutHLSL.Appendf(TEXT("RWBuffer<int> %s%s;\n"), *ParamInfo.DataInterfaceHLSLSymbol, *OutputContactCountName);
}

bool UNiagaraDataInterfaceDWCWetCollision::GetFunctionHLSL(
    const FNiagaraDataInterfaceGPUParamInfo& ParamInfo,
    const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo,
    int FunctionInstanceIndex,
    FString& OutHLSL)
{
    using namespace DWCNiagaraWetCollisionLocal;
    if (FunctionInfo.DefinitionName != WriteWetContactName)
    {
        return false;
    }

    TMap<FString, FStringFormatArg> FormatArgs =
    {
        {TEXT("FunctionName"), FunctionInfo.InstanceName},
        {TEXT("MaxContacts"), ParamInfo.DataInterfaceHLSLSymbol + MaxContactsName},
        {TEXT("OutputContacts"), ParamInfo.DataInterfaceHLSLSymbol + OutputContactsName},
        {TEXT("OutputContactCount"), ParamInfo.DataInterfaceHLSLSymbol + OutputContactCountName},
    };

    static const TCHAR* FormatHLSL = TEXT(R"(
void {FunctionName}(bool Execute, float3 WorldPosition, float Radius, float Amount, out bool Success)
{
    Success = false;
    if (!Execute || Amount == 0.0 || Radius <= 0.0)
    {
        return;
    }

    int ContactIndex = 0;
    InterlockedAdd({OutputContactCount}[0], 1, ContactIndex);
    if (ContactIndex >= {MaxContacts})
    {
        return;
    }

    const int BaseIndex = ContactIndex * 2;
    {OutputContacts}[BaseIndex + 0] = float4(WorldPosition, Radius);
    {OutputContacts}[BaseIndex + 1] = float4(0.0, 0.0, 0.0, Amount);
    Success = true;
}
)");
    OutHLSL += FString::Format(FormatHLSL, FormatArgs);
    return true;
}
#endif

void UNiagaraDataInterfaceDWCWetCollision::BuildShaderParameters(
    FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
    ShaderParametersBuilder.AddNestedStruct<FShaderParameters>();
}

void UNiagaraDataInterfaceDWCWetCollision::SetShaderParameters(
    const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
    FDWCNiagaraDataInterfaceProxyWetCollision& DIProxy =
        Context.GetProxy<FDWCNiagaraDataInterfaceProxyWetCollision>();
    FDWCWetCollisionInstanceData_RT* ProxyData =
        DIProxy.SystemInstancesToProxyData_RT.Find(Context.GetSystemInstanceID());
    FRDGBuilder& GraphBuilder = Context.GetGraphBuilder();

    FShaderParameters* ShaderParameters = Context.GetParameterNestedStruct<FShaderParameters>();
    if (ProxyData != nullptr && ProxyData->bNeedsRealloc)
    {
        ProxyData->ResizeBuffers(GraphBuilder);
        DWCGPUNiagaraWetCollisionBridge::RegisterBuffer_RenderThread(
            Context.GetSystemInstanceID(),
            ProxyData->MaxContacts,
            ProxyData->ContactBuffer.GetPooledBuffer(),
            ProxyData->ContactCountBuffer.GetPooledBuffer());
    }

    if (ProxyData != nullptr && ProxyData->ContactBuffer.IsValid())
    {
        ShaderParameters->MaxContacts = ProxyData->MaxContacts;
        ShaderParameters->Contacts = ProxyData->ContactBuffer.GetOrCreateSRV(GraphBuilder);
        ShaderParameters->ContactCount = ProxyData->ContactCountBuffer.GetOrCreateSRV(GraphBuilder);
        ShaderParameters->OutputContacts = ProxyData->ContactBuffer.GetOrCreateUAV(GraphBuilder);
        ShaderParameters->OutputContactCount = ProxyData->ContactCountBuffer.GetOrCreateUAV(GraphBuilder);
    }
    else
    {
        ShaderParameters->MaxContacts = 0;
        ShaderParameters->Contacts =
            Context.GetComputeDispatchInterface().GetEmptyBufferSRV(GraphBuilder, PF_A32B32G32R32F);
        ShaderParameters->ContactCount =
            Context.GetComputeDispatchInterface().GetEmptyBufferSRV(GraphBuilder, PF_R32_SINT);
        ShaderParameters->OutputContacts =
            Context.GetComputeDispatchInterface().GetEmptyBufferUAV(GraphBuilder, PF_A32B32G32R32F);
        ShaderParameters->OutputContactCount =
            Context.GetComputeDispatchInterface().GetEmptyBufferUAV(GraphBuilder, PF_R32_SINT);
    }
}

void FDWCNiagaraDataInterfaceProxyWetCollision::ResetData(const FNDIGpuComputeResetContext& Context)
{
    FDWCWetCollisionInstanceData_RT* ProxyData =
        SystemInstancesToProxyData_RT.Find(Context.GetSystemInstanceID());
    if (ProxyData == nullptr || !ProxyData->ContactCountBuffer.IsValid())
    {
        return;
    }

    FRDGBuilder& GraphBuilder = Context.GetGraphBuilder();
    AddClearUAVPass(GraphBuilder, ProxyData->ContactCountBuffer.GetOrCreateUAV(GraphBuilder), 0);
    ProxyData->LastClearedRenderFrame = GFrameNumberRenderThread;
    DWCGPUNiagaraWetCollisionBridge::MarkBufferActive_RenderThread(Context.GetSystemInstanceID());
}

void FDWCNiagaraDataInterfaceProxyWetCollision::PreStage(const FNDIGpuComputePreStageContext& Context)
{
    FDWCWetCollisionInstanceData_RT& ProxyData =
        SystemInstancesToProxyData_RT.FindChecked(Context.GetSystemInstanceID());
    FRDGBuilder& GraphBuilder = Context.GetGraphBuilder();

    if (ProxyData.bNeedsRealloc)
    {
        ProxyData.ResizeBuffers(GraphBuilder);
        DWCGPUNiagaraWetCollisionBridge::RegisterBuffer_RenderThread(
            Context.GetSystemInstanceID(),
            ProxyData.MaxContacts,
            ProxyData.ContactBuffer.GetPooledBuffer(),
            ProxyData.ContactCountBuffer.GetPooledBuffer());
    }

    if (ProxyData.ContactCountBuffer.IsValid() &&
        ProxyData.LastClearedRenderFrame != GFrameNumberRenderThread)
    {
        AddClearUAVPass(GraphBuilder, ProxyData.ContactCountBuffer.GetOrCreateUAV(GraphBuilder), 0);
        ProxyData.LastClearedRenderFrame = GFrameNumberRenderThread;
    }

    if (ProxyData.ContactCountBuffer.IsValid())
    {
        DWCGPUNiagaraWetCollisionBridge::MarkBufferActive_RenderThread(Context.GetSystemInstanceID());
    }
}

void FDWCNiagaraDataInterfaceProxyWetCollision::PostSimulate(const FNDIGpuComputePostSimulateContext& Context)
{
    if (Context.IsFinalPostSimulate())
    {
        FDWCWetCollisionInstanceData_RT* ProxyData =
            SystemInstancesToProxyData_RT.Find(Context.GetSystemInstanceID());
        if (ProxyData != nullptr)
        {
            ProxyData->ContactBuffer.EndGraphUsage();
            ProxyData->ContactCountBuffer.EndGraphUsage();
        }
    }
}

#undef LOCTEXT_NAMESPACE
