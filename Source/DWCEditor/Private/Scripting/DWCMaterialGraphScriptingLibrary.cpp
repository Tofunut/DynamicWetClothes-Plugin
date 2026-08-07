//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Scripting/DWCMaterialGraphScriptingLibrary.h"

#include "MaterialGraph/MaterialGraph.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialFunction.h"

UMaterialExpressionComment* UDWCMaterialGraphScriptingLibrary::CreateMaterialFunctionComment(
    UMaterialFunction* MaterialFunction,
    const FString& Text,
    const int32 NodePosX,
    const int32 NodePosY,
    const int32 Width,
    const int32 Height,
    const FLinearColor Color,
    const int32 FontSize,
    const bool bGroupMode)
{
#if WITH_EDITOR
    if (!IsValid(MaterialFunction))
    {
        return nullptr;
    }

    MaterialFunction->Modify();

    UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(
        MaterialFunction,
        NAME_None,
        RF_Transactional);
    if (!IsValid(Comment))
    {
        return nullptr;
    }

    // Comments are editor graph objects, not shader expressions. AddComment is
    // the critical difference from CreateMaterialExpressionInFunction().
    MaterialFunction->GetExpressionCollection().AddComment(Comment);

    Comment->Function = MaterialFunction;
    Comment->Material = nullptr;
    Comment->MaterialExpressionEditorX = NodePosX;
    Comment->MaterialExpressionEditorY = NodePosY;
    Comment->Text = Text;
    Comment->SizeX = FMath::Max(Width, 256);
    Comment->SizeY = FMath::Max(Height, 192);
    Comment->CommentColor = Color;
    Comment->FontSize = FMath::Max(FontSize, 1);
    Comment->bGroupMode = bGroupMode;
    Comment->UpdateMaterialExpressionGuid(true, true);
    Comment->MarkPackageDirty();

    // Usually the graph is built when the asset opens. If it is already open,
    // add the visual graph node immediately as well.
    if (MaterialFunction->MaterialGraph != nullptr && Comment->GraphNode == nullptr)
    {
        MaterialFunction->MaterialGraph->AddComment(Comment, false);
    }

    MaterialFunction->MarkPackageDirty();
    return Comment;
#else
    return nullptr;
#endif
}

UMaterialExpressionNamedRerouteUsage*
UDWCMaterialGraphScriptingLibrary::CreateMaterialFunctionNamedRerouteUsage(
    UMaterialFunction* MaterialFunction,
    UMaterialExpressionNamedRerouteDeclaration* Declaration,
    const int32 NodePosX,
    const int32 NodePosY)
{
#if WITH_EDITOR
    if (!IsValid(MaterialFunction) || !IsValid(Declaration))
    {
        return nullptr;
    }

    // MaterialEditingLibrary creates Material Function expressions through the
    // same UE 5.8 editor path used by the Material Editor and Python API. Do
    // not NewObject + AddExpression here: that bypasses initialization carried
    // out by CreateMaterialExpressionInFunction and can leave the node invalid.
    bool bDeclarationBelongsToFunction = false;
    for (const TObjectPtr<UMaterialExpression>& Expression : MaterialFunction->GetExpressions())
    {
        if (Expression.Get() == Declaration)
        {
            bDeclarationBelongsToFunction = true;
            break;
        }
    }
    if (!bDeclarationBelongsToFunction)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DWC MF scripting: Named Reroute Declaration '%s' is not registered in Material Function '%s'."),
            *GetNameSafe(Declaration),
            *GetNameSafe(MaterialFunction));
        return nullptr;
    }

    MaterialFunction->Modify();
    Declaration->Modify();

    // Python-created declarations are owned by the correct Material Function,
    // but their legacy Function back-pointer is not guaranteed to be populated
    // immediately. Repair it instead of rejecting an otherwise valid node.
    Declaration->Function = MaterialFunction;
    Declaration->Material = nullptr;

    if (!Declaration->VariableGuid.IsValid())
    {
        Declaration->VariableGuid = FGuid::NewGuid();
    }

    UMaterialExpressionNamedRerouteUsage* Usage =
        Cast<UMaterialExpressionNamedRerouteUsage>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                MaterialFunction,
                UMaterialExpressionNamedRerouteUsage::StaticClass(),
                NodePosX,
                NodePosY));
    if (!IsValid(Usage))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DWC MF scripting: UE 5.8 MaterialEditingLibrary failed to create a Named Reroute Usage in '%s'."),
            *GetNameSafe(MaterialFunction));
        return nullptr;
    }

    Usage->Modify();
    Usage->Function = MaterialFunction;
    Usage->Material = nullptr;
    Usage->Declaration = Declaration;
    Usage->DeclarationGuid = Declaration->VariableGuid;
    Usage->UpdateMaterialExpressionGuid(true, true);

    // Refresh editor state after assigning the C++-only relationship fields.
    Declaration->PostEditChange();
    Usage->PostEditChange();

    if (!IsValid(Usage->Declaration))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DWC MF scripting: Named Reroute Usage '%s' could not validate Declaration '%s' (Guid=%s)."),
            *GetNameSafe(Usage),
            *GetNameSafe(Declaration),
            *Declaration->VariableGuid.ToString());
        UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(MaterialFunction, Usage);
        return nullptr;
    }

    Declaration->MarkPackageDirty();
    Usage->MarkPackageDirty();
    MaterialFunction->MarkPackageDirty();
    return Usage;
#else
    return nullptr;
#endif
}
