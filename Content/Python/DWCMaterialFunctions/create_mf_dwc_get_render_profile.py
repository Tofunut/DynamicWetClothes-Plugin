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

    c.create_comment(mf, "3. Profile Decode & Outputs", 4500, -2400, 7600, 3400, parent=True)
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
        description="Slot-local Wet Part data. R=Local Profile ID, G=Static Droplet Size, B=Flow Droplet Size, A=reserved.",
    )
    c.try_connect(data_uv_use, ("", "Result"), profile_id, ("Coordinates", "UVs"))
    profile_id_r = c.component_mask(mf, profile_id, "R", "R", -5350, -1450)
    sampled_profile_id = c.named_declaration(
        mf, "PROFILE_SampledProfileID", profile_id_r, ("", "Result"), -5200, -1200
    )
    droplet_detail_encoded = c.component_mask(mf, profile_id, "G", "G", -5350, -850)
    droplet_detail = c.custom_expression(
        mf,
        "return lerp(0.0, 4.0, saturate(Encoded));",
        [("Encoded", droplet_detail_encoded, ("", "Result"))],
        "float1", -4700, -850,
        "Decode the Part-local Droplet Detail Size from the G channel.",
    )
    droplet_detail_decl = c.named_declaration(
        mf, "PART_DropletDetailSize", droplet_detail, ("", "Result"), -4000, -850
    )
    droplet_flow_detail_encoded = c.component_mask(mf, profile_id, "B", "B", -5350, -250)
    droplet_flow_detail = c.custom_expression(
        mf,
        "return lerp(0.0, 4.0, saturate(Encoded));",
        [("Encoded", droplet_flow_detail_encoded, ("", "Result"))],
        "float1", -4700, -250,
        "Decode the Part-local Flow Droplet Size from the B channel.",
    )
    droplet_flow_detail_decl = c.named_declaration(
        mf, "PART_DropletFlowDetailSize", droplet_flow_detail, ("", "Result"), -4000, -250
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
        description="Maps Local Profile ID to the first U coordinate of its global 7-texel profile.",
    )
    c.try_connect(remap_uv, ("", "Result"), remap_lut, ("Coordinates", "UVs"))
    remap_r = c.component_mask(mf, remap_lut, "R", "R", -250, -1450)
    global_start_decl = c.named_declaration(
        mf, "PROFILE_GlobalProfileStartU", remap_r, ("", "Result"), -100, -1200
    )

    # 2-2/2-3 Sample all seven packed texels. The profile start U points at
    # texel 0; subsequent texels are one global LUT texel apart.
    texel_size = c.scalar_parameter(
        mf, "DWC_GlobalRenderProfileTexelSize", 1.0 / 1785.0, 500, -1900,
        group="DWC Render Profile",
        description="U width of one texel in the 255 x 7 runtime Render Profile LUT.",
    )
    use_runtime_lut = c.scalar_parameter(
        mf, "DWC_UseRenderProfileLUT", 0.0, 3200, -2100,
        group="DWC Render Profile",
        description="0 uses fallback profile values; 1 uses runtime LUT data.",
    )
    fallback_defaults = [
        (0.5, 0.5, 0.0, 0.0),
        (1.0, 0.5, 0.25, 0.5),
        (0.0, 0.0, 0.02, 0.5),
        (90.0, 2.0, 0.05, 0.15),
        (0.0, 0.0, 0.0, 0.0),
        (0.5, 0.02, 0.85, 0.5),
        (1.0, 1.0, 3.0, 0.0),
    ]
    packed_texels = []
    for texel_index in range(7):
        y = -1700 + texel_index * 650
        start_use = c.named_usage(mf, global_start_decl, 500, y)
        texel_uv = c.custom_expression(
            mf,
            f"return float2(StartU + {float(texel_index):.1f} * TexelSize, 0.5);",
            [
                ("StartU", start_use, ("", "Result")),
                ("TexelSize", texel_size, ("", "Result")),
            ],
            "float2", 1100, y,
            f"Build the center UV for packed Render Profile texel {texel_index}.",
        )
        lut_sample = c.texture2d_parameter(
            mf, "DWC_GlobalRenderProfileLUT", data_fallback, 1900, y,
            sampler_type=c.linear_color_sampler(), group="DWC Render Profile",
            description=f"Runtime global Render Profile LUT texel {texel_index}.",
        )
        c.try_connect(texel_uv, ("", "Result"), lut_sample, ("Coordinates", "UVs"))
        lut_rgba = c.append_vector(
            mf, lut_sample, "RGB", lut_sample, "A", 2500, y,
            f"Reconstruct packed RGBA texel {texel_index}.",
        )
        fallback = c.vector_parameter(
            mf, f"DWC_FallbackRenderProfile{texel_index}",
            fallback_defaults[texel_index], 3150, y + 250,
            group="DWC Render Profile",
            description=f"Fallback packed profile texel {texel_index}.",
        )
        fallback_rgba = c.append_vector(
            mf, fallback, "RGB", fallback, "A", 3600, y + 250,
            f"Reconstruct fallback RGBA texel {texel_index}.",
        )
        selected = c.lerp(
            mf, fallback_rgba, ("", "Result"), lut_rgba, ("", "Result"),
            use_runtime_lut, ("", "Result"), 4200, y,
            f"Select fallback or runtime packed profile texel {texel_index}.",
        )
        packed_texels.append(c.named_declaration(
            mf, f"PROFILE_Texel{texel_index}", selected, ("", "Result"), 4850, y
        ))

    # Packed layout must remain identical to DWCGPUResourceSubsystem.cpp.
    packed_outputs = [
        ("AbsorbedDarkeningStrength", 0, "R"),
        ("AbsorbedGlossinessStrength", 0, "G"),
        ("DropletNormalSlice", 0, "B"),
        ("DropletFlowEnabled", 0, "A"),
        ("SurfaceWaterNormalStrength", 1, "R"),
        ("SurfaceWaterRoughnessBlend", 1, "G"),
        ("DropletFlowSpeed", 1, "B"),
        ("SurfaceWaterSpecular", 1, "A"),
        ("DropletMaskSlice", 2, "R"),
        ("DropletFlowNoiseSlice", 2, "G"),
        ("SurfaceWaterTargetRoughness", 2, "B"),
        ("SurfaceWaterTotalStrength", 2, "A"),
        ("DropletFlowDirectionDegrees", 3, "R"),
        ("DropletFlowNoiseTiling", 3, "G"),
        ("DropletFlowNoiseStrength", 3, "B"),
        ("DropletFlowNoiseSpeed", 3, "A"),
        ("DropletFlowNormalSlice", 4, "R"),
        ("DropletFlowMaskSlice", 4, "G"),
        ("DropletFlowTotalStrength", 5, "R"),
        ("DropletFlowTargetRoughness", 5, "G"),
        ("DropletFlowRoughnessBlend", 5, "B"),
        ("DropletFlowSpecular", 5, "A"),
        ("SurfaceWaterColorBlend", 6, "R"),
        ("DropletFlowColorBlend", 6, "G"),
        ("DropletFlowNormalStrength", 6, "B"),
    ]
    profile_decls: dict[str, object] = {}
    for output_index, (name, texel_index, channel) in enumerate(packed_outputs):
        y = -2000 + output_index * 230
        texel_use = c.named_usage(mf, packed_texels[texel_index], 5600, y)
        mask = c.component_mask(mf, texel_use, ("", "Result"), channel, 6100, y)
        profile_decls[name] = c.named_declaration(
            mf, f"PROFILE_{name}", mask, ("", "Result"), 6700, y
        )
    profile_decls["DropletDetailSize"] = droplet_detail_decl
    profile_decls["DropletFlowDetailSize"] = droplet_flow_detail_decl

    ordered_outputs = [name for name, _, _ in packed_outputs] + [
        "DropletDetailSize",
        "DropletFlowDetailSize",
    ]
    descriptions = {
        "AbsorbedDarkeningStrength": "Absorbed wetness base-color darkening strength.",
        "AbsorbedGlossinessStrength": "Absorbed wetness roughness blend strength.",
        "DropletNormalSlice": "Stationary Droplet normal Texture2DArray slice.",
        "DropletFlowEnabled": "Whether the independently stamped Flow Droplet path is enabled.",
        "SurfaceWaterNormalStrength": "Common surface-water detail-normal strength.",
        "SurfaceWaterRoughnessBlend": "Surface-water roughness blend strength.",
        "DropletFlowSpeed": "Signed Flow Droplet panning speed in UV per second.",
        "SurfaceWaterSpecular": "Wet surface target specular.",
        "DropletMaskSlice": "Stationary Droplet mask Texture2DArray slice.",
        "DropletFlowNoiseSlice": "Flow noise Texture2DArray slice.",
        "SurfaceWaterTargetRoughness": "Wet surface target roughness.",
        "SurfaceWaterTotalStrength": "Overall Surface Water rendering response strength.",
        "DropletFlowDirectionDegrees": "Flow direction in UV-space degrees.",
        "DropletFlowNoiseTiling": "Flow noise UV tiling.",
        "DropletFlowNoiseStrength": "Flow noise sideways bend strength.",
        "DropletFlowNoiseSpeed": "Signed Flow noise panning speed.",
        "DropletFlowNormalSlice": "Flow Droplet normal Texture2DArray slice.",
        "DropletFlowMaskSlice": "Flow Droplet mask Texture2DArray slice.",
        "DropletFlowTotalStrength": "Overall Flow Droplet rendering response strength.",
        "DropletFlowTargetRoughness": "Flow Droplet target roughness.",
        "DropletFlowRoughnessBlend": "Flow Droplet roughness blend strength.",
        "DropletFlowSpecular": "Flow Droplet target specular.",
        "SurfaceWaterColorBlend": "Stationary Droplet Base Color blend strength.",
        "DropletFlowColorBlend": "Flow Droplet Base Color blend strength.",
        "DropletFlowNormalStrength": "Flow Droplet detail-normal strength.",
        "DropletDetailSize": "Part-local physical-looking size of the stationary Droplet detail pattern.",
        "DropletFlowDetailSize": "Part-local physical-looking size of the Flow Droplet detail pattern.",
    }
    for output_index, name in enumerate(ordered_outputs):
        y = -2100 + output_index * 230
        usage = c.named_usage(mf, profile_decls[name], 7600, y)
        c.function_output(
            mf, name, usage, ("", "Result"), output_index, 8400, y, descriptions[name]
        )

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
