#include "Components/DynamicWetClothesComponentCustomization.h"

#include "Components/DynamicWetClothesComponent.h"
#include "Async/Async.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "IPropertyUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "TextureResource.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/StrongObjectPtr.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DynamicWetClothesComponentCustomization"

namespace
{
    enum class EDWCDebugRTKind : uint8
    {
        Wetness,
        Droplets,
        Rivulets
    };

    enum class EDWCDebugChannel : uint8
    {
        RGBA,
        R,
        G,
        B,
        A
    };

    UBlueprint* FindOwningBlueprint(const UDynamicWetClothesComponent* DWC)
    {
        if (DWC == nullptr)
        {
            return nullptr;
        }
        if (UBlueprint* Blueprint = DWC->GetTypedOuter<UBlueprint>())
        {
            return Blueprint;
        }
        if (const AActor* Owner = DWC->GetOwner())
        {
            return Cast<UBlueprint>(Owner->GetClass()->ClassGeneratedBy);
        }
        if (const UClass* OuterClass = DWC->GetTypedOuter<UClass>())
        {
            return Cast<UBlueprint>(OuterClass->ClassGeneratedBy);
        }
        return nullptr;
    }

    void CollectCandidateMeshes(const UDynamicWetClothesComponent* DWC, TArray<USkeletalMeshComponent*>& OutCandidates)
    {
        OutCandidates.Reset();
        if (DWC == nullptr)
        {
            return;
        }

        auto AddOwnerMeshes = [&OutCandidates](AActor* Owner)
        {
            if (Owner == nullptr)
            {
                return;
            }
            TArray<USkeletalMeshComponent*> OwnerMeshes;
            Owner->GetComponents<USkeletalMeshComponent>(OwnerMeshes);
            for (USkeletalMeshComponent* Mesh : OwnerMeshes)
            {
                OutCandidates.AddUnique(Mesh);
            }
        };

        if (!DWC->IsTemplate())
        {
            AddOwnerMeshes(DWC->GetOwner());
            return;
        }

        UBlueprint* Blueprint = FindOwningBlueprint(DWC);
        if (Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr)
        {
            for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (Node != nullptr)
                {
                    if (USkeletalMeshComponent* MeshTemplate = Cast<USkeletalMeshComponent>(Node->ComponentTemplate))
                    {
                        OutCandidates.AddUnique(MeshTemplate);
                    }
                }
            }
        }

