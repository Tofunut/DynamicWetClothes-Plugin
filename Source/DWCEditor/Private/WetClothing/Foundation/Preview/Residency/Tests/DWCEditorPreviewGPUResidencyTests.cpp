// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Preview/Residency/DWCEditorPreviewGPUResidencyManager.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewGPUResidencyLifecycleTest,
    "DWC.Editor.Preview.GPUResidency.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewGPUResidencyLifecycleTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    TestEqual(
        TEXT("Wet Part surface textures map to the Wet Part domain"),
        FDWCEditorPreviewGPUResidencyManager::GetDomainForPurpose(
            EDWCEditorTexturePurpose::WetPartSurfaceDroplet),
        EDWCEditorPreviewGPUDomain::WetPart);
    TestEqual(
        TEXT("Wrinkle hover maps to the Wrinkle domain"),
        FDWCEditorPreviewGPUResidencyManager::GetDomainForPurpose(
            EDWCEditorTexturePurpose::WrinkleHover),
        EDWCEditorPreviewGPUDomain::Wrinkle);
    TestEqual(
        TEXT("Transparency visualization maps to the Transparency domain"),
        FDWCEditorPreviewGPUResidencyManager::GetDomainForPurpose(
            EDWCEditorTexturePurpose::TransparencyVisualization),
        EDWCEditorPreviewGPUDomain::Transparency);

    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    const TSharedRef<FDWCEditorTextureWorkspace> Workspace =
        MakeShared<FDWCEditorTextureWorkspace>(
            UploadQueue,
            16ull * 1024ull * 1024ull,
            16ull * 1024ull * 1024ull);
    FDWCEditorPreviewGPUResidencyManager Manager(UploadQueue, Workspace);

    UTexture2D* Owner = NewObject<UTexture2D>(GetTransientPackage());
    FDWCEditorTextureKey Key;
    Key.Owner = FObjectKey(Owner);
    Key.Purpose = EDWCEditorTexturePurpose::WetPartSurfaceWetness;
    Key.MaterialSlotIndex = 3;
    Key.LayerGuid = FGuid::NewGuid();

    FDWCEditorTextureDescriptor Descriptor;
    Descriptor.Size = FIntPoint(4, 4);
    Descriptor.WorkingSize = Descriptor.Size;
    Descriptor.PixelFormat = PF_R32_FLOAT;
    Descriptor.InitialR32F = 0.25f;

    TArray<float> Pixels;
    Pixels.Init(0.75f, Descriptor.Size.X * Descriptor.Size.Y);
    FDWCEditorTextureLease Lease = Workspace->TransferR32FAndAcquireLease(
        Key,
        Descriptor,
        MoveTemp(Pixels),
        EDWCEditorTextureUploadPriority::Interactive);
    TestTrue(TEXT("R32F preview data acquires a workspace lease"), Lease.IsValid());

    TArray<FDWCEditorTextureGPUResidencyRecord> Records;
    Workspace->GetGPUResidencySnapshot(Records);
    const FDWCEditorTextureGPUResidencyRecord* Resident = Records.FindByPredicate(
        [&Key](const FDWCEditorTextureGPUResidencyRecord& Record)
        {
            return Record.Key == Key;
        });
    TestNotNull(TEXT("The resident texture is visible in diagnostics"), Resident);
    if (Resident != nullptr)
    {
        TestEqual(TEXT("The R32F format is retained"), Resident->PixelFormat, PF_R32_FLOAT);
        TestEqual(TEXT("An active lease prevents retirement"), Resident->ActiveLeaseCount, 1u);
        TestEqual(TEXT("The resource is resident"), Resident->State, EDWCEditorTextureGPUState::Resident);
    }

    Manager.SuspendDomain(EDWCEditorPreviewGPUDomain::WetPart);
    Workspace->GetGPUResidencySnapshot(Records);
    Resident = Records.FindByPredicate(
        [&Key](const FDWCEditorTextureGPUResidencyRecord& Record)
        {
            return Record.Key == Key;
        });
    TestTrue(
        TEXT("Suspension cannot retire a texture while a viewport still owns its lease"),
        Resident != nullptr && Resident->State == EDWCEditorTextureGPUState::Resident);

    Lease.Reset();
    Manager.SuspendDomain(EDWCEditorPreviewGPUDomain::WetPart);
    Workspace->GetGPUResidencySnapshot(Records);
    Resident = Records.FindByPredicate(
        [&Key](const FDWCEditorTextureGPUResidencyRecord& Record)
        {
            return Record.Key == Key;
        });
    TestTrue(
        TEXT("An unleased inactive-domain texture begins retirement"),
        Resident != nullptr && Resident->State == EDWCEditorTextureGPUState::Retiring);

    FlushRenderingCommands();
    Manager.Tick();
    Workspace->GetGPUResidencySnapshot(Records);
    Resident = Records.FindByPredicate(
        [&Key](const FDWCEditorTextureGPUResidencyRecord& Record)
        {
            return Record.Key == Key;
        });
    TestTrue(
        TEXT("The completed fence leaves a reusable CPU-only workspace entry"),
        Resident != nullptr && Resident->State == EDWCEditorTextureGPUState::CPUOnly);

    Manager.SetActiveDomain(EDWCEditorPreviewGPUDomain::Transparency);
    TestEqual(
        TEXT("The active preview domain is explicit"),
        Manager.GetActiveDomain(),
        EDWCEditorPreviewGPUDomain::Transparency);
    Manager.SuspendAll();
    TestTrue(TEXT("PIE/editor suspension is represented explicitly"), Manager.IsSuspended());
    TestEqual(
        TEXT("Suspending all clears the active domain"),
        Manager.GetActiveDomain(),
        EDWCEditorPreviewGPUDomain::None);

    Manager.Shutdown();
    UploadQueue->Shutdown();
    Workspace->Reset();
    return true;
}

#endif
