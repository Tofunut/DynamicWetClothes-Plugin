//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "SWCAEditorPanel.h"

#include "DataAssets/WetClothingAsset.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionCoverageService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionCacheService.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Foundation/Preview/Residency/DWCEditorPreviewGPUResidencyManager.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Build/DWCEditorExclusiveBuildCoordinator.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildOperationManager.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Modes/Part/Editor/SWetClothingPartEditorPanel.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"
#include "WetClothing/Modes/Transparency/Editor/SWetClothingTransparencyBakePanel.h"
#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialColorBakeCache.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"
#include "WetClothing/Foundation/UV/DWCEditorUVTopologyCache.h"
#include "WetClothing/Foundation/Preview/DWCEditorPreviewResourceContext.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"
#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"
#include "WetClothing/Foundation/Build/DWCTransparencyBuildTargetResolver.h"
#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleEditorPanel.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "WCAEditorPanel"

DEFINE_LOG_CATEGORY_STATIC(LogWCAEditorLifecycle, Log, All);

namespace
{
    EDWCEditorPreviewMode ResolvePreviewMode(const EWCAEditorMode Mode)
    {
        switch (Mode)
        {
        case EWCAEditorMode::PartEdit: return EDWCEditorPreviewMode::WetPart;
        case EWCAEditorMode::WrinkleEdit: return EDWCEditorPreviewMode::Wrinkle;
        case EWCAEditorMode::TransparencyBake: return EDWCEditorPreviewMode::Transparency;
        default: return EDWCEditorPreviewMode::None;
        }
    }

    EDWCEditorPreviewGPUDomain ResolvePreviewGPUDomain(const EWCAEditorMode Mode)
    {
        switch (Mode)
        {
        case EWCAEditorMode::PartEdit:
            return EDWCEditorPreviewGPUDomain::WetPart;
        case EWCAEditorMode::WrinkleEdit:
            return EDWCEditorPreviewGPUDomain::Wrinkle;
        case EWCAEditorMode::TransparencyBake:
            return EDWCEditorPreviewGPUDomain::Transparency;
        default:
            return EDWCEditorPreviewGPUDomain::None;
        }
    }

    FDWCEditorAuthoringIndex BuildAuthoringIndex(const UWetClothingAsset* Asset)
    {
        FDWCEditorAuthoringIndex Index;
        if (Asset == nullptr)
        {
            return Index;
        }

        for (const FWetWrinklePatchPlacement& Patch : Asset->Authored.WrinkleData.EditablePatches)
        {
            Index.WrinkleMaterialSlots.Add(Patch.MaterialSlotIndex);
            if (Patch.PatchGuid.IsValid())
            {
                Index.WrinkleElementGuids.Add(Patch.PatchGuid);
            }
        }
        for (const FWetProceduralRidgeStroke& Stroke : Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
        {
            Index.WrinkleMaterialSlots.Add(Stroke.MaterialSlotIndex);
            if (Stroke.StrokeGuid.IsValid())
            {
                Index.WrinkleElementGuids.Add(Stroke.StrokeGuid);
            }
        }
        for (const FWetClothingTransparencyLayerData& Layer : Asset->Authored.TransparencyData.TransparencyLayers)
        {
            if (Layer.LayerGuid.IsValid())
            {
                Index.TransparencyLayerGuids.Add(Layer.LayerGuid);
                Index.TransparencyLayerByMaterialSlot.FindOrAdd(
                    Layer.TargetSurface.OuterMaterialSlotIndex,
                    Layer.LayerGuid);
            }
        }
        return Index;
    }

    void RaiseIssueSeverity(FWCAEditorIssueStatus& Status, const EWCAEditorStatusSeverity Severity)
    {
        if (static_cast<uint8>(Severity) > static_cast<uint8>(Status.Severity))
        {
            Status.Severity = Severity;
        }
    }

    void AppendIssueSection(TArray<FString>& Sections, const TCHAR* Heading, const TArray<FString>& Messages)
    {
        if (!Messages.IsEmpty())
        {
            Sections.Add(FString::Printf(TEXT("%s\n%s"), Heading, *FString::Join(Messages, TEXT("\n"))));
        }
    }

    EWCAEditorStatusSeverity ToEditorSeverity(const EDWCEditorValidationSeverity Severity)
    {
        switch (Severity)
        {
        case EDWCEditorValidationSeverity::Error: return EWCAEditorStatusSeverity::Error;
        case EDWCEditorValidationSeverity::Warning: return EWCAEditorStatusSeverity::Warning;
        default: return EWCAEditorStatusSeverity::Info;
        }
    }

    FString BuildIssueStatusMessage(const FDWCEditorValidationDiagnostic& Diagnostic)
    {
        FString Message = Diagnostic.Presentation.Detail.IsEmpty()
            ? Diagnostic.Presentation.Title.ToString()
            : Diagnostic.Presentation.Detail.ToString();
        if (!Diagnostic.Presentation.RequiredAction.IsEmpty())
        {
            Message += FString::Printf(
                TEXT(" %s"),
                *Diagnostic.Presentation.RequiredAction.ToString());
        }
        return Message;
    }

    void AddDiagnosticToStatus(
        FWCAEditorIssueStatus& Status,
        const FDWCEditorValidationDiagnostic& Diagnostic)
    {
        ++Status.IssueCount;
        RaiseIssueSeverity(Status, ToEditorSeverity(Diagnostic.Severity));

        TArray<FString>* TargetMessages = nullptr;
        switch (Diagnostic.Target.Domain)
        {
        case EDWCEditorValidationDomain::Asset:
        case EDWCEditorValidationDomain::DataUV:
            Status.bGeneratedDataUVIssue = true;
            TargetMessages = &Status.GeneratedDataUVMessages;
            break;
        case EDWCEditorValidationDomain::RuntimeCPU:
        case EDWCEditorValidationDomain::RuntimeGPU:
            Status.bRuntimeIssue = true;
            TargetMessages = &Status.RuntimeMessages;
            break;
        case EDWCEditorValidationDomain::GeneratedMaterial:
            Status.bGeneratedMaterialsIssue = true;
            TargetMessages = &Status.GeneratedMaterialMessages;
            break;
        case EDWCEditorValidationDomain::GPUSimulationMap:
            Status.bGPUMapsIssue = true;
            TargetMessages = &Status.GPUMapMessages;
            break;
        case EDWCEditorValidationDomain::WetPart:
        case EDWCEditorValidationDomain::RenderProfile:
            Status.bRenderProfileIssue = true;
            TargetMessages = &Status.RenderProfileMessages;
            break;
        case EDWCEditorValidationDomain::Wrinkle:
            Status.bWrinkleMapsIssue = true;
            TargetMessages = &Status.WrinkleMapMessages;
            break;
        case EDWCEditorValidationDomain::Transparency:
            Status.bTransparencyMapsIssue = true;
            TargetMessages = &Status.TransparencyMapMessages;
            break;
        case EDWCEditorValidationDomain::Failure:
        default:
            Status.bFailure = true;
            TargetMessages = &Status.FailureMessages;
            break;
        }

        TargetMessages->Add(BuildIssueStatusMessage(Diagnostic));
    }
}

FString FWCAEditorIssueStatus::BuildSummary() const
{
    TArray<FString> Sections;
    AppendIssueSection(Sections, TEXT("DWC UV Channel"), GeneratedDataUVMessages);
    AppendIssueSection(Sections, TEXT("Runtime Data"), RuntimeMessages);
    AppendIssueSection(Sections, TEXT("Generated Materials"), GeneratedMaterialMessages);
    AppendIssueSection(Sections, TEXT("GPU Runtime Data"), GPUMapMessages);
    AppendIssueSection(Sections, TEXT("Render Profile Lookup Texture"), RenderProfileMessages);
    AppendIssueSection(Sections, TEXT("Wrinkle Textures"), WrinkleMapMessages);
    AppendIssueSection(Sections, TEXT("Transparency Textures"), TransparencyMapMessages);
    AppendIssueSection(Sections, TEXT("Internal Failure"), FailureMessages);
    return FString::Join(Sections, TEXT("\n\n"));
}

SWCAEditorPanel::~SWCAEditorPanel()
{
    if (GeneratedDataInvalidationHandle.IsValid())
    {
        FWCAGeneratedDataInvalidator::OnInvalidated().Remove(GeneratedDataInvalidationHandle);
        GeneratedDataInvalidationHandle.Reset();
    }
    if (PreBeginPIEHandle.IsValid())
    {
        FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
        PreBeginPIEHandle.Reset();
    }
    if (EndPIEHandle.IsValid())
    {
        FEditorDelegates::EndPIE.Remove(EndPIEHandle);
        EndPIEHandle.Reset();
    }
    SetHostLifecycleBlocker(EDWCEditorHostLifecycleBlocker::EditorClosing, true);
    BeginPreviewResourceShutdown();
    if (BakeCoordinator.IsValid())
    {
        BakeCoordinator->Shutdown();
    }
    if (ExclusiveBuildCoordinator.IsValid())
    {
        ExclusiveBuildCoordinator->Shutdown();
    }
    if (BuildOperationManager.IsValid())
    {
        BuildOperationManager->BeginShutdown();
    }
    if (WorkerJobScheduler.IsValid())
    {
        FDWCEditorPreviewDiagnostics::UnregisterWorkerScheduler(WorkerJobScheduler.Get());
        WorkerJobScheduler->Shutdown();
    }
    if (BuildOperationManager.IsValid())
    {
        BuildOperationManager->CompleteShutdown();
    }
    BakeCoordinator.Reset();
    ExclusiveBuildCoordinator.Reset();
    BuildOperationManager.Reset();
    WorkerJobScheduler.Reset();
    PartPreviewLifetime.Reset();
    WrinklePreviewLifetime.Reset();
    TransparencyPreviewLifetime.Reset();

    CompletePreviewResourceShutdown();
    FDWCTransparencyMaterialColorBakeCache::Clear(WetClothingAsset.Get());
    WrinkleSuppressionCoverageService.Reset();
    SurfacePatchProjectionCache.Reset();
    SpatialQueryService.Reset();
    CacheStore.Reset();
    ResourceGovernor.Reset();
    if (ResourceBroker.IsValid() && ResourceBrokerSessionId.IsValid())
    {
        ResourceBroker->CloseSession(ResourceBrokerSessionId);
    }
    ResourceBrokerSessionId.Invalidate();
    ResourceBroker.Reset();
    if (AuthoringDocument.IsValid())
    {
        AuthoringDocument->OnChanged().RemoveAll(this);
    }
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        FWCAGeneratedDataInvalidator::InvalidateAsset(*Asset);
        Asset->ReleaseLoadedOriginalUVTopologiesForEditor();
    }
}

