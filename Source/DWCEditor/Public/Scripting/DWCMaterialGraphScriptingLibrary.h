// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "DWCMaterialGraphScriptingLibrary.generated.h"

class UMaterialExpressionComment;
class UMaterialExpressionNamedRerouteDeclaration;
class UMaterialExpressionNamedRerouteUsage;
class UMaterialFunction;

/** UE 5.8 material-function authoring operations that are not exposed by MaterialEditingLibrary. */
UCLASS()
class DWCEDITOR_API UDWCMaterialGraphScriptingLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

  public:
    UFUNCTION(BlueprintCallable, Category = "DWC|Material Graph")
    static UMaterialExpressionComment* CreateMaterialFunctionComment(
        UMaterialFunction* MaterialFunction,
        const FString& Text,
        int32 NodePosX,
        int32 NodePosY,
        int32 SizeX,
        int32 SizeY,
        FLinearColor CommentColor,
        int32 FontSize,
        bool bGroupMode);

    UFUNCTION(BlueprintCallable, Category = "DWC|Material Graph")
    static UMaterialExpressionNamedRerouteUsage* CreateMaterialFunctionNamedRerouteUsage(
        UMaterialFunction* MaterialFunction,
        UMaterialExpressionNamedRerouteDeclaration* Declaration,
        int32 NodePosX,
        int32 NodePosY);
};
