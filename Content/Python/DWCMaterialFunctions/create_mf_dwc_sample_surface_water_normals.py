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
    _, normal_array_fallback, mask_array_fallback = c.ensure_default_texture_arrays()
    mf = c.create_or_replace_material_function(ASSET_NAME, OVERWRITE_EXISTING)

    c.create_comment(mf, "1. Inputs & Shared Flow Data", -10500, -3200, 7200, 4200, parent=True)
    c.create_comment(mf, "1-1. UV & Time Inputs", -10100, -2750, 1900, 3200)
    c.create_comment(mf, "1-2. Profile Sampling Inputs", -7700, -2750, 1900, 3200)
    c.create_comment(mf, "1-3. Flow Angle Decode", -5300, -2750, 1600, 3200)

    c.create_comment(mf, "2. Detail Normal UV", -2800, -3200, 5000, 4200, parent=True)
    c.create_comment(mf, "2-1. Droplet Normal UV", -2400, -2750, 1800, 3200)
    c.create_comment(mf, "2-2. Rivulet Flow-Aligned UV", -100, -2750, 1900, 3200)

    c.create_comment(mf, "3. TextureArray Sampling", 2700, -3200, 8200, 4200, parent=True)
    c.create_comment(mf, "3-1. Droplet Mask & Normal Sampling", 3100, -2750, 2200, 3200)
    c.create_comment(mf, "3-2. Rivulet Mask & Normal Sampling", 5700, -2750, 2200, 3200)
    c.create_comment(mf, "3-3. Slice 0 Flat-Normal Fallback", 7700, -2750, 1900, 3200)

    c.create_comment(mf, "4. Function Outputs", 10500, -3200, 2800, 4200, parent=True)
    c.create_comment(mf, "4-1. Normal Outputs", 10900, -2750, 2000, 3200)

    # 1-1 UV & Time Inputs
    inputs: dict[str, object] = {}
    declarations: dict[str, object] = {}
    specs = [
        ("SurfaceWaterNormalUV", "vector2", (0.0, 0.0), "Mesh UV used for repeating surface-water detail normals."),
        ("SurfaceTime", "scalar", (0.0,), "Runtime surface-water time used for rivulet scrolling."),
        ("DropletDetailSize", "scalar", (1.0,), "Part-local Droplet detail-pattern size."),
        ("RivuletDetailSize", "scalar", (1.0,), "Part-local Rivulet detail-pattern size."),
    ]
    for i, (name, kind, preview, desc) in enumerate(specs):
        node = c.function_input(mf, name, kind, preview, i, -9800, -2250 + i * 650, desc)
        inputs[name] = node
        declarations[name] = c.named_declaration(
            mf, f"IN_{name}", node, ("", "Result"), -9000, -2250 + i * 650
        )

    # 1-2 Profile Sampling Inputs
    profile_specs = [
        ("DropletMaskSlice", (0.0,), "Droplet mask Texture2DArray slice."),
        ("DropletNormalSlice", (0.0,), "Droplet normal Texture2DArray slice."),
        ("RivuletMaskSlice", (0.0,), "Rivulet mask Texture2DArray slice."),
        ("RivuletNormalSlice", (0.0,), "Rivulet normal Texture2DArray slice."),
        ("RivuletEncodedFlowAngle", (0.75,), "SurfaceWaterNormalUV-space flow angle encoded to 0..1."),
        ("RivuletUVScrollSpeed", (0.0,), "Rivulet scroll speed along the flow axis."),
    ]
    for i, (name, preview, desc) in enumerate(profile_specs):
        node = c.function_input(mf, name, "scalar", preview, 4 + i, -7400, -2250 + i * 650, desc)
        inputs[name] = node
        prefix = "PROFILE_" if name != "RivuletEncodedFlowAngle" else "SURFACE_"
        declarations[name] = c.named_declaration(
            mf, f"{prefix}{name}", node, ("", "Result"), -6600, -2250 + i * 650
        )

    # 1-3 Flow Angle Decode
    encoded_use = c.named_usage(mf, declarations["RivuletEncodedFlowAngle"], -5050, -2050)
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
    flip_x = c.scalar_parameter(
        mf, "DWC_SurfaceWaterNormalFlipX", 0.0, -5050, 50,
        group="DWC Surface Water",
        description="Preview/runtime debug switch: invert the final surface-water tangent normal X channel when > 0.5.",
    )
    flip_y = c.scalar_parameter(
        mf, "DWC_SurfaceWaterNormalFlipY", 0.0, -5050, 600,
        group="DWC Surface Water",
        description="Preview/runtime debug switch: invert the final surface-water tangent normal Y channel when > 0.5.",
    )
    flip_x_decl = c.named_declaration(mf, "SHARED_NormalFlipX", flip_x, ("", "Result"), -4050, 50)
    flip_y_decl = c.named_declaration(mf, "SHARED_NormalFlipY", flip_y, ("", "Result"), -4050, 600)

    # 2-1 Droplet UV
    normal_uv_use = c.named_usage(mf, declarations["SurfaceWaterNormalUV"], -2150, -1750)
    droplet_size_use = c.named_usage(mf, declarations["DropletDetailSize"], -2150, -1050)
    droplet_uv = c.custom_expression(
        mf,
        "return UV / max(DetailSize, 1.0e-4);",
        [("UV", normal_uv_use, ("", "Result")), ("DetailSize", droplet_size_use, ("", "Result"))],
        "float2", -1450, -1450,
        "Scale the UV by the Part-local Droplet Detail Size. Droplets remain UV-fixed and do not rotate or scroll.",
    )
    droplet_uv_decl = c.named_declaration(
        mf, "SURFACE_DropletNormalUV", droplet_uv, ("", "Result"), -850, -1450
    )

    # 2-2 Flow-aligned rivulet UV
    rivulet_uv_use = c.named_usage(mf, declarations["SurfaceWaterNormalUV"], 100, -2350)
    rivulet_size_use = c.named_usage(mf, declarations["RivuletDetailSize"], 100, -1850)
    time_use = c.named_usage(mf, declarations["SurfaceTime"], 100, -1350)
    scroll_use = c.named_usage(mf, declarations["RivuletUVScrollSpeed"], 100, -850)
    flow_use2 = c.named_usage(mf, flow_decl, 100, -350)
    across_use2 = c.named_usage(mf, across_decl, 100, 100)
    aligned_uv = c.custom_expression(
        mf,
        """
float2 Aligned = float2(dot(UV, AcrossDirection), dot(UV, FlowDirection));
Aligned /= max(DetailSize, 1.0e-4);
Aligned.y += SurfaceTime * ScrollSpeed;
return Aligned;
""",
        [
            ("UV", rivulet_uv_use, ("", "Result")),
            ("DetailSize", rivulet_size_use, ("", "Result")),
            ("SurfaceTime", time_use, ("", "Result")),
            ("ScrollSpeed", scroll_use, ("", "Result")),
            ("FlowDirection", flow_use2, ("", "Result")),
            ("AcrossDirection", across_use2, ("", "Result")),
        ],
        "float2", 850, -1450,
        "Rotate UV into across/flow coordinates, then apply Part-local size and flow-axis scroll.",
    )
    rivulet_uv_decl = c.named_declaration(
        mf, "SURFACE_RivuletNormalUV", aligned_uv, ("", "Result"), 1450, -1450
    )

    # 3-1 Droplet array sampling
    droplet_uv_use2 = c.named_usage(mf, droplet_uv_decl, 3350, -1800)
    droplet_mask_slice_use = c.named_usage(mf, declarations["DropletMaskSlice"], 3350, -1250)
    droplet_normal_slice_use = c.named_usage(mf, declarations["DropletNormalSlice"], 3350, -850)
    droplet_mask_array_uv = c.append_vector(
        mf, droplet_uv_use2, ("", "Result"), droplet_mask_slice_use, ("", "Result"),
        3900, -1650, "Append the runtime Texture2DArray slice to droplet mask UV."
    )
    droplet_normal_array_uv = c.append_vector(
        mf, droplet_uv_use2, ("", "Result"), droplet_normal_slice_use, ("", "Result"),
        3900, -1200, "Append the runtime Texture2DArray slice to droplet normal UV."
    )
    droplet_mask_sample = c.texture2d_array_parameter(
        mf, "DWC_DropletMaskTextureArray", mask_array_fallback, 4450, -1850,
        sampler_type=c.mask_sampler(), group="DWC Surface Water",
        description="Global droplet mask Texture2DArray. Slice 0 is treated as no mask.",
    )
    c.try_connect(droplet_mask_array_uv, ("", "Result"), droplet_mask_sample, ("Coordinates", "UVs"))
    droplet_sample = c.texture2d_array_parameter(
        mf, "DWC_DropletNormalTextureArray", normal_array_fallback, 4450, -1050,
        sampler_type=c.normal_sampler(), group="DWC Surface Water",
        description="Global droplet normal Texture2DArray. Slice 0 is flat normal.",
    )
    c.try_connect(droplet_normal_array_uv, ("", "Result"), droplet_sample, ("Coordinates", "UVs"))
    droplet_mask_raw_decl = c.named_declaration(
        mf, "SURFACE_DropletMaskRaw", droplet_mask_sample, ("R", ""), 4700, -2100
    )
    droplet_raw_decl = c.named_declaration(
        mf, "SURFACE_DropletNormalRaw", droplet_sample, ("RGB", ""), 4700, -500
    )
    droplet_mask_raw_use = c.named_usage(mf, droplet_mask_raw_decl, 7950, -2500)
    droplet_mask_slice_use2 = c.named_usage(mf, declarations["DropletMaskSlice"], 7950, -2100)
    droplet_mask_safe = c.custom_expression(
        mf,
        "return DropletMaskSlice > 0.5 ? saturate(MaskValue) : 0.0;",
        [
            ("MaskValue", droplet_mask_raw_use, ("", "Result")),
            ("DropletMaskSlice", droplet_mask_slice_use2, ("", "Result")),
        ],
        "float1", 8500, -2300,
        "Reference-style droplet mask: use the authored/baked mask slice; slice 0 means no droplet-shaped contribution.",
    )
    droplet_raw_for_decode = c.named_usage(mf, droplet_raw_decl, 7950, -1500)
    droplet_normal_slice_use2 = c.named_usage(mf, declarations["DropletNormalSlice"], 7950, -1100)
    droplet_flip_x_use = c.named_usage(mf, flip_x_decl, 7950, -700)
    droplet_flip_y_use = c.named_usage(mf, flip_y_decl, 7950, -300)
    droplet_decoded = c.custom_expression(
        mf,
        """
if (DropletNormalSlice <= 0.5)
{
    return float3(0.0, 0.0, 1.0);
}
float3 N = normalize(SampledNormal);
float2 FlipSign = float2(FlipX > 0.5 ? -1.0 : 1.0, FlipY > 0.5 ? -1.0 : 1.0);
return normalize(float3((-N.xy) * FlipSign, N.z));
""",
        [
            ("SampledNormal", droplet_raw_for_decode, ("", "Result")),
            ("DropletNormalSlice", droplet_normal_slice_use2, ("", "Result")),
            ("FlipX", droplet_flip_x_use, ("", "Result")),
            ("FlipY", droplet_flip_y_use, ("", "Result")),
        ],
        "float3", 8500, -1400,
        "Normalize the tangent-space droplet normal already decoded by the Normal sampler, flip XY to match DWC convex-water convention, or return flat for slice 0.",
    )

    # 3-2 Rivulet array sampling
    rivulet_uv_use2 = c.named_usage(mf, rivulet_uv_decl, 5950, -1800)
    rivulet_mask_slice_use = c.named_usage(mf, declarations["RivuletMaskSlice"], 5950, -1250)
    rivulet_normal_slice_use = c.named_usage(mf, declarations["RivuletNormalSlice"], 5950, -850)
    rivulet_mask_array_uv = c.append_vector(
        mf, rivulet_uv_use2, ("", "Result"), rivulet_mask_slice_use, ("", "Result"),
        6500, -1650, "Append the runtime Texture2DArray slice to rivulet mask UV."
    )
    rivulet_normal_array_uv = c.append_vector(
        mf, rivulet_uv_use2, ("", "Result"), rivulet_normal_slice_use, ("", "Result"),
        6500, -1200, "Append the runtime Texture2DArray slice to rivulet normal UV."
    )
    rivulet_mask_sample = c.texture2d_array_parameter(
        mf, "DWC_RivuletMaskTextureArray", mask_array_fallback, 7050, -1850,
        sampler_type=c.mask_sampler(), group="DWC Surface Water",
        description="Global rivulet mask Texture2DArray. Slice 0 is treated as no mask.",
    )
    c.try_connect(rivulet_mask_array_uv, ("", "Result"), rivulet_mask_sample, ("Coordinates", "UVs"))
    rivulet_sample = c.texture2d_array_parameter(
        mf, "DWC_RivuletNormalTextureArray", normal_array_fallback, 7050, -1050,
        sampler_type=c.normal_sampler(), group="DWC Surface Water",
        description="Global rivulet normal Texture2DArray. Slice 0 is flat normal.",
    )
    c.try_connect(rivulet_normal_array_uv, ("", "Result"), rivulet_sample, ("Coordinates", "UVs"))
    rivulet_mask_raw_decl = c.named_declaration(
        mf, "SURFACE_RivuletMaskRaw", rivulet_mask_sample, ("R", ""), 7300, -2100
    )
    rivulet_raw_decl = c.named_declaration(
        mf, "SURFACE_RivuletNormalRaw", rivulet_sample, ("RGB", ""), 7300, -500
    )
    rivulet_mask_raw_use = c.named_usage(mf, rivulet_mask_raw_decl, 7950, -300)
    rivulet_mask_slice_use2 = c.named_usage(mf, declarations["RivuletMaskSlice"], 7950, 100)
    rivulet_mask_safe = c.custom_expression(
        mf,
        "return RivuletMaskSlice > 0.5 ? saturate(MaskValue) : 0.0;",
        [
            ("MaskValue", rivulet_mask_raw_use, ("", "Result")),
            ("RivuletMaskSlice", rivulet_mask_slice_use2, ("", "Result")),
        ],
        "float1", 8500, -100,
        "Reference-style rivulet mask: use the authored/baked mask slice; slice 0 means no rivulet-shaped contribution.",
    )
    rivulet_raw_for_decode = c.named_usage(mf, rivulet_raw_decl, 7950, 550)
    rivulet_normal_slice_use2 = c.named_usage(mf, declarations["RivuletNormalSlice"], 7950, 950)
    rivulet_flip_x_use = c.named_usage(mf, flip_x_decl, 7950, 1350)
    rivulet_flip_y_use = c.named_usage(mf, flip_y_decl, 7950, 1750)
    rivulet_decoded = c.custom_expression(
        mf,
        """
if (RivuletNormalSlice <= 0.5)
{
    return float3(0.0, 0.0, 1.0);
}
float3 N = normalize(SampledNormal);
float2 FlipSign = float2(FlipX > 0.5 ? -1.0 : 1.0, FlipY > 0.5 ? -1.0 : 1.0);
return normalize(float3((-N.xy) * FlipSign, N.z));
""",
        [
            ("SampledNormal", rivulet_raw_for_decode, ("", "Result")),
            ("RivuletNormalSlice", rivulet_normal_slice_use2, ("", "Result")),
            ("FlipX", rivulet_flip_x_use, ("", "Result")),
            ("FlipY", rivulet_flip_y_use, ("", "Result")),
        ],
        "float3", 8500, 750,
        "Normalize the tangent-space rivulet normal already decoded by the Normal sampler, flip XY to match DWC convex-water convention, or return flat for slice 0.",
    )

    # 3-3 Profile-level enable is encoded by the render profile slice.
    # Slice 0 is a flat normal, so profiles that do not use a given normal source
    # still compile the same material permutation and simply sample slice 0.
    droplet_mask_final_decl = c.named_declaration(
        mf, "SURFACE_DropletMask", droplet_mask_safe, ("", "Result"), 9200, -2300
    )
    droplet_final_decl = c.named_declaration(
        mf, "SURFACE_DropletNormal", droplet_decoded, ("", "Result"), 9200, -1400
    )
    rivulet_mask_final_decl = c.named_declaration(
        mf, "SURFACE_RivuletMask", rivulet_mask_safe, ("", "Result"), 9200, -100
    )
    rivulet_final_decl = c.named_declaration(
        mf, "SURFACE_RivuletNormal", rivulet_decoded, ("", "Result"), 9200, 750
    )

    # 4-1 Outputs
    droplet_mask_out_use = c.named_usage(mf, droplet_mask_final_decl, 11200, -2450)
    droplet_out_use = c.named_usage(mf, droplet_final_decl, 11200, -1650)
    rivulet_mask_out_use = c.named_usage(mf, rivulet_mask_final_decl, 11200, -850)
    rivulet_out_use = c.named_usage(mf, rivulet_final_decl, 11200, -50)
    c.function_output(
        mf, "DropletMask", droplet_mask_out_use, ("", "Result"), 0,
        12200, -2450, "Sampled droplet mask, or zero when the profile has no droplet mask slice."
    )
    c.function_output(
        mf, "DropletNormal", droplet_out_use, ("", "Result"), 1,
        12200, -1650, "Raw tangent-space droplet normal without coverage or strength."
    )
    c.function_output(
        mf, "RivuletMask", rivulet_mask_out_use, ("", "Result"), 2,
        12200, -850, "Sampled rivulet mask, or zero when the profile has no rivulet mask slice."
    )
    c.function_output(
        mf, "RivuletNormal", rivulet_out_use, ("", "Result"), 3,
        12200, -50, "Raw tangent-space rivulet normal aligned to the stored flow angle."
    )

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
