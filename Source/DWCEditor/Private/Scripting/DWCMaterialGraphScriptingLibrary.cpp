// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Scripting/DWCMaterialGraphScriptingLibrary.h"

#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialFunction.h"

UMaterialExpressionComment* UDWCMaterialGraphScriptingLibrary::CreateMaterialFunctionComment(
    UMaterialFunction* MaterialFunction,
    const FString& Text,
    const int32 NodePosX,
    const int32 NodePosY,
    const int32 SizeX,
    const int32 SizeY,
    const FLinearColor CommentColor,
    const int32 FontSize,
    const bool bGroupMode)
{
    if (!IsValid(MaterialFunction))
    {
        return nullptr;
    }

    MaterialFunction->Modify();
    UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(
        MaterialFunction,
        NAME_None,
        RF_Transactional);
    if (Comment == nullptr)
    {
        return nullptr;
    }

    Comment->MaterialExpressionEditorX = NodePosX;
    Comment->MaterialExpressionEditorY = NodePosY;
    Comment->SizeX = FMath::Max(1, SizeX);
    Comment->SizeY = FMath::Max(1, SizeY);
    Comment->Text = Text;
    Comment->CommentColor = CommentColor;
    Comment->FontSize = FMath::Max(1, FontSize);
    Comment->bGroupMode = bGroupMode;
    Comment->UpdateMaterialExpressionGuid(true, true);

    MaterialFunction->GetExpressionCollection().AddComment(Comment);
    Comment->MarkPackageDirty();
    return Comment;
}

UMaterialExpressionNamedRerouteUsage*
UDWCMaterialGraphScriptingLibrary::CreateMaterialFunctionNamedRerouteUsage(
    UMaterialFunction* MaterialFunction,
    UMaterialExpressionNamedRerouteDeclaration* Declaration,
    const int32 NodePosX,
    const int32 NodePosY)
{
    if (!IsValid(MaterialFunction) || !IsValid(Declaration) ||
        Declaration->GetOuter() != MaterialFunction || !Declaration->VariableGuid.IsValid())
    {
        return nullptr;
    }

    UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(
        UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
            MaterialFunction,
            UMaterialExpressionNamedRerouteUsage::StaticClass(),
            NodePosX,
            NodePosY));
    if (Usage == nullptr)
    {
        return nullptr;
    }

    Usage->Modify();
    Usage->Declaration = Declaration;
    Usage->DeclarationGuid = Declaration->VariableGuid;
    Usage->MarkPackageDirty();
    return Usage;
}
