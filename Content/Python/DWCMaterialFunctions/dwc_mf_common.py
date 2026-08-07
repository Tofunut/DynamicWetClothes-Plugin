# Copyright 2026 Team Tofunut. All Rights Reserved.
"""Shared helpers for one-shot Dynamic Wet Clothes material-function authoring.

This module never runs automatically.  Each entry script imports it only when the
user explicitly executes that script inside Unreal Editor.
"""
from __future__ import annotations

import os
import sys
from typing import Any, Iterable, Sequence

import unreal

PLUGIN_MOUNT = "/DynamicWetClothes"
FUNCTION_ROOT = f"{PLUGIN_MOUNT}/Materials/Functions"
DEFAULT_ROOT = f"{FUNCTION_ROOT}/GeneratedDefaults"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESOURCE_DIR = os.path.join(SCRIPT_DIR, "Resources")

DATA_FALLBACK_NAME = "T_DWC_DefaultProfileData"
NORMAL_FALLBACK_NAME = "T_DWC_DefaultSurfaceNormal"
MASK_ARRAY_FALLBACK_NAME = "TA_DWC_DefaultSurfaceMask"
NORMAL_ARRAY_FALLBACK_NAME = "TA_DWC_DefaultSurfaceNormal"

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


class DwcMaterialFunctionError(RuntimeError):
    pass


def log(message: str) -> None:
    unreal.log(f"[DWC MF] {message}")


def warn(message: str) -> None:
    unreal.log_warning(f"[DWC MF] {message}")


def fail(message: str) -> None:
    raise DwcMaterialFunctionError(message)


def asset_path(asset_name: str, root: str = FUNCTION_ROOT) -> str:
    return f"{root}/{asset_name}"


def object_path(asset_name: str, root: str = FUNCTION_ROOT) -> str:
    package = asset_path(asset_name, root)
    return f"{package}.{asset_name}"


def load_asset(path: str) -> Any:
    return unreal.load_asset(path)


def checkout_asset(path: str) -> None:
    checkout = getattr(EAL, "checkout_asset", None)
    does_asset_exist = getattr(EAL, "does_asset_exist", None)
    if callable(checkout) and (not callable(does_asset_exist) or does_asset_exist(path)):
        try:
            checkout(path)
        except Exception as exc:
            warn(f"Could not checkout {path}: {exc}")


def save_asset(path: str) -> None:
    checkout_asset(path)
    if not EAL.save_asset(path, only_if_is_dirty=False):
        fail(f"Could not save asset: {path}")


def get_property(obj: Any, property_name: str, default: Any = None) -> Any:
    try:
        return obj.get_editor_property(property_name)
    except Exception:
        return default


def enum_value(enum_type: Any, *candidate_names: str) -> Any:
    for name in candidate_names:
        if hasattr(enum_type, name):
            return getattr(enum_type, name)
    fail(f"Could not resolve {enum_type.__name__} enum member from {candidate_names}.")


def make_vector4(x: float, y: float, z: float, w: float) -> Any:
    for type_name in ("Vector4f", "Vector4"):
        cls = getattr(unreal, type_name, None)
        if cls is not None:
            try:
                return cls(x, y, z, w)
            except TypeError:
                value = cls()
                for prop, component in (("x", x), ("y", y), ("z", z), ("w", w)):
                    try:
                        value.set_editor_property(prop, component)
                    except Exception:
                        setattr(value, prop, component)
                return value
    return unreal.LinearColor(x, y, z, w)


def set_property(obj: Any, property_name: str, value: Any, *, required: bool = True) -> bool:
    try:
        obj.set_editor_property(property_name, value)
        return True
    except Exception as exc:
        if required:
            fail(
                f"Could not set {obj.get_class().get_name()}.{property_name}: {exc}"
            )
        return False


def set_property_if_changed(obj: Any, property_name: str, value: Any) -> bool:
    current = get_property(obj, property_name, None)
    if current == value:
        return False
    return set_property(obj, property_name, value, required=False)


def set_first_property(
    obj: Any,
    property_names: Sequence[str],
    value: Any,
    *,
    required: bool = True,
) -> str | None:
    errors: list[str] = []
    for property_name in property_names:
        try:
            obj.set_editor_property(property_name, value)
            return property_name
        except Exception as exc:
            errors.append(f"{property_name}: {exc}")
    if required:
        fail(
            f"Could not set any compatible property on {obj.get_class().get_name()}: "
            + " | ".join(errors)
        )
    return None