        // Include inherited/default components even when the current Blueprint also has SCS nodes.
        if (Blueprint != nullptr && Blueprint->GeneratedClass != nullptr)
        {
            AddOwnerMeshes(Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()));
        }
        AddOwnerMeshes(DWC->GetOwner());
    }

    FText MeshPathText(const UObject* Object)
    {
        return Object != nullptr ? FText::FromString(Object->GetPathName()) : LOCTEXT("None", "None");
    }

    FText GetRTKindLabel(const EDWCDebugRTKind Kind)
    {
        switch (Kind)
        {
        case EDWCDebugRTKind::Wetness:
            return LOCTEXT("DebugRTWetness", "Wetness");
        case EDWCDebugRTKind::Droplets:
            return LOCTEXT("DebugRTDroplets", "Droplets");
        case EDWCDebugRTKind::Rivulets:
            return LOCTEXT("DebugRTRivulets", "Rivulets");
        default:
            return FText::GetEmpty();
        }
    }

    FText GetChannelLabel(const EDWCDebugChannel Channel)
    {
        switch (Channel)
        {
        case EDWCDebugChannel::RGBA:
            return LOCTEXT("DebugChannelRGBA", "RGBA");
        case EDWCDebugChannel::R:
            return LOCTEXT("DebugChannelR", "R");
        case EDWCDebugChannel::G:
            return LOCTEXT("DebugChannelG", "G");
        case EDWCDebugChannel::B:
            return LOCTEXT("DebugChannelB", "B");
        case EDWCDebugChannel::A:
            return LOCTEXT("DebugChannelA", "A");
        default:
            return FText::GetEmpty();
        }
    }

    bool IsLiveRuntimeWorld(const UWorld* World)
    {
        return World != nullptr &&
               (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game);
    }

    FString GetComponentDebugLabel(const UDynamicWetClothesComponent* DWC)
    {
        if (DWC == nullptr)
        {
            return FString();
        }

        const AActor* Owner = DWC->GetOwner();
        const FString OwnerName = Owner != nullptr ? Owner->GetActorLabel() : GetNameSafe(DWC->GetOuter());
        return FString::Printf(TEXT("%s / %s"), *OwnerName, *GetNameSafe(DWC));
    }

    int32 ScoreRuntimeComponentMatch(
        const UDynamicWetClothesComponent* RuntimeComponent,
        const UDynamicWetClothesComponent* SourceComponent)
    {
        if (RuntimeComponent == nullptr || SourceComponent == nullptr)
        {
            return 0;
        }

        int32 Score = 0;
        if (RuntimeComponent->GetFName() == SourceComponent->GetFName())
        {
            Score += 8;
        }

        const AActor* RuntimeOwner = RuntimeComponent->GetOwner();
        const AActor* SourceOwner = SourceComponent->GetOwner();
        if (RuntimeOwner != nullptr && SourceOwner != nullptr)
        {
            if (RuntimeOwner->GetFName() == SourceOwner->GetFName())
            {
                Score += 8;
            }
            if (RuntimeOwner->GetActorLabel() == SourceOwner->GetActorLabel())
            {
                Score += 12;
            }
        }

        for (const TObjectPtr<UWetClothingAsset>& RuntimeAsset : RuntimeComponent->WetClothingAssets)
        {
            if (RuntimeAsset != nullptr && SourceComponent->WetClothingAssets.Contains(RuntimeAsset))
            {
                Score += 4;
            }
        }

        return Score;
    }

    class SDWCGPURenderTargetDebugger final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SDWCGPURenderTargetDebugger) {}
            SLATE_ARGUMENT(TWeakObjectPtr<UDynamicWetClothesComponent>, Component)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            Component = InArgs._Component;
            PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
            PreviewBrush.Tiling = ESlateBrushTileType::NoTile;
            PreviewBrush.Mirroring = ESlateBrushMirrorType::NoMirror;

            RebuildSnapshots();

            ChildSlot
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(8.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(this, &SDWCGPURenderTargetDebugger::GetSourceSummaryText)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("DebugRTRefresh", "Refresh"))
                        .OnClicked(this, &SDWCGPURenderTargetDebugger::HandleRefreshClicked)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(8.0f, 0.0f, 8.0f, 8.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("DebugRTSlotLabel", "Slot"))
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .MinWidth(260.0f)
                    [
                        SAssignNew(SnapshotComboBox, SComboBox<TSharedPtr<int32>>)
                        .OptionsSource(&SnapshotOptions)
                        .OnGenerateWidget(this, &SDWCGPURenderTargetDebugger::GenerateSnapshotComboRow)
                        .OnSelectionChanged(this, &SDWCGPURenderTargetDebugger::HandleSnapshotSelectionChanged)
                        .InitiallySelectedItem(SelectedSnapshotOption)
                        [
                            SNew(STextBlock)
                            .Text(this, &SDWCGPURenderTargetDebugger::GetSelectedSnapshotText)
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(12.0f, 0.0f, 0.0f, 0.0f)
                    [
                        BuildRTKindButton(EDWCDebugRTKind::Wetness)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        BuildRTKindButton(EDWCDebugRTKind::Droplets)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        BuildRTKindButton(EDWCDebugRTKind::Rivulets)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(12.0f, 0.0f, 0.0f, 0.0f)
                    [
                        BuildChannelButton(EDWCDebugChannel::RGBA)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        BuildChannelButton(EDWCDebugChannel::R)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        BuildChannelButton(EDWCDebugChannel::G)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        BuildChannelButton(EDWCDebugChannel::B)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        BuildChannelButton(EDWCDebugChannel::A)
                    ]
                ]
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                .Padding(8.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                    .Padding(8.0f)
                    [
                        SNew(SOverlay)
                        + SOverlay::Slot()
                        [
                            SNew(SScrollBox)
                            .Orientation(Orient_Horizontal)
                            + SScrollBox::Slot()
                            [
                                SNew(SScrollBox)
                                .Orientation(Orient_Vertical)
                                + SScrollBox::Slot()
                                [
                                    SAssignNew(PreviewImage, SImage)
                                    .Image(&PreviewBrush)
                                    .Visibility(this, &SDWCGPURenderTargetDebugger::GetPreviewVisibility)
                                ]
                            ]
                        ]
                        + SOverlay::Slot()
                        .HAlign(HAlign_Center)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .AutoWrapText(true)
                            .Justification(ETextJustify::Center)
                            .Text(this, &SDWCGPURenderTargetDebugger::GetEmptyStateText)
                            .Visibility(this, &SDWCGPURenderTargetDebugger::GetEmptyStateVisibility)
                        ]
                    ]
                ]
            ];

            UpdatePreview(true);
        }

        virtual void Tick(
            const FGeometry& AllottedGeometry,
            const double InCurrentTime,
            const float InDeltaTime) override
        {
            SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
            if (InCurrentTime - LastSnapshotRefreshTime > 0.5)
            {
                RebuildSnapshots();
                LastSnapshotRefreshTime = InCurrentTime;
            }
            if (InCurrentTime - LastPreviewRefreshTime > 0.1)
            {
                UpdatePreview(false);
                LastPreviewRefreshTime = InCurrentTime;
            }
        }

    private:
        void RebuildSnapshots()
        {
            TArray<FDWCGPURenderTargetDebugSnapshot> NewSnapshots;
            if (const UDynamicWetClothesComponent* DWC = Component.Get())
            {
                DWC->GetGPUDebugRenderTargets(NewSnapshots);
                if (!NewSnapshots.IsEmpty())
                {
                    SnapshotSourceDescription = FText::Format(
                        LOCTEXT("DebugRTDirectSource", "Source: {0}"),
                        FText::FromString(GetComponentDebugLabel(DWC)));
                }
            }

            if (NewSnapshots.IsEmpty())
            {
                GatherRuntimeSnapshots(NewSnapshots);
            }

            const int32 PreviousMaterialSlot = Snapshots.IsValidIndex(SelectedSnapshotIndex)
                ? Snapshots[SelectedSnapshotIndex].MaterialSlotIndex
                : INDEX_NONE;
            const FName PreviousReceiverId = Snapshots.IsValidIndex(SelectedSnapshotIndex)
                ? Snapshots[SelectedSnapshotIndex].ReceiverId
                : NAME_None;

            Snapshots = MoveTemp(NewSnapshots);
            SnapshotOptions.Reset();
            SelectedSnapshotOption.Reset();
            SelectedSnapshotIndex = INDEX_NONE;

            for (int32 Index = 0; Index < Snapshots.Num(); ++Index)
            {
                TSharedPtr<int32> Option = MakeShared<int32>(Index);
                SnapshotOptions.Add(Option);
                if (!SelectedSnapshotOption.IsValid() ||
                    (Snapshots[Index].ReceiverId == PreviousReceiverId &&
                     Snapshots[Index].MaterialSlotIndex == PreviousMaterialSlot))
                {
                    SelectedSnapshotIndex = Index;
                    SelectedSnapshotOption = Option;
                }
            }

            if (SnapshotComboBox.IsValid())
            {
                SnapshotComboBox->RefreshOptions();
                SnapshotComboBox->SetSelectedItem(SelectedSnapshotOption);
            }
        }

        void GatherRuntimeSnapshots(TArray<FDWCGPURenderTargetDebugSnapshot>& OutSnapshots)
        {
            const UDynamicWetClothesComponent* SourceComponent = Component.Get();
            TArray<FDWCGPURenderTargetDebugSnapshot> BestMatchedSnapshots;
            TArray<FDWCGPURenderTargetDebugSnapshot> FallbackSnapshots;
            FString BestMatchedLabel;
            int32 BestMatchScore = 0;
            int32 RuntimeComponentCount = 0;

            for (TObjectIterator<UDynamicWetClothesComponent> It; It; ++It)
            {
                UDynamicWetClothesComponent* RuntimeComponent = *It;
                if (RuntimeComponent == nullptr ||
                    RuntimeComponent->IsTemplate() ||
                    !IsLiveRuntimeWorld(RuntimeComponent->GetWorld()))
                {
                    continue;
                }

                TArray<FDWCGPURenderTargetDebugSnapshot> CandidateSnapshots;
                RuntimeComponent->GetGPUDebugRenderTargets(CandidateSnapshots);
                if (CandidateSnapshots.IsEmpty())
                {
                    continue;
                }

                ++RuntimeComponentCount;
                FallbackSnapshots.Append(CandidateSnapshots);

                const int32 MatchScore = ScoreRuntimeComponentMatch(RuntimeComponent, SourceComponent);
                if (MatchScore > BestMatchScore)
                {
                    BestMatchScore = MatchScore;
                    BestMatchedSnapshots = MoveTemp(CandidateSnapshots);
                    BestMatchedLabel = GetComponentDebugLabel(RuntimeComponent);
                }
            }

            if (!BestMatchedSnapshots.IsEmpty())
            {
                OutSnapshots = MoveTemp(BestMatchedSnapshots);
                SnapshotSourceDescription = FText::Format(
                    LOCTEXT("DebugRTPIESourceMatched", "Source: PIE {0}"),
                    FText::FromString(BestMatchedLabel));
                return;
            }

            OutSnapshots = MoveTemp(FallbackSnapshots);
            SnapshotSourceDescription = OutSnapshots.IsEmpty()
                ? LOCTEXT("DebugRTNoRuntimeSource", "Source: no live PIE GPU component found")
                : FText::Format(
                    LOCTEXT("DebugRTPIESourceFallback", "Source: {0} live PIE GPU component(s)"),
                    FText::AsNumber(RuntimeComponentCount));
        }

        UTextureRenderTarget2D* GetSelectedRenderTarget() const
        {
            if (!Snapshots.IsValidIndex(SelectedSnapshotIndex))
            {
                return nullptr;
            }

            const FDWCGPURenderTargetDebugSnapshot& Snapshot = Snapshots[SelectedSnapshotIndex];
            switch (SelectedRTKind)
            {
            case EDWCDebugRTKind::Wetness:
                return Snapshot.WetnessMap.Get();
            case EDWCDebugRTKind::Droplets:
                return Snapshot.DropletsMap.Get();
            case EDWCDebugRTKind::Rivulets:
                return Snapshot.RivuletsMap.Get();
            default:
                return nullptr;
            }
        }

        void UpdatePreview(const bool bForce)
        {
            UTextureRenderTarget2D* SourceRT = GetSelectedRenderTarget();
            if (SourceRT == nullptr)
            {
                ResetPreview();
                return;
            }

            const int32 Width = SourceRT->SizeX;
            const int32 Height = SourceRT->SizeY;
            if (Width <= 0 || Height <= 0)
            {
                ResetPreview();
                return;
            }

            if (bForce || SourceRT != LastSourceRT.Get() || Width != PreviewSize.X || Height != PreviewSize.Y)
            {
                CreatePreviewTexture(Width, Height);
                LastSourceRT = SourceRT;
            }

            FTextureRenderTargetResource* Resource = SourceRT->GameThread_GetRenderTargetResource();
            if (Resource == nullptr || !PreviewTexture.IsValid())
            {
                ResetPreview();
                return;
            }

            TArray<FLinearColor> SourcePixels;
            if (!Resource->ReadLinearColorPixels(SourcePixels) || SourcePixels.Num() != Width * Height)
            {
                return;
            }

            TArray<uint8> PreviewPixels;
            PreviewPixels.SetNumUninitialized(Width * Height * 4);
            for (int32 PixelIndex = 0; PixelIndex < SourcePixels.Num(); ++PixelIndex)
            {
                const FLinearColor& Pixel = SourcePixels[PixelIndex];
                FLinearColor DisplayPixel = Pixel;
                if (SelectedChannel != EDWCDebugChannel::RGBA)
                {
                    float ChannelValue = 0.0f;
                    switch (SelectedChannel)
                    {
                    case EDWCDebugChannel::R:
                        ChannelValue = Pixel.R;
                        break;
                    case EDWCDebugChannel::G:
                        ChannelValue = Pixel.G;
                        break;
                    case EDWCDebugChannel::B:
                        ChannelValue = Pixel.B;
                        break;
                    case EDWCDebugChannel::A:
                        ChannelValue = Pixel.A;
                        break;
                    default:
                        break;
                    }
                    DisplayPixel = FLinearColor(ChannelValue, ChannelValue, ChannelValue, 1.0f);
                }
                else if (SelectedRTKind == EDWCDebugRTKind::Wetness)
                {
                    DisplayPixel = FLinearColor(Pixel.R, Pixel.R, Pixel.R, 1.0f);
                }
                DisplayPixel.A = 1.0f;

                const FColor Color = DisplayPixel.GetClamped().ToFColor(false);
                const int32 OutIndex = PixelIndex * 4;
                PreviewPixels[OutIndex + 0] = Color.B;
                PreviewPixels[OutIndex + 1] = Color.G;
                PreviewPixels[OutIndex + 2] = Color.R;
                PreviewPixels[OutIndex + 3] = Color.A;
            }

            UploadPreviewPixels(MoveTemp(PreviewPixels), Width, Height);
        }

        void CreatePreviewTexture(const int32 Width, const int32 Height)
        {
            UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
            Texture->NeverStream = true;
            Texture->SRGB = false;
            Texture->Filter = TF_Nearest;
            Texture->UpdateResource();

            PreviewTexture.Reset(Texture);
            PreviewSize = FIntPoint(Width, Height);
            PreviewBrush.SetResourceObject(Texture);
            PreviewBrush.ImageSize = FVector2D(
                static_cast<double>(Width),
                static_cast<double>(Height));
            if (PreviewImage.IsValid())
            {
                PreviewImage->Invalidate(EInvalidateWidgetReason::PaintAndVolatility);
            }
        }

        void UploadPreviewPixels(TArray<uint8>&& Pixels, const int32 Width, const int32 Height)
        {
            if (!PreviewTexture.IsValid() || Pixels.IsEmpty())
            {
                return;
            }

            FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
            TArray<uint8>* UploadPixels = new TArray<uint8>(MoveTemp(Pixels));
            PreviewTexture->UpdateTextureRegions(
                0,
                1,
                Region,
                Width * 4,
                4,
                UploadPixels->GetData(),
                [UploadPixels](uint8*, const FUpdateTextureRegion2D* Regions)
                {
                    delete UploadPixels;
                    delete Regions;
                });
        }

        void ResetPreview()
        {
            PreviewTexture.Reset();
            LastSourceRT.Reset();
            PreviewSize = FIntPoint::ZeroValue;
            PreviewBrush.SetResourceObject(nullptr);
            PreviewBrush.ImageSize = FVector2D::ZeroVector;
        }

        TSharedRef<SWidget> BuildRTKindButton(const EDWCDebugRTKind Kind)
        {
            return SNew(SCheckBox)
                .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
                .Type(ESlateCheckBoxType::ToggleButton)
                .IsChecked_Lambda([this, Kind]()
                {
                    return SelectedRTKind == Kind ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this, Kind](ECheckBoxState)
                {
                    SelectedRTKind = Kind;
                    UpdatePreview(true);
                })
                [
                    SNew(STextBlock)
                    .Text(GetRTKindLabel(Kind))
                ];
        }

        TSharedRef<SWidget> BuildChannelButton(const EDWCDebugChannel Channel)
        {
            return SNew(SCheckBox)
                .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
                .Type(ESlateCheckBoxType::ToggleButton)
                .IsChecked_Lambda([this, Channel]()
                {
                    return SelectedChannel == Channel ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this, Channel](ECheckBoxState)
                {
                    SelectedChannel = Channel;
                    UpdatePreview(true);
                })
                [
                    SNew(STextBlock)
                    .Text(GetChannelLabel(Channel))
                ];
        }

        TSharedRef<SWidget> GenerateSnapshotComboRow(TSharedPtr<int32> Item) const
        {
            return SNew(STextBlock).Text(GetSnapshotText(Item));
        }

        void HandleSnapshotSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type)
        {
            SelectedSnapshotOption = Item;
            SelectedSnapshotIndex = Item.IsValid() ? *Item : INDEX_NONE;
            UpdatePreview(true);
        }

        FReply HandleRefreshClicked()
        {
            RebuildSnapshots();
            UpdatePreview(true);
            return FReply::Handled();
        }

        FText GetSnapshotText(TSharedPtr<int32> Item) const
        {
            if (!Item.IsValid() || !Snapshots.IsValidIndex(*Item))
            {
                return LOCTEXT("DebugRTNoSlot", "No slot");
            }

            const FDWCGPURenderTargetDebugSnapshot& Snapshot = Snapshots[*Item];
            return FText::Format(
                LOCTEXT("DebugRTSlotFormat", "{0} / Slot {1}"),
                Snapshot.ReceiverId.IsNone()
                    ? FText::Format(LOCTEXT("DebugRTReceiverGPUId", "GPU {0}"), FText::AsNumber(Snapshot.ReceiverGPUId))
                    : FText::FromName(Snapshot.ReceiverId),
                FText::AsNumber(Snapshot.MaterialSlotIndex));
        }

        FText GetSelectedSnapshotText() const
        {
            return GetSnapshotText(SelectedSnapshotOption);
        }

        FText GetSourceSummaryText() const
        {
            const UTextureRenderTarget2D* SourceRT = GetSelectedRenderTarget();
            if (SourceRT == nullptr)
            {
                return SnapshotSourceDescription;
            }

            return FText::Format(
                LOCTEXT("DebugRTSourceSummary", "{0} / {1} / {2} / {3}x{4}"),
                SnapshotSourceDescription,
                GetRTKindLabel(SelectedRTKind),
                GetChannelLabel(SelectedChannel),
                FText::AsNumber(SourceRT->SizeX),
                FText::AsNumber(SourceRT->SizeY));
        }

        FText GetEmptyStateText() const
        {
            if (Snapshots.IsEmpty())
            {
                return LOCTEXT("DebugRTNoSnapshots", "No live GPU RTs found. Start PIE with WetnessMapGPU simulation and an initialized receiver.");
            }
            return LOCTEXT("DebugRTNoSelectedRT", "This slot does not have the selected surface RT.");
        }

        EVisibility GetPreviewVisibility() const
        {
            return PreviewTexture.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
        }

        EVisibility GetEmptyStateVisibility() const
        {
            return PreviewTexture.IsValid() ? EVisibility::Collapsed : EVisibility::Visible;
        }

        TWeakObjectPtr<UDynamicWetClothesComponent> Component;
        TArray<FDWCGPURenderTargetDebugSnapshot> Snapshots;
        TArray<TSharedPtr<int32>> SnapshotOptions;
        TSharedPtr<int32> SelectedSnapshotOption;
        TSharedPtr<SComboBox<TSharedPtr<int32>>> SnapshotComboBox;
        TSharedPtr<SImage> PreviewImage;
        TStrongObjectPtr<UTexture2D> PreviewTexture;
        TWeakObjectPtr<UTextureRenderTarget2D> LastSourceRT;
        FSlateBrush PreviewBrush;
        FIntPoint PreviewSize = FIntPoint::ZeroValue;
        FText SnapshotSourceDescription = LOCTEXT("DebugRTInitialSource", "Source: searching");
        EDWCDebugRTKind SelectedRTKind = EDWCDebugRTKind::Wetness;
        EDWCDebugChannel SelectedChannel = EDWCDebugChannel::RGBA;
        int32 SelectedSnapshotIndex = INDEX_NONE;
        double LastSnapshotRefreshTime = 0.0;
        double LastPreviewRefreshTime = 0.0;
    };
}

