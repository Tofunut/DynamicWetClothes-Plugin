# Copyright 2026 Team Tofunut. All Rights Reserved.
"""Create MF_DWC_SampleSurfaceWaterNormals for two stationary droplet layers."""
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

    c.create_comment(mf, "1. Inputs", -7600, -3600, 2600, 6500, parent=True)
    c.create_comment(mf, "2. Droplet1", -4500, -3600, 3900, 3000, parent=True)
    c.create_comment(mf, "3. Droplet2", -4500, 0, 3900, 3000, parent=True)
    c.create_comment(mf, "4. Outputs", 0, -3600, 2200, 6500, parent=True)

    specs = [
        ("SurfaceWaterNormalUV", "vector2", (0.0, 0.0), "Mesh UV used for repeating details."),
        ("Droplet1DetailSize", "scalar", (1.0,), "Part-local Droplet1 detail size."),
        ("Droplet2DetailSize", "scalar", (1.0,), "Part-local Droplet2 detail size."),
        ("Droplet1MaskSlice", "scalar", (0.0,), "Droplet1 mask array slice."),
        ("Droplet1NormalSlice", "scalar", (0.0,), "Droplet1 normal array slice."),
        ("Droplet2MaskSlice", "scalar", (0.0,), "Droplet2 mask array slice."),
        ("Droplet2NormalSlice", "scalar", (0.0,), "Droplet2 normal array slice."),
    ]
    declarations = {}
    for index, (name, kind, preview, description) in enumerate(specs):
        y = -3150 + index * 650
        node = c.function_input(mf, name, kind, preview, index, -7250, y, description)
        declarations[name] = c.named_declaration(mf, f"IN_{name}", node, ("", "Result"), -6200, y)

    flip_x = c.scalar_parameter(
        mf, "DWC_SurfaceWaterNormalFlipX", 0.0, -4200, 2850,
        group="DWC Surface Water",
        description="Invert final droplet tangent-normal X when greater than 0.5.",
    )
    flip_y = c.scalar_parameter(
        mf, "DWC_SurfaceWaterNormalFlipY", 0.0, -3200, 2850,
        group="DWC Surface Water",
        description="Invert final droplet tangent-normal Y when greater than 0.5.",
    )

    def build_layer(prefix: str, detail_name: str, mask_name: str, normal_name: str, y: int):
        uv = c.custom_expression(
            mf,
            "return UV / max(DetailSize, 1.0e-4);",
            [
                ("UV", c.named_usage(mf, declarations["SurfaceWaterNormalUV"], -4300, y), ("", "Result")),
                ("DetailSize", c.named_usage(mf, declarations[detail_name], -4300, y + 400), ("", "Result")),
            ],
            "float2", -3500, y,
            f"Scale mesh UV by the part-local {prefix} detail size.",
        )
        mask_uv = c.append_vector(
            mf, uv, ("", "Result"),
            c.named_usage(mf, declarations[mask_name], -2800, y + 400), ("", "Result"),
            -2200, y, f"Append {prefix} mask slice.",
        )
        normal_uv = c.append_vector(
            mf, uv, ("", "Result"),
            c.named_usage(mf, declarations[normal_name], -2800, y + 1500), ("", "Result"),
            -2200, y + 1100, f"Append {prefix} normal slice.",
        )
        mask_sample = c.texture2d_array_parameter(
            mf, "DWC_DropletMaskTextureArray", mask_array_fallback, -1500, y,
            sampler_type=c.mask_sampler(), group="DWC Surface Water",
            description="Shared Droplet1/Droplet2 mask Texture2DArray.",
        )
        normal_sample = c.texture2d_array_parameter(
            mf, "DWC_DropletNormalTextureArray", normal_array_fallback, -1500, y + 1100,
            sampler_type=c.linear_color_sampler(), group="DWC Surface Water",
            description="Shared Droplet1/Droplet2 normal Texture2DArray.",
        )
        c.try_connect(mask_uv, ("", "Result"), mask_sample, ("Coordinates", "UVs"))
        c.try_connect(normal_uv, ("", "Result"), normal_sample, ("Coordinates", "UVs"))
        mask = c.custom_expression(
            mf, "return Slice > 0.5 ? saturate(Value) : 1.0;",
            [
                ("Value", mask_sample, ("R", "")),
                ("Slice", c.named_usage(mf, declarations[mask_name], -700, y + 350), ("", "Result")),
            ],
            "float1", 0, y, f"Decode {prefix} mask; slice 0 is unmasked.",
        )
        normal = c.custom_expression(
            mf,
            """
if (Slice <= 0.5) return float3(0.0, 0.0, 1.0);
float2 XY = -(SampledNormal.rg * 2.0 - 1.0);
float2 FlipSign = float2(FlipX > 0.5 ? -1.0 : 1.0, FlipY > 0.5 ? -1.0 : 1.0);
return normalize(float3(XY * FlipSign, 1.0));
""",
            [
                ("SampledNormal", normal_sample, ("RGB", "")),
                ("Slice", c.named_usage(mf, declarations[normal_name], -700, y + 1450), ("", "Result")),
                ("FlipX", flip_x, ("", "Result")),
                ("FlipY", flip_y, ("", "Result")),
            ],
            "float3", 0, y + 1100, f"Decode {prefix} tangent normal.",
        )
        return mask, normal

    droplet1_mask, droplet1_normal = build_layer(
        "Droplet1", "Droplet1DetailSize", "Droplet1MaskSlice", "Droplet1NormalSlice", -3000
    )
    droplet2_mask, droplet2_normal = build_layer(
        "Droplet2", "Droplet2DetailSize", "Droplet2MaskSlice", "Droplet2NormalSlice", 600
    )

    outputs = [
        ("Droplet1Mask", droplet1_mask, "Droplet1 detail mask."),
        ("Droplet1Normal", droplet1_normal, "Droplet1 tangent normal."),
        ("Droplet2Mask", droplet2_mask, "Droplet2 detail mask."),
        ("Droplet2Normal", droplet2_normal, "Droplet2 tangent normal."),
    ]
    for index, (name, node, description) in enumerate(outputs):
        c.function_output(mf, name, node, ("", "Result"), index, 1300, -2800 + index * 1500, description)

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
