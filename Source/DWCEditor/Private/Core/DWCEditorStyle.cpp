#include "Core/DWCEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FDWCEditorStyle::StyleSet;

namespace
{
    const FVector2D Icon16x16(16.0f, 16.0f);
    const FVector2D Icon20x20(20.0f, 20.0f);
    const FVector2D Thumbnail64x64(64.0f, 64.0f);
} // namespace

void FDWCEditorStyle::Initialize()
{
    if (StyleSet.IsValid())
    {
        return;
    }

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DynamicWetClothes"));
    if (!Plugin.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("DWCEditorStyle: DynamicWetClothes plugin was not found."));
        return;
    }

    StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
    StyleSet->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

    auto SetPngBrush = [](FSlateStyleSet& InStyleSet, const TCHAR* BrushName, const TCHAR* RelativePath, const FVector2D& Size)
    {
        InStyleSet.Set(BrushName, new FSlateImageBrush(InStyleSet.RootToContentDir(RelativePath, TEXT(".png")), Size));
    };

    auto SetSvgBrush = [](FSlateStyleSet& InStyleSet, const TCHAR* BrushName, const TCHAR* RelativePath, const FVector2D& Size)
    {
        InStyleSet.Set(BrushName, new FSlateVectorImageBrush(InStyleSet.RootToContentDir(RelativePath, TEXT(".svg")), Size));
    };

    SetPngBrush(*StyleSet, TEXT("ClassIcon.WetClothingAsset"), TEXT("AssetIcons/WetClothing_128"), Icon16x16);
    SetPngBrush(*StyleSet, TEXT("ClassThumbnail.WetClothingAsset"), TEXT("AssetIcons/WetClothing_128"), Thumbnail64x64);
    SetPngBrush(*StyleSet, TEXT("ClassIcon.WetWrinkleAsset"), TEXT("AssetIcons/WetClothing_128"), Icon16x16);
    SetPngBrush(*StyleSet, TEXT("ClassThumbnail.WetWrinkleAsset"), TEXT("AssetIcons/WetClothing_128"), Thumbnail64x64);
    SetPngBrush(*StyleSet, TEXT("ClassIcon.WetnessProfile"), TEXT("AssetIcons/WetnessProfile_128"), Icon16x16);
    SetPngBrush(*StyleSet, TEXT("ClassThumbnail.WetnessProfile"), TEXT("AssetIcons/WetnessProfile_128"), Thumbnail64x64);

    SetSvgBrush(*StyleSet, TEXT("DWCEditor.UVTool.Select"), TEXT("EditorIcons/SelectClick_20"), Icon20x20);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.UVTool.BoxSelect"), TEXT("EditorIcons/SelectBox_20"), Icon20x20);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.UVTool.EllipseSelect"), TEXT("EditorIcons/SelectEllipse_20"), Icon20x20);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.UVTool.LassoSelect"), TEXT("EditorIcons/SelectLasso_20"), Icon20x20);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.AutoPartitioning"), TEXT("EditorIcons/AutoPartitioning"), Icon20x20);

    FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FDWCEditorStyle::Shutdown()
{
    if (!StyleSet.IsValid())
    {
        return;
    }

    FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
    ensure(StyleSet.IsUnique());
    StyleSet.Reset();
}

FName FDWCEditorStyle::GetStyleSetName()
{
    static const FName StyleSetName(TEXT("DWCEditorStyle"));
    return StyleSetName;
}

const ISlateStyle& FDWCEditorStyle::Get()
{
    if (!StyleSet.IsValid())
    {
        Initialize();
    }

    check(StyleSet.IsValid());
    return *StyleSet.Get();
}

const FSlateBrush* FDWCEditorStyle::GetBrush(const FName BrushName)
{
    return Get().GetBrush(BrushName);
}