TSharedRef<IDetailCustomization> FDynamicWetClothesComponentCustomization::MakeInstance()
{
    return MakeShared<FDynamicWetClothesComponentCustomization>();
}

FDynamicWetClothesComponentCustomization::~FDynamicWetClothesComponentCustomization()
{
    if (ObjectPropertyChangedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
    }
}

void FDynamicWetClothesComponentCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    if (Objects.Num() != 1)
    {
        return;
    }

    Component = Cast<UDynamicWetClothesComponent>(Objects[0].Get());
    PropertyUtilities = DetailBuilder.GetPropertyUtilities();
    if (!ObjectPropertyChangedHandle.IsValid())
    {
        ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
            this,
            &FDynamicWetClothesComponentCustomization::HandleObjectPropertyChanged);
    }
    RebuildBindingStatuses();

    const TSharedRef<IPropertyHandle> AssetsHandle = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetClothingAssets));
    AssetsHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FDynamicWetClothesComponentCustomization::RequestRefresh));
    AssetsHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FDynamicWetClothesComponentCustomization::RequestRefresh));
    if (const TSharedPtr<IPropertyHandleArray> ArrayHandle = AssetsHandle->AsArray())
    {
        ArrayHandle->SetOnNumElementsChanged(FSimpleDelegate::CreateRaw(this, &FDynamicWetClothesComponentCustomization::RequestRefresh));
    }

    IDetailCategoryBuilder& DebugCategory = DetailBuilder.EditCategory(TEXT("Wetness|Debug"));
    DebugCategory.AddCustomRow(LOCTEXT("DWCOpenGPURTDebuggerFilter", "GPU RT Debugger"))
    .WholeRowContent()
    [
        SNew(SButton)
        .Text(LOCTEXT("DWCOpenGPURTDebugger", "Open GPU RT Debugger"))
        .ToolTipText(LOCTEXT("DWCOpenGPURTDebuggerTooltip", "Open a live UV-space preview for GPU wetness, droplets, and rivulet render targets."))
        .IsEnabled(this, &FDynamicWetClothesComponentCustomization::CanOpenGPURenderTargetDebugger)
        .OnClicked(this, &FDynamicWetClothesComponentCustomization::HandleOpenGPURenderTargetDebugger)
    ];

    IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Wetness"));
    Category.AddCustomRow(LOCTEXT("DWCBindingHeaderFilter", "DWC Mesh Bindings"))
    .WholeRowContent()
    [
        SNew(STextBlock)
        .Text(LOCTEXT("DWCBindingHeader", "Wet Clothing Asset Bindings"))
        .Font(FAppStyle::GetFontStyle(TEXT("DetailsView.CategoryFontStyle")))
    ];

    if (CachedBindingStatuses.IsEmpty())
    {
        Category.AddCustomRow(LOCTEXT("NoWCAFilter", "No Wet Clothing Assets"))
        .WholeRowContent()
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text(LOCTEXT("NoWCA", "Add one or more Wet Clothing Assets. Each matching SkeletalMeshComponent will become a runtime receiver."))
        ];
        return;
    }

    for (int32 BindingIndex = 0; BindingIndex < CachedBindingStatuses.Num(); ++BindingIndex)
    {
        Category.AddCustomRow(FText::FromString(FString::Printf(TEXT("DWC Binding %d"), BindingIndex)))
        .WholeRowContent()
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            .Padding(8.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Top)
                    .Padding(0.0f, 1.0f, 8.0f, 0.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))
                        .Visibility(this, &FDynamicWetClothesComponentCustomization::GetBindingWarningVisibility, BindingIndex)
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(STextBlock)
                        .AutoWrapText(true)
                        .Text(this, &FDynamicWetClothesComponentCustomization::GetBindingText, BindingIndex)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Apply", "Apply"))
                        .Visibility(this, &FDynamicWetClothesComponentCustomization::GetBindingApplyVisibility, BindingIndex)
                        .IsEnabled(this, &FDynamicWetClothesComponentCustomization::CanApplyBinding, BindingIndex)
                        .OnClicked(this, &FDynamicWetClothesComponentCustomization::HandleApplyBinding, BindingIndex)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(24.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(this, &FDynamicWetClothesComponentCustomization::GetBindingStateText, BindingIndex)
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                ]
            ]
        ];
    }

    Category.AddCustomRow(LOCTEXT("ApplyAllFilter", "Apply All DWC Meshes"))
    .WholeRowContent()
    [
        SNew(SButton)
        .Text(LOCTEXT("ApplyAll", "Apply All"))
        .ToolTipText(LOCTEXT("ApplyAllTooltip", "Replace every unambiguous Source Skeletal Mesh reference with the DWC Skeletal Mesh required by its WCA."))
        .IsEnabled(this, &FDynamicWetClothesComponentCustomization::CanApplyAll)
        .OnClicked(this, &FDynamicWetClothesComponentCustomization::HandleApplyAll)
    ];
}