def create_expression(
    material_function: Any,
    expression_class: Any,
    x: int,
    y: int,
    *,
    description: str | None = None,
) -> Any:
    expression = MEL.create_material_expression_in_function(
        material_function, expression_class, x, y
    )
    if expression is None:
        fail(
            f"Could not create {expression_class.__name__} in "
            f"{material_function.get_name()} at ({x}, {y})."
        )
    if description:
        set_property(expression, "desc", description, required=False)
    return expression


def try_connect(
    source: Any,
    source_output_names: str | Sequence[str],
    target: Any,
    target_input_names: str | Sequence[str],
) -> tuple[str, str]:
    outputs = (
        [source_output_names]
        if isinstance(source_output_names, str)
        else list(source_output_names)
    )
    inputs = (
        [target_input_names]
        if isinstance(target_input_names, str)
        else list(target_input_names)
    )
    for output_name in outputs:
        for input_name in inputs:
            try:
                result = MEL.connect_material_expressions(
                    source, output_name, target, input_name
                )
                if result is not False:
                    return output_name, input_name
            except Exception:
                pass
    fail(
        f"Could not connect {source.get_name()} outputs {outputs} to "
        f"{target.get_name()} inputs {inputs}."
    )


def create_comment(
    material_function: Any,
    text: str,
    x: int,
    y: int,
    width: int,
    height: int,
    *,
    parent: bool = False,
) -> Any:
    """Create a real editor comment box in a Material Function.

    MaterialEditingLibrary.create_material_expression_in_function() must not
    be used for comments. That API inserts UMaterialExpressionComment into the
    shader-expression array, which UE renders as a small green Comment node.
    The DWCEditor bridge adds it to the function's EditorComments collection.
    """
    scripting_library = getattr(
        unreal, "DWCMaterialGraphScriptingLibrary", None
    )
    creator = getattr(
        scripting_library,
        "create_material_function_comment",
        None,
    ) if scripting_library is not None else None

    if not callable(creator):
        fail(
            "DWCEditor material-comment scripting bridge is unavailable. "
            "Replace the plugin Source with the patched version, compile the "
            "DWCEditor module, restart Unreal Editor, and run the script again."
        )

    color = (
        unreal.LinearColor(0.055, 0.075, 0.11, 1.0)
        if parent
        else unreal.LinearColor(0.12, 0.15, 0.20, 1.0)
    )
    comment = creator(
        material_function,
        text,
        int(x),
        int(y),
        int(width),
        int(height),
        color,
        32 if parent else 22,
        False,
    )
    if comment is None:
        fail(
            f"Could not create material-function comment box '{text}' "
            f"at ({x}, {y}) with size {width} x {height}."
        )

    # These properties are cosmetic and not available in every engine version.
    set_property(comment, "comment_bubble_visible_in_details_panel", False, required=False)
    set_property(comment, "color_comment_bubble", True, required=False)
    return comment

def input_type(kind: str) -> Any:
    table = {
        "scalar": ("FUNCTION_INPUT_SCALAR", "SCALAR"),
        "vector2": ("FUNCTION_INPUT_VECTOR2", "VECTOR2"),
        "vector3": ("FUNCTION_INPUT_VECTOR3", "VECTOR3"),
        "vector4": ("FUNCTION_INPUT_VECTOR4", "VECTOR4"),
    }
    if kind not in table:
        fail(f"Unsupported FunctionInput type: {kind}")
    return enum_value(unreal.FunctionInputType, *table[kind])


def function_input(
    material_function: Any,
    name: str,
    kind: str,
    preview: Sequence[float],
    sort_priority: int,
    x: int,
    y: int,
    description: str,
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionFunctionInput,
        x,
        y,
        description=description,
    )
    values = list(preview) + [0.0, 0.0, 0.0, 0.0]
    set_property(node, "input_name", name)
    set_property(node, "input_type", input_type(kind))
    set_property(node, "preview_value", make_vector4(*values[:4]))
    set_property(node, "use_preview_value_as_default", True)
    set_property(node, "sort_priority", sort_priority)
    set_property(node, "description", description, required=False)
    return node


