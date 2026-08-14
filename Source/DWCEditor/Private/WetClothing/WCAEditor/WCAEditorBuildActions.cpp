//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WCAEditor.h"

#include "DataAssets/WetClothingAsset.h"
#include "Misc/MessageDialog.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionEvaluator.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionRegistry.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildPlanResolver.h"
#include "WetClothing/WCAEditor/Build/WCAEditorCanonicalStateProvider.h"
#include "WetClothing/WCAEditor/UI/SWCAEditorPanel.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"

TSharedRef<SWidget> FWCAEditor::BuildRuntimeBuildMenu()
{
    FWCARuntimeBuildMenuArgs Args;
    const FWCAEditorCanonicalStateSnapshot CanonicalState = BuildCanonicalStateSnapshot(false);
    Args.Snapshot = CanonicalState.BuildStatus;
    Args.RequiredPlan = FDWCEditorBuildPlanResolver::ResolveRequired(Args.Snapshot);
    switch (CurrentMode)
    {
    case EWCAEditorMode::WrinkleEdit:
        Args.SurfaceMode = EDWCEditorBuildSurfaceMode::Wrinkle;
        break;
    case EWCAEditorMode::TransparencyBake:
        Args.SurfaceMode = EDWCEditorBuildSurfaceMode::Transparency;
        break;
    case EWCAEditorMode::PartEdit:
    default:
        Args.SurfaceMode = EDWCEditorBuildSurfaceMode::WetPart;
        break;
    }

    const TWeakPtr<FWCAEditor> WeakEditor = SharedThis(this);
    Args.OnExecuteAction = [WeakEditor](const EDWCEditorBuildAction Action)
    {
        if (const TSharedPtr<FWCAEditor> Editor = WeakEditor.Pin())
        {
            Editor->ExecuteBuildAction(Action);
        }
    };
    Args.OnBuildAllRequired = FSimpleDelegate::CreateLambda([WeakEditor]()
    {
        if (const TSharedPtr<FWCAEditor> Editor = WeakEditor.Pin())
        {
            Editor->HandleBuildAllRequiredClicked();
        }
    });
    return FWCAEditorWidgets::BuildRuntimeBuildMenu(Args);
}

FDWCEditorBuildStatusSnapshot FWCAEditor::BuildBuildStatusSnapshot(const bool bDeepValidation) const
{
    return BuildCanonicalStateSnapshot(bDeepValidation).BuildStatus;
}

FWCAEditorCanonicalStateSnapshot FWCAEditor::BuildCanonicalStateSnapshot(
    const bool bDeepValidation) const
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        FWCAEditorCanonicalStateSnapshot Empty;
        Empty.BuildStatus = FDWCEditorBuildActionEvaluator::Evaluate(FDWCEditorBuildEvaluationInput());
        return Empty;
    }

    EDWCEditorBuildSurfaceMode SurfaceMode = EDWCEditorBuildSurfaceMode::WetPart;
    switch (CurrentMode)
    {
    case EWCAEditorMode::WrinkleEdit:
        SurfaceMode = EDWCEditorBuildSurfaceMode::Wrinkle;
        break;
    case EWCAEditorMode::TransparencyBake:
        SurfaceMode = EDWCEditorBuildSurfaceMode::Transparency;
        break;
    case EWCAEditorMode::PartEdit:
    default:
        break;
    }
    return FWCAEditorCanonicalStateProvider::Build(
        *Asset,
        EditorPanel.Get(),
        SurfaceMode,
        bDeepValidation);
}

bool FWCAEditor::CanExecuteBuildAction(const EDWCEditorBuildAction Action) const
{
    FString BarrierReason;
    if (!EditorPanel.IsValid() || !EditorPanel->CanStartBuildAction(&BarrierReason))
    {
        return false;
    }
    const FDWCEditorBuildStatusSnapshot Snapshot = BuildBuildStatusSnapshot(false);
    const FDWCEditorBuildActionStatus* Status = Snapshot.Find(Action);
    return Status != nullptr && Status->IsExecutable();
}