void FDynamicWetClothesComponentCustomization::RebuildBindingStatuses()
{
    CachedBindingStatuses.Reset();
    UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr)
    {
        return;
    }

    TArray<USkeletalMeshComponent*> Candidates;
    CollectCandidateMeshes(DWC, Candidates);
    TSet<UWetClothingAsset*> SeenAssets;

    for (UWetClothingAsset* Asset : DWC->WetClothingAssets)
    {
        if (Asset == nullptr)
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.State = EBindingState::MissingAsset;
            continue;
        }

        if (SeenAssets.Contains(Asset))
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.State = EBindingState::DuplicateAsset;
            continue;
        }
        SeenAssets.Add(Asset);

        if (!Asset->IsCurrentAssetDataVersion())
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.State = EBindingState::UnsupportedAssetVersion;
            continue;
        }

        USkeletalMesh* SourceMesh = Asset->GetSourceSkeletalMesh();
        USkeletalMesh* RequiredMesh = Asset->GetDWCSkeletalMesh();
        if (SourceMesh == nullptr)
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.RequiredMesh = RequiredMesh;
            Status.State = EBindingState::MissingSourceMesh;
            continue;
        }
        if (RequiredMesh == nullptr)
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.SourceMesh = SourceMesh;
            Status.State = EBindingState::MissingDWCMesh;
            continue;
        }

        bool bAddedMatch = false;
        for (USkeletalMeshComponent* Candidate : Candidates)
        {
            if (Candidate == nullptr)
            {
                continue;
            }
            USkeletalMesh* CurrentMesh = Candidate->GetSkeletalMeshAsset();
            if (CurrentMesh != RequiredMesh && CurrentMesh != SourceMesh)
            {
                continue;
            }

            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.MeshComponent = Candidate;
            Status.CurrentMesh = CurrentMesh;
            Status.SourceMesh = SourceMesh;
            Status.RequiredMesh = RequiredMesh;
            Status.State = CurrentMesh == RequiredMesh ? EBindingState::Ready : EBindingState::NeedsApply;
            bAddedMatch = true;
        }

        if (!bAddedMatch)
        {
            FBindingStatus& Status = CachedBindingStatuses.AddDefaulted_GetRef();
            Status.Asset = Asset;
            Status.SourceMesh = SourceMesh;
            Status.RequiredMesh = RequiredMesh;
            Status.State = EBindingState::NoMatchingComponent;
        }
    }

    TMap<USkeletalMeshComponent*, UWetClothingAsset*> FirstClaim;
    TSet<USkeletalMeshComponent*> ConflictingComponents;
    for (const FBindingStatus& Status : CachedBindingStatuses)
    {
        USkeletalMeshComponent* MeshComponent = Status.MeshComponent.Get();
        UWetClothingAsset* Asset = Status.Asset.Get();
        if (MeshComponent == nullptr || Asset == nullptr ||
            (Status.State != EBindingState::Ready && Status.State != EBindingState::NeedsApply))
        {
            continue;
        }

        if (UWetClothingAsset** Existing = FirstClaim.Find(MeshComponent))
        {
            if (*Existing != Asset)
            {
                ConflictingComponents.Add(MeshComponent);
            }
        }
        else
        {
            FirstClaim.Add(MeshComponent, Asset);
        }
    }

    for (FBindingStatus& Status : CachedBindingStatuses)
    {
        if (ConflictingComponents.Contains(Status.MeshComponent.Get()))
        {
            Status.State = EBindingState::ConflictingAsset;
        }
    }
}

