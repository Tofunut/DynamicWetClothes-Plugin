// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "WetClothing/Foundation/Assets/DWCEditorArtifactStore.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"

namespace
{
    TSharedRef<FDWCEditorArtifactStore> MakeArtifactStore(const uint64 CommitBudget)
    {
        FDWCEditorResourceBudgetConfig Config;
        Config.GlobalEditorCPUBytes = CommitBudget;
        Config.AssetCommitCPUBytes = CommitBudget;
        return FDWCEditorArtifactStore::CreateForTesting(
            MakeShared<FDWCEditorResourceGovernor>(Config));
    }

    UWetClothingAsset* MakeOwnerAsset()
    {
        UWetClothingAsset* Asset = NewObject<UWetClothingAsset>();
        Asset->EnsureAssetGuid();
        return Asset;
    }

    FDWCEditorArtifactTextureRequest MakeRequest(
        UWetClothingAsset& Owner,
        const FString& PackageName,
        const FString& AssetName,
        const TConstArrayView<FColor> Pixels)
    {
        FDWCEditorArtifactTextureRequest Request;
        Request.OwnerAsset = &Owner;
        Request.PackageName = PackageName;
        Request.AssetName = AssetName;
        Request.Lifetime = EDWCEditorArtifactLifetime::EditorIntermediate;
        Request.Resolution = FIntPoint(2, 2);
        Request.SourceFormat = TSF_BGRA8;
        Request.PixelData = reinterpret_cast<const uint8*>(Pixels.GetData());
        Request.PixelBytes = static_cast<uint64>(Pixels.Num()) * sizeof(FColor);
        Request.Settings.CompressionSettings = TC_Default;
        Request.Settings.bSRGB = true;
        Request.DebugName = TEXT("Artifact store automation texture");
        return Request;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorArtifactStorePreflightTest,
    "DWC.Editor.Foundation.Assets.ArtifactStore.PreflightAndBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorArtifactStorePreflightTest::RunTest(const FString&)
{
    UWetClothingAsset* Owner = MakeOwnerAsset();
    TArray<FColor> Pixels;
    Pixels.Init(FColor::White, 4);
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PackageA = FString::Printf(
        TEXT("/Game/__DWC_Automation__/ArtifactStore/%s/T_A"), *Token);
    const FString PackageB = FString::Printf(
        TEXT("/Game/__DWC_Automation__/ArtifactStore/%s/T_B"), *Token);

    TArray<FDWCEditorArtifactTextureRequest> Requests;
    Requests.Add(MakeRequest(*Owner, PackageA, TEXT("T_A"), Pixels));
    Requests.Add(MakeRequest(*Owner, PackageB, TEXT("T_B"), Pixels));
    Requests[1].PixelBytes -= 1;
    TArray<FDWCEditorArtifactCommitReceipt> Receipts;
    FString Error;
    TestFalse(TEXT("An invalid member rejects the complete batch."),
        MakeArtifactStore(1024)->CommitTextureBatch(Requests, Receipts, Error));
    TestTrue(TEXT("Rejected preflight publishes no receipts."), Receipts.IsEmpty());
    TestNull(TEXT("The first artifact is not created before full preflight succeeds."),
        FindObject<UTexture2D>(nullptr, *(PackageA + TEXT(".T_A"))));

    Requests.SetNum(1);
    Error.Reset();
    TestFalse(TEXT("A commit larger than the dedicated budget is rejected."),
        MakeArtifactStore(16)->CommitTextureBatch(Requests, Receipts, Error));
    TestTrue(TEXT("Budget rejection reports its reason."), !Error.IsEmpty());
    TestNull(TEXT("Budget rejection occurs before object creation."),
        FindObject<UTexture2D>(nullptr, *(PackageA + TEXT(".T_A"))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorArtifactStoreCommitTest,
    "DWC.Editor.Foundation.Assets.ArtifactStore.CommitAndDirtyLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorArtifactStoreCommitTest::RunTest(const FString&)
{
    UWetClothingAsset* Owner = MakeOwnerAsset();
    TArray<FColor> Pixels;
    Pixels.Init(FColor(12, 34, 56, 78), 4);
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PackageName = FString::Printf(
        TEXT("/Game/__DWC_Automation__/ArtifactStore/%s/T_Result"), *Token);
    TArray<FDWCEditorArtifactTextureRequest> Requests;
    Requests.Add(MakeRequest(*Owner, PackageName, TEXT("T_Result"), Pixels));
    Requests[0].PixelData = nullptr;
    Requests[0].SourceWriter = [Pixels](uint8* Destination, const uint64 DestinationBytes)
    {
        check(DestinationBytes == static_cast<uint64>(Pixels.Num()) * sizeof(FColor));
        FMemory::Memcpy(Destination, Pixels.GetData(), DestinationBytes);
    };

    TSharedRef<FDWCEditorArtifactStore> Store = MakeArtifactStore(1024);
    TArray<FDWCEditorArtifactCommitReceipt> Receipts;
    FString Error;
    TestTrue(TEXT("A valid artifact batch commits."),
        Store->CommitTextureBatch(Requests, Receipts, Error));
    TestEqual(TEXT("One receipt is published."), Receipts.Num(), 1);
    if (Receipts.Num() != 1 || Receipts[0].Texture == nullptr)
    {
        return false;
    }

    UTexture2D* Texture = Receipts[0].Texture;
    TestEqual(TEXT("The source width is preserved."), Texture->Source.GetSizeX(), int64(2));
    TestEqual(TEXT("The source height is preserved."), Texture->Source.GetSizeY(), int64(2));
    TestTrue(TEXT("The authored sRGB setting is preserved."), Texture->SRGB);
    TestTrue(TEXT("Intermediate artifacts are editor-only."),
        Texture->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly));

    TArray<UPackage*> DirtyPackages;
    Store->CollectDirtyPackages(*Owner, DirtyPackages);
    TestTrue(TEXT("The committed package participates in the save lifecycle."),
        DirtyPackages.Contains(Texture->GetOutermost()));
    Store->NotifyPackagesSaved(DirtyPackages);
    const FDWCEditorArtifactStoreDiagnostics SavedDiagnostics = Store->GetDiagnostics();
    TestEqual(TEXT("Save acknowledgement updates dirty tracking diagnostics immediately."),
        SavedDiagnostics.DirtyArtifactCount, 0);
    DirtyPackages.Reset();
    Store->CollectDirtyPackages(*Owner, DirtyPackages);
    TestTrue(TEXT("A successful save acknowledgement clears store dirtiness."),
        DirtyPackages.IsEmpty());
    Texture->GetOutermost()->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorArtifactStoreBatchParityTest,
    "DWC.Editor.Integration.Parity.ArtifactStoreBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorArtifactStoreBatchParityTest::RunTest(const FString&)
{
    constexpr uint64 SourceBytes = 4 * sizeof(FColor);
    constexpr uint64 ExactCommitPeakBytes = SourceBytes * 2;

    FDWCEditorResourceBudgetConfig Config;
    Config.GlobalEditorCPUBytes = ExactCommitPeakBytes;
    Config.AssetCommitCPUBytes = ExactCommitPeakBytes;
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(Config);
    const TSharedRef<FDWCEditorArtifactStore> Store =
        FDWCEditorArtifactStore::CreateForTesting(Governor);

    UWetClothingAsset* Owner = MakeOwnerAsset();
    TArray<FColor> Pixels;
    Pixels.Add(FColor(1, 2, 3, 4));
    Pixels.Add(FColor(10, 20, 30, 40));
    Pixels.Add(FColor(50, 60, 70, 80));
    Pixels.Add(FColor(90, 100, 110, 120));

    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString DirectPackage = FString::Printf(
        TEXT("/Game/__DWC_Automation__/ArtifactStore/%s/T_Direct"), *Token);
    const FString WriterPackage = FString::Printf(
        TEXT("/Game/__DWC_Automation__/ArtifactStore/%s/T_Writer"), *Token);

    TArray<FDWCEditorArtifactTextureRequest> Requests;
    Requests.Add(MakeRequest(*Owner, DirectPackage, TEXT("T_Direct"), Pixels));
    Requests.Last().Lifetime = EDWCEditorArtifactLifetime::RuntimeFinal;
    Requests.Add(MakeRequest(*Owner, WriterPackage, TEXT("T_Writer"), Pixels));
    Requests.Last().PixelData = nullptr;
    Requests.Last().SourceWriter = [Pixels](uint8* Destination, const uint64 DestinationBytes)
    {
        check(DestinationBytes == static_cast<uint64>(Pixels.Num()) * sizeof(FColor));
        FMemory::Memcpy(Destination, Pixels.GetData(), DestinationBytes);
    };

    TArray<FDWCEditorArtifactCommitReceipt> Receipts;
    FString Error;
    TestTrue(TEXT("A multi-artifact batch fits the largest single-artifact peak."),
        Store->CommitTextureBatch(Requests, Receipts, Error));
    TestEqual(TEXT("Both batch members publish receipts."), Receipts.Num(), 2);
    if (Receipts.Num() != 2 || Receipts[0].Texture == nullptr || Receipts[1].Texture == nullptr)
    {
        return false;
    }

    TArray64<uint8> DirectBytes;
    TArray64<uint8> WriterBytes;
    TestTrue(TEXT("The direct payload source can be read back."),
        Receipts[0].Texture->Source.GetMipData(DirectBytes, 0));
    TestTrue(TEXT("The writer payload source can be read back."),
        Receipts[1].Texture->Source.GetMipData(WriterBytes, 0));
    TestEqual(TEXT("Both commit paths publish the same byte count."),
        DirectBytes.Num(), WriterBytes.Num());
    TestTrue(TEXT("Direct and zero-copy writer payloads are byte-identical."),
        DirectBytes.Num() == WriterBytes.Num() &&
        FMemory::Memcmp(DirectBytes.GetData(), WriterBytes.GetData(), DirectBytes.Num()) == 0);

    const FDWCEditorArtifactStoreDiagnostics StoreDiagnostics = Store->GetDiagnostics();
    TestEqual(TEXT("The batch reserves one largest source plus texture-source copy."),
        StoreDiagnostics.PeakCommitReservationBytes, ExactCommitPeakBytes);
    TestEqual(TEXT("The commit lease is released before CommitTextureBatch returns."),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, 0ull);
    TestEqual(TEXT("The commit lease leaves no live governor reservation."),
        Governor->GetDiagnostics().Reservations.Num(), 0);

    TestFalse(TEXT("Runtime-final artifacts remain cookable."),
        Receipts[0].Texture->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly));
    TestTrue(TEXT("Editor-intermediate artifacts are excluded from cook."),
        Receipts[1].Texture->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly));

    Receipts[0].Texture->GetOutermost()->SetDirtyFlag(false);
    Receipts[1].Texture->GetOutermost()->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorArtifactStoreCookBoundaryTest,
    "DWC.Editor.Integration.Cook.ArtifactLifetimeBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorArtifactStoreCookBoundaryTest::RunTest(const FString&)
{
    UWetClothingAsset* Owner = MakeOwnerAsset();
    TArray<FColor> Pixels;
    Pixels.Init(FColor(8, 16, 32, 255), 4);
    const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString RuntimePackage = FString::Printf(
        TEXT("/Game/__DWC_Automation__/ArtifactStore/%s/T_Runtime"), *Token);
    const FString IntermediatePackage = FString::Printf(
        TEXT("/Game/__DWC_Automation__/ArtifactStore/%s/Temp/T_Intermediate"), *Token);

    TArray<FDWCEditorArtifactTextureRequest> Requests;
    Requests.Add(MakeRequest(*Owner, RuntimePackage, TEXT("T_Runtime"), Pixels));
    Requests.Last().Lifetime = EDWCEditorArtifactLifetime::RuntimeFinal;
    Requests.Add(MakeRequest(*Owner, IntermediatePackage, TEXT("T_Intermediate"), Pixels));
    Requests.Last().Lifetime = EDWCEditorArtifactLifetime::EditorIntermediate;

    TArray<FDWCEditorArtifactCommitReceipt> Receipts;
    FString Error;
    const TSharedRef<FDWCEditorArtifactStore> Store = MakeArtifactStore(1024);
    TestTrue(TEXT("Runtime and intermediate artifacts commit in one batch."),
        Store->CommitTextureBatch(Requests, Receipts, Error));
    TestEqual(TEXT("The lifetime batch publishes two artifacts."), Receipts.Num(), 2);
    if (Receipts.Num() != 2 || Receipts[0].Texture == nullptr || Receipts[1].Texture == nullptr)
    {
        return false;
    }

    TestFalse(TEXT("Runtime-final package flags keep the texture cookable."),
        Receipts[0].Texture->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly));
    TestTrue(TEXT("Intermediate package flags strip the texture from runtime cook."),
        Receipts[1].Texture->GetOutermost()->HasAnyPackageFlags(PKG_EditorOnly));

    TArray<UPackage*> DirtyPackages;
    Store->CollectDirtyPackages(*Owner, DirtyPackages);
    TestTrue(TEXT("Both lifetime classes remain part of the editor save lifecycle."),
        DirtyPackages.Contains(Receipts[0].Texture->GetOutermost()) &&
        DirtyPackages.Contains(Receipts[1].Texture->GetOutermost()));
    Store->NotifyPackagesSaved(DirtyPackages);
    DirtyPackages.Reset();
    Store->CollectDirtyPackages(*Owner, DirtyPackages);
    TestTrue(TEXT("Save acknowledgement clears both lifetime classes."), DirtyPackages.IsEmpty());

    Receipts[0].Texture->GetOutermost()->SetDirtyFlag(false);
    Receipts[1].Texture->GetOutermost()->SetDirtyFlag(false);
    return true;
}

#endif
