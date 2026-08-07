//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectGlobals.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialCache.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialFactory.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialParameters.h"

namespace
{
    FDWCEditorPreviewMaterialRequest MakeLifecycleRequest(UMaterialInterface* SourceMaterial)
    {
        FDWCEditorPreviewMaterialRequest Request;
        Request.SourceMaterial = SourceMaterial;
        Request.SlotOwner = SourceMaterial;
        Request.MaterialSlotIndex = 0;
        Request.DWCDataUVChannelIndex = 0;
        Request.SurfaceWaterNormalUVChannelIndex = 0;
        return Request;
    }

    class FWaitForPreviewMaterialReady final : public IAutomationLatentCommand
    {
      public:
        FWaitForPreviewMaterialReady(
            FAutomationTestBase* InTest,
            TSharedRef<FDWCEditorPreviewMaterialCache> InCache,
            UMaterialInterface* InSourceMaterial)
            : Test(InTest),
              Cache(MoveTemp(InCache)),
              SourceMaterial(InSourceMaterial),
              DeadlineSeconds(FPlatformTime::Seconds() + 60.0)
        {
        }

        virtual bool Update() override
        {
            if (Test == nullptr || SourceMaterial == nullptr)
            {
                return true;
            }

            const FDWCEditorPreviewMaterialResult Result = Cache->GetOrCreate(
                MakeLifecycleRequest(SourceMaterial));
            if (Result.State == EDWCEditorPreviewMaterialState::Ready)
            {
                Test->TestTrue(TEXT("A ready preview result owns a usable MID"), Result.bSucceeded);
                Test->TestNotNull(TEXT("The ready preview MID was created"), Result.PreviewMID);
                if (Result.PreviewMID != nullptr)
                {
                    constexpr float ExpectedWetness = 0.375f;
                    Result.PreviewMID->SetScalarParameterValue(
                        DWCEditorPreviewMaterialParameters::PreviewWetness(),
                        ExpectedWetness);
                    float ActualWetness = -1.0f;
                    const bool bReadParameter = Result.PreviewMID->GetScalarParameterValue(
                        DWCEditorPreviewMaterialParameters::PreviewWetness(),
                        ActualWetness);
                    Test->TestTrue(TEXT("The preview MID exposes DWC_PreviewWetness"), bReadParameter);
                    Test->TestEqual(
                        TEXT("Preview wetness can be updated without rebuilding the graph"),
                        ActualWetness,
                        ExpectedWetness);
                }
                Cache->Reset();
                return true;
            }

            if (Result.State == EDWCEditorPreviewMaterialState::Failed)
            {
                Test->AddError(FString::Printf(
                    TEXT("Transient preview material failed before becoming ready: %s"),
                    *Result.Message));
                Cache->Reset();
                return true;
            }

            if (FPlatformTime::Seconds() >= DeadlineSeconds)
            {
                const FDWCEditorPreviewMaterialCacheStats Stats = Cache->GetStats();
                Test->AddError(FString::Printf(
                    TEXT("Transient preview material did not become ready within 60 seconds "
                         "(pending=%d, completed=%llu, failed=%llu)."),
                    Stats.PendingGraphEntryCount,
                    Stats.GraphCompileCompleteCount,
                    Stats.GraphCompileFailureCount));
                Cache->Reset();
                return true;
            }
            return false;
        }

