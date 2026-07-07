#include "Core/DWCEditorStyle.h"
#include "ToolMenus.h"
#include "WetClothing/RevealBake/DWCRevealBakeMenu.h"
#include "Modules/ModuleManager.h"

class FDWCEditorModule : public IModuleInterface
{
  public:
    virtual void StartupModule() override
    {
        FDWCEditorStyle::Initialize();
        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDWCEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FDWCEditorStyle::Shutdown();
    }

  private:
    void RegisterMenus()
    {
        UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        if (ToolsMenu == nullptr)
        {
            return;
        }

        FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("DWC"));
        Section.AddMenuEntry(
            TEXT("DWCRevealBakeSelected"),
            FText::FromString(TEXT("Bake DWC Reveal Textures")),
            FText::FromString(TEXT("Bake wet reveal lookup, mask, and confidence textures from selected actors with a DWC Bake Component.")),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateStatic(&FDWCRevealBakeMenu::BakeSelectedActors)));
    }
};

IMPLEMENT_MODULE(FDWCEditorModule, DWCEditor)
