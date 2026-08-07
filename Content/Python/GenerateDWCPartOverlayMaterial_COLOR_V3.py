# Copyright 2026 Team Tofunut. All Rights Reserved.
"""Generate Original-UV Part Preview overlay materials for UE 5.8.

Version 2026-08-05-color-v3.

Creates eight precompiled variants so the Part Editor can select the WCA's
OriginalUVChannelIndex without constructing or compiling materials at runtime:

    /DynamicWetClothes/Editor/Materials/M_DWC_PartPreviewOverlay_UV0
    ...
    /DynamicWetClothes/Editor/Materials/M_DWC_PartPreviewOverlay_UV7

The viewport uses UMeshComponent::SetOverlayMaterial on the original skeletal
mesh. No duplicate skeletal mesh and no procedural overlay geometry are used.
"""
from __future__ import annotations
import unreal

OUTPUT_FOLDER = "/DynamicWetClothes/Editor/Materials"
MATERIAL_PREFIX = "M_DWC_PartPreviewOverlay_UV"
UV_CHANNEL_COUNT = 8

PART_TEXTURE = "DWC_PartPreviewColorTexture"
SELECTION_TEXTURE = "DWC_PartPreviewSelectionTexture"
PART_OPACITY = "DWC_PartPreviewColorOpacity"
SELECTION_FILL_OPACITY = "DWC_PartPreviewSelectionFillOpacity"
SELECTION_BOUNDARY_OPACITY = "DWC_PartPreviewSelectionBoundaryOpacity"
SELECTION_FILL_COLOR = "DWC_PartPreviewSelectionFillColor"
SELECTION_BOUNDARY_COLOR = "DWC_PartPreviewSelectionBoundaryColor"

ENGINE_BLACK_TEXTURE = "/Engine/EngineResources/Black.Black"


def log(message: str) -> None:
    unreal.log(f"[DWC Original UV Preview] {message}")


def require_class(name: str):
    value = getattr(unreal, name, None)
    if value is None:
        raise RuntimeError(f"Missing Unreal Python class: {name}")
    return value


def node(material, class_name: str, x: int, y: int):
    value = unreal.MaterialEditingLibrary.create_material_expression(
        material, require_class(class_name), x, y
    )
    if value is None:
        raise RuntimeError(f"Could not create {class_name}")
    return value


def scalar(material, name: str, default: float, x: int, y: int):
    value = node(material, "MaterialExpressionScalarParameter", x, y)
    value.set_editor_property("parameter_name", unreal.Name(name))
    value.set_editor_property("default_value", float(default))
    return value


def vector(material, name: str, default: unreal.LinearColor, x: int, y: int):
    value = node(material, "MaterialExpressionVectorParameter", x, y)
    value.set_editor_property("parameter_name", unreal.Name(name))
    value.set_editor_property("default_value", default)
    return value


def connect(source, output: str, target, input_name: str) -> None:
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        source, output, target, input_name
    ):
        raise RuntimeError(
            f"Could not connect {source.get_name()}:{output or '<default>'} "
            f"to {target.get_name()}:{input_name or '<default>'}"
        )


def connect_property(source, output: str, material_property) -> None:
    if not unreal.MaterialEditingLibrary.connect_material_property(
        source, output, material_property
    ):
        raise RuntimeError(
            f"Could not connect {source.get_name()} to {material_property}"
        )


def load_or_create(name: str):
    path = f"{OUTPUT_FOLDER}/{name}"
    # Avoid calling load_asset for a path that is not registered yet. UE logs a
    # scary LoadAsset error for a perfectly normal first-time creation.
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        existing = unreal.EditorAssetLibrary.load_asset(path)
        if existing is None:
            raise RuntimeError(f"Could not load existing material: {path}")
        if not isinstance(existing, unreal.Material):
            raise RuntimeError(f"{path} exists but is not a Material")
        return existing

    unreal.EditorAssetLibrary.make_directory(OUTPUT_FOLDER)
    created = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name,
        OUTPUT_FOLDER,
        unreal.Material,
        unreal.MaterialFactoryNew(),
    )
    if created is None:
        raise RuntimeError(f"Could not create {path}")
    return created




def texture_parameter(material, name: str, uv_node, x: int, y: int):
    """Create a color texture parameter with a safe engine default.

    The default texture is /Engine/EngineResources/Black.Black, which is an
    sRGB color texture. Therefore the node must use SAMPLERTYPE_COLOR. The
    runtime preview textures may still have SRGB disabled; the texture resource
    controls whether sRGB decoding is applied when the MID overrides the
    parameter.
    """
    value = node(material, "MaterialExpressionTextureSampleParameter2D", x, y)
    value.set_editor_property("parameter_name", unreal.Name(name))

    sampler_enum = getattr(unreal, "MaterialSamplerType", None)
    color_sampler = (
        getattr(sampler_enum, "SAMPLERTYPE_COLOR", None)
        if sampler_enum is not None else None
    )
    if color_sampler is None:
        raise RuntimeError("MaterialSamplerType.SAMPLERTYPE_COLOR is unavailable")
    default_texture = unreal.EditorAssetLibrary.load_asset(ENGINE_BLACK_TEXTURE)
    if default_texture is None:
        raise RuntimeError(f"Could not load default texture: {ENGINE_BLACK_TEXTURE}")

    # Set the texture first, then explicitly override the sampler. Some UE
    # builds re-infer SamplerType when Texture is assigned. Doing it in this
    # order prevents the node from falling back to Linear Color for Black.Black.
    value.set_editor_property("texture", default_texture)
    value.set_editor_property("sampler_type", color_sampler)
    try:
        value.post_edit_change()
    except Exception:
        pass

    # UE Python versions expose the texture-coordinate input under different
    # display names. An empty target input selects the first input (UVs).
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        uv_node, "", value, ""
    ):
        input_names = list(
            unreal.MaterialEditingLibrary.get_material_expression_input_names(value)
        )
        raise RuntimeError(
            f"Could not connect {uv_node.get_name()}:<default> to "
            f"{value.get_name()}:<first input>; available inputs={input_names}"
        )

    # Fail early with a useful message if the properties were not accepted.
    actual_sampler = value.get_editor_property("sampler_type")
    actual_texture = value.get_editor_property("texture")
    if actual_sampler != color_sampler:
        raise RuntimeError(
            f"Sampler type write did not stick for {name}: {actual_sampler}"
        )
    if actual_texture is None or actual_texture.get_path_name() != ENGINE_BLACK_TEXTURE:
        actual_path = "<None>" if actual_texture is None else actual_texture.get_path_name()
        raise RuntimeError(
            f"Default texture write did not stick for {name}: {actual_path}"
        )
    log(
        f"Verified {name}: sampler={actual_sampler}, "
        f"texture={actual_texture.get_path_name()}"
    )
    return value