void SWCAEditorPanel::BeginPreviewResourceShutdown()
{
    check(IsInGameThread());
    if (PreviewResourceShutdownState != EWCAEditorPreviewResourceShutdownState::Running)
    {
        return;
    }

    PreviewResourceShutdownState = EWCAEditorPreviewResourceShutdownState::Quiescing;
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->SetWorkAvailableCallback(nullptr);
    }
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->SetMaintenanceRequiredCallback(nullptr);
    }
    if (PreviewCommitCoordinator.IsValid())
    {
        FDWCEditorPreviewDiagnostics::UnregisterCommitCoordinator(PreviewCommitCoordinator.Get());
        PreviewCommitCoordinator->Shutdown();
    }
}

void SWCAEditorPanel::CompletePreviewResourceShutdown()
{
    check(IsInGameThread());
    if (PreviewResourceShutdownState == EWCAEditorPreviewResourceShutdownState::Closed)
    {
        return;
    }
    BeginPreviewResourceShutdown();

    // Mode panels must release their texture and asset leases before render
    // uploads are drained and transient texture resources are retired.
    if (ModeContentBox.IsValid())
    {
        ModeContentBox->SetContent(SNullWidget::NullWidget);
    }
    PartEditorPanel.Reset();
    WrinkleEditorPanel.Reset();
    TransparencyBakePanel.Reset();

    if (PreviewGPUResidencyManager.IsValid())
    {
        PreviewGPUResidencyManager->Shutdown();
    }
    UnregisterResourceParticipants();
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->Shutdown();
    }
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->Shutdown();
    }

    PreviewResources.Reset();
    PreviewCommitCoordinator.Reset();
    PreviewGPUResidencyManager.Reset();
    TextureWorkspace.Reset();
    RenderUploadQueue.Reset();
    EditorAssetResidencyLease.Reset();
    if (AssetResidency.IsValid())
    {
        AssetResidency->Shutdown();
    }
    AssetResidency.Reset();
    PreviewResourceShutdownState = EWCAEditorPreviewResourceShutdownState::Closed;
}

void SWCAEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    AuthoringDocument = MakeShared<FDWCEditorAuthoringDocument>(WetClothingAsset.Get());
    ResourceBroker = FDWCEditorResourceBroker::Get();
    ResourceBrokerSessionId = ResourceBroker->OpenSession(
        WetClothingAsset.IsValid()
            ? FString::Printf(TEXT("WCA Editor: %s"), *WetClothingAsset->GetName())
            : TEXT("WCA Editor"));
    ResourceBroker->SetSessionActive(
        ResourceBrokerSessionId,
        HostLifecycle.CanRunInteractivePreview());
    const FDWCEditorResourceBudgetConfig& ResourceBudget = ResourceBroker->GetBudgetConfig();
    ResourceGovernor = ResourceBroker->GetResourceGovernor();
    WorkerJobScheduler = MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(
        ResourceGovernor.ToSharedRef());
    const FGuid SessionEpoch = WorkerJobScheduler->GetSessionEpoch();
    PartPreviewLifetime = MakeShared<FDWCEditorPreviewModeLifetime>(
        EDWCEditorPreviewMode::WetPart,
        SessionEpoch);
    WrinklePreviewLifetime = MakeShared<FDWCEditorPreviewModeLifetime>(
        EDWCEditorPreviewMode::Wrinkle,
        SessionEpoch);
    TransparencyPreviewLifetime = MakeShared<FDWCEditorPreviewModeLifetime>(
        EDWCEditorPreviewMode::Transparency,
        SessionEpoch);
    CacheStore = MakeShared<FDWCEditorCacheStore>(
        ResourceGovernor.ToSharedRef(),
        SessionEpoch,
        ResourceBudget.SharedCacheCPUBytes);
    GeneratedDataInvalidationHandle =
        FWCAGeneratedDataInvalidator::OnInvalidated().AddSP(
            SharedThis(this),
            &SWCAEditorPanel::HandleGeneratedDataInvalidated);
    WrinkleSuppressionCoverageService =
        MakeShared<FDWCWrinkleSuppressionCoverageService>(CacheStore.ToSharedRef());
    SpatialQueryService = MakeShared<FDWCEditorSpatialQueryService>(CacheStore.ToSharedRef());
    SurfacePatchProjectionCache = MakeShared<FDWCEditorSurfacePatchProjectionCacheService>(
        ResourceGovernor.ToSharedRef(),
        SessionEpoch,
        FMath::Min<uint64>(
            FDWCEditorSurfacePatchProjectionCacheService::DefaultBudgetBytes,
            ResourceBudget.SharedCacheCPUBytes / 2));
    RenderUploadQueue = MakeShared<FDWCEditorRenderUploadQueue>(
        ResourceGovernor.ToSharedRef(),
        SessionEpoch,
        ResourceBudget.UploadStagingCPUBytes);
    TextureWorkspace = MakeShared<FDWCEditorTextureWorkspace>(
        RenderUploadQueue.ToSharedRef(),
        ResourceGovernor.ToSharedRef(),
        SessionEpoch,
        ResourceBudget.PreviewWorkspaceCPUBytes,
        ResourceBudget.PreviewGPUBytes);
    AssetResidency = MakeShared<FDWCEditorAssetResidencyRegistry>();
    EditorAssetResidencyLease = AssetResidency->Acquire(
        WetClothingAsset.Get(),
        EDWCEditorAssetResidencyDomain::Session,
        TEXT("Edited WCA"));
    PreviewGPUResidencyManager = MakeShared<FDWCEditorPreviewGPUResidencyManager>(
        RenderUploadQueue.ToSharedRef(),
        TextureWorkspace.ToSharedRef());
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        FDWCTransparencyMaterialColorBakeCache::ConfigureCacheBudget(
            *Asset,
            ResourceBudget.SharedCacheCPUBytes / 2);
    }
    RegisterResourceParticipants();
    SessionStore = MakeShared<FDWCEditorSessionStore>();
    BuildOperationManager = MakeShared<FDWCEditorBuildOperationManager>(
        WorkerJobScheduler.ToSharedRef());
    ExclusiveBuildCoordinator = MakeShared<FDWCEditorExclusiveBuildCoordinator>(
        ResourceBroker.ToSharedRef(),
        WorkerJobScheduler.ToSharedRef(),
        ResourceBrokerSessionId,
        WetClothingAsset.IsValid() ? WetClothingAsset->GetPathName() : FString(TEXT("None")));
    const TWeakPtr<FDWCEditorResourceBroker> WeakResourceBroker = ResourceBroker;
    const FGuid BuildBarrierSessionId = ResourceBrokerSessionId;
    WorkerJobScheduler->SetAdmissionBarrier(
        [WeakResourceBroker, BuildBarrierSessionId](
            const FDWCEditorWorkerJobDescriptor& Descriptor,
            FString& OutReason)
        {
            const TSharedPtr<FDWCEditorResourceBroker> Broker = WeakResourceBroker.Pin();
            return !Broker.IsValid() || Broker->CanAdmitWork(
                BuildBarrierSessionId,
                Descriptor.WorkClass,
                Descriptor.ExclusiveBuildScopeId,
                &OutReason);
        });
    BuildOperationManager->SetActionBarrier(
        [WeakResourceBroker, BuildBarrierSessionId](
            const EDWCEditorBuildAction,
            FString& OutReason)
        {
            const TSharedPtr<FDWCEditorResourceBroker> Broker = WeakResourceBroker.Pin();
            return !Broker.IsValid() || Broker->CanAdmitWork(
                BuildBarrierSessionId,
                EDWCEditorWorkClass::UserBuild,
                FGuid(),
                &OutReason);
        });
    const TWeakPtr<SWCAEditorPanel> WeakPanel = SharedThis(this);
    RenderUploadQueue->SetWorkAvailableCallback(
        [WeakPanel]()
        {
            if (const TSharedPtr<SWCAEditorPanel> Panel = WeakPanel.Pin())
            {
                Panel->EnsureTextureUploadTimer();
            }
        });
    TextureWorkspace->SetMaintenanceRequiredCallback(
        [WeakPanel]()
        {
            if (const TSharedPtr<SWCAEditorPanel> Panel = WeakPanel.Pin())
            {
                Panel->EnsureTextureUploadTimer();
            }
        });
    WorkerJobScheduler->SetPreviewLifecycleProvider(
        [WeakPanel](const FDWCEditorWorkerJobDescriptor& Descriptor)
        {
            const TSharedPtr<SWCAEditorPanel> Panel = WeakPanel.Pin();
            return Panel.IsValid() ? Panel->CapturePreviewRunToken(Descriptor)
                                   : FDWCEditorPreviewRunToken();
        });
    const TWeakPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> WeakScheduler =
        WorkerJobScheduler;
    ResourceBroker->SetSessionBuildBarrierHooks(
        ResourceBrokerSessionId,
        [WeakPanel](const bool bActive)
        {
            if (const TSharedPtr<SWCAEditorPanel> Panel = WeakPanel.Pin())
            {
                Panel->HandleExclusiveBuildBarrierChanged(bActive);
            }
        },
        [WeakScheduler]()
        {
            const TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
                WeakScheduler.Pin();
            return Scheduler.IsValid() &&
                Scheduler->HasOutstandingWorkClass(EDWCEditorWorkClass::InteractivePreview);
        });
    PreviewCommitCoordinator = MakeShared<FDWCEditorPreviewCommitCoordinator>(
        TextureWorkspace.ToSharedRef(),
        WorkerJobScheduler->GetSessionEpoch());
    PreviewResources = MakeShared<FDWCEditorPreviewResourceContext>(
        RenderUploadQueue.ToSharedRef(),
        TextureWorkspace.ToSharedRef(),
        PreviewCommitCoordinator.ToSharedRef(),
        AssetResidency);
    FDWCEditorPreviewDiagnostics::RegisterCommitCoordinator(PreviewCommitCoordinator.Get());
    FDWCEditorPreviewDiagnostics::RegisterWorkerScheduler(WorkerJobScheduler.Get());
    TWeakPtr<FDWCEditorSessionStore> WeakSessionStore = SessionStore;
    WorkerJobScheduler->SetDomainRevisionProvider(
        [WeakSessionStore](const EDWCEditorAuthoringDomain Domain)
        {
            const TSharedPtr<FDWCEditorSessionStore> Store = WeakSessionStore.Pin();
            return Store.IsValid() ? GetDWCEditorDomainRevision(Store->GetState(), Domain) : MAX_uint64;
        });
    BakeCoordinator = MakeShared<FDWCEditorBakeCoordinator>(
        WetClothingAsset.Get(),
        WorkerJobScheduler.ToSharedRef(),
        BuildOperationManager.ToSharedRef(),
        SpatialQueryService.ToSharedRef(),
        SurfacePatchProjectionCache.ToSharedRef(),
        WrinkleSuppressionCoverageService,
        CacheStore);
    AuthoringDocument->OnChanged().AddSP(this, &SWCAEditorPanel::HandleAuthoringDocumentChanged);
    FDWCReconcileAuthoringAction InitialReconcile;
    InitialReconcile.AuthoringRevision = AuthoringDocument->GetRevision();
    InitialReconcile.Index = BuildAuthoringIndex(WetClothingAsset.Get());
    SessionStore->Dispatch(InitialReconcile);
    DetailsView = InArgs._DetailsView;
    OnStatusChanged = InArgs._OnStatusChanged;

    ChildSlot
    [
        SAssignNew(ModeContentBox, SBox)
    ];

    {
        TGuardValue<bool> SuppressStatusChangedNotification(bSuppressStatusChangedNotification, true);
        SetEditorMode(EWCAEditorMode::PartEdit);
    }
    EnsureTextureUploadTimer();

    PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddSP(
        this,
        &SWCAEditorPanel::HandlePreBeginPIE);
    EndPIEHandle = FEditorDelegates::EndPIE.AddSP(
        this,
        &SWCAEditorPanel::HandleEndPIE);
}

void SWCAEditorPanel::HandleGeneratedDataInvalidated(
    const FWCAGeneratedDataInvalidation& Invalidation)
{
    if (!CacheStore.IsValid())
    {
        return;
    }

    const FName UVTopologyNamespace = FDWCEditorUVTopologyCache::CacheNamespace();
    switch (Invalidation.Scope)
    {
    case EWCAGeneratedDataInvalidationScope::All:
        CacheStore->InvalidateNamespace(UVTopologyNamespace);
        break;
    case EWCAGeneratedDataInvalidationScope::Asset:
        CacheStore->InvalidateOwnerNamespace(Invalidation.Asset, UVTopologyNamespace);
        break;
    case EWCAGeneratedDataInvalidationScope::Mesh:
        CacheStore->InvalidateResourceIdentity(Invalidation.Mesh, UVTopologyNamespace);
        break;
    default:
        break;
    }
}

