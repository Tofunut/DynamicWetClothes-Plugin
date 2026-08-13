//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "DataAssets/WetClothingWrinkleData.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionCoverageService.h"
#include "WetClothing/Modes/Wrinkle/Authoring/DWCEditorWrinkleTextureResolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCWrinkleSuppressionEvaluationParityTest,
    "DWC.Transparency.Processing.WrinkleSuppressionEvaluationParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCWrinkleSuppressionEvaluationParityTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Coverage below threshold is rejected."),
        FDWCWrinkleSuppressionCoverageService::EvaluateSuppression(0.1f, 0.2f, 0.1f),
        0.0f);
    TestTrue(TEXT("Coverage inside softness uses smoothstep before multiplication."),
        FMath::IsNearlyEqual(
            FDWCWrinkleSuppressionCoverageService::EvaluateSuppression(0.25f, 0.2f, 0.1f),
            0.125f,
            0.0001f));
    TestEqual(TEXT("Coverage above the transition is preserved."),
        FDWCWrinkleSuppressionCoverageService::EvaluateSuppression(0.4f, 0.2f, 0.1f),
        0.4f);
    TestEqual(TEXT("Zero softness is a hard threshold."),
        FDWCWrinkleSuppressionCoverageService::EvaluateSuppression(0.2f, 0.2f, 0.0f),
        0.2f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCWrinkleSoftTextureResolverTest,
    "DWC.Wrinkle.Authoring.SoftTextureResolver",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCWrinkleSoftTextureResolverTest::RunTest(const FString&)
{
    const FProperty* SourceProperty = FindFProperty<FProperty>(
        FWetWrinklePatchPlacement::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWetWrinklePatchPlacement, WrinkleNormalTexture));
    TestNotNull(TEXT("The authored wrinkle source property exists."), SourceProperty);
    if (SourceProperty != nullptr)
    {
        TestTrue(
            TEXT("The authored wrinkle source does not pull its texture package into cooked builds."),
            SourceProperty->HasMetaData(FSoftObjectPath::NAME_Untracked));
    }

#if WITH_EDITORONLY_DATA
    const FProperty* MaskProperty = FindFProperty<FProperty>(
        FWetWrinkleBakedMapSet::StaticStruct(),
        GET_MEMBER_NAME_CHECKED(FWetWrinkleBakedMapSet, BakedWrinkleMask));
    TestNotNull(TEXT("The editor wrinkle coverage mask property exists."), MaskProperty);
    if (MaskProperty != nullptr)
    {
        TestTrue(
            TEXT("The editor wrinkle coverage mask is excluded from cooked WCA data."),
            MaskProperty->HasAnyPropertyFlags(CPF_EditorOnly));
    }
#endif

    FWetWrinklePatchPlacement Patch;
    const FDWCEditorWrinkleTextureReferenceSnapshot Unset =
        FDWCEditorWrinkleTextureResolver::InspectSource(Patch);
    TestEqual(
        TEXT("An unassigned source remains distinct from an unloaded source"),
        Unset.Status,
        EDWCEditorWrinkleTextureResolveStatus::Unset);

    UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
    const FColor Pixel(128, 128, 255, 255);
    Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(&Pixel));
    Patch.SetWrinkleNormalTexture(Texture);
    const FDWCEditorWrinkleTextureReferenceSnapshot Resolved =
        FDWCEditorWrinkleTextureResolver::ResolveSource(Patch);
    TestTrue(TEXT("A loaded soft wrinkle source resolves without another package load"), Resolved.IsReady());
    TestEqual(TEXT("The source resolution is retained"), Resolved.SourceSize, FIntPoint(1, 1));
    TestEqual(TEXT("The loaded texture identity is retained"), Resolved.Texture.Get(), Texture);

    FWetWrinkleBakedMapSet BakedMap;
#if WITH_EDITORONLY_DATA
    BakedMap.BakedWrinkleMask = Texture;
    const FDWCEditorWrinkleTextureReferenceSnapshot Mask =
        FDWCEditorWrinkleTextureResolver::ResolveEditorMask(BakedMap);
    TestTrue(TEXT("The editor coverage mask uses the same soft resolver contract"), Mask.IsReady());

    FDWCWrinkleSuppressionDependencySnapshot Dependency;
    Dependency.Status = EDWCWrinkleSuppressionDependencyStatus::Ready;
    Dependency.MaterialSlotIndex = 0;
    Dependency.DataUVChannelIndex = 0;
    Dependency.MaskTexture = BakedMap.BakedWrinkleMask;
    Dependency.MaskTexturePath = BakedMap.GetBakedWrinkleMaskPath().ToString();
    Dependency.BuildSignature = TEXT("SoftMaskDependency");
    Dependency.BakeGuid = FGuid::NewGuid();
    Dependency.TextureSourceId = Texture->Source.GetId();
    Dependency.SourceResolution = FIntPoint(1, 1);
    TestTrue(TEXT("A soft coverage dependency remains available"), Dependency.IsAvailable());
    TestEqual(TEXT("A soft coverage dependency resolves at the readback boundary"), Dependency.ResolveTexture(), Texture);
#endif

    Patch.WrinkleNormalTexture = TSoftObjectPtr<UTexture2D>(
        FSoftObjectPath(TEXT("/DWC/Tests/T_MissingWrinkle.T_MissingWrinkle")));
    const FDWCEditorWrinkleTextureReferenceSnapshot Missing =
        FDWCEditorWrinkleTextureResolver::InspectSource(Patch);
    TestEqual(
        TEXT("A missing source package is reported without loading it"),
        Missing.Status,
        EDWCEditorWrinkleTextureResolveStatus::Missing);
    return true;
}

#endif