def function_output(
    material_function: Any,
    name: str,
    source: Any,
    source_outputs: str | Sequence[str],
    sort_priority: int,
    x: int,
    y: int,
    description: str,
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionFunctionOutput,
        x,
        y,
        description=description,
    )
    set_property(node, "output_name", name)
    set_property(node, "sort_priority", sort_priority)
    set_property(node, "description", description, required=False)
    try_connect(source, source_outputs, node, ("A", "Input", ""))
    return node


def scalar_constant(material_function: Any, value: float, x: int, y: int, description: str = "") -> Any:
    node = create_expression(
        material_function, unreal.MaterialExpressionConstant, x, y, description=description or None
    )
    set_property(node, "r", value)
    return node


def vector_constant(
    material_function: Any,
    value: Sequence[float],
    x: int,
    y: int,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionConstant3Vector,
        x,
        y,
        description=description or None,
    )
    vals = list(value) + [0.0, 0.0, 1.0]
    set_property(node, "constant", unreal.LinearColor(vals[0], vals[1], vals[2], 1.0))
    return node


def scalar_parameter(
    material_function: Any,
    name: str,
    default: float,
    x: int,
    y: int,
    *,
    group: str,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionScalarParameter,
        x,
        y,
        description=description or None,
    )
    set_property(node, "parameter_name", name)
    set_property(node, "default_value", default)
    set_property(node, "group", group, required=False)
    return node


def vector_parameter(
    material_function: Any,
    name: str,
    default: Sequence[float],
    x: int,
    y: int,
    *,
    group: str,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionVectorParameter,
        x,
        y,
        description=description or None,
    )
    vals = list(default) + [0.0, 0.0, 0.0, 0.0]
    set_property(node, "parameter_name", name)
    set_property(node, "default_value", unreal.LinearColor(*vals[:4]))
    set_property(node, "group", group, required=False)
    return node


def texture2d_parameter(
    material_function: Any,
    name: str,
    fallback_texture: Any,
    x: int,
    y: int,
    *,
    sampler_type: Any,
    group: str,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionTextureSampleParameter2D,
        x,
        y,
        description=description or None,
    )
    set_property(node, "parameter_name", name)
    set_property(node, "texture", fallback_texture)
    set_property(node, "sampler_type", sampler_type)
    set_property(node, "group", group, required=False)
    return node


def texture2d_array_parameter(
    material_function: Any,
    name: str,
    fallback_texture_array: Any,
    x: int,
    y: int,
    *,
    sampler_type: Any,
    group: str,
    description: str = "",
) -> Any:
    expression_class = getattr(
        unreal, "MaterialExpressionTextureSampleParameter2DArray", None
    )
    if expression_class is None:
        fail("MaterialExpressionTextureSampleParameter2DArray is unavailable.")
    node = create_expression(
        material_function,
        expression_class,
        x,
        y,
        description=description or None,
    )
    set_property(node, "parameter_name", name)
    set_property(node, "texture", fallback_texture_array)
    set_property(node, "sampler_type", sampler_type)
    set_property(node, "group", group, required=False)
    return node


def static_switch_parameter(
    material_function: Any,
    name: str,
    default: bool,
    true_source: Any,
    true_outputs: str | Sequence[str],
    false_source: Any,
    false_outputs: str | Sequence[str],
    x: int,
    y: int,
    *,
    group: str,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionStaticSwitchParameter,
        x,
        y,
        description=description or None,
    )
    set_property(node, "parameter_name", name)
    set_property(node, "default_value", default)
    set_property(node, "group", group, required=False)
    try_connect(true_source, true_outputs, node, ("True", "A"))
    try_connect(false_source, false_outputs, node, ("False", "B"))
    return node


def component_mask(
    material_function: Any,
    source: Any,
    source_outputs: str | Sequence[str],
    channel: str,
    x: int,
    y: int,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionComponentMask,
        x,
        y,
        description=description or None,
    )
    channel = channel.upper()
    for prop in ("r", "g", "b", "a"):
        set_property(node, prop, prop.upper() == channel)
    try_connect(source, source_outputs, node, ("Input", ""))
    return node


def multiply(
    material_function: Any,
    a: Any,
    a_outputs: str | Sequence[str],
    b: Any,
    b_outputs: str | Sequence[str],
    x: int,
    y: int,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionMultiply,
        x,
        y,
        description=description or None,
    )
    try_connect(a, a_outputs, node, "A")
    try_connect(b, b_outputs, node, "B")
    return node


