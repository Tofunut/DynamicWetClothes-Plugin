#include "DWCEditorUtils.h"

#include "FileHelpers.h"
#include "UObject/Object.h"
#include "UObject/Package.h"

bool DWCEditorUtils::SaveAsset(UObject* Asset)
{
    if (Asset == nullptr)
    {
        return false;
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
