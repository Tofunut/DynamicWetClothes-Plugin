"""Create or recreate MF_DWC_SampleSurfaceWaterNormals as a droplet-only sampler."""
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

    c.create_comment(mf, "1. Inputs", -7600, -2600, 3000, 3600, parent=True)
    c.create_comment(mf, "2. Droplet UV", -4000, -2600, 2400, 3600, parent=True)
    c.create_comment(mf, "3. Droplet Array Sampling", -1000, -2600, 5200, 3600, parent=True)
    c.create_comment(mf, "4. Outputs", 4800, -2600, 2400, 3600, parent=True)

    specs = [
        ("SurfaceWaterNormalUV", "vector2", (0.0, 0.0), "Mesh UV used for repeated droplet detail."),
        ("DropletDetailSize", "scalar", (1.0,), "Part-local droplet detail size."),
        ("DropletMaskSlice", "scalar", (0.0,), "Droplet mask Texture2DArray slice."),
        ("DropletNormalSlice", "scalar", (0.0,), "Droplet normal Texture2DArray slice."),
    ]
    declarations: dict[str, object] = {}
    for i, (name, kind, preview, desc) in enumerate(specs):
        node = c.function_input(mf, name, kind, preview, i, -7200, -2050 + i * 650, desc)
        declarations[name] = c.named_declaration(
            mf, f"IN_{name}", node, ("", "Result"), -6200, -2050 + i * 650
        )

    flip_x = c.scalar_parameter(
        mf, "DWC_SurfaceWaterNormalFlipX", 0.0, -7200, 800,
        group="DWC Surface Water",
        description="Invert the final droplet tangent-normal X channel when greater than 0.5.",
    )
    flip_y = c.scalar_parameter(
        mf, "DWC_SurfaceWaterNormalFlipY", 0.0, -6200, 800,
        group="DWC Surface Water",
        description="Invert the final droplet tangent-normal Y channel when greater than 0.5.",
    )
    flip_x_decl = c.named_declaration(mf, "SHARED_NormalFlipX", flip_x, ("", "Result"), -5400, 600)
    flip_y_decl = c.named_declaration(mf, "SHARED_NormalFlipY", flip_y, ("", "Result"), -5400, 1050)

    uv_use = c.named_usage(mf, declarations["SurfaceWaterNormalUV"], -3650, -1650)
    size_use = c.named_usage(mf, declarations["DropletDetailSize"], -3650, -950)
    detail_uv = c.custom_expression(
        mf,
        "return UV / max(DetailSize, 1.0e-4);",
        [("UV", uv_use, ("", "Result")), ("DetailSize", size_use, ("", "Result"))],
        "float2", -2850, -1300,
        "Scale the mesh UV by the part-local droplet detail size.",
    )
    detail_uv_decl = c.named_declaration(
        mf, "SURFACE_DropletNormalUV", detail_uv, ("", "Result"), -2050, -1300
    )

    uv_for_mask = c.named_usage(mf, detail_uv_decl, -650, -1850)
    uv_for_normal = c.named_usage(mf, detail_uv_decl, -650, -900)
    mask_slice = c.named_usage(mf, declarations["DropletMaskSlice"], -650, -1450)
    normal_slice = c.named_usage(mf, declarations["DropletNormalSlice"], -650, -500)
    mask_array_uv = c.append_vector(
        mf, uv_for_mask, ("", "Result"), mask_slice, ("", "Result"),
        0, -1700, "Append the droplet mask array slice to the detail UV.",
    )
    normal_array_uv = c.append_vector(
        mf, uv_for_normal, ("", "Result"), normal_slice, ("", "Result"),
        0, -750, "Append the droplet normal array slice to the detail UV.",
    )

    mask_sample = c.texture2d_array_parameter(
        mf, "DWC_DropletMaskTextureArray", mask_array_fallback, 650, -1850,
        sampler_type=c.mask_sampler(), group="DWC Surface Water",
        description="Global droplet mask Texture2DArray. Slice 0 means no authored mask.",
    )
    normal_sample = c.texture2d_array_parameter(
        mf, "DWC_DropletNormalTextureArray", normal_array_fallback, 650, -750,
        sampler_type=c.normal_sampler(), group="DWC Surface Water",
        description="Global droplet normal Texture2DArray. Slice 0 is flat.",
    )
    c.try_connect(mask_array_uv, ("", "Result"), mask_sample, ("Coordinates", "UVs"))
    c.try_connect(normal_array_uv, ("", "Result"), normal_sample, ("Coordinates", "UVs"))

    mask_raw = c.named_declaration(mf, "SURFACE_DropletMaskRaw", mask_sample, ("R", ""), 1450, -2050)
    normal_raw = c.named_declaration(mf, "SURFACE_DropletNormalRaw", normal_sample, ("RGB", ""), 1450, -650)

    mask_raw_use = c.named_usage(mf, mask_raw, 2250, -1900)
    mask_slice_use = c.named_usage(mf, declarations["DropletMaskSlice"], 2250, -1450)
    safe_mask = c.custom_expression(
        mf,
        "return DropletMaskSlice > 0.5 ? saturate(MaskValue) : 1.0;",
        [
            ("MaskValue", mask_raw_use, ("", "Result")),
            ("DropletMaskSlice", mask_slice_use, ("", "Result")),
        ],
        "float1", 3000, -1700,
        "Return full coverage for the reserved no-mask slice and the authored mask otherwise.",
    )

    normal_raw_use = c.named_usage(mf, normal_raw, 2250, -700)
    normal_slice_use = c.named_usage(mf, declarations["DropletNormalSlice"], 2250, -250)
    flip_x_use = c.named_usage(mf, flip_x_decl, 2250, 200)
    flip_y_use = c.named_usage(mf, flip_y_decl, 2250, 650)
    decoded_normal = c.custom_expression(
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
            ("SampledNormal", normal_raw_use, ("", "Result")),
            ("DropletNormalSlice", normal_slice_use, ("", "Result")),
            ("FlipX", flip_x_use, ("", "Result")),
            ("FlipY", flip_y_use, ("", "Result")),
        ],
        "float3", 3000, -350,
        "Decode the droplet tangent normal or return flat for the reserved slice.",
    )

    mask_decl = c.named_declaration(mf, "SURFACE_DropletMask", safe_mask, ("", "Result"), 3900, -1700)
    normal_decl = c.named_declaration(mf, "SURFACE_DropletNormal", decoded_normal, ("", "Result"), 3900, -350)
    mask_out = c.named_usage(mf, mask_decl, 5200, -1250)
    normal_out = c.named_usage(mf, normal_decl, 5200, -250)
    c.function_output(
        mf, "DropletMask", mask_out, ("", "Result"), 0, 6200, -1250,
        "Sampled droplet mask, or full coverage when the profile has no droplet mask slice.",
    )
    c.function_output(
        mf, "DropletNormal", normal_out, ("", "Result"), 1, 6200, -250,
        "Raw tangent-space droplet normal without coverage or strength.",
    )

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