def add(
    material_function: Any,
    a: Any,
    a_outputs: str | Sequence[str],
    b: Any,
    b_outputs: str | Sequence[str],
    x: int,
    y: int,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionAdd,
        x,
        y,
        description=description or None,
    )
    try_connect(a, a_outputs, node, "A")
    try_connect(b, b_outputs, node, "B")
    return node


def lerp(
    material_function: Any,
    a: Any,
    a_outputs: str | Sequence[str],
    b: Any,
    b_outputs: str | Sequence[str],
    alpha: Any,
    alpha_outputs: str | Sequence[str],
    x: int,
    y: int,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionLinearInterpolate,
        x,
        y,
        description=description or None,
    )
    try_connect(a, a_outputs, node, "A")
    try_connect(b, b_outputs, node, "B")
    try_connect(alpha, alpha_outputs, node, "Alpha")
    return node


def append_vector(
    material_function: Any,
    a: Any,
    a_outputs: str | Sequence[str],
    b: Any,
    b_outputs: str | Sequence[str],
    x: int,
    y: int,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionAppendVector,
        x,
        y,
        description=description or None,
    )
    try_connect(a, a_outputs, node, "A")
    try_connect(b, b_outputs, node, "B")
    return node


def custom_output_type(kind: str) -> Any:
    table = {
        "float1": ("CMOT_FLOAT1", "FLOAT1"),
        "float2": ("CMOT_FLOAT2", "FLOAT2"),
        "float3": ("CMOT_FLOAT3", "FLOAT3"),
        "float4": ("CMOT_FLOAT4", "FLOAT4"),
    }
    return enum_value(unreal.CustomMaterialOutputType, *table[kind])


def custom_expression(
    material_function: Any,
    code: str,
    input_sources: Sequence[tuple[str, Any, str | Sequence[str]]],
    output_kind: str,
    x: int,
    y: int,
    description: str,
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionCustom,
        x,
        y,
        description=description,
    )
    inputs = []
    for input_name, _, _ in input_sources:
        custom_input = unreal.CustomInput()
        set_property(custom_input, "input_name", input_name)
        inputs.append(custom_input)
    set_property(node, "inputs", inputs)
    set_property(node, "code", code.strip())
    set_property(node, "description", description, required=False)
    set_property(node, "output_type", custom_output_type(output_kind))
    # RebuildOutputs is not exposed in every Python version; setting inputs and
    # output_type is enough in current UE versions, and the optional method makes
    # the pins available immediately when present.
    rebuild = getattr(node, "rebuild_outputs", None)
    if callable(rebuild):
        rebuild()
    for input_name, source, outputs in input_sources:
        try_connect(source, outputs, node, input_name)
    return node


def named_declaration(
    material_function: Any,
    name: str,
    source: Any,
    source_outputs: str | Sequence[str],
    x: int,
    y: int,
    description: str = "",
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionNamedRerouteDeclaration,
        x,
        y,
        description=description or None,
    )
    set_property(node, "name", name)
    try_connect(source, source_outputs, node, ("Input", ""))
    return node


def named_usage(
    material_function: Any,
    declaration: Any,
    x: int,
    y: int,
    description: str = "",
) -> Any:
    """Create a valid UE 5.8 Named Reroute Usage node.

    UE 5.8's Python wrapper intentionally exposes no editable properties on
    MaterialExpressionNamedRerouteUsage beyond the base expression fields.
    In particular, Declaration and DeclarationGuid cannot be assigned with
    set_editor_property(), even though both are public C++ members. The narrow
    DWCEditor bridge creates the expression and binds both fields in C++.
    """
    scripting_library = getattr(
        unreal, "DWCMaterialGraphScriptingLibrary", None
    )
    creator = getattr(
        scripting_library,
        "create_material_function_named_reroute_usage",
        None,
    ) if scripting_library is not None else None

    if not callable(creator):
        fail(
            "UE 5.8 Named Reroute scripting bridge is unavailable. "
            "Compile the patched DWCEditor module, restart Unreal Editor, "
            "and run the script again."
        )

    node = creator(
        material_function,
        declaration,
        int(x),
        int(y),
    )
    if node is None:
        fail(
            f"Could not create a Named Reroute Usage for "
            f"{declaration.get_name()} at ({x}, {y})."
        )

    if description:
        set_property(node, "desc", description, required=False)
    return node


