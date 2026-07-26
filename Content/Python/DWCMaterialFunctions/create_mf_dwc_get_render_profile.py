"""Create or recreate MF_DWC_GetRenderProfile with a spacious hierarchical graph."""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal
import dwc_mf_common as c

OVERWRITE_EXISTING = True
ASSET_NAME = "MF_DWC_GetRenderProfile"


def build() -> None:
    data_fallback, _ = c.ensure_default_textures()
    mf = c.create_or_replace_material_function(ASSET_NAME, OVERWRITE_EXISTING)

    # Parent comments are deliberately oversized; child comments have at least
    # 400 graph units of space between them.
    c.create_comment(mf, "1. Inputs & Profile Identification", -9000, -2400, 6200, 3000, parent=True)
    c.create_comment(mf, "1-1. Function Input", -8600, -1950, 1500, 1900)
    c.create_comment(mf, "1-2. Profile ID Sampling", -6650, -1950, 1500, 1900)
    c.create_comment(mf, "1-3. Local Profile ID Decode", -4700, -1950, 1500, 1900)

    c.create_comment(mf, "2. Runtime Profile Lookup", -2400, -2400, 6500, 3000, parent=True)
    c.create_comment(mf, "2-1. Profile Remap Lookup", -2000, -1950, 1700, 1900)
    c.create_comment(mf, "2-2. Global LUT Coordinates", 150, -1950, 1700, 1900)
    c.create_comment(mf, "2-3. Global Profile Sampling", 2300, -1950, 1800, 1900)

    c.create_comment(mf, "3. Profile Decode & Outputs", 4500, -2400, 6900, 3000, parent=True)
    c.create_comment(mf, "3-1. Profile Texel 0 Decode", 4900, -1950, 1800, 1900)
    c.create_comment(mf, "3-2. Profile Texel 1 Decode", 7150, -1950, 1800, 1900)
    c.create_comment(mf, "3-3. Function Outputs", 9400, -1950, 1600, 1900)

    # 1-1 Function Input
    data_uv = c.function_input(
        mf, "DWCDataUV", "vector2", (0.0, 0.0), 0,
        -8350, -1450,
        "DWC Data UV used to read the slot-local Wet Part Data Texture.",
    )
    in_data_uv = c.named_declaration(mf, "IN_DWCDataUV", data_uv, ("", "Result"), -7700, -1450)

    # 1-2 Profile ID Sampling
    data_uv_use = c.named_usage(mf, in_data_uv, -6400, -1450)
    profile_id = c.texture2d_parameter(
        mf, "DWC_WetPartDataTexture", data_fallback, -5950, -1450,
        sampler_type=c.linear_color_sampler(), group="DWC Render Profile",
        description="Slot-local Wet Part data. R=Local Profile ID, G=Droplet Detail Size, B=Rivulet Detail Size.",
    )
    c.try_connect(data_uv_use, ("", "Result"), profile_id, ("Coordinates", "UVs"))
    profile_id_r = c.component_mask(mf, profile_id, "R", "R", -5350, -1450)
    sampled_profile_id = c.named_declaration(
        mf, "PROFILE_SampledProfileID", profile_id_r, ("", "Result"), -5200, -1200
    )
    droplet_detail_encoded = c.component_mask(mf, profile_id, "G", "G", -5350, -850)
    rivulet_detail_encoded = c.component_mask(mf, profile_id, "B", "B", -5350, -450)
    droplet_detail = c.custom_expression(
        mf,
        "return lerp(0.25, 4.0, saturate(Encoded));",
        [("Encoded", droplet_detail_encoded, ("", "Result"))],
        "float1", -4700, -850,
        "Decode the Part-local Droplet Detail Size from the G channel.",
    )
    rivulet_detail = c.custom_expression(
        mf,
        "return lerp(0.25, 4.0, saturate(Encoded));",
        [("Encoded", rivulet_detail_encoded, ("", "Result"))],
        "float1", -4700, -450,
        "Decode the Part-local Rivulet Detail Size from the B channel.",
    )
    droplet_detail_decl = c.named_declaration(
        mf, "PART_DropletDetailSize", droplet_detail, ("", "Result"), -4000, -850
    )
    rivulet_detail_decl = c.named_declaration(
        mf, "PART_RivuletDetailSize", rivulet_detail, ("", "Result"), -4000, -450
    )

    # 1-3 Local Profile ID Decode
    sampled_id_use = c.named_usage(mf, sampled_profile_id, -4450, -1450)
    local_id = c.custom_expression(
        mf,
        "return floor(saturate(ProfileID) * 255.0 + 0.5);",
        [("ProfileID", sampled_id_use, ("", "Result"))],
        "float1", -4000, -1450,
        "Decode the normalized profile-id texel into an integer Local Profile ID.",
    )
    local_id_decl = c.named_declaration(
        mf, "PROFILE_LocalProfileID", local_id, ("", "Result"), -3400, -1450
    )

    # 2-1 Profile Remap Lookup
    local_id_use = c.named_usage(mf, local_id_decl, -1750, -1450)
    remap_uv = c.custom_expression(
        mf,
        "return float2((LocalProfileID + 0.5) / 256.0, 0.5);",
        [("LocalProfileID", local_id_use, ("", "Result"))],
        "float2", -1300, -1450,
        "Build the texel-center UV for the 256-entry Local Profile remap LUT.",
    )
    remap_lut = c.texture2d_parameter(
        mf, "DWC_ProfileRemapLUT", data_fallback, -750, -1450,
        sampler_type=c.linear_color_sampler(), group="DWC Render Profile",
        description="Maps Local Profile ID to the first U coordinate of its global 2-texel profile.",
    )
    c.try_connect(remap_uv, ("", "Result"), remap_lut, ("Coordinates", "UVs"))
    remap_r = c.component_mask(mf, remap_lut, "R", "R", -250, -1450)
    global_start_decl = c.named_declaration(
        mf, "PROFILE_GlobalProfileStartU", remap_r, ("", "Result"), -100, -1200
    )

    # 2-2 Global LUT Coordinates
    start_u0 = c.named_usage(mf, global_start_decl, 400, -1550)
    start_u1 = c.named_usage(mf, global_start_decl, 400, -950)
    half0 = c.scalar_constant(mf, 0.5, 700, -1350, "LUT center V")
    half1 = c.scalar_constant(mf, 0.5, 700, -750, "LUT center V")
    texel_size = c.scalar_parameter(
        mf, "DWC_GlobalRenderProfileTexelSize", 1.0 / 510.0, 700, -1050,
        group="DWC Render Profile",
        description="U width of one texel in the runtime global Render Profile LUT.",
    )
    one = c.scalar_constant(mf, 1.0, 700, -450)
    offset = c.multiply(mf, texel_size, ("", "Result"), one, ("", "Result"), 1050, -550)
    profile1_u = c.add(mf, start_u1, ("", "Result"), offset, ("", "Result"), 1400, -950)
    profile0_uv = c.append_vector(mf, start_u0, ("", "Result"), half0, ("", "Result"), 1400, -1550)
    profile1_uv = c.append_vector(mf, profile1_u, ("", "Result"), half1, ("", "Result"), 1650, -950)
    profile0_uv_decl = c.named_declaration(mf, "PROFILE_Texel0UV", profile0_uv, ("", "Result"), 1700, -1550)
    profile1_uv_decl = c.named_declaration(mf, "PROFILE_Texel1UV", profile1_uv, ("", "Result"), 1700, -750)

    # 2-3 Global Profile Sampling and optional fallback.
    uv0_use = c.named_usage(mf, profile0_uv_decl, 2500, -1550)
    uv1_use = c.named_usage(mf, profile1_uv_decl, 2500, -850)
    lut0 = c.texture2d_parameter(
        mf, "DWC_GlobalRenderProfileLUT", data_fallback, 2850, -1550,
        sampler_type=c.linear_color_sampler(), group="DWC Render Profile",
        description="Runtime global Render Profile LUT, sampled at profile texel 0.",
    )
    lut1 = c.texture2d_parameter(
        mf, "DWC_GlobalRenderProfileLUT", data_fallback, 2850, -850,
        sampler_type=c.linear_color_sampler(), group="DWC Render Profile",
        description="Runtime global Render Profile LUT, sampled at profile texel 1.",
    )
    c.try_connect(uv0_use, ("", "Result"), lut0, ("Coordinates", "UVs"))
    c.try_connect(uv1_use, ("", "Result"), lut1, ("Coordinates", "UVs"))
    # Texture Sample's primary output is RGB, so append A explicitly to preserve
    # the packed fourth channels used by the render-profile data.
    lut0_rgba = c.append_vector(mf, lut0, "RGB", lut0, "A", 3150, -1650, "Reconstruct packed RGBA texel 0.")
    lut1_rgba = c.append_vector(mf, lut1, "RGB", lut1, "A", 3150, -950, "Reconstruct packed RGBA texel 1.")
    fallback0 = c.vector_parameter(
        mf, "DWC_FallbackRenderProfile0", (0.5, 0.5, 0.0, 0.0), 3200, -1250,
        group="DWC Render Profile", description="Fallback packed profile texel 0.",
    )
    fallback1 = c.vector_parameter(
        mf, "DWC_FallbackRenderProfile1", (1.0, 0.5, 0.2, 0.5), 3200, -550,
        group="DWC Render Profile", description="Fallback packed profile texel 1.",
    )
    fallback0_rgba = c.append_vector(
        mf, fallback0, "RGB", fallback0, "A", 3500, -1250,
        "Reconstruct packed RGBA fallback profile texel 0.",
    )
    fallback1_rgba = c.append_vector(
        mf, fallback1, "RGB", fallback1, "A", 3500, -550,
        "Reconstruct packed RGBA fallback profile texel 1.",
    )
    # This must be a scalar parameter, not a Static Switch: the runtime resource
    # subsystem updates it on each MID with SetScalarParameterValue.
    use_runtime_lut = c.scalar_parameter(
        mf, "DWC_UseRenderProfileLUT", 0.0, 3200, -200,
        group="DWC Render Profile", description="0 uses fallback profile values; 1 uses runtime LUT data.",
    )
    profile0 = c.lerp(
        mf, fallback0_rgba, ("", "Result"), lut0_rgba, ("", "Result"),
        use_runtime_lut, ("", "Result"), 3800, -1550,
        "Select fallback or runtime packed profile texel 0.",
    )
    profile1 = c.lerp(
        mf, fallback1_rgba, ("", "Result"), lut1_rgba, ("", "Result"),
        use_runtime_lut, ("", "Result"), 3800, -850,
        "Select fallback or runtime packed profile texel 1.",
    )
    texel0_decl = c.named_declaration(mf, "PROFILE_Texel0", profile0, ("", "Result"), 4100, -1350)
    texel1_decl = c.named_declaration(mf, "PROFILE_Texel1", profile1, ("", "Result"), 4100, -650)

    # 3-1 Decode packed texel 0.
    texel0_uses = [c.named_usage(mf, texel0_decl, 5150, -1600 + i * 380) for i in range(4)]
    texel0_names = [
        ("AbsorbedDarkeningStrength", "R", "PROFILE_AbsorbedDarkeningStrength"),
        ("AbsorbedGlossinessStrength", "G", "PROFILE_AbsorbedGlossinessStrength"),
        ("DropletNormalSlice", "B", "PROFILE_DropletNormalSlice"),
        ("RivuletNormalSlice", "A", "PROFILE_RivuletNormalSlice"),
    ]
    profile_decls: dict[str, object] = {}
    for i, (output_name, channel, reroute_name) in enumerate(texel0_names):
        mask = c.component_mask(mf, texel0_uses[i], ("", "Result"), channel, 5550, -1600 + i * 380)
        profile_decls[output_name] = c.named_declaration(
            mf, reroute_name, mask, ("", "Result"), 6100, -1600 + i * 380
        )

    # 3-2 Decode packed texel 1.
    texel1_uses = [c.named_usage(mf, texel1_decl, 7400, -1500 + i * 480) for i in range(4)]
    texel1_names = [
        ("SurfaceWaterNormalStrength", "R", "PROFILE_SurfaceWaterNormalStrength"),
        ("SurfaceWaterRoughnessStrength", "G", "PROFILE_SurfaceWaterRoughnessStrength"),
        ("SurfaceVisibilityThreshold", "B", "PROFILE_SurfaceVisibilityThreshold"),
        ("RivuletUVScrollSpeed", "A", "PROFILE_RivuletUVScrollSpeed"),
    ]
    for i, (output_name, channel, reroute_name) in enumerate(texel1_names):
        mask = c.component_mask(mf, texel1_uses[i], ("", "Result"), channel, 7800, -1500 + i * 480)
        profile_decls[output_name] = c.named_declaration(
            mf, reroute_name, mask, ("", "Result"), 8350, -1500 + i * 480
        )

    profile_decls["DropletDetailSize"] = droplet_detail_decl
    profile_decls["RivuletDetailSize"] = rivulet_detail_decl

    # 3-3 Function outputs use only local Named Reroute usages; no wires cross comments.
    ordered_outputs = [
        "AbsorbedDarkeningStrength", "AbsorbedGlossinessStrength",
        "DropletNormalSlice", "RivuletNormalSlice",
        "SurfaceWaterNormalStrength", "SurfaceWaterRoughnessStrength",
        "SurfaceVisibilityThreshold", "RivuletUVScrollSpeed",
        "DropletDetailSize", "RivuletDetailSize",
    ]
    descriptions = {
        "AbsorbedDarkeningStrength": "Absorbed wetness base-color darkening strength.",
        "AbsorbedGlossinessStrength": "Absorbed wetness roughness blend strength.",
        "DropletNormalSlice": "Droplet normal Texture2DArray slice.",
        "RivuletNormalSlice": "Rivulet normal Texture2DArray slice.",
        "SurfaceWaterNormalStrength": "Common surface-water detail-normal strength.",
        "SurfaceWaterRoughnessStrength": "Surface-water roughness blend strength.",
        "SurfaceVisibilityThreshold": "Visible amount threshold for droplet/rivulet coverage.",
        "RivuletUVScrollSpeed": "Flow-axis scroll speed for rivulet detail normals.",
        "DropletDetailSize": "Part-local physical-looking size of the Droplet detail pattern.",
        "RivuletDetailSize": "Part-local physical-looking size of the Rivulet detail pattern.",
    }
    for i, name in enumerate(ordered_outputs):
        usage = c.named_usage(mf, profile_decls[name], 9650, -1700 + i * 240)
        c.function_output(
            mf, name, usage, ("", "Result"), i, 10400, -1700 + i * 240, descriptions[name]
        )

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
