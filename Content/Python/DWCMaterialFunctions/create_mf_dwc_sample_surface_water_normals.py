"""Create MF_DWC_SampleSurfaceWaterNormals for stationary and flowing droplets."""
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

    c.create_comment(mf, "1. Inputs", -9800, -5200, 3400, 8600, parent=True)
    c.create_comment(mf, "2. Static Droplet", -6000, -5200, 3600, 4300, parent=True)
    c.create_comment(mf, "3. Flow UV + Noise", -6000, -500, 5600, 4300, parent=True)
    c.create_comment(mf, "4. Flow Droplet", 0, -500, 4200, 4300, parent=True)
    c.create_comment(mf, "5. Outputs", 4600, -5200, 2600, 8600, parent=True)

    specs = [
        ("SurfaceWaterNormalUV", "vector2", (0.0, 0.0), "Mesh UV used for repeated detail."),
        ("DropletDetailSize", "scalar", (1.0,), "Part-local stationary Droplet size."),
        ("DropletFlowDetailSize", "scalar", (1.0,), "Part-local Flow Droplet size."),
        ("DropletMaskSlice", "scalar", (0.0,), "Stationary mask array slice."),
        ("DropletNormalSlice", "scalar", (0.0,), "Stationary normal array slice."),
        ("DropletFlowEnabled", "scalar", (0.0,), "Flow path enable."),
        ("DropletFlowSpeed", "scalar", (0.25,), "Flow panning speed."),
        ("DropletFlowDirectionDegrees", "scalar", (90.0,), "Flow direction in degrees."),
        ("DropletFlowNoiseTiling", "scalar", (2.0,), "Noise UV tiling."),
        ("DropletFlowNoiseStrength", "scalar", (0.05,), "Sideways noise bend."),
        ("DropletFlowNoiseSpeed", "scalar", (0.15,), "Noise panning speed."),
        ("DropletFlowNoiseSlice", "scalar", (0.0,), "Flow noise array slice."),
        ("DropletFlowMaskSlice", "scalar", (0.0,), "Flow mask array slice."),
        ("DropletFlowNormalSlice", "scalar", (0.0,), "Flow normal array slice."),
        ("SurfaceTime", "scalar", (0.0,), "Runtime animation time."),
    ]
    declarations: dict[str, object] = {}
    for index, (name, kind, preview, description) in enumerate(specs):
        y = -4650 + index * 570
        node = c.function_input(mf, name, kind, preview, index, -9450, y, description)
        declarations[name] = c.named_declaration(
            mf, f"IN_{name}", node, ("", "Result"), -8350, y
        )

    flip_x = c.scalar_parameter(
        mf, "DWC_SurfaceWaterNormalFlipX", 0.0, -8200, 3500,
        group="DWC Surface Water",
        description="Invert final droplet tangent-normal X when greater than 0.5.",
    )
    flip_y = c.scalar_parameter(
        mf, "DWC_SurfaceWaterNormalFlipY", 0.0, -7200, 3500,
        group="DWC Surface Water",
        description="Invert final droplet tangent-normal Y when greater than 0.5.",
    )

    base_uv = c.custom_expression(
        mf,
        "return UV / max(DetailSize, 1.0e-4);",
        [
            ("UV", c.named_usage(mf, declarations["SurfaceWaterNormalUV"], -5850, -4550), ("", "Result")),
            ("DetailSize", c.named_usage(mf, declarations["DropletDetailSize"], -5850, -4000), ("", "Result")),
        ],
        "float2", -4900, -4300,
        "Scale mesh UV by the Part-local stationary Droplet size.",
    )
    base_uv_decl = c.named_declaration(mf, "SURFACE_BaseDropletUV", base_uv, ("", "Result"), -4050, -4300)

    flow_base_uv = c.custom_expression(
        mf,
        "return UV / max(DetailSize, 1.0e-4);",
        [
            ("UV", c.named_usage(mf, declarations["SurfaceWaterNormalUV"], -5850, -700), ("", "Result")),
            ("DetailSize", c.named_usage(mf, declarations["DropletFlowDetailSize"], -5850, -250), ("", "Result")),
        ],
        "float2", -4900, -500,
        "Scale mesh UV by the Part-local Flow Droplet size.",
    )
    flow_base_uv_decl = c.named_declaration(
        mf, "SURFACE_BaseFlowDropletUV", flow_base_uv, ("", "Result"), -4050, -500
    )

    def sample_detail(
        prefix: str,
        uv_declaration: object,
        mask_slice_declaration: object,
        normal_slice_declaration: object,
        x: int,
        y: int,
    ) -> tuple[object, object]:
        mask_uv = c.append_vector(
            mf,
            c.named_usage(mf, uv_declaration, x, y),
            ("", "Result"),
            c.named_usage(mf, mask_slice_declaration, x, y + 450),
            ("", "Result"),
            x + 650,
            y,
            f"Append {prefix} mask slice.",
        )
        normal_uv = c.append_vector(
            mf,
            c.named_usage(mf, uv_declaration, x, y + 1050),
            ("", "Result"),
            c.named_usage(mf, normal_slice_declaration, x, y + 1500),
            ("", "Result"),
            x + 650,
            y + 1050,
            f"Append {prefix} normal slice.",
        )
        mask_sample = c.texture2d_array_parameter(
            mf, "DWC_DropletMaskTextureArray", mask_array_fallback, x + 1350, y,
            sampler_type=c.mask_sampler(), group="DWC Surface Water",
            description="Shared stationary/flow mask Texture2DArray.",
        )
        normal_sample = c.texture2d_array_parameter(
            mf, "DWC_DropletNormalTextureArray", normal_array_fallback, x + 1350, y + 1050,
            sampler_type=c.linear_color_sampler(), group="DWC Surface Water",
            description="Shared stationary/flow normal Texture2DArray.",
        )
        c.try_connect(mask_uv, ("", "Result"), mask_sample, ("Coordinates", "UVs"))
        c.try_connect(normal_uv, ("", "Result"), normal_sample, ("Coordinates", "UVs"))
        safe_mask = c.custom_expression(
            mf,
            "return Slice > 0.5 ? saturate(Value) : 1.0;",
            [
                ("Value", mask_sample, ("R", "")),
                ("Slice", c.named_usage(mf, mask_slice_declaration, x + 2050, y + 400), ("", "Result")),
            ],
            "float1", x + 2650, y,
            f"Decode {prefix} mask with slice 0 as unmasked.",
        )
        decoded_normal = c.custom_expression(
            mf,
            """
if (Slice <= 0.5) return float3(0.0, 0.0, 1.0);
float2 XY = -(SampledNormal.rg * 2.0 - 1.0);
float2 FlipSign = float2(FlipX > 0.5 ? -1.0 : 1.0, FlipY > 0.5 ? -1.0 : 1.0);
return normalize(float3(XY * FlipSign, 1.0));
""",
            [
                ("SampledNormal", normal_sample, ("RGB", "")),
                ("Slice", c.named_usage(mf, normal_slice_declaration, x + 2050, y + 1450), ("", "Result")),
                ("FlipX", flip_x, ("", "Result")),
                ("FlipY", flip_y, ("", "Result")),
            ],
            "float3", x + 2650, y + 1050,
            f"Decode {prefix} tangent normal.",
        )
        return (
            c.named_declaration(mf, f"SURFACE_{prefix}Mask", safe_mask, ("", "Result"), x + 3450, y),
            c.named_declaration(mf, f"SURFACE_{prefix}Normal", decoded_normal, ("", "Result"), x + 3450, y + 1050),
        )

    static_mask, static_normal = sample_detail(
        "Droplet",
        base_uv_decl,
        declarations["DropletMaskSlice"],
        declarations["DropletNormalSlice"],
        -5850,
        -3250,
    )

    noise_uv = c.custom_expression(
        mf,
        """
float Radians = radians(DirectionDegrees);
float2 Direction = float2(cos(Radians), sin(Radians));
return BaseUV * max(Tiling, 0.01) + Direction * (SurfaceTime * NoiseSpeed);
""",
        [
            ("BaseUV", c.named_usage(mf, flow_base_uv_decl, -5850, 0), ("", "Result")),
            ("DirectionDegrees", c.named_usage(mf, declarations["DropletFlowDirectionDegrees"], -5850, 450), ("", "Result")),
            ("Tiling", c.named_usage(mf, declarations["DropletFlowNoiseTiling"], -5850, 900), ("", "Result")),
            ("SurfaceTime", c.named_usage(mf, declarations["SurfaceTime"], -5850, 1350), ("", "Result")),
            ("NoiseSpeed", c.named_usage(mf, declarations["DropletFlowNoiseSpeed"], -5850, 1800), ("", "Result")),
        ],
        "float2", -4550, 700,
        "Pan the noise field independently along the configured flow direction.",
    )
    noise_array_uv = c.append_vector(
        mf,
        noise_uv,
        ("", "Result"),
        c.named_usage(mf, declarations["DropletFlowNoiseSlice"], -4550, 1900),
        ("", "Result"),
        -3600,
        900,
        "Append the Flow Noise Texture2DArray slice.",
    )
    noise_sample = c.texture2d_array_parameter(
        mf, "DWC_DropletFlowNoiseTextureArray", mask_array_fallback, -2850, 900,
        sampler_type=c.mask_sampler(), group="DWC Surface Water",
        description="Dedicated Flow Noise Texture2DArray.",
    )
    c.try_connect(noise_array_uv, ("", "Result"), noise_sample, ("Coordinates", "UVs"))

    flow_uv = c.custom_expression(
        mf,
        """
float Radians = radians(DirectionDegrees);
float2 Direction = float2(cos(Radians), sin(Radians));
float2 Sideways = float2(-Direction.y, Direction.x);
float NoiseOffset = NoiseSlice > 0.5 ? (NoiseValue * 2.0 - 1.0) * saturate(NoiseStrength) : 0.0;
float2 Panned = BaseUV - Direction * (SurfaceTime * FlowSpeed);
return lerp(BaseUV, Panned + Sideways * NoiseOffset, saturate(FlowEnabled));
""",
        [
            ("BaseUV", c.named_usage(mf, flow_base_uv_decl, -2850, -50), ("", "Result")),
            ("FlowEnabled", c.named_usage(mf, declarations["DropletFlowEnabled"], -2850, 400), ("", "Result")),
            ("FlowSpeed", c.named_usage(mf, declarations["DropletFlowSpeed"], -2850, 850), ("", "Result")),
            ("DirectionDegrees", c.named_usage(mf, declarations["DropletFlowDirectionDegrees"], -2850, 1300), ("", "Result")),
            ("SurfaceTime", c.named_usage(mf, declarations["SurfaceTime"], -2850, 1750), ("", "Result")),
            ("NoiseValue", noise_sample, ("R", "")),
            ("NoiseSlice", c.named_usage(mf, declarations["DropletFlowNoiseSlice"], -2850, 2200), ("", "Result")),
            ("NoiseStrength", c.named_usage(mf, declarations["DropletFlowNoiseStrength"], -2850, 2650), ("", "Result")),
        ],
        "float2", -1450, 1200,
        "Pan only Flow Droplet detail and bend it sideways with animated noise.",
    )
    flow_uv_decl = c.named_declaration(mf, "SURFACE_FlowDropletUV", flow_uv, ("", "Result"), -650, 1200)

    flow_mask, flow_normal = sample_detail(
        "FlowDroplet",
        flow_uv_decl,
        declarations["DropletFlowMaskSlice"],
        declarations["DropletFlowNormalSlice"],
        250,
        100,
    )

    outputs = [
        ("DropletMask", static_mask, "Stationary Droplet mask."),
        ("DropletNormal", static_normal, "Stationary Droplet tangent normal."),
        ("FlowDropletMask", flow_mask, "Panned Flow Droplet mask."),
        ("FlowDropletNormal", flow_normal, "Panned and noise-bent Flow Droplet tangent normal."),
    ]
    for index, (name, declaration, description) in enumerate(outputs):
        y = -3700 + index * 1650
        usage = c.named_usage(mf, declaration, 5000, y)
        c.function_output(mf, name, usage, ("", "Result"), index, 6200, y, description)

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
