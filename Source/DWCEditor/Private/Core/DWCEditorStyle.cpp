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
    const FVector2D Icon24x24(24.0f, 24.0f);
    const FVector2D ToolbarIconSize(40.0f, 40.0f);
    const FVector2D WettableIconSize(30.0f, 30.0f);
    const FVector2D Thumbnail64x64(64.0f, 64.0f);
    const FVector2D ModeIconSize(32.0f, 32.0f);
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
    SetPngBrush(*StyleSet, TEXT("ClassIcon.WetnessProfile"), TEXT("AssetIcons/WetnessProfile_128"), Icon16x16);
    SetPngBrush(*StyleSet, TEXT("ClassThumbnail.WetnessProfile"), TEXT("AssetIcons/WetnessProfile_128"), Thumbnail64x64);

    SetSvgBrush(*StyleSet, TEXT("DWCEditor.BuildForRuntime"), TEXT("EditorIcons/BuildForRuntime_400"), ToolbarIconSize);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.BuildForRuntime.Small"), TEXT("EditorIcons/BuildForRuntime_400"), Icon20x20);

    SetSvgBrush(*StyleSet, TEXT("DWCEditor.UVTool.Select"), TEXT("EditorIcons/SelectClick_400"), Icon24x24);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.UVTool.BoxSelect"), TEXT("EditorIcons/SelectBox_400"), Icon24x24);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.UVTool.EllipseSelect"), TEXT("EditorIcons/SelectEllipse_400"), Icon24x24);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.UVTool.LassoSelect"), TEXT("EditorIcons/SelectLasso_400"), Icon24x24);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.MagicWandTool"), TEXT("EditorIcons/MagicWandTool"), Icon20x20);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.MagicWandTool.Large"), TEXT("EditorIcons/MagicWandTool"), ModeIconSize);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.AutoPartitioning"), TEXT("EditorIcons/MagicWandTool"), Icon20x20);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.Bake"), TEXT("EditorIcons/Bake_400"), Icon24x24);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.SurfaceWaterTiling"), TEXT("EditorIcons/SurfaceWaterTiling"), Icon20x20);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.WetnessProfile.AddWater"), TEXT("EditorIcons/WetnessProfile_AddWater"), Icon20x20);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.WetnessProfile.Play"), TEXT("EditorIcons/WetnessProfile_Play"), Icon20x20);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.WetnessProfile.Pause"), TEXT("EditorIcons/WetnessProfile_Pause"), Icon20x20);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.WetnessProfile.Crosshair"), TEXT("EditorIcons/WetnessProfile_Crosshair"), FVector2D(64.0f, 64.0f));
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.WetnessProfile.StatusDot"), TEXT("EditorIcons/WetnessProfile_StatusDot"), FVector2D(10.0f, 10.0f));
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.WetnessProfile.RevertSaved"), TEXT("EditorIcons/WetnessProfile_RevertSaved"), Icon16x16);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.Status.Error"), TEXT("EditorIcons/StatusError"), Icon16x16);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.Validation.Failure"), TEXT("EditorIcons/ValidationFailure"), Icon24x24);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.Validation.Diagnostics"), TEXT("EditorIcons/ValidationDiagnostics"), Icon24x24);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.Part.IsWettable.True"), TEXT("EditorIcons/Part_IsWettable_True_400"), WettableIconSize);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.Part.IsWettable.False"), TEXT("EditorIcons/Part_IsWettable_False_400"), WettableIconSize);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.Mode.Part"), TEXT("EditorIcons/Mode_Part_400"), ModeIconSize);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.Mode.Wrinkle"), TEXT("EditorIcons/Mode_Wrinkles_400"), ModeIconSize);
    SetSvgBrush(*StyleSet, TEXT("DWCEditor.Mode.Transparency"), TEXT("EditorIcons/Mode_Transparency_400"), ModeIconSize);

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