      private:
        FAutomationTestBase* Test = nullptr;
        TSharedRef<FDWCEditorPreviewMaterialCache> Cache;
        TObjectPtr<UMaterialInterface> SourceMaterial = nullptr;
        double DeadlineSeconds = 0.0;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewMaterialCacheInvalidRequestTest,
    "DWC.Editor.Foundation.Preview.MaterialCache.InvalidRequests",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewMaterialCacheInvalidRequestTest::RunTest(const FString&)
{
    FDWCEditorPreviewMaterialCache Cache;

    const FDWCEditorPreviewMaterialResult MissingSource = Cache.GetOrCreate(
        FDWCEditorPreviewMaterialRequest());
    TestEqual(
        TEXT("A missing source material fails without becoming pending"),
        static_cast<uint8>(MissingSource.State),
        static_cast<uint8>(EDWCEditorPreviewMaterialState::Failed));
    TestFalse(TEXT("A missing source material is never reported as pending"), MissingSource.bPending);
    TestFalse(TEXT("A missing source material does not create a MID"), MissingSource.bSucceeded);

    UMaterialInterface* SourceMaterial = LoadObject<UMaterialInterface>(
        nullptr,
        TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
    TestNotNull(TEXT("Engine default material is available for the lifecycle fixture"), SourceMaterial);
    if (SourceMaterial == nullptr)
    {
        return false;
    }
    FDWCEditorPreviewMaterialRequest MissingSlotRequest = MakeLifecycleRequest(SourceMaterial);
    MissingSlotRequest.MaterialSlotIndex = INDEX_NONE;
    const FDWCEditorPreviewMaterialResult MissingSlot = Cache.GetOrCreate(MissingSlotRequest);
    TestEqual(
        TEXT("An invalid slot fails before graph creation"),
        static_cast<uint8>(MissingSlot.State),
        static_cast<uint8>(EDWCEditorPreviewMaterialState::Failed));
    TestEqual(TEXT("Invalid requests leave the graph cache empty"), Cache.GetStats().GraphEntryCount, 0);

    FString ErrorMessage;
    TestFalse(
        TEXT("Starting compilation for a null material fails safely"),
        FDWCEditorPreviewMaterialFactory::BeginTransientBaseMaterialCompilation(nullptr, ErrorMessage));
    TestFalse(TEXT("Null compilation reports an error"), ErrorMessage.IsEmpty());
    TestEqual(
        TEXT("Polling a null material returns Failed"),
        static_cast<uint8>(FDWCEditorPreviewMaterialFactory::PollTransientBaseMaterialCompilation(nullptr, ErrorMessage)),
        static_cast<uint8>(EDWCEditorPreviewMaterialState::Failed));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewMaterialCachePendingLifecycleTest,
    "DWC.Editor.Foundation.Preview.MaterialCache.PendingLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
        EAutomationTestFlags::NonNullRHI)

bool FDWCEditorPreviewMaterialCachePendingLifecycleTest::RunTest(const FString&)
{
    TSharedRef<FDWCEditorPreviewMaterialCache> Cache = MakeShared<FDWCEditorPreviewMaterialCache>();
    UMaterialInterface* SourceMaterial = LoadObject<UMaterialInterface>(
        nullptr,
        TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
    TestNotNull(TEXT("Engine default material is available for the pending fixture"), SourceMaterial);
    if (SourceMaterial == nullptr)
    {
        return false;
    }

    const FDWCEditorPreviewMaterialResult FirstResult = Cache->GetOrCreate(
        MakeLifecycleRequest(SourceMaterial));
    const bool bReachedCompileLifecycle =
        FirstResult.State == EDWCEditorPreviewMaterialState::Compiling ||
        FirstResult.State == EDWCEditorPreviewMaterialState::Ready;
    if (!bReachedCompileLifecycle)
    {
        AddError(FString::Printf(
            TEXT("Transient material fixture failed before compile lifecycle: state=%d, message=%s"),
            static_cast<int32>(FirstResult.State),
            *FirstResult.Message));
    }
    TestTrue(
        TEXT("A valid transient source reaches Compiling or Ready instead of failing"),
        bReachedCompileLifecycle);

    const FDWCEditorPreviewMaterialCacheStats InitialStats = Cache->GetStats();
    TestEqual(TEXT("One graph entry is retained while the request is alive"), InitialStats.GraphEntryCount, 1);
    if (FirstResult.State == EDWCEditorPreviewMaterialState::Compiling)
    {
        TestEqual(TEXT("The first request reports one pending graph"), InitialStats.PendingGraphEntryCount, 1);

        Cache->PruneUnusedHierarchies();
        const FDWCEditorPreviewMaterialCacheStats PendingStats = Cache->GetStats();
        TestEqual(TEXT("Pruning does not remove a graph owned by the shader compiler"), PendingStats.GraphEntryCount, 1);
        TestEqual(TEXT("The pending graph remains pending after pruning"), PendingStats.PendingGraphEntryCount, 1);

    }

    ADD_LATENT_AUTOMATION_COMMAND(FWaitForPreviewMaterialReady(this, Cache, SourceMaterial));
    return true;
}

#endif