def configure(material: unreal.Material, uv_channel: int) -> None:
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    unreal.MaterialEditingLibrary.set_base_material_usage(
        material, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH, True
    )

    uv = node(material, "MaterialExpressionTextureCoordinate", -1400, 20)
    uv.set_editor_property("coordinate_index", uv_channel)

    part_sample = texture_parameter(
        material, PART_TEXTURE, uv, -1160, -260
    )
    selection_sample = texture_parameter(
        material, SELECTION_TEXTURE, uv, -1160, 260
    )

    # Read the texture sample channels directly. In UE 5.8 some Python
    # builds reject TextureSample -> ComponentMask connections even when the
    # graph pins are visible. TextureSample's named outputs are stable and
    # avoid that version-dependent ComponentMask input path entirely:
    #   Part texture      RGB = display color, A = assigned mask
    #   Selection texture R   = fill mask,    G = boundary mask

    fill_color = vector(
        material, SELECTION_FILL_COLOR,
        unreal.LinearColor(1.0, 0.24, 0.01, 1.0), -900, 500
    )
    boundary_color = vector(
        material, SELECTION_BOUNDARY_COLOR,
        unreal.LinearColor(1.0, 0.72, 0.02, 1.0), -900, 650
    )

    fill_lerp = node(material, "MaterialExpressionLinearInterpolate", -560, -170)
    connect(part_sample, "RGB", fill_lerp, "A")
    connect(fill_color, "", fill_lerp, "B")
    connect(selection_sample, "R", fill_lerp, "Alpha")

    boundary_lerp = node(material, "MaterialExpressionLinearInterpolate", -300, -100)
    connect(fill_lerp, "", boundary_lerp, "A")
    connect(boundary_color, "", boundary_lerp, "B")
    connect(selection_sample, "G", boundary_lerp, "Alpha")
    connect_property(boundary_lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    part_opacity = scalar(material, PART_OPACITY, 0.72, -660, 120)
    fill_opacity = scalar(material, SELECTION_FILL_OPACITY, 0.72, -660, 260)
    boundary_opacity = scalar(material, SELECTION_BOUNDARY_OPACITY, 1.0, -660, 400)

    part_mul = node(material, "MaterialExpressionMultiply", -390, 120)
    connect(part_sample, "A", part_mul, "A")
    connect(part_opacity, "", part_mul, "B")
    fill_mul = node(material, "MaterialExpressionMultiply", -390, 260)
    connect(selection_sample, "R", fill_mul, "A")
    connect(fill_opacity, "", fill_mul, "B")
    boundary_mul = node(material, "MaterialExpressionMultiply", -390, 400)
    connect(selection_sample, "G", boundary_mul, "A")
    connect(boundary_opacity, "", boundary_mul, "B")

    first_max = node(material, "MaterialExpressionMax", -120, 210)
    connect(part_mul, "", first_max, "A")
    connect(fill_mul, "", first_max, "B")
    final_max = node(material, "MaterialExpressionMax", 100, 280)
    connect(first_max, "", final_max, "A")
    connect(boundary_mul, "", final_max, "B")
    connect_property(final_max, "", unreal.MaterialProperty.MP_OPACITY)

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    try:
        material.post_edit_change()
    except Exception:
        pass
    errors = unreal.MaterialEditingLibrary.recompile_material(material)
    # UE 5.8 returns Array[str]. An empty array means success. Converting [] to
    # text produces the non-empty string "[]", which caused the old script to
    # report a false failure even though compilation succeeded.
    compile_errors = [str(error).strip() for error in (errors or []) if str(error).strip()]
    if compile_errors:
        raise RuntimeError(
            f"{material.get_path_name()} compilation errors:\n"
            + "\n".join(compile_errors)
        )
    material.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)


SCRIPT_VERSION = "2026-08-05-color-v3"


def main() -> None:
    log(f"Script version: {SCRIPT_VERSION}")
    log(f"Default texture: {ENGINE_BLACK_TEXTURE}; sampler: Color")
    for uv_channel in range(UV_CHANNEL_COUNT):
        name = f"{MATERIAL_PREFIX}{uv_channel}"
        material = load_or_create(name)
        configure(material, uv_channel)
        log(f"Generated {OUTPUT_FOLDER}/{name}")
    log("Completed all Original-UV preview material variants.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[DWC Original UV Preview] FAILED: {exc}")
        raise