void FDynamicWetClothesComponentCustomization::RequestRefresh()
{
    RebuildBindingStatuses();
    if (const TSharedPtr<IPropertyUtilities> Utilities = PropertyUtilities.Pin())
    {
        Utilities->ForceRefresh();
    }
}

void FDynamicWetClothesComponentCustomization::HandleObjectPropertyChanged(
    UObject* ChangedObject,
    FPropertyChangedEvent& PropertyChangedEvent)
{
    UDynamicWetClothesComponent* DWC = Component.Get();
    if (DWC == nullptr || ChangedObject == nullptr || ChangedObject == DWC || bApplyingBinding)
    {
        return;
    }

    bool bAffectsBindings = DWC->WetClothingAssets.ContainsByPredicate(
        [ChangedObject](const TObjectPtr<UWetClothingAsset>& Asset)
        {
            return Asset.Get() == ChangedObject;
        });

    if (!bAffectsBindings)
    {
        for (const FBindingStatus& Status : CachedBindingStatuses)
        {
            if (Status.MeshComponent.Get() == ChangedObject)
            {
                bAffectsBindings = true;
                break;
            }
        }
    }

    if (!bAffectsBindings && ChangedObject == FindOwningBlueprint(DWC))
    {
        bAffectsBindings = true;
    }

    if (bAffectsBindings)
    {
        const TWeakPtr<IPropertyUtilities> WeakUtilities = PropertyUtilities;
        AsyncTask(ENamedThreads::GameThread, [WeakUtilities]()
        {
            if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
            {
                Utilities->ForceRefresh();
            }
        });
    }
}

