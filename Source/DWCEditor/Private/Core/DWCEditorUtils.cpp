#include "DWCEditorUtils.h"

#include "FileHelpers.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/ScopedSlowTask.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
    FOnDWCEditorAssetSaved GDWCEditorAssetSaved;
    FOnDWCEditorAssetSaveAttemptFinished GDWCEditorAssetSaveAttemptFinished;

    void ShowDWCEditorNotification(const FText& Message, const SNotificationItem::ECompletionState CompletionState)
    {
        FNotificationInfo Info(Message);
        Info.ExpireDuration = 3.0f;
        Info.bUseSuccessFailIcons = true;

        if (TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
        {
            Notification->SetCompletionState(CompletionState);
        }
    }
}

FOnDWCEditorAssetSaved& DWCEditorUtils::OnAssetSaved()
{
    return GDWCEditorAssetSaved;
}

FOnDWCEditorAssetSaveAttemptFinished& DWCEditorUtils::OnAssetSaveAttemptFinished()
{
    return GDWCEditorAssetSaveAttemptFinished;
}

bool DWCEditorUtils::SaveAsset(UObject* Asset)
{
    if (Asset == nullptr)
    {
        return false;
    }

    UWetClothingAsset* WetClothingAsset = Cast<UWetClothingAsset>(Asset);
    if (WetClothingAsset != nullptr)
    {
        WetClothingAsset->BeginRuntimeDataEditorSaveAttempt();
    }

    TUniquePtr<FScopedSlowTask> SaveSlowTask;
    if (WetClothingAsset != nullptr)
    {
        SaveSlowTask = MakeUnique<FScopedSlowTask>(
            2.0f,
            FText::FromString(FString::Printf(TEXT("Saving %s..."), *GetNameSafe(WetClothingAsset))));
        SaveSlowTask->MakeDialog(false);
        SaveSlowTask->EnterProgressFrame(
            1.0f,
            FText::FromString(TEXT("Collecting asset packages to save...")));
    }

    UPackage* Package = Asset->GetOutermost();
    if (Package == nullptr)
    {
        if (WetClothingAsset != nullptr)
        {
            WetClothingAsset->CompleteRuntimeDataEditorSaveAttempt(false);
        }
        GDWCEditorAssetSaveAttemptFinished.Broadcast(Asset, false);
        return false;
    }

    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Add(Package);
    if (WetClothingAsset != nullptr)
    {
        if (USkeletalMesh* GeneratedDataUV = WetClothingAsset->GetRuntimeSkeletalMesh())
        {
            if (UPackage* RuntimeMeshPackage = GeneratedDataUV->GetOutermost();
                RuntimeMeshPackage != nullptr &&
                RuntimeMeshPackage != Package &&
                RuntimeMeshPackage->IsDirty())
            {
                PackagesToSave.AddUnique(RuntimeMeshPackage);
            }
        }
    }

    if (SaveSlowTask.IsValid())
    {
        SaveSlowTask->EnterProgressFrame(
            1.0f,
            FText::FromString(TEXT("Saving Wet Clothing Asset packages...")));
    }

    const bool bSaved = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) == FEditorFileUtils::PR_Success;
    if (WetClothingAsset != nullptr)
    {
        WetClothingAsset->CompleteRuntimeDataEditorSaveAttempt(bSaved);
    }
    if (bSaved)
    {
        if (WetClothingAsset != nullptr)
        {
            WetClothingAsset->RefreshBakeState(false);
            ShowDWCEditorNotification(
                FText::FromString(TEXT("Wet Clothing Asset saved.")),
                SNotificationItem::CS_Success);
        }
        GDWCEditorAssetSaved.Broadcast(Asset);
    }
    GDWCEditorAssetSaveAttemptFinished.Broadcast(Asset, bSaved);
    return bSaved;
}