void SWCAEditorPanel::RegisterResourceParticipants()
{
    check(IsInGameThread());
    if (!ResourceBroker.IsValid() || !ResourceBrokerSessionId.IsValid())
    {
        return;
    }

    UnregisterResourceParticipants();
    const auto Register = [this](FDWCEditorReclaimParticipantDescriptor Descriptor)
    {
        Descriptor.SessionId = ResourceBrokerSessionId;
        Descriptor.ReservationSessionEpoch = WorkerJobScheduler.IsValid()
            ? WorkerJobScheduler->GetSessionEpoch()
            : FGuid();
        const uint64 ParticipantId = ResourceBroker->RegisterParticipant(MoveTemp(Descriptor));
        if (ParticipantId != 0)
        {
            ResourceParticipantIds.Add(ParticipantId);
        }
    };

    const TWeakPtr<FDWCEditorCacheStore> WeakCacheStore = CacheStore;
    FDWCEditorReclaimParticipantDescriptor CacheDescriptor;
    CacheDescriptor.Name = TEXT("WCA.SharedCache");
    CacheDescriptor.ReservationOwnerNamespace = TEXT("DWC.SharedCache");
    CacheDescriptor.Pool = EDWCEditorResourcePool::SharedCacheCPU;
    CacheDescriptor.Priority = EDWCEditorReclaimPriority::SharedCache;
    CacheDescriptor.QueryReclaimableBytes = [WeakCacheStore]
    {
        const TSharedPtr<FDWCEditorCacheStore> Store = WeakCacheStore.Pin();
        return Store.IsValid() ? Store->GetReclaimableBytes() : 0;
    };
    CacheDescriptor.Reclaim = [WeakCacheStore](const FDWCEditorResourceReclaimRequest& Request)
    {
        FDWCEditorResourceReclaimResult Result;
        if (const TSharedPtr<FDWCEditorCacheStore> Store = WeakCacheStore.Pin())
        {
            Result.ImmediateBytes = Store->ReclaimUnleasedBytes(Request.TargetBytes);
        }
        return Result;
    };
    Register(MoveTemp(CacheDescriptor));

    const TWeakObjectPtr<UWetClothingAsset> WeakAsset = WetClothingAsset;
    FDWCEditorReclaimParticipantDescriptor TopologyDescriptor;
    TopologyDescriptor.Name = TEXT("WCA.OriginalUVTopologyBulk");
    TopologyDescriptor.ReservationOwnerNamespace = TEXT("DWC.OriginalUVTopologyBulk");
    TopologyDescriptor.Pool = EDWCEditorResourcePool::SharedCacheCPU;
    TopologyDescriptor.Priority = EDWCEditorReclaimPriority::Background;
    TopologyDescriptor.QueryReclaimableBytes = [WeakAsset]
    {
        const UWetClothingAsset* Asset = WeakAsset.Get();
        return Asset != nullptr
            ? Asset->GetReclaimableOriginalUVTopologyBytesForEditor()
            : 0;
    };
    TopologyDescriptor.Reclaim = [WeakAsset](const FDWCEditorResourceReclaimRequest&)
    {
        FDWCEditorResourceReclaimResult Result;
        if (UWetClothingAsset* Asset = WeakAsset.Get())
        {
            Result.ImmediateBytes = Asset->ReclaimOriginalUVTopologyBytesForEditor();
        }
        return Result;
    };
    Register(MoveTemp(TopologyDescriptor));

    const TWeakPtr<FDWCEditorSurfacePatchProjectionCacheService> WeakProjectionCache =
        SurfacePatchProjectionCache;
    FDWCEditorReclaimParticipantDescriptor ProjectionDescriptor;
    ProjectionDescriptor.Name = TEXT("WCA.SurfaceProjectionCache");
    ProjectionDescriptor.ReservationOwnerNamespace = TEXT("DWC.SurfacePatchProjectionCache");
    ProjectionDescriptor.Pool = EDWCEditorResourcePool::SharedCacheCPU;
    ProjectionDescriptor.Priority = EDWCEditorReclaimPriority::SharedCache;
    ProjectionDescriptor.QueryReclaimableBytes = [WeakProjectionCache]
    {
        const TSharedPtr<FDWCEditorSurfacePatchProjectionCacheService> Cache =
            WeakProjectionCache.Pin();
        return Cache.IsValid() ? Cache->GetReclaimableBytes() : 0;
    };
    ProjectionDescriptor.Reclaim =
        [WeakProjectionCache](const FDWCEditorResourceReclaimRequest& Request)
        {
            FDWCEditorResourceReclaimResult Result;
            if (const TSharedPtr<FDWCEditorSurfacePatchProjectionCacheService> Cache =
                    WeakProjectionCache.Pin())
            {
                Result.ImmediateBytes = Cache->ReclaimUnleasedBytes(Request.TargetBytes);
            }
            return Result;
        };
    Register(MoveTemp(ProjectionDescriptor));

    const TWeakPtr<FDWCEditorTextureWorkspace> WeakWorkspace = TextureWorkspace;
    FDWCEditorReclaimParticipantDescriptor WorkspaceCPUDescriptor;
    WorkspaceCPUDescriptor.Name = TEXT("WCA.TextureWorkspaceCPU");
    WorkspaceCPUDescriptor.ReservationOwnerNamespace = TEXT("DWC.TextureWorkspace.CPU");
    WorkspaceCPUDescriptor.Pool = EDWCEditorResourcePool::PreviewWorkspaceCPU;
    WorkspaceCPUDescriptor.Priority = EDWCEditorReclaimPriority::ActivePreview;
    WorkspaceCPUDescriptor.QueryReclaimableBytes = [WeakWorkspace]
    {
        const TSharedPtr<FDWCEditorTextureWorkspace> Workspace = WeakWorkspace.Pin();
        return Workspace.IsValid() ? Workspace->GetReclaimableCPUBytes() : 0;
    };
    WorkspaceCPUDescriptor.Reclaim =
        [WeakWorkspace](const FDWCEditorResourceReclaimRequest& Request)
        {
            FDWCEditorResourceReclaimResult Result;
            if (const TSharedPtr<FDWCEditorTextureWorkspace> Workspace = WeakWorkspace.Pin())
            {
                Result.ImmediateBytes = Workspace->ReclaimUnleasedCPUBytes(
                    Request.TargetBytes, &Result.RetiringGPUBytes);
            }
            return Result;
        };
    Register(MoveTemp(WorkspaceCPUDescriptor));

    FDWCEditorReclaimParticipantDescriptor WorkspaceGPUDescriptor;
    WorkspaceGPUDescriptor.Name = TEXT("WCA.TextureWorkspaceGPU");
    WorkspaceGPUDescriptor.ReservationOwnerNamespace = TEXT("DWC.TextureWorkspace.GPU");
    WorkspaceGPUDescriptor.Pool = EDWCEditorResourcePool::PreviewGPU;
    WorkspaceGPUDescriptor.Priority = EDWCEditorReclaimPriority::InactivePreview;
    WorkspaceGPUDescriptor.QueryReclaimableBytes = [WeakWorkspace]
    {
        const TSharedPtr<FDWCEditorTextureWorkspace> Workspace = WeakWorkspace.Pin();
        return Workspace.IsValid() ? Workspace->GetReclaimableGPUBytes() : 0;
    };
    WorkspaceGPUDescriptor.Reclaim =
        [WeakWorkspace](const FDWCEditorResourceReclaimRequest& Request)
        {
            FDWCEditorResourceReclaimResult Result;
            if (const TSharedPtr<FDWCEditorTextureWorkspace> Workspace = WeakWorkspace.Pin())
            {
                Result.RetiringGPUBytes = Workspace->RetireUnleasedGPUBytes(Request.TargetBytes);
            }
            return Result;
        };
    Register(MoveTemp(WorkspaceGPUDescriptor));

    FDWCEditorReclaimParticipantDescriptor MaterialCacheDescriptor;
    MaterialCacheDescriptor.Name = TEXT("WCA.TransparencyMaterialCache");
    MaterialCacheDescriptor.ReservationOwnerNamespace = TEXT("DWC.TransparencyMaterialColorCache");
    MaterialCacheDescriptor.Pool = EDWCEditorResourcePool::SharedCacheCPU;
    MaterialCacheDescriptor.Priority = EDWCEditorReclaimPriority::Background;
    MaterialCacheDescriptor.QueryReclaimableBytes = [WeakAsset]
    {
        return FDWCTransparencyMaterialColorBakeCache::GetReclaimableBytes(WeakAsset.Get());
    };
    MaterialCacheDescriptor.Reclaim =
        [WeakAsset](const FDWCEditorResourceReclaimRequest& Request)
        {
            FDWCEditorResourceReclaimResult Result;
            Result.ImmediateBytes = FDWCTransparencyMaterialColorBakeCache::ReclaimUnleasedBytes(
                WeakAsset.Get(), Request.TargetBytes);
            return Result;
        };
    Register(MoveTemp(MaterialCacheDescriptor));
}