def function_call(
    material_function: Any,
    called_function: Any,
    x: int,
    y: int,
    description: str,
) -> Any:
    node = create_expression(
        material_function,
        unreal.MaterialExpressionMaterialFunctionCall,
        x,
        y,
        description=description,
    )
    setter = getattr(node, "set_material_function", None)
    if callable(setter):
        if setter(called_function) is False:
            fail(f"Could not assign {called_function.get_name()} to function-call node.")
    else:
        set_property(node, "material_function", called_function)
    updater = getattr(node, "update_from_function_resource", None)
    if callable(updater):
        updater()
    return node


def create_or_replace_material_function(asset_name: str, overwrite_existing: bool) -> Any:
    package_path = asset_path(asset_name)
    existing = load_asset(package_path)
    if existing is not None:
        if not overwrite_existing:
            fail(
                f"{package_path} already exists. The script stopped without modifying it. "
                "Set OVERWRITE_EXISTING = True only when you intentionally want to replace it."
            )
        log(f"Deleting existing material function: {package_path}")
        warn(
            "Existing generated DWC materials may log temporary 'Missing Material Function' "
            "warnings while this function is replaced. Run all three DWC MF scripts, then "
            "run Generate Materials for each affected Wet Clothing Asset."
        )
        checkout_asset(package_path)
        if not EAL.delete_asset(package_path):
            fail(f"Could not delete existing asset: {package_path}")

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.MaterialFunctionFactoryNew()
    material_function = asset_tools.create_asset(
        asset_name,
        FUNCTION_ROOT,
        unreal.MaterialFunction,
        factory,
    )
    if material_function is None:
        fail(f"Could not create material function: {package_path}")
    set_property(
        material_function,
        "description",
        "Generated once by the explicit DWC Unreal Python authoring script. Manual edits are preserved unless the script is run with OVERWRITE_EXISTING=True.",
        required=False,
    )
    return material_function


def finalize_material_function(material_function: Any) -> None:
    updater = getattr(MEL, "update_material_function", None)
    if callable(updater):
        try:
            updater(material_function, None)
        except TypeError:
            updater(material_function)
    material_function.modify()
    package_path = material_function.get_path_name().split(".")[0]
    save_asset(package_path)
    log(f"Created and saved {package_path}")


