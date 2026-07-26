"""Create or recreate MF_DWC_SampleSurfaceWaterNormals with hierarchical comments."""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import dwc_mf_common as c

OVERWRITE_EXISTING = True
ASSET_NAME = "MF_DWC_SampleSurfaceWaterNormals"


def build() -> None:
    _, normal_array_fallback = c.ensure_default_textures()
    mf = c.create_or_replace_material_function(ASSET_NAME, OVERWRITE_EXISTING)

    c.create_comment(mf, "1. Inputs & Shared Flow Data", -10500, -3200, 7200, 4200, parent=True)
    c.create_comment(mf, "1-1. UV & Time Inputs", -10100, -2750, 1900, 3200)
    c.create_comment(mf, "1-2. Profile Sampling Inputs", -7700, -2750, 1900, 3200)
    c.create_comment(mf, "1-3. Flow Angle Decode", -5300, -2750, 1600, 3200)

    c.create_comment(mf, "2. Detail Normal UV", -2800, -3200, 5000, 4200, parent=True)
    c.create_comment(mf, "2-1. Droplet Normal UV", -2400, -2750, 1800, 3200)
    c.create_comment(mf, "2-2. Streak Flow-Aligned UV", -100, -2750, 1900, 3200)

    c.create_comment(mf, "3. TextureArray Sampling", 2700, -3200, 7300, 4200, parent=True)
    c.create_comment(mf, "3-1. Droplet Normal Sampling", 3100, -2750, 1800, 3200)
    c.create_comment(mf, "3-2. Streak Normal Sampling", 5400, -2750, 1800, 3200)
    c.create_comment(mf, "3-3. Static Feature Selection", 7700, -2750, 1900, 3200)

    c.create_comment(mf, "4. Function Outputs", 10500, -3200, 2800, 4200, parent=True)
    c.create_comment(mf, "4-1. Normal Outputs", 10900, -2750, 2000, 3200)

    # 1-1 UV & Time Inputs
    inputs: dict[str, object] = {}
    declarations: dict[str, object] = {}
    specs = [
        ("SurfaceWaterNormalUV", "vector2", (0.0, 0.0), "Mesh UV used for repeating surface-water detail normals."),
        ("SurfaceTime", "scalar", (0.0,), "Runtime surface-water time used for streak scrolling."),
        ("DropletUVTiling", "vector2", (1.0, 1.0), "Droplet detail-normal repeat count in X/Y."),
        ("StreakUVTiling", "vector2", (1.0, 1.0), "Streak repeat count across/along flow."),
    ]
    for i, (name, kind, preview, desc) in enumerate(specs):
        node = c.function_input(mf, name, kind, preview, i, -9800, -2250 + i * 650, desc)
        inputs[name] = node
        declarations[name] = c.named_declaration(
            mf, f"IN_{name}", node, ("", "Result"), -9000, -2250 + i * 650
        )

    # 1-2 Profile Sampling Inputs
    profile_specs = [
        ("DropletNormalSlice", (0.0,), "Droplet normal Texture2DArray slice."),
        ("StreakNormalSlice", (0.0,), "Streak normal Texture2DArray slice."),
        ("EncodedFlowAngle", (0.75,), "SurfaceWaterNormalUV-space flow angle encoded to 0..1."),
        ("StreakDetailMotionSpeed", (0.0,), "Streak scroll speed along the flow axis."),
    ]
    for i, (name, preview, desc) in enumerate(profile_specs):
        node = c.function_input(mf, name, "scalar", preview, 4 + i, -7400, -2250 + i * 650, desc)
        inputs[name] = node
        prefix = "PROFILE_" if name != "EncodedFlowAngle" else "SURFACE_"
        declarations[name] = c.named_declaration(
            mf, f"{prefix}{name}", node, ("", "Result"), -6600, -2250 + i * 650
        )

    # 1-3 Flow Angle Decode
    encoded_use = c.named_usage(mf, declarations["EncodedFlowAngle"], -5050, -2050)
    flow_dir = c.custom_expression(
        mf,
        "float Angle = (EncodedAngle - 0.5) * 6.28318530718;\nreturn float2(cos(Angle), sin(Angle));",
        [("EncodedAngle", encoded_use, ("", "Result"))],
        "float2", -4650, -2050,
        "Decode RT.A into the SurfaceWaterNormalUV-space flow direction.",
    )
    flow_decl = c.named_declaration(mf, "SHARED_FlowDirection", flow_dir, ("", "Result"), -4050, -2050)
    flow_use = c.named_usage(mf, flow_decl, -5050, -950)
    across_dir = c.custom_expression(
        mf,
        "return float2(FlowDirection.y, -FlowDirection.x);",
        [("FlowDirection", flow_use, ("", "Result"))],
        "float2", -4650, -950,
        "Build the UV direction perpendicular to flow.",
    )
    across_decl = c.named_declaration(mf, "SHARED_AcrossDirection", across_dir, ("", "Result"), -4050, -950)

    # 2-1 Droplet UV
    normal_uv_use = c.named_usage(mf, declarations["SurfaceWaterNormalUV"], -2150, -1750)
    droplet_tiling_use = c.named_usage(mf, declarations["DropletUVTiling"], -2150, -1050)
    droplet_uv = c.multiply(
        mf, normal_uv_use, ("", "Result"), droplet_tiling_use, ("", "Result"),
        -1450, -1450, "Apply material-slot droplet UV tiling."
    )
    droplet_uv_decl = c.named_declaration(
        mf, "SURFACE_DropletNormalUV", droplet_uv, ("", "Result"), -850, -1450
    )

    # 2-2 Flow-aligned streak UV
    streak_uv_use = c.named_usage(mf, declarations["SurfaceWaterNormalUV"], 100, -2350)
    streak_tiling_use = c.named_usage(mf, declarations["StreakUVTiling"], 100, -1850)
    time_use = c.named_usage(mf, declarations["SurfaceTime"], 100, -1350)
    scroll_use = c.named_usage(mf, declarations["StreakDetailMotionSpeed"], 100, -850)
    flow_use2 = c.named_usage(mf, flow_decl, 100, -350)
    across_use2 = c.named_usage(mf, across_decl, 100, 100)
    aligned_uv = c.custom_expression(
        mf,
        """
float2 Aligned = float2(dot(UV, AcrossDirection), dot(UV, FlowDirection));
Aligned *= Tiling;
Aligned.y += SurfaceTime * ScrollSpeed;
return Aligned;
""",
        [
            ("UV", streak_uv_use, ("", "Result")),
            ("Tiling", streak_tiling_use, ("", "Result")),
            ("SurfaceTime", time_use, ("", "Result")),
            ("ScrollSpeed", scroll_use, ("", "Result")),
            ("FlowDirection", flow_use2, ("", "Result")),
            ("AcrossDirection", across_use2, ("", "Result")),
        ],
        "float2", 850, -1450,
        "Rotate UV into across/flow coordinates, then apply tiling and flow-axis scroll.",
    )
    streak_uv_decl = c.named_declaration(
        mf, "SURFACE_StreakNormalUV", aligned_uv, ("", "Result"), 1450, -1450
    )

    # 3-1 Droplet array sampling
    droplet_uv_use2 = c.named_usage(mf, droplet_uv_decl, 3350, -1800)
    droplet_slice_use = c.named_usage(mf, declarations["DropletNormalSlice"], 3350, -1100)
    droplet_array_uv = c.append_vector(
        mf, droplet_uv_use2, ("", "Result"), droplet_slice_use, ("", "Result"),
        3900, -1450, "Append the runtime Texture2DArray slice to droplet UV."
    )
    droplet_sample = c.texture2d_array_parameter(
        mf, "DWC_DropletNormalTextureArray", normal_array_fallback, 4450, -1450,
        sampler_type=c.normal_sampler(), group="DWC Surface Water",
        description="Global droplet normal Texture2DArray. Slice 0 is flat normal.",
    )
    c.try_connect(droplet_array_uv, ("", "Result"), droplet_sample, ("Coordinates", "UVs"))
    droplet_raw_decl = c.named_declaration(
        mf, "SURFACE_DropletNormalRaw", droplet_sample, ("RGB", ""), 4700, -800
    )

    # 3-2 Streak array sampling
    streak_uv_use2 = c.named_usage(mf, streak_uv_decl, 5650, -1800)
    streak_slice_use = c.named_usage(mf, declarations["StreakNormalSlice"], 5650, -1100)
    streak_array_uv = c.append_vector(
        mf, streak_uv_use2, ("", "Result"), streak_slice_use, ("", "Result"),
        6200, -1450, "Append the runtime Texture2DArray slice to streak UV."
    )
    streak_sample = c.texture2d_array_parameter(
        mf, "DWC_StreakNormalTextureArray", normal_array_fallback, 6750, -1450,
        sampler_type=c.normal_sampler(), group="DWC Surface Water",
        description="Global streak normal Texture2DArray. Slice 0 is flat normal.",
    )
    c.try_connect(streak_array_uv, ("", "Result"), streak_sample, ("Coordinates", "UVs"))
    streak_raw_decl = c.named_declaration(
        mf, "SURFACE_StreakNormalRaw", streak_sample, ("RGB", ""), 7000, -800
    )

    # 3-3 Static feature selection
    flat_normal = c.vector_constant(mf, (0.0, 0.0, 1.0), 7950, -200, "Flat tangent-space normal")
    droplet_raw_use = c.named_usage(mf, droplet_raw_decl, 7950, -1850)
    streak_raw_use = c.named_usage(mf, streak_raw_decl, 7950, -950)
    use_droplet = c.static_switch_parameter(
        mf, "DWC_UseDropletNormal", False,
        droplet_raw_use, ("", "Result"), flat_normal, ("", "Result"), 8500, -1850,
        group="DWC Surface Water",
        description="Compile droplet normal sampling only when this material slot needs it.",
    )
    use_streak = c.static_switch_parameter(
        mf, "DWC_UseStreakNormal", False,
        streak_raw_use, ("", "Result"), flat_normal, ("", "Result"), 8500, -950,
        group="DWC Surface Water",
        description="Compile streak normal sampling only when this material slot needs it.",
    )
    droplet_final_decl = c.named_declaration(
        mf, "SURFACE_DropletNormal", use_droplet, ("", "Result"), 9200, -1850
    )
    streak_final_decl = c.named_declaration(
        mf, "SURFACE_StreakNormal", use_streak, ("", "Result"), 9200, -950
    )

    # 4-1 Outputs
    droplet_out_use = c.named_usage(mf, droplet_final_decl, 11200, -1650)
    streak_out_use = c.named_usage(mf, streak_final_decl, 11200, -750)
    c.function_output(
        mf, "DropletNormal", droplet_out_use, ("", "Result"), 0,
        12200, -1650, "Raw tangent-space droplet normal without coverage or strength."
    )
    c.function_output(
        mf, "StreakNormal", streak_out_use, ("", "Result"), 1,
        12200, -750, "Raw tangent-space streak normal aligned to the stored flow angle."
    )

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