void SWCAEditorPanel::UnregisterResourceParticipants()
{
    if (ResourceBroker.IsValid())
    {
        for (const uint64 ParticipantId : ResourceParticipantIds)
        {
            ResourceBroker->UnregisterParticipant(ParticipantId);
        }
    }
    ResourceParticipantIds.Reset();
}

void SWCAEditorPanel::EnsureTextureUploadTimer()
{
    check(IsInGameThread());
    if (TextureUploadTimerHandle.IsValid())
    {
        return;
    }
    TextureUploadTimerHandle = RegisterActiveTimer(
        0.0,
        FWidgetActiveTimerDelegate::CreateSP(this, &SWCAEditorPanel::HandleTextureUploadTimer));
}

EActiveTimerReturnType SWCAEditorPanel::HandleTextureUploadTimer(
    double,
    float)
{
    if (PreviewGPUResidencyManager.IsValid())
    {
        PreviewGPUResidencyManager->Tick();
    }
    else
    {
        if (RenderUploadQueue.IsValid())
        {
            RenderUploadQueue->Flush();
        }
        if (TextureWorkspace.IsValid())
        {
            TextureWorkspace->ProcessRetiredGPUResources();
        }
    }

    const bool bHasUploadWork = RenderUploadQueue.IsValid() && RenderUploadQueue->HasPendingWork();
    const bool bHasResidencyWork = PreviewGPUResidencyManager.IsValid() &&
        PreviewGPUResidencyManager->HasPendingMaintenance();
    if (bHasUploadWork || bHasResidencyWork)
    {
        return EActiveTimerReturnType::Continue;
    }

    TextureUploadTimerHandle.Reset();
    return EActiveTimerReturnType::Stop;
}

TSharedRef<SWidget> SWCAEditorPanel::EnsureModeWidget(const EWCAEditorMode Mode)
{
    switch (Mode)
    {
    case EWCAEditorMode::PartEdit:
        if (!PartEditorPanel.IsValid())
        {
            SAssignNew(PartEditorPanel, SWetClothingPartEditorPanel)
                .WetClothingAsset(WetClothingAsset.Get())
                .AuthoringDocument(AuthoringDocument)
                .SessionStore(SessionStore)
                .CacheStore(CacheStore)
                .PreviewResources(PreviewResources)
                .ResourceGovernor(ResourceGovernor)
                .WorkerJobScheduler(WorkerJobScheduler)
                .DetailsView(DetailsView);
        }
        return PartEditorPanel.ToSharedRef();

    case EWCAEditorMode::WrinkleEdit:
        if (!WrinkleEditorPanel.IsValid())
        {
            SAssignNew(WrinkleEditorPanel, SWetWrinkleEditorPanel)
                .WetClothingAsset(WetClothingAsset.Get())
                .AuthoringDocument(AuthoringDocument)
                .SessionStore(SessionStore)
                .WorkerJobScheduler(WorkerJobScheduler)
                .BakeCoordinator(BakeCoordinator)
                .SpatialQueryService(SpatialQueryService)
                .SurfacePatchProjectionCache(SurfacePatchProjectionCache)
                .PreviewResources(PreviewResources)
                .PreviewModeLifetime(WrinklePreviewLifetime)
                .DetailsView(DetailsView);
        }
        return WrinkleEditorPanel.ToSharedRef();

    case EWCAEditorMode::TransparencyBake:
        if (!TransparencyBakePanel.IsValid())
        {
            SAssignNew(TransparencyBakePanel, SWetClothingTransparencyBakePanel)
                .WetClothingAsset(WetClothingAsset.Get())
                .AuthoringDocument(AuthoringDocument)
                .SessionStore(SessionStore)
                .WorkerJobScheduler(WorkerJobScheduler)
                .BakeCoordinator(BakeCoordinator)
                .WrinkleSuppressionCoverageService(WrinkleSuppressionCoverageService)
                .SpatialQueryService(SpatialQueryService)
                .PreviewResources(PreviewResources)
                .PreviewModeLifetime(TransparencyPreviewLifetime)
                .ResourceGovernor(ResourceGovernor)
                .CacheStore(CacheStore)
                .DetailsView(DetailsView);
        }
        return TransparencyBakePanel.ToSharedRef();

    default:
        return SNullWidget::NullWidget;
    }
}

void SWCAEditorPanel::RefreshFromAsset(const bool bRebuildActiveModePreview)
{
    bRefreshPending = false;
    bPendingFullModeRefresh = false;
    UpdateCachedStatus();

    const EWCAEditorMode ActiveMode = SessionStore.IsValid()
        ? SessionStore->GetState().ActiveMode
        : EWCAEditorMode::PartEdit;
    switch (ActiveMode)
    {
    case EWCAEditorMode::PartEdit:
        if (PartEditorPanel.IsValid()) PartEditorPanel->RefreshFromAsset();
        break;
    case EWCAEditorMode::WrinkleEdit:
        if (WrinkleEditorPanel.IsValid())
        {
            if (bRebuildActiveModePreview)
            {
                WrinkleEditorPanel->RefreshFromAsset();
            }
            else
            {
                WrinkleEditorPanel->RefreshFromAssetLightweight();
            }
        }
        break;
    case EWCAEditorMode::TransparencyBake:
        if (TransparencyBakePanel.IsValid()) TransparencyBakePanel->RefreshFromAsset();
        break;
    default:
        break;
    }
}

void SWCAEditorPanel::RefreshStatusFromAsset()
{
    UpdateCachedStatus();
}

void SWCAEditorPanel::RequestRefreshFromAsset(const bool bRebuildActiveModePreview)
{
    bPendingFullModeRefresh |= bRebuildActiveModePreview;
    if (bRefreshPending)
    {
        return;
    }
    bRefreshPending = true;
    RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SWCAEditorPanel::HandleDeferredRefresh));
}

EActiveTimerReturnType SWCAEditorPanel::HandleDeferredRefresh(double CurrentTime, float DeltaTime)
{
    const bool bRebuildActiveModePreview = bPendingFullModeRefresh;
    RefreshFromAsset(bRebuildActiveModePreview);
    return EActiveTimerReturnType::Stop;
}

void SWCAEditorPanel::RequestStatusRefresh()
{
    if (bStatusRefreshPending)
    {
        return;
    }
    bStatusRefreshPending = true;
    RegisterActiveTimer(
        0.0f,
        FWidgetActiveTimerDelegate::CreateSP(
            this,
            &SWCAEditorPanel::HandleDeferredStatusRefresh));
}

EActiveTimerReturnType SWCAEditorPanel::HandleDeferredStatusRefresh(double, float)
{
    bStatusRefreshPending = false;
    UpdateCachedStatus();
    return EActiveTimerReturnType::Stop;
}

FWCAEditorIssueStatus SWCAEditorPanel::CollectIssueStatus(
    const bool bRunDeepValidation) const
{
    FWCAEditorIssueStatus Result;
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return Result;
    }

    const FWCAEditorValidationSnapshot Snapshot = BuildWCAValidationSnapshot(
        *Asset,
        bRunDeepValidation
            ? EWCAValidationMode::ExactPayload
            : EWCAValidationMode::MetadataOnly);
    for (const FDWCEditorValidationDiagnostic& Diagnostic : Snapshot.Diagnostics)
    {
        AddDiagnosticToStatus(Result, Diagnostic);
    }
    return Result;
}

void SWCAEditorPanel::UpdateCachedStatus()
{
    const int32 PreviousIssueCount = CachedIssueCount;
    const EWCAEditorStatusSeverity PreviousStatusSeverity = CachedStatusSeverity;

    const FWCAEditorIssueStatus Status = CollectIssueStatus(false);
    CachedIssueCount = Status.IssueCount;
    CachedStatusSeverity = Status.Severity;

    if (!bSuppressStatusChangedNotification &&
        OnStatusChanged.IsBound() &&
        (PreviousIssueCount != CachedIssueCount || PreviousStatusSeverity != CachedStatusSeverity))
    {
        OnStatusChanged.Execute();
    }
}

