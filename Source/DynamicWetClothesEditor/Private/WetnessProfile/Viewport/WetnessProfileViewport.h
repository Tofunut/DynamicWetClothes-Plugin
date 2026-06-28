#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class FAdvancedPreviewScene;
class FWetnessProfileViewportClient;
class SRichTextBlock;
class UStaticMeshComponent;
class UWetnessProfile;

class SWetnessProfileViewport : public SEditorViewport, public FGCObject
{
  public:
    SLATE_BEGIN_ARGS(SWetnessProfileViewport) {}
    SLATE_ARGUMENT(UWetnessProfile*, WetnessProfile)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetnessProfileViewport() override;

    virtual void    AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("SWetnessProfileViewport");
    }

    void RefreshPreviewScene();
    void FocusOnPreviewMesh(bool bInstant = false);

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual void                              PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void                              OnFocusViewportToSelection() override;

  private:
    TWeakObjectPtr<UWetnessProfile>           WetnessProfile;
    TSharedPtr<FAdvancedPreviewScene>         PreviewScene;
    TSharedPtr<FWetnessProfileViewportClient> ViewportClient;
    TObjectPtr<UStaticMeshComponent>          PreviewMeshComponent = nullptr;
    TSharedPtr<SRichTextBlock>                OverlayText;
};
