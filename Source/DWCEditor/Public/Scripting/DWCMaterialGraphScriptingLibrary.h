#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWCMaterialGraphScriptingLibrary.generated.h"

class UMaterialExpressionComment;
class UMaterialExpressionNamedRerouteDeclaration;
class UMaterialExpressionNamedRerouteUsage;
class UMaterialFunction;

/**
 * Editor-only scripting helpers used by the explicit DWC Python authoring
 * scripts. This class never creates or repairs assets automatically; every
 * function runs only when an explicit user script calls it.
 *
 * UE 5.8 does not expose several material-graph fields to Python. Keep those
 * narrow compatibility operations here instead of restoring automatic MF
 * generation to the plugin.
 */
UCLASS()
class DWCEDITOR_API UDWCMaterialGraphScriptingLibrary final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

  public:
    /**
     * Creates a real material-function editor comment box.
     *
     * UMaterialEditingLibrary::CreateMaterialExpressionInFunction must not be
     * used for comments: it inserts the comment into the function's expression
     * array and the editor renders it as a small expression node. Real comment
     * boxes belong to FMaterialExpressionCollection::EditorComments instead.
     */
    UFUNCTION(BlueprintCallable, Category = "Dynamic Wet Clothes|Editor Scripting")
    static UMaterialExpressionComment* CreateMaterialFunctionComment(
        UMaterialFunction* MaterialFunction,
        const FString& Text,
        int32 NodePosX,
        int32 NodePosY,
        int32 Width,
        int32 Height,
        FLinearColor Color,
        int32 FontSize,
        bool bGroupMode = false);

    /**
     * Creates and binds a Named Reroute Usage node in a Material Function.
     *
     * In UE 5.8 the Python wrapper exposes neither Declaration nor
     * DeclarationGuid on UMaterialExpressionNamedRerouteUsage, although both
     * are public C++ fields. Python therefore cannot create a valid Usage node
     * by set_editor_property().
     */
    UFUNCTION(BlueprintCallable, Category = "Dynamic Wet Clothes|Editor Scripting")
    static UMaterialExpressionNamedRerouteUsage* CreateMaterialFunctionNamedRerouteUsage(
        UMaterialFunction* MaterialFunction,
        UMaterialExpressionNamedRerouteDeclaration* Declaration,
        int32 NodePosX,
        int32 NodePosY);
};