FText FDynamicWetClothesComponentCustomization::GetBindingText(const int32 BindingIndex) const
{
    if (!CachedBindingStatuses.IsValidIndex(BindingIndex))
    {
        return FText::GetEmpty();
    }
    const FBindingStatus& Status = CachedBindingStatuses[BindingIndex];
    return FText::Format(
        LOCTEXT("BindingText", "WCA: {0}\nComponent: {1}"),
        MeshPathText(Status.Asset.Get()),
        Status.MeshComponent.IsValid() ? MeshPathText(Status.MeshComponent.Get()) : LOCTEXT("NoMatchingComponentName", "Not found"));
}

FText FDynamicWetClothesComponentCustomization::GetBindingStateText(const int32 BindingIndex) const
{
    if (!CachedBindingStatuses.IsValidIndex(BindingIndex))
    {
        return FText::GetEmpty();
    }
    const FBindingStatus& Status = CachedBindingStatuses[BindingIndex];
    switch (Status.State)
    {
    case EBindingState::Ready:
        return FText::Format(LOCTEXT("Ready", "Ready: {0}"), MeshPathText(Status.RequiredMesh.Get()));
    case EBindingState::NeedsApply:
        return FText::Format(LOCTEXT("NeedsApply", "Current: {0}\nRequired: {1}"), MeshPathText(Status.CurrentMesh.Get()), MeshPathText(Status.RequiredMesh.Get()));
    case EBindingState::MissingAsset:
        return LOCTEXT("MissingAsset", "This array entry has no Wet Clothing Asset assigned.");
    case EBindingState::UnsupportedAssetVersion:
        return FText::Format(
            LOCTEXT("UnsupportedAssetVersion", "Unsupported WCA schema version. Current plugin version requires schema {0}. Recreate or regenerate this asset."),
            FText::AsNumber(UWetClothingAsset::CurrentAssetDataVersion));
    case EBindingState::NoMatchingComponent:
        return FText::Format(LOCTEXT("NoMatch", "No component uses Source '{0}' or DWC mesh '{1}'."), MeshPathText(Status.SourceMesh.Get()), MeshPathText(Status.RequiredMesh.Get()));
    case EBindingState::MissingSourceMesh:
        return LOCTEXT("MissingSource", "The WCA has no Source Skeletal Mesh.");
    case EBindingState::MissingDWCMesh:
        return LOCTEXT("MissingDWC", "The WCA has no generated DWC Skeletal Mesh.");
    case EBindingState::DuplicateAsset:
        return LOCTEXT("Duplicate", "This WCA is registered more than once. Duplicate entries are ignored.");
    case EBindingState::ConflictingAsset:
        return LOCTEXT("Conflict", "Multiple WCA assets target this same SkeletalMeshComponent. Remove the conflicting entry before applying.");
    default:
        return FText::GetEmpty();
    }
}