bool SWCAEditorPanel::HasPendingVisualBakeTasks(FString* OutSummary) const
{
    TArray<FString> PendingSections;
    FString PartSummary;
    if (FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(WetClothingAsset.Get(), &PartSummary))
    {
        PendingSections.Add(PartSummary);
    }
    if (OutSummary)
    {
        *OutSummary = PendingSections.IsEmpty() ? TEXT("Render Profile Lookup Texture is up to date.") : FString::Join(PendingSections, TEXT("\n\n"));
    }
    return !PendingSections.IsEmpty();
}

bool SWCAEditorPanel::BakeWetVisualAssets(FString& OutSummary, bool* OutHadWarnings)
{
    return FWetClothingRenderProfileBakeService::BakeRenderProfileDataAndUpdateMaterials(WetClothingAsset.Get(), OutSummary, OutHadWarnings);
}

bool SWCAEditorPanel::BakePendingVisualAssets(FString& OutSummary, bool* OutHadWarnings)
{
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = false;
    }

    TArray<FString> Sections;
    TArray<FString> Failures;
    bool bHadWarnings = false;

    FString PartPendingSummary;
    if (FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(WetClothingAsset.Get(), &PartPendingSummary))
    {
        FString PartBakeSummary;
        bool bPartWarnings = false;
        if (FWetClothingRenderProfileBakeService::BakeRenderProfileDataAndUpdateMaterials(WetClothingAsset.Get(), PartBakeSummary, &bPartWarnings))
        {
            Sections.Add(PartBakeSummary);
            bHadWarnings |= bPartWarnings;
        }
        else
        {
            Failures.Add(FString::Printf(TEXT("Render Profile Lookup Texture: %s"), *PartBakeSummary));
        }
    }

    if (!Failures.IsEmpty())
    {
        OutSummary = FString::Join(Failures, TEXT("\n\n"));
        if (OutHadWarnings != nullptr)
        {
            *OutHadWarnings = true;
        }
        return false;
    }

    OutSummary = Sections.IsEmpty() ? TEXT("Render Profile Lookup Texture is up to date.") : FString::Join(Sections, TEXT("\n\n"));
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = bHadWarnings;
    }
    return true;
}

bool SWCAEditorPanel::BakeAllWrinkleMaps(FString& OutSummary, bool* OutHadWarnings)
{
    if (!SpatialQueryService.IsValid() || !SurfacePatchProjectionCache.IsValid())
    {
        OutSummary = TEXT("The editor spatial query service is unavailable.");
        return false;
    }
    return FWetWrinkleBakeService::BakeAllWrinkleMaps(
        WetClothingAsset.Get(),
        SpatialQueryService.ToSharedRef(),
        SurfacePatchProjectionCache.ToSharedRef(),
        OutSummary,
        OutHadWarnings);
}

bool SWCAEditorPanel::RequestBakeAllWrinkleMaps(
    TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
    FString* OutError)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !BakeCoordinator.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The asynchronous bake service is unavailable.");
        return false;
    }
    TArray<int32> MaterialSlots;
    FWetWrinkleBakeService::CollectBakeMaterialSlots(*Asset, MaterialSlots);
    return BakeCoordinator->RequestWrinkleBake(
        MoveTemp(MaterialSlots),
        true,
        MoveTemp(Completion),
        OutError);
}

bool SWCAEditorPanel::RequestBakeAllTransparencyMaps(
    TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
    FString* OutError)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !BakeCoordinator.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The asynchronous bake service is unavailable.");
        return false;
    }

    TArray<FGuid> LayerGuids;
    const FDWCTransparencyBuildTargetSnapshot Targets =
        FDWCTransparencyBuildTargetResolver::Resolve(
            *Asset, EDWCEditorValidationAccess::ExactPayload);
    Targets.CollectLayerGuids(
        EDWCTransparencyBuildRequirement::FullBake,
        LayerGuids);
    if (LayerGuids.IsEmpty())
    {
        if (OutError != nullptr)
        {
            *OutError = Targets.HasEnabledLayers()
                ? TEXT("No enabled Transparency Target Part requires a full bake.")
                : TEXT("No enabled Transparency Target Parts require runtime output.");
        }
        return false;
    }
    return BakeCoordinator->RequestTransparencyBake(
        MoveTemp(LayerGuids),
        true,
        MoveTemp(Completion),
        OutError);
}

bool SWCAEditorPanel::RequestRebakeAffectedTransparencyMaps(
    TFunction<void(const FDWCEditorBakeBatchResult&)> Completion,
    FString* OutError)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !BakeCoordinator.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The asynchronous bake service is unavailable.");
        return false;
    }
    const FDWCTransparencyBuildTargetSnapshot Targets =
        FDWCTransparencyBuildTargetResolver::Resolve(
            *Asset, EDWCEditorValidationAccess::ExactPayload);
    TArray<int32> MaterialSlots;
    for (const FDWCTransparencyBuildTarget& Target : Targets.Targets)
    {
        if (Target.Requirement == EDWCTransparencyBuildRequirement::AffectedStage4 &&
            Target.IsBuildable())
        {
            MaterialSlots.AddUnique(Target.MaterialSlotIndex);
        }
    }
    if (MaterialSlots.IsEmpty())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("No Transparency Stage 4 outputs require an affected wrinkle-only rebake.");
        }
        return false;
    }
    return BakeCoordinator->RequestAffectedTransparencyStage4Rebake(
        MoveTemp(MaterialSlots),
        true,
        MoveTemp(Completion),
        OutError);
}

bool SWCAEditorPanel::IsWrinkleBakeActive() const
{
    return BakeCoordinator.IsValid() && BakeCoordinator->IsWrinkleBakeActive();
}

EDWCEditorTransparencyBakeKind SWCAEditorPanel::GetActiveTransparencyBakeKind() const
{
    return BakeCoordinator.IsValid()
        ? BakeCoordinator->GetActiveTransparencyBakeKind()
        : EDWCEditorTransparencyBakeKind::None;
}

TSet<EDWCEditorBuildAction> SWCAEditorPanel::GetRunningBuildActions() const
{
    return BuildOperationManager.IsValid()
        ? BuildOperationManager->GetRunningActions()
        : TSet<EDWCEditorBuildAction>();
}

bool SWCAEditorPanel::CanStartBuildAction(FString* OutReason) const
{
    if (OutReason != nullptr)
    {
        OutReason->Reset();
    }
    if (IsExclusiveBuildActive())
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("An exclusive WCA Build is already in progress.");
        }
        return false;
    }
    if (BuildOperationManager.IsValid() && !BuildOperationManager->GetRunningActions().IsEmpty())
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("A WCA Build action is already in progress.");
        }
        return false;
    }
    if (WorkerJobScheduler.IsValid() &&
        WorkerJobScheduler->HasOutstandingWorkClass(EDWCEditorWorkClass::UserBuild))
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("A WCA Build worker job is already in progress.");
        }
        return false;
    }
    return !ResourceBroker.IsValid() || ResourceBroker->CanAdmitWork(
        ResourceBrokerSessionId,
        EDWCEditorWorkClass::UserBuild,
        FGuid(),
        OutReason);
}

bool SWCAEditorPanel::IsExclusiveBuildActive() const
{
    return ExclusiveBuildCoordinator.IsValid() && ExclusiveBuildCoordinator->IsActive();
}

bool SWCAEditorPanel::RequestExclusiveBuild(
    const FString& DebugName,
    TFunction<void()> Work,
    FString* OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    if (!Work)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The exclusive Build has no work callback.");
        }
        return false;
    }
    if (!ExclusiveBuildCoordinator.IsValid())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The WCA editor Build resource services are unavailable.");
        }
        return false;
    }
    if (!CanStartBuildAction(OutError))
    {
        return false;
    }

    if (!ExclusiveBuildCoordinator->Request(DebugName, MoveTemp(Work), OutError))
    {
        return false;
    }
    if (OnStatusChanged.IsBound())
    {
        OnStatusChanged.Execute();
    }
    return true;
}