def _import_texture_if_missing(asset_name: str, filename: str) -> Any:
    package_path = asset_path(asset_name, DEFAULT_ROOT)
    existing = load_asset(package_path)
    if existing is not None:
        return existing

    source_file = os.path.join(RESOURCE_DIR, filename)
    if not os.path.isfile(source_file):
        fail(f"Missing packaged script resource: {source_file}")

    task = unreal.AssetImportTask()
    set_property(task, "filename", source_file)
    set_property(task, "destination_path", DEFAULT_ROOT)
    set_property(task, "destination_name", asset_name)
    set_property(task, "automated", True)
    set_property(task, "replace_existing", False)
    set_property(task, "save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported = load_asset(package_path)
    if imported is None:
        fail(f"Could not import fallback texture: {package_path}")
    return imported


def _configure_data_texture(texture: Any) -> None:
    changed = set_property_if_changed(texture, "srgb", False)
    compression_enum = getattr(unreal, "TextureCompressionSettings", None)
    if compression_enum is not None:
        for name in ("TC_VECTOR_DISPLACEMENTMAP", "TC_HDR", "TC_DEFAULT"):
            if hasattr(compression_enum, name):
                changed |= set_property_if_changed(texture, "compression_settings", getattr(compression_enum, name))
                break
    mip_enum = getattr(unreal, "TextureMipGenSettings", None)
    if mip_enum is not None and hasattr(mip_enum, "TMGS_NO_MIPMAPS"):
        changed |= set_property_if_changed(texture, "mip_gen_settings", mip_enum.TMGS_NO_MIPMAPS)
    filter_enum = getattr(unreal, "TextureFilter", None)
    if filter_enum is not None and hasattr(filter_enum, "TF_NEAREST"):
        changed |= set_property_if_changed(texture, "filter", filter_enum.TF_NEAREST)
    address_enum = getattr(unreal, "TextureAddress", None)
    if address_enum is not None and hasattr(address_enum, "TA_CLAMP"):
        changed |= set_property_if_changed(texture, "address_x", address_enum.TA_CLAMP)
        changed |= set_property_if_changed(texture, "address_y", address_enum.TA_CLAMP)
    if changed:
        texture.modify()
        save_asset(texture.get_path_name().split(".")[0])


def _configure_normal_texture(texture: Any) -> None:
    changed = set_property_if_changed(texture, "srgb", False)
    compression_enum = getattr(unreal, "TextureCompressionSettings", None)
    if compression_enum is not None:
        for name in ("TC_VECTOR_DISPLACEMENTMAP", "TC_DEFAULT"):
            if hasattr(compression_enum, name):
                changed |= set_property_if_changed(texture, "compression_settings", getattr(compression_enum, name))
                break
    mip_enum = getattr(unreal, "TextureMipGenSettings", None)
    if mip_enum is not None and hasattr(mip_enum, "TMGS_NO_MIPMAPS"):
        changed |= set_property_if_changed(texture, "mip_gen_settings", mip_enum.TMGS_NO_MIPMAPS)
    filter_enum = getattr(unreal, "TextureFilter", None)
    if filter_enum is not None and hasattr(filter_enum, "TF_BILINEAR"):
        changed |= set_property_if_changed(texture, "filter", filter_enum.TF_BILINEAR)
    address_enum = getattr(unreal, "TextureAddress", None)
    if address_enum is not None and hasattr(address_enum, "TA_WRAP"):
        changed |= set_property_if_changed(texture, "address_x", address_enum.TA_WRAP)
        changed |= set_property_if_changed(texture, "address_y", address_enum.TA_WRAP)
    if changed:
        texture.modify()
        save_asset(texture.get_path_name().split(".")[0])


def ensure_default_textures() -> tuple[Any, Any]:
    data_texture = _import_texture_if_missing(
        DATA_FALLBACK_NAME, "T_DWC_DefaultProfileData.png"
    )
    normal_texture = _import_texture_if_missing(
        NORMAL_FALLBACK_NAME, "T_DWC_DefaultSurfaceNormal.png"
    )
    _configure_data_texture(data_texture)
    _configure_normal_texture(normal_texture)

    array_path = asset_path(NORMAL_ARRAY_FALLBACK_NAME, DEFAULT_ROOT)
    normal_array = load_asset(array_path)
    if normal_array is None:
        factory_class = getattr(unreal, "Texture2DArrayFactory", None)
        texture_array_class = getattr(unreal, "Texture2DArray", None)
        if factory_class is None or texture_array_class is None:
            fail("Texture2DArrayFactory/Texture2DArray is unavailable in this editor build.")
        factory = factory_class()
        set_first_property(
            factory,
            ("initial_textures", "source_textures"),
            [normal_texture],
            required=False,
        )
        normal_array = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            NORMAL_ARRAY_FALLBACK_NAME,
            DEFAULT_ROOT,
            texture_array_class,
            factory,
        )
        if normal_array is None:
            fail(f"Could not create fallback Texture2DArray: {array_path}")

    array_changed = False
    current_sources = get_property(normal_array, "source_textures", [])
    if list(current_sources or []) != [normal_texture]:
        array_changed |= set_first_property(
            normal_array,
            ("source_textures",),
            [normal_texture],
            required=False,
        ) is not None
    if array_changed:
        update_source = getattr(normal_array, "update_source_from_source_textures", None)
        if callable(update_source):
            try:
                update_source(False)
            except TypeError:
                update_source()
    array_changed |= set_property_if_changed(normal_array, "srgb", False)
    compression_enum = getattr(unreal, "TextureCompressionSettings", None)
    if compression_enum is not None:
        for name in ("TC_VECTOR_DISPLACEMENTMAP", "TC_DEFAULT"):
            if hasattr(compression_enum, name):
                array_changed |= set_property_if_changed(normal_array, "compression_settings", getattr(compression_enum, name))
                break
    if array_changed:
        normal_array.modify()
        save_asset(array_path)
    return data_texture, normal_array