void FWCAEditor::ExecuteBuildAction(const EDWCEditorBuildAction Action)
{
    if (!CanExecuteBuildAction(Action) || !EditorPanel.IsValid())
    {
        return;
    }

    const FDWCEditorBuildActionDescriptor* Descriptor = FDWCEditorBuildActionRegistry::Find(Action);
    const FString ActionName = Descriptor != nullptr
        ? Descriptor->DisplayName.ToString()
        : TEXT("Build Action");
    const TWeakPtr<FWCAEditor> WeakEditor = SharedThis(this);
    FString RequestError;
    if (!EditorPanel->RequestExclusiveBuild(
            ActionName,
            [WeakEditor, Action, ActionName]()
            {
                const TSharedPtr<FWCAEditor> Editor = WeakEditor.Pin();
                if (!Editor.IsValid())
                {
                    return;
                }

                FString Failure;
                FString Summary;
                if (!Editor->ResolveIssuesAndSave(
                        Failure,
                        &Summary,
                        *ActionName,
                        EDWCEditorBuildPlanPolicy::ExplicitActions,
                        Action))
                {
                    FMessageDialog::Open(
                        EAppMsgCategory::Error,
                        EAppMsgType::Ok,
                        FText::FromString(Failure.IsEmpty()
                            ? FString::Printf(TEXT("%s failed."), *ActionName)
                            : Failure));
                }
                else
                {
                    FMessageDialog::Open(
                        EAppMsgCategory::Success,
                        EAppMsgType::Ok,
                        FText::FromString(Summary.IsEmpty() ? ActionName : Summary));
                }
                Editor->RefreshAssetStateAndEditor();
            },
            &RequestError))
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            FText::FromString(RequestError.IsEmpty()
                ? FString::Printf(TEXT("%s cannot start while another Build is active."), *ActionName)
                : RequestError));
    }
}

FReply FWCAEditor::HandleBuildAllRequiredClicked()
{
    if (WetClothingAsset.Get() == nullptr || !EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    const TWeakPtr<FWCAEditor> WeakEditor = SharedThis(this);
    FString RequestError;
    if (!EditorPanel->RequestExclusiveBuild(
            TEXT("Build All Required"),
            [WeakEditor]()
            {
                if (const TSharedPtr<FWCAEditor> Editor = WeakEditor.Pin())
                {
                    Editor->ExecuteBuildAllRequiredExclusive();
                }
            },
            &RequestError))
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            FText::FromString(RequestError.IsEmpty()
                ? TEXT("Build All Required cannot start while another Build is active.")
                : RequestError));
    }
    return FReply::Handled();
}

void FWCAEditor::ExecuteBuildAllRequiredExclusive()
{
    if (WetClothingAsset.Get() == nullptr || !EditorPanel.IsValid())
    {
        return;
    }

    FString Failure;
    FString SuccessSummary;
    if (!ResolveIssuesAndSave(
            Failure,
            &SuccessSummary,
            TEXT("Build All Required"),
            EDWCEditorBuildPlanPolicy::AllRequired))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(
            EAppMsgCategory::Error,
            EAppMsgType::Ok,
            FText::FromString(Failure.IsEmpty()
                ? TEXT("Required runtime outputs could not be completed.")
                : Failure));
        return;
    }

    RefreshAssetStateAndEditor();
    const FString SuccessMessage = SuccessSummary.IsEmpty()
        ? FString(TEXT("All required runtime outputs are up to date."))
        : FString::Printf(TEXT("All required runtime outputs were completed.\n\n%s"), *SuccessSummary);
    FMessageDialog::Open(
        EAppMsgCategory::Success,
        EAppMsgType::Ok,
        FText::FromString(SuccessMessage));
}