void SWCAEditorPanel::HandleExclusiveBuildBarrierChanged(const bool bActive)
{
    check(IsInGameThread());
    SetHostLifecycleBlocker(EDWCEditorHostLifecycleBlocker::ExclusiveBuild, bActive);
    if (OnStatusChanged.IsBound())
    {
        OnStatusChanged.Execute();
    }
}

FReply SWCAEditorPanel::BakeSelectedWrinkleNormalMap()
{
    EnsureModeWidget(EWCAEditorMode::WrinkleEdit);
    return WrinkleEditorPanel.IsValid()
               ? WrinkleEditorPanel->BakeSelectedWrinkleNormalMap()
               : FReply::Handled();
}

bool SWCAEditorPanel::SaveTransparencySetupAssets() const
{
    return FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(WetClothingAsset.Get());
}

bool SWCAEditorPanel::SaveBakedVisualAssets() const
{
    bool bSaved = true;
    bSaved &= FWetClothingRenderProfileBakeService::SaveBakedRenderProfileAssets(WetClothingAsset.Get());
    bSaved &= FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(WetClothingAsset.Get());
    return bSaved;
}

void SWCAEditorPanel::SetEditorMode(const EWCAEditorMode NewMode)
{
    if (IsExclusiveBuildActive())
    {
        return;
    }
    if (bHasActiveEditorMode && ActiveEditorMode != NewMode)
    {
        SuspendPreviewMode(ActiveEditorMode, EDWCEditorPreviewSuspendReason::ModeSwitch);
    }
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCActivateEditorModeAction{NewMode});
    }
    const bool bHadModeWidget =
        (NewMode == EWCAEditorMode::PartEdit && PartEditorPanel.IsValid()) ||
        (NewMode == EWCAEditorMode::WrinkleEdit && WrinkleEditorPanel.IsValid()) ||
        (NewMode == EWCAEditorMode::TransparencyBake && TransparencyBakePanel.IsValid());

    ActiveEditorMode = NewMode;
    bHasActiveEditorMode = true;
    if (const TSharedPtr<FDWCEditorPreviewModeLifetime> Lifetime = FindPreviewModeLifetime(NewMode))
    {
        if (CanRunInteractivePreview())
        {
            Lifetime->Activate(HostLifecycle.GetSnapshot().InteractiveGeneration);
        }
        else
        {
            Lifetime->Suspend(HostLifecycle.GetSnapshot().InteractiveGeneration);
        }
    }

    if (ModeContentBox.IsValid())
    {
        ModeContentBox->SetContent(EnsureModeWidget(NewMode));
    }

    if (CanRunInteractivePreview())
    {
        ResumePreviewModeIfNeeded(NewMode);
    }
    else
    {
        SuspendPreviewMode(NewMode, ResolveHostSuspendReason());
    }

    if (bHadModeWidget)
    {
        RefreshFromAsset(false);
    }
    else
    {
        bRefreshPending = false;
        UpdateCachedStatus();
    }
}

void SWCAEditorPanel::HandleAuthoringDocumentChanged(const FDWCEditorAuthoringChange& Change)
{
    if (!SessionStore.IsValid())
    {
        return;
    }

    FDWCReconcileAuthoringAction Action;
    Action.AuthoringRevision = Change.Revision;
    Action.Domain = Change.Domain;
    Action.Index = BuildAuthoringIndex(WetClothingAsset.Get());
    Action.Impact = Change.Impact;
    Action.MaterialSlotIndex = Change.MaterialSlotIndex;
    Action.WetPartID = Change.WetPartID;
    SessionStore->Dispatch(Action);

    if (Change.Phase != EDWCEditorAuthoringChangePhase::Interactive)
    {
        RequestStatusRefresh();
    }
}

void SWCAEditorPanel::SetHostLifecycleBlocker(
    const EDWCEditorHostLifecycleBlocker Blocker,
    const bool bEnabled)
{
    check(IsInGameThread());
    ApplyHostLifecycleTransition(HostLifecycle.SetBlocker(Blocker, bEnabled));
}

void SWCAEditorPanel::SetHostVisibilitySnapshot(
    const FDWCEditorHostVisibilitySnapshot& Visibility)
{
    check(IsInGameThread());
    ApplyHostLifecycleTransition(HostLifecycle.SetVisibilitySnapshot(Visibility));
}

void SWCAEditorPanel::ApplyHostLifecycleTransition(
    const FDWCEditorHostLifecycleTransition& Transition)
{
    check(IsInGameThread());
    if (!Transition.bBlockersChanged)
    {
        return;
    }

    UE_LOG(
        LogWCAEditorLifecycle,
        Verbose,
        TEXT("WCA host lifecycle: state=%s blockers=%s revision=%llu generation=%llu."),
        LexToString(Transition.Current.RunState),
        *LexToString(Transition.Current.Blockers),
        Transition.Current.StateRevision,
        Transition.Current.InteractiveGeneration);

    if (ResourceBroker.IsValid() && ResourceBrokerSessionId.IsValid())
    {
        ResourceBroker->SetSessionActive(
            ResourceBrokerSessionId,
            Transition.Current.CanRunInteractivePreview());
    }

    if (Transition.bBecameClosing)
    {
        SuspendAllPreviewModes(EDWCEditorPreviewSuspendReason::EditorClosing);
    }
    else if (Transition.bBecameSuspended)
    {
        SuspendAllPreviewModes(ResolveHostSuspendReason());
    }
    else if (!Transition.Current.CanRunInteractivePreview() && PreviewGPUResidencyManager.IsValid())
    {
        const EDWCEditorPreviewSuspendReason Reason = ResolveHostSuspendReason();
        const EDWCEditorPreviewResourceReleasePolicy ReleasePolicy =
            ResolveResourceReleasePolicy(Reason);
        if (ReleasePolicy == EDWCEditorPreviewResourceReleasePolicy::Immediate)
        {
            PreviewGPUResidencyManager->SuspendAll(ReleasePolicy);
            EnsureTextureUploadTimer();
        }
    }
    else if (Transition.bBecameInteractive && bHasActiveEditorMode)
    {
        ResumePreviewModeIfNeeded(ActiveEditorMode);
    }
}

bool SWCAEditorPanel::CanRunInteractivePreview() const
{
    return HostLifecycle.CanRunInteractivePreview();
}

EDWCEditorPreviewSuspendReason SWCAEditorPanel::ResolveHostSuspendReason() const
{
    if (HostLifecycle.HasBlocker(EDWCEditorHostLifecycleBlocker::EditorClosing))
    {
        return EDWCEditorPreviewSuspendReason::EditorClosing;
    }
    if (HostLifecycle.HasBlocker(EDWCEditorHostLifecycleBlocker::ExclusiveBuild))
    {
        return EDWCEditorPreviewSuspendReason::ExclusiveBuild;
    }
    if (HostLifecycle.HasBlocker(EDWCEditorHostLifecycleBlocker::PIE))
    {
        return EDWCEditorPreviewSuspendReason::BeginPIE;
    }
    return EDWCEditorPreviewSuspendReason::HostInactive;
}

EDWCEditorPreviewResourceReleasePolicy SWCAEditorPanel::ResolveResourceReleasePolicy(
    const EDWCEditorPreviewSuspendReason Reason) const
{
    if (Reason == EDWCEditorPreviewSuspendReason::ModeSwitch)
    {
        return EDWCEditorPreviewResourceReleasePolicy::ModeSwitch;
    }
    if (Reason != EDWCEditorPreviewSuspendReason::HostInactive)
    {
        return EDWCEditorPreviewResourceReleasePolicy::Immediate;
    }

    return RequiresImmediatePreviewResourceRelease(HostLifecycle.GetSnapshot().Blockers)
        ? EDWCEditorPreviewResourceReleasePolicy::Immediate
        : EDWCEditorPreviewResourceReleasePolicy::DeferredHostInactive;
}