def ensure_default_texture_arrays() -> tuple[Any, Any, Any]:
    data_texture, normal_array = ensure_default_textures()

    array_path = asset_path(MASK_ARRAY_FALLBACK_NAME, DEFAULT_ROOT)
    mask_array = load_asset(array_path)
    if mask_array is None:
        factory_class = getattr(unreal, "Texture2DArrayFactory", None)
        texture_array_class = getattr(unreal, "Texture2DArray", None)
        if factory_class is None or texture_array_class is None:
            fail("Texture2DArrayFactory/Texture2DArray is unavailable in this editor build.")
        factory = factory_class()
        set_first_property(
            factory,
            ("initial_textures", "source_textures"),
            [data_texture],
            required=False,
        )
        mask_array = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MASK_ARRAY_FALLBACK_NAME,
            DEFAULT_ROOT,
            texture_array_class,
            factory,
        )
        if mask_array is None:
            fail(f"Could not create fallback Texture2DArray: {array_path}")

    array_changed = False
    current_sources = get_property(mask_array, "source_textures", [])
    if list(current_sources or []) != [data_texture]:
        array_changed |= set_first_property(
            mask_array,
            ("source_textures",),
            [data_texture],
            required=False,
        ) is not None
    if array_changed:
        update_source = getattr(mask_array, "update_source_from_source_textures", None)
        if callable(update_source):
            try:
                update_source(False)
            except TypeError:
                update_source()
    array_changed |= set_property_if_changed(mask_array, "srgb", False)
    compression_enum = getattr(unreal, "TextureCompressionSettings", None)
    if compression_enum is not None and hasattr(compression_enum, "TC_MASKS"):
        array_changed |= set_property_if_changed(mask_array, "compression_settings", compression_enum.TC_MASKS)
    if array_changed:
        mask_array.modify()
        save_asset(array_path)
    return data_texture, normal_array, mask_array


def linear_color_sampler() -> Any:
    return enum_value(
        unreal.MaterialSamplerType,
        "SAMPLERTYPE_LINEAR_COLOR",
        "LINEAR_COLOR",
    )


def normal_sampler() -> Any:
    return enum_value(
        unreal.MaterialSamplerType,
        "SAMPLERTYPE_NORMAL",
        "NORMAL",
    )


def mask_sampler() -> Any:
    return enum_value(
        unreal.MaterialSamplerType,
        "SAMPLERTYPE_MASKS",
        "MASKS",
        "SAMPLERTYPE_LINEAR_COLOR",
        "LINEAR_COLOR",
    )


def validate_ue58_authoring_api() -> None:
    """Fail before asset creation when the required UE 5.8 API is missing."""
    required_classes = (
        "MaterialExpressionFunctionInput",
        "MaterialExpressionFunctionOutput",
        "MaterialExpressionNamedRerouteDeclaration",
        "MaterialExpressionNamedRerouteUsage",
        "MaterialExpressionMaterialFunctionCall",
        "MaterialExpressionTextureSampleParameter2D",
        "MaterialExpressionTextureSampleParameter2DArray",
        "MaterialExpressionStaticSwitchParameter",
        "MaterialExpressionCustom",
        "Vector4f",
    )
    missing_classes = [
        name for name in required_classes if getattr(unreal, name, None) is None
    ]
    if missing_classes:
        fail(
            "This Unreal Editor Python build is missing required UE 5.8 "
            "classes: " + ", ".join(missing_classes)
        )

    required_mel_methods = (
        "create_material_expression_in_function",
        "connect_material_expressions",
    )
    missing_methods = [
        name for name in required_mel_methods if not callable(getattr(MEL, name, None))
    ]
    if missing_methods:
        fail(
            "MaterialEditingLibrary is missing required methods: "
            + ", ".join(missing_methods)
        )

    bridge = getattr(unreal, "DWCMaterialGraphScriptingLibrary", None)
    required_bridge_methods = (
        "create_material_function_comment",
        "create_material_function_named_reroute_usage",
    )
    missing_bridge = [
        name
        for name in required_bridge_methods
        if bridge is None or not callable(getattr(bridge, name, None))
    ]
    if missing_bridge:
        fail(
            "The patched DWCEditor UE 5.8 scripting bridge is not loaded: "
            + ", ".join(missing_bridge)
            + ". Rebuild the DWCEditor module and restart Unreal Editor."
        )


def run_entry(build_callable: Any) -> None:
    try:
        validate_ue58_authoring_api()
        build_callable()
    except Exception as exc:
        unreal.log_error(f"[DWC MF] {type(exc).__name__}: {exc}")
        raise