EVisibility FDynamicWetClothesComponentCustomization::GetBindingWarningVisibility(const int32 BindingIndex) const
{
    return CachedBindingStatuses.IsValidIndex(BindingIndex) && CachedBindingStatuses[BindingIndex].State != EBindingState::Ready
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

EVisibility FDynamicWetClothesComponentCustomization::GetBindingApplyVisibility(const int32 BindingIndex) const
{
    return CanApplyBinding(BindingIndex) ? EVisibility::Visible : EVisibility::Collapsed;
}

bool FDynamicWetClothesComponentCustomization::CanApplyBinding(const int32 BindingIndex) const
{
    return CachedBindingStatuses.IsValidIndex(BindingIndex) &&
           CachedBindingStatuses[BindingIndex].State == EBindingState::NeedsApply &&
           CachedBindingStatuses[BindingIndex].MeshComponent.IsValid() &&
           CachedBindingStatuses[BindingIndex].RequiredMesh.IsValid();
}

bool FDynamicWetClothesComponentCustomization::CanApplyAll() const
{
    for (int32 Index = 0; Index < CachedBindingStatuses.Num(); ++Index)
    {
        if (CanApplyBinding(Index))
        {
            return true;
        }
    }
    return false;
}

bool FDynamicWetClothesComponentCustomization::CanOpenGPURenderTargetDebugger() const
{
    return Component.IsValid();
}

FReply FDynamicWetClothesComponentCustomization::HandleOpenGPURenderTargetDebugger()
{
    if (!Component.IsValid())
    {
        return FReply::Handled();
    }

    const TSharedRef<SWindow> DebugWindow = SNew(SWindow)
        .Title(LOCTEXT("DWCGPURTDebuggerTitle", "DWC GPU RT Debugger"))
        .ClientSize(FVector2D(960.0, 760.0))
        .SupportsMaximize(true)
        .SupportsMinimize(false);

    DebugWindow->SetContent(
        SNew(SDWCGPURenderTargetDebugger)
        .Component(Component));

    if (const TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow())
    {
        FSlateApplication::Get().AddWindowAsNativeChild(DebugWindow, ParentWindow.ToSharedRef());
    }
    else
    {
        FSlateApplication::Get().AddWindow(DebugWindow);
    }

    return FReply::Handled();
}

bool FDynamicWetClothesComponentCustomization::ApplyBinding(const int32 BindingIndex, const bool bUseTransaction)
{
    if (!CanApplyBinding(BindingIndex))
    {
        return false;
    }

    FBindingStatus& Status = CachedBindingStatuses[BindingIndex];
    USkeletalMeshComponent* Target = Status.MeshComponent.Get();
    USkeletalMesh* Required = Status.RequiredMesh.Get();
    UDynamicWetClothesComponent* DWC = Component.Get();
    if (Target == nullptr || Required == nullptr || DWC == nullptr)
    {
        return false;
    }

    TUniquePtr<FScopedTransaction> Transaction;
    if (bUseTransaction)
    {
        Transaction = MakeUnique<FScopedTransaction>(LOCTEXT("ApplyRuntimeMeshTransaction", "Apply DWC Skeletal Mesh"));
    }

    TGuardValue<bool> ApplyingGuard(bApplyingBinding, true);
    UBlueprint* OwningBlueprint = DWC->IsTemplate() ? FindOwningBlueprint(DWC) : nullptr;
    if (OwningBlueprint != nullptr)
    {
        OwningBlueprint->Modify();
    }
    Target->Modify();
    Target->SetSkeletalMeshAsset(Required);
    Target->PostEditChange();
    Target->MarkPackageDirty();
    if (OwningBlueprint != nullptr)
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(OwningBlueprint);
    }
    return true;
}

FReply FDynamicWetClothesComponentCustomization::HandleApplyBinding(const int32 BindingIndex)
{
    ApplyBinding(BindingIndex, true);
    RequestRefresh();
    return FReply::Handled();
}

FReply FDynamicWetClothesComponentCustomization::HandleApplyAll()
{
    const FScopedTransaction Transaction(LOCTEXT("ApplyAllRuntimeMeshesTransaction", "Apply All DWC Skeletal Meshes"));
    for (int32 Index = 0; Index < CachedBindingStatuses.Num(); ++Index)
    {
        ApplyBinding(Index, false);
    }
    RequestRefresh();
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