TSharedPtr<FDWCEditorPreviewModeLifetime> SWCAEditorPanel::FindPreviewModeLifetime(
    const EWCAEditorMode Mode) const
{
    switch (Mode)
    {
    case EWCAEditorMode::PartEdit: return PartPreviewLifetime;
    case EWCAEditorMode::WrinkleEdit: return WrinklePreviewLifetime;
    case EWCAEditorMode::TransparencyBake: return TransparencyPreviewLifetime;
    default: return nullptr;
    }
}

FDWCEditorPreviewRunToken SWCAEditorPanel::CapturePreviewRunToken(
    const FDWCEditorWorkerJobDescriptor& Descriptor) const
{
    if (Descriptor.WorkClass != EDWCEditorWorkClass::InteractivePreview)
    {
        return {};
    }

    EDWCEditorPreviewMode Mode = EDWCEditorPreviewMode::None;
    switch (Descriptor.Domain)
    {
    case EDWCEditorAuthoringDomain::Wrinkle:
        Mode = EDWCEditorPreviewMode::Wrinkle;
        break;
    case EDWCEditorAuthoringDomain::Transparency:
        Mode = EDWCEditorPreviewMode::Transparency;
        break;
    default:
        break;
    }
    if (Mode == EDWCEditorPreviewMode::None)
    {
        switch (Descriptor.Key.Kind)
        {
        case EDWCEditorWorkerJobKind::WrinkleAccumulatedPreview:
        case EDWCEditorWorkerJobKind::WrinkleIncrementalPreview:
        case EDWCEditorWorkerJobKind::WrinkleTransientPreview:
        case EDWCEditorWorkerJobKind::WrinkleHoverPreview:
            Mode = EDWCEditorPreviewMode::Wrinkle;
            break;
        case EDWCEditorWorkerJobKind::TransparencyVisualization:
        case EDWCEditorWorkerJobKind::TransparencyAlphaIncremental:
        case EDWCEditorWorkerJobKind::TransparencyRevealColorIncremental:
        case EDWCEditorWorkerJobKind::TransparencyAlphaDirtyReplay:
        case EDWCEditorWorkerJobKind::TransparencyRevealColorDirtyReplay:
        case EDWCEditorWorkerJobKind::TransparencyRevealColorCommit:
            Mode = EDWCEditorPreviewMode::Transparency;
            break;
        default:
            break;
        }
    }

    EWCAEditorMode EditorMode = EWCAEditorMode::PartEdit;
    if (Mode == EDWCEditorPreviewMode::Wrinkle)
    {
        EditorMode = EWCAEditorMode::WrinkleEdit;
    }
    else if (Mode == EDWCEditorPreviewMode::Transparency)
    {
        EditorMode = EWCAEditorMode::TransparencyBake;
    }
    if (Mode == EDWCEditorPreviewMode::None || !bHasActiveEditorMode || ActiveEditorMode != EditorMode)
    {
        return {};
    }

    const TSharedPtr<FDWCEditorPreviewModeLifetime> Lifetime = FindPreviewModeLifetime(EditorMode);
    return Lifetime.IsValid() ? Lifetime->CaptureToken() : FDWCEditorPreviewRunToken();
}

void SWCAEditorPanel::SuspendPreviewMode(
    const EWCAEditorMode Mode,
    const EDWCEditorPreviewSuspendReason Reason,
    const bool bManageResidency)
{
    const EDWCEditorPreviewMode PreviewMode = ResolvePreviewMode(Mode);
    if (const TSharedPtr<FDWCEditorPreviewModeLifetime> Lifetime = FindPreviewModeLifetime(Mode))
    {
        const uint64 HostGeneration = HostLifecycle.GetSnapshot().InteractiveGeneration;
        if (Reason == EDWCEditorPreviewSuspendReason::EditorClosing)
        {
            Lifetime->Revoke(HostGeneration);
        }
        else if (Reason == EDWCEditorPreviewSuspendReason::ModeSwitch)
        {
            Lifetime->Deactivate(HostGeneration);
        }
        else
        {
            Lifetime->Suspend(HostGeneration);
        }
    }
    if (WorkerJobScheduler.IsValid())
    {
        WorkerJobScheduler->CancelPreviewMode(PreviewMode);
    }
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->CancelPreviewMode(WetClothingAsset.Get(), PreviewMode);
    }
    if (const TSharedPtr<FDWCEditorPreviewModeLifetime> Lifetime = FindPreviewModeLifetime(Mode))
    {
        UE_LOG(
            LogWCAEditorLifecycle,
            Verbose,
            TEXT("WCA preview mode: mode=%s state=%s generation=%llu reason=%d."),
            LexToString(PreviewMode),
            LexToString(Lifetime->GetRunState()),
            Lifetime->GetGeneration(),
            static_cast<int32>(Reason));
    }

    switch (Mode)
    {
    case EWCAEditorMode::PartEdit:
        if (PartEditorPanel.IsValid())
        {
            PartEditorPanel->SuspendPreview(Reason);
        }
        break;
    case EWCAEditorMode::WrinkleEdit:
        if (WrinkleEditorPanel.IsValid())
        {
            WrinkleEditorPanel->SuspendPreview(Reason);
        }
        break;
    case EWCAEditorMode::TransparencyBake:
        if (TransparencyBakePanel.IsValid())
        {
            TransparencyBakePanel->SuspendPreview(Reason);
        }
        break;
    default:
        break;
    }
    if (bManageResidency && PreviewGPUResidencyManager.IsValid())
    {
        PreviewGPUResidencyManager->SuspendDomain(
            ResolvePreviewGPUDomain(Mode),
            ResolveResourceReleasePolicy(Reason));
        EnsureTextureUploadTimer();
    }
}

void SWCAEditorPanel::ResumePreviewModeIfNeeded(const EWCAEditorMode Mode)
{
    if (!CanRunInteractivePreview() || !bHasActiveEditorMode || ActiveEditorMode != Mode)
    {
        return;
    }
    if (const TSharedPtr<FDWCEditorPreviewModeLifetime> Lifetime = FindPreviewModeLifetime(Mode))
    {
        Lifetime->Activate(HostLifecycle.GetSnapshot().InteractiveGeneration);
        UE_LOG(
            LogWCAEditorLifecycle,
            Verbose,
            TEXT("WCA preview mode: mode=%s state=%s generation=%llu."),
            LexToString(ResolvePreviewMode(Mode)),
            LexToString(Lifetime->GetRunState()),
            Lifetime->GetGeneration());
    }
    if (PreviewGPUResidencyManager.IsValid())
    {
        PreviewGPUResidencyManager->SetActiveDomain(ResolvePreviewGPUDomain(Mode));
    }
    switch (Mode)
    {
    case EWCAEditorMode::PartEdit:
        if (PartEditorPanel.IsValid())
        {
            PartEditorPanel->ResumePreviewIfNeeded();
        }
        break;
    case EWCAEditorMode::WrinkleEdit:
        if (WrinkleEditorPanel.IsValid())
        {
            WrinkleEditorPanel->ResumePreviewIfNeeded();
        }
        break;
    case EWCAEditorMode::TransparencyBake:
        if (TransparencyBakePanel.IsValid())
        {
            TransparencyBakePanel->ResumePreviewIfNeeded();
        }
        break;
    default:
        break;
    }
}

void SWCAEditorPanel::SuspendAllPreviewModes(const EDWCEditorPreviewSuspendReason Reason)
{
    SuspendPreviewMode(EWCAEditorMode::PartEdit, Reason, false);
    SuspendPreviewMode(EWCAEditorMode::WrinkleEdit, Reason, false);
    SuspendPreviewMode(EWCAEditorMode::TransparencyBake, Reason, false);
    if (PreviewGPUResidencyManager.IsValid())
    {
        PreviewGPUResidencyManager->SuspendAll(ResolveResourceReleasePolicy(Reason));
        EnsureTextureUploadTimer();
    }
}

void SWCAEditorPanel::HandlePreBeginPIE(const bool)
{
    SetHostLifecycleBlocker(EDWCEditorHostLifecycleBlocker::PIE, true);
}

void SWCAEditorPanel::HandleEndPIE(const bool)
{
    SetHostLifecycleBlocker(EDWCEditorHostLifecycleBlocker::PIE, false);
}

#undef LOCTEXT_NAMESPACE
