#include "DWCEditorUtils.h"

#include "FileHelpers.h"
#include "DataAssets/WetClothingAsset.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
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

bool DWCEditorUtils::SaveAsset(UObject* Asset)
{
    if (Asset == nullptr)
    {
        return false;
    }

    if (UWetClothingAsset* WetClothingAsset = Cast<UWetClothingAsset>(Asset))
    {
        if (WetClothingAsset->TargetMesh == nullptr)
        {
            UE_LOG(LogTemp, Warning, TEXT("DWCEditor: Saving %s without TargetMesh. Precomputed simulation data was not updated."), *GetNameSafe(WetClothingAsset));
        }
        else if (!WetClothingAsset->IsPrecomputedSimulationDataValidForMesh(WetClothingAsset->TargetMesh, 0))
        {
            FString ErrorMessage;
            if (!WetClothingAsset->RebuildPrecomputedSimulationData(&ErrorMessage, 0))
            {
                UE_LOG(
                    LogTemp,
                    Error,
                    TEXT("DWCEditor: Failed to update precomputed simulation data before saving %s. %s"),
                    *GetNameSafe(WetClothingAsset),
                    *ErrorMessage);

                ShowDWCEditorNotification(
                    FText::FromString(TEXT("Failed to update runtime-ready data. Asset was not saved.")),
                    SNotificationItem::CS_Fail);
                return false;
            }

            ShowDWCEditorNotification(
                FText::FromString(TEXT("Runtime-ready data updated.")),
                SNotificationItem::CS_Success);
        }
    }

    UPackage* Package = Asset->GetOutermost();
    if (Package == nullptr)
    {
        return false;
    }

    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Add(Package);
    return FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) == FEditorFileUtils::PR_Success;
}
