# Copyright 2026 Team Tofunut. All Rights Reserved.
"""Create or recreate MF_DWC_EvaluateSurfaceAppearance with droplet-only Surface Water."""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import dwc_mf_common as c

OVERWRITE_EXISTING = True
ASSET_NAME = "MF_DWC_EvaluateSurfaceAppearance"
PROFILE_FUNCTION_NAME = "MF_DWC_GetRenderProfile"
NORMAL_FUNCTION_NAME = "MF_DWC_SampleSurfaceWaterNormals"


def require_helper_function(name: str):
    value = c.load_asset(c.asset_path(name))
    if value is None:
        c.fail(f"{c.asset_path(name)} is missing. Run its DWC Python creation script first.")
    return value


def build() -> None:
    data_fallback, _ = c.ensure_default_textures()
    profile_function = require_helper_function(PROFILE_FUNCTION_NAME)
    normal_function = require_helper_function(NORMAL_FUNCTION_NAME)
    mf = c.create_or_replace_material_function(ASSET_NAME, OVERWRITE_EXISTING)

    c.create_comment(mf, "1. Inputs & Profile", -18000, -6200, 7600, 9600, parent=True)
    c.create_comment(mf, "2. Absorbed Wetness", -9600, -6200, 7600, 9600, parent=True)
    c.create_comment(mf, "3. Droplet Surface Water", -1200, -6200, 9500, 9600, parent=True)
    c.create_comment(mf, "4. Final Composition", 9000, -6200, 7600, 9600, parent=True)

    declarations: dict[str, object] = {}
    specs = [
        ("BaseColor", "vector3", (1.0, 1.0, 1.0), "Original material Base Color."),
        ("BaseRoughness", "scalar", (0.5,), "Original material Roughness."),
        ("BaseSpecular", "scalar", (0.5,), "Original material Specular."),
        ("BaseMetallic", "scalar", (0.0,), "Original material Metallic."),
        ("BaseNormal", "vector3", (0.0, 0.0, 1.0), "Original tangent-space Normal."),
        ("Wetness", "scalar", (0.0,), "Resolved absorbed wetness."),
        ("DWCDataUV", "vector2", (0.0, 0.0), "DWC Data UV for runtime maps."),
        ("SurfaceWaterNormalUV", "vector2", (0.0, 0.0), "Mesh UV for droplet detail."),
        ("WetDarkeningStrength", "scalar", (0.35,), "Absorbed wetness darkening strength."),
        ("WetRoughness", "scalar", (0.12,), "Absorbed wetness target roughness."),
        ("WrinkleNormal", "vector3", (0.0, 0.0, 1.0), "Baked wrinkle tangent normal."),
        ("UseWrinkleNormalMap", "scalar", (0.0,), "Wrinkle normal enable weight."),
        ("WrinkleStrength", "scalar", (1.0,), "Wrinkle normal strength."),
        ("WrinkleWetnessMin", "scalar", (0.25,), "Wrinkle response start wetness."),
        ("WrinkleWetnessMax", "scalar", (1.0,), "Wrinkle response full wetness."),
        ("TransparencyColor", "vector3", (1.0, 1.0, 1.0), "Color revealed by wet transparency."),
        ("TransparencyAlpha", "scalar", (0.0,), "Baked transparency region and strength."),
        ("UseTransparencyMap", "scalar", (0.0,), "Transparency map enable weight."),
        ("TransparencyWetnessMin", "scalar", (0.25,), "Transparency response start wetness."),
        ("TransparencyWetnessMax", "scalar", (1.0,), "Transparency response full wetness."),
        ("RevealNormal", "vector3", (0.0, 0.0, 1.0), "Coverage-weighted reveal tangent normal."),
        ("UseRevealNormalMap", "scalar", (0.0,), "Reveal normal enable weight."),
        ("RevealNormalStrength", "scalar", (1.0,), "Reveal normal intensity."),
    ]
    for i, (name, kind, preview, desc) in enumerate(specs):
        column = i // 10
        row = i % 10
        x = -17500 + column * 2600
        y = -5600 + row * 620
        node = c.function_input(mf, name, kind, preview, i, x, y, desc)
        declarations[name] = c.named_declaration(mf, f"IN_{name}", node, ("", "Result"), x + 1050, y)

    profile_call = c.function_call(
        mf, profile_function, -12200, 500,
        "Resolve the current pixel's droplet-only Render Profile.",
    )
    profile_uv = c.named_usage(mf, declarations["DWCDataUV"], -13200, 500)
    c.try_connect(profile_uv, ("", "Result"), profile_call, "DWCDataUV")
    profile_outputs = [
        "AbsorbedDarkeningStrength",
        "AbsorbedGlossinessStrength",
        "Droplet1NormalSlice",
        "Droplet1NormalStrength",
        "Droplet1RoughnessBlend",
        "Droplet1Specular",
        "Droplet1TargetRoughness",
        "Droplet1TotalStrength",
        "Droplet1ColorBlend",
        "Droplet1MaskSlice",
        "Droplet2NormalSlice",
        "Droplet2MaskSlice",
        "Droplet2TotalStrength",
        "Droplet2TargetRoughness",
        "Droplet2RoughnessBlend",
        "Droplet2Specular",
        "Droplet2ColorBlend",
        "Droplet2NormalStrength",
        "Droplet1DetailSize",
        "Droplet2DetailSize",
    ]
    profile_declarations: dict[str, object] = {}
    for i, name in enumerate(profile_outputs):
        profile_declarations[name] = c.named_declaration(
            mf, f"PROFILE_{name}", profile_call, name,
            -11200 + (i % 2) * 1250, 100 + (i // 2) * 620,
        )

    # Absorbed-wetness color, roughness, wrinkles, and transparency.
    absorbed_inputs = []
    absorbed_names = (
        "BaseColor", "BaseMetallic", "Wetness", "WetDarkeningStrength",
    )
    for i, name in enumerate(absorbed_names):
        absorbed_inputs.append((name, c.named_usage(mf, declarations[name], -9000, -5350 + i * 600), ("", "Result")))
    profile_dark = c.named_usage(mf, profile_declarations["AbsorbedDarkeningStrength"], -9000, -2950)
    absorbed_inputs.append(("ProfileDarkeningStrength", profile_dark, ("", "Result")))
    wet_color = c.custom_expression(
        mf,
        """
float W = saturate(Wetness);
float NonMetal = 1.0 - saturate(BaseMetallic);
float Darken = saturate(W * WetDarkeningStrength * ProfileDarkeningStrength * NonMetal);
return BaseColor * (1.0 - Darken);
""",
        absorbed_inputs,
        "float3", -7600, -4200,
        "Apply absorbed-water darkening while preserving metallic response.",
    )
    wet_color_decl = c.named_declaration(mf, "ABSORBED_WetBaseColor", wet_color, ("", "Result"), -6400, -4200)

    rough_inputs = []
    for i, name in enumerate(("BaseRoughness", "WetRoughness", "Wetness")):
        rough_inputs.append((name, c.named_usage(mf, declarations[name], -9000, -2200 + i * 550), ("", "Result")))
    profile_gloss = c.named_usage(mf, profile_declarations["AbsorbedGlossinessStrength"], -9000, -550)
    rough_inputs.append(("ProfileGlossinessStrength", profile_gloss, ("", "Result")))
    absorbed_roughness = c.custom_expression(
        mf,
        "return saturate(lerp(BaseRoughness, WetRoughness, saturate(Wetness * ProfileGlossinessStrength)));",
        rough_inputs,
        "float1", -7600, -1350,
        "Blend the original roughness toward the absorbed-wetness target.",
    )
    absorbed_rough_decl = c.named_declaration(mf, "ABSORBED_Roughness", absorbed_roughness, ("", "Result"), -6400, -1350)

    reveal_inputs = []
    for i, name in enumerate((
        "BaseNormal", "RevealNormal", "UseRevealNormalMap", "RevealNormalStrength",
        "TransparencyAlpha", "UseTransparencyMap", "Wetness",
        "TransparencyWetnessMin", "TransparencyWetnessMax",
    )):
        reveal_inputs.append((name, c.named_usage(mf, declarations[name], -6500, -5750 + i * 520), ("", "Result")))
    reveal_normal = c.custom_expression(
        mf,
        """
float SafeMin = saturate(TransparencyWetnessMin);
float SafeMax = max(SafeMin, saturate(TransparencyWetnessMax));
float WetnessWeight = saturate((Wetness - SafeMin) / max(SafeMax - SafeMin, 1.0e-4));
float Weight = saturate(
    WetnessWeight * TransparencyAlpha * UseTransparencyMap * UseRevealNormalMap);
float3 B = normalize(BaseNormal);
float2 RevealXY = RevealNormal.xy * max(RevealNormalStrength, 0.0);
float RevealXYLengthSquared = dot(RevealXY, RevealXY);
if (RevealXYLengthSquared > 0.999)
{
    RevealXY *= sqrt(0.999 / RevealXYLengthSquared);
}
float3 R = float3(RevealXY, sqrt(saturate(1.0 - dot(RevealXY, RevealXY))));
return normalize(float3(
    B.xy + R.xy * Weight,
    B.z * lerp(1.0, R.z, Weight)));
""",
        reveal_inputs,
        "float3", -5100, -4300,
        "Blend coverage-weighted reveal detail before wrinkle and Surface Water normals.",
    )
    reveal_decl = c.named_declaration(
        mf, "ABSORBED_RevealNormal", reveal_normal, ("", "Result"), -3900, -4300
    )

    wrinkle_inputs = []
    for i, name in enumerate((
        "WrinkleNormal", "Wetness", "UseWrinkleNormalMap",
        "WrinkleStrength", "WrinkleWetnessMin", "WrinkleWetnessMax",
    )):
        wrinkle_inputs.append((name, c.named_usage(mf, declarations[name], -3550, -3300 + i * 560), ("", "Result")))
    wrinkle_inputs.insert(
        0,
        ("BaseNormal", c.named_usage(mf, reveal_decl, -3550, -3900), ("", "Result")),
    )
    wrinkle_normal = c.custom_expression(
        mf,
        """
float T = saturate((Wetness - WrinkleWetnessMin) / max(WrinkleWetnessMax - WrinkleWetnessMin, 1.0e-4));
float Weight = saturate(T * UseWrinkleNormalMap * WrinkleStrength);
float3 B = normalize(BaseNormal);
float3 W = normalize(WrinkleNormal);
float3 Detail = normalize(float3(W.xy * Weight, lerp(1.0, W.z, Weight)));
return normalize(float3(B.xy + Detail.xy, B.z * Detail.z));
""",
        wrinkle_inputs,
        "float3", -2150, -2550,
        "Blend the baked wrinkle normal after reveal detail.",
    )
    wrinkle_decl = c.named_declaration(mf, "ABSORBED_Normal", wrinkle_normal, ("", "Result"), -950, -2550)

    transparency_inputs = [("WetBaseColor", c.named_usage(mf, wet_color_decl, -5450, 0), ("", "Result"))]
    for i, name in enumerate((
        "TransparencyColor", "TransparencyAlpha", "UseTransparencyMap", "Wetness",
        "TransparencyWetnessMin", "TransparencyWetnessMax",
    )):
        transparency_inputs.append((name, c.named_usage(mf, declarations[name], -5450, 500 + i * 550), ("", "Result")))
    transparent_color = c.custom_expression(
        mf,
        """
float T = saturate((Wetness - TransparencyWetnessMin) / max(TransparencyWetnessMax - TransparencyWetnessMin, 1.0e-4));
float Alpha = saturate(T * TransparencyAlpha * UseTransparencyMap);
return lerp(WetBaseColor, TransparencyColor, Alpha);
""",
        transparency_inputs,
        "float3", -3900, 1450,
        "Apply the authored wet-transparency color response.",
    )
    absorbed_color_decl = c.named_declaration(mf, "ABSORBED_FinalBaseColor", transparent_color, ("", "Result"), -2550, 1450)

    # Single-channel Surface Water wetness. Dry Rate updates R every frame on the GPU.
    rt_uv = c.named_usage(mf, declarations["DWCDataUV"], -850, -5250)
    droplet_rt = c.texture2d_parameter(
        mf, "DWC_SurfaceDroplet1RT", data_fallback, -250, -5250,
        sampler_type=c.linear_color_sampler(), group="DWC Surface Water",
        description="Single-channel Droplet1 RT: R=Wetness. Dry Rate fades the value out on the GPU.",
    )
    c.try_connect(rt_uv, ("", "Result"), droplet_rt, ("Coordinates", "UVs"))
    droplet_wetness = c.component_mask(mf, droplet_rt, "R", "R", 450, -5500)
    wetness_decl = c.named_declaration(
        mf, "SURFACE_DropletWetness", droplet_wetness, ("", "Result"), 1200, -5500
    )
    raw_coverage_decl = c.named_declaration(
        mf, "SURFACE_RawDropletWetness", droplet_wetness, ("", "Result"), 3100, -4450
    )

    # Droplet2 uses the same stationary single-channel Wetness contract in an independent RT.
    flow_rt_uv = c.named_usage(mf, declarations["DWCDataUV"], -850, -3550)
    flow_droplet_rt = c.texture2d_parameter(
        mf, "DWC_SurfaceDroplet2RT", data_fallback, -250, -3550,
        sampler_type=c.linear_color_sampler(), group="DWC Surface Water",
        description="Single-channel Droplet2 RT: R=Wetness. Dry Rate fades the value out on the GPU.",
    )
    c.try_connect(flow_rt_uv, ("", "Result"), flow_droplet_rt, ("Coordinates", "UVs"))
    flow_wetness = c.component_mask(mf, flow_droplet_rt, "R", "R", 450, -3500)
    flow_wetness_decl = c.named_declaration(
        mf, "SURFACE_FlowDropletWetness", flow_wetness, ("", "Result"), 1200, -3500
    )
    flow_visible_wetness = c.custom_expression(
        mf,
        "return saturate(Wetness);",
        [
            ("Wetness", c.named_usage(mf, flow_wetness_decl, 3100, -3800), ("", "Result")),
        ],
        "float1", 4300, -3800,
        "Resolve Droplet2 RT wetness independently from Droplet1 stamps.",
    )
    flow_visible_wetness_decl = c.named_declaration(
        mf, "SURFACE_FlowDropletVisibleWetness", flow_visible_wetness, ("", "Result"), 5400, -3800
    )
    raw_flow_coverage_decl = c.named_declaration(
        mf, "SURFACE_RawFlowDropletWetness", flow_visible_wetness, ("", "Result"), 6600, -3800
    )

    preview_override = c.scalar_parameter(
        mf, "DWC_PreviewSurfaceWaterOverride", 0.0, 1900, -2700,
        group="DWC Surface Water",
        description="Editor preview only: replace RT droplet coverage when greater than 0.5.",
    )
    preview_amount = c.scalar_parameter(
        mf, "DWC_PreviewSurfaceWaterAmount", 0.0, 1900, -2300,
        group="DWC Surface Water",
        description="Editor preview only: direct Surface Water wetness.",
    )
    droplet1_rendering_enabled = c.scalar_parameter(
        mf, "DWC_Droplet1RenderingEnabled", 1.0, 1900, -1900,
        group="DWC Surface Water",
        description="Runtime visual-only toggle for Droplet1. Simulation, stamping, and drying continue unchanged.",
    )
    droplet2_rendering_enabled = c.scalar_parameter(
        mf, "DWC_Droplet2RenderingEnabled", 1.0, 1900, -1500,
        group="DWC Surface Water",
        description="Runtime visual-only toggle for Droplet2. Simulation, stamping, and drying continue unchanged.",
    )
    raw_cov_use = c.named_usage(mf, raw_coverage_decl, 3000, -2450)
    effective_coverage = c.custom_expression(
        mf,
        "float SelectedCoverage = PreviewOverride > 0.5 ? saturate(PreviewAmount) : saturate(Coverage);\nreturn saturate(RenderingEnabled) * SelectedCoverage;",
        [
            ("PreviewOverride", preview_override, ("", "Result")),
            ("PreviewAmount", preview_amount, ("", "Result")),
            ("Coverage", raw_cov_use, ("", "Result")),
            ("RenderingEnabled", droplet1_rendering_enabled, ("", "Result")),
        ],
        "float1", 4200, -2450,
        "Select preview or runtime Droplet1 coverage, then apply its visual-only runtime toggle.",
    )
    effective_coverage_decl = c.named_declaration(mf, "SURFACE_EffectiveDropletCoverage", effective_coverage, ("", "Result"), 5400, -2450)

    effective_flow_coverage = c.custom_expression(
        mf,
        """
float RuntimeCoverage = saturate(Coverage);
float PreviewCoverage = saturate(PreviewAmount);
float SelectedCoverage = PreviewOverride > 0.5 ? PreviewCoverage : RuntimeCoverage;
return saturate(RenderingEnabled) * SelectedCoverage;
""",
        [
            ("PreviewOverride", preview_override, ("", "Result")),
            ("PreviewAmount", preview_amount, ("", "Result")),
            ("Coverage", c.named_usage(mf, raw_flow_coverage_decl, 3000, -2050), ("", "Result")),
            ("RenderingEnabled", droplet2_rendering_enabled, ("", "Result")),
        ],
        "float1", 4200, -1900,
        "Select preview or runtime Droplet2 coverage, then apply its visual-only runtime toggle.",
    )
    effective_flow_coverage_decl = c.named_declaration(
        mf, "SURFACE_EffectiveFlowDropletCoverage", effective_flow_coverage, ("", "Result"), 5400, -1900
    )

    normal_call = c.function_call(
        mf, normal_function, 800, -900,
        "Sample stationary Droplet1 and Droplet2 detail.",
    )
    normal_inputs = [
        ("SurfaceWaterNormalUV", declarations["SurfaceWaterNormalUV"]),
        ("Droplet1DetailSize", profile_declarations["Droplet1DetailSize"]),
        ("Droplet2DetailSize", profile_declarations["Droplet2DetailSize"]),
        ("Droplet1MaskSlice", profile_declarations["Droplet1MaskSlice"]),
        ("Droplet1NormalSlice", profile_declarations["Droplet1NormalSlice"]),
        ("Droplet2MaskSlice", profile_declarations["Droplet2MaskSlice"]),
        ("Droplet2NormalSlice", profile_declarations["Droplet2NormalSlice"]),
    ]
    for i, (input_name, declaration) in enumerate(normal_inputs):
        usage = c.named_usage(mf, declaration, -1800 + (i % 2) * 850, -1450 + (i // 2) * 520)
        c.try_connect(usage, ("", "Result"), normal_call, input_name)
    droplet_mask_decl = c.named_declaration(mf, "SURFACE_Droplet1Mask", normal_call, "Droplet1Mask", 2100, -1150)
    droplet_normal_decl = c.named_declaration(mf, "SURFACE_Droplet1Normal", normal_call, "Droplet1Normal", 2100, -450)
    flow_droplet_mask_decl = c.named_declaration(
        mf, "SURFACE_Droplet2Mask", normal_call, "Droplet2Mask", 2100, 250
    )
    flow_droplet_normal_decl = c.named_declaration(
        mf, "SURFACE_Droplet2Normal", normal_call, "Droplet2Normal", 2100, 950
    )

    static_visible_coverage = c.custom_expression(
        mf,
        "return saturate(Coverage) * saturate(Mask);",
        [
            ("Coverage", c.named_usage(mf, effective_coverage_decl, 5700, -1900), ("", "Result")),
            ("Mask", c.named_usage(mf, droplet_mask_decl, 5700, -1300), ("", "Result")),
        ],
        "float1", 6600, -1750,
        "Gate stationary Droplet RT coverage by its authored mask.",
    )
    static_coverage_decl = c.named_declaration(
        mf, "SURFACE_StaticCoverage", static_visible_coverage, ("", "Result"), 7600, -1750
    )
    flow_visible_coverage = c.custom_expression(
        mf,
        "return saturate(Coverage) * saturate(Mask);",
        [
            ("Coverage", c.named_usage(mf, effective_flow_coverage_decl, 5700, -1150), ("", "Result")),
            ("Mask", c.named_usage(mf, flow_droplet_mask_decl, 5700, -550), ("", "Result")),
        ],
        "float1", 6600, -900,
        "Gate Droplet2 RT coverage by its authored mask.",
    )
    flow_coverage_decl = c.named_declaration(
        mf, "SURFACE_FlowCoverage", flow_visible_coverage, ("", "Result"), 7600, -900
    )
    combined_coverage = c.custom_expression(
        mf,
        "return max(saturate(StaticCoverage), saturate(FlowCoverage));",
        [
            ("StaticCoverage", c.named_usage(mf, static_coverage_decl, 7800, -1550), ("", "Result")),
            ("FlowCoverage", c.named_usage(mf, flow_coverage_decl, 7800, -1000), ("", "Result")),
        ],
        "float1", 8600, -1300,
        "Union Droplet1 and Droplet2 coverage without coupling their RT state.",
    )
    surface_coverage_decl = c.named_declaration(
        mf, "SURFACE_Coverage", combined_coverage, ("", "Result"), 9400, -1300
    )

    visual_brush = c.custom_expression(
        mf,
        """
float StaticBrush = saturate(StaticCoverage) * saturate(StaticMask);
float FlowBrush = saturate(FlowCoverage) * saturate(FlowMask);
return max(StaticBrush, FlowBrush);
""",
        [
            ("StaticCoverage", c.named_usage(mf, effective_coverage_decl, 5700, -150), ("", "Result")),
            ("StaticMask", c.named_usage(mf, droplet_mask_decl, 5700, 300), ("", "Result")),
            ("FlowCoverage", c.named_usage(mf, effective_flow_coverage_decl, 5700, 750), ("", "Result")),
            ("FlowMask", c.named_usage(mf, flow_droplet_mask_decl, 5700, 1200), ("", "Result")),
        ],
        "float1", 6600, 600,
        "Union Droplet1 and Droplet2 visual brushes.",
    )
    visual_brush_decl = c.named_declaration(mf, "SURFACE_VisualBrush", visual_brush, ("", "Result"), 7600, 600)

    surface_appearance = c.custom_expression(
        mf,
        """
float StaticResponse = saturate(StaticCoverage) * saturate(StaticTotalStrength);
float FlowResponse = saturate(FlowCoverage) * saturate(FlowTotalStrength);
float WeightSum = StaticResponse + FlowResponse;
float SafeWeight = max(WeightSum, 1.0e-5);
float TargetRoughness =
    (StaticTargetRoughness * StaticResponse + FlowTargetRoughness * FlowResponse) / SafeWeight;
float RoughnessBlend =
    (StaticRoughnessBlend * StaticResponse + FlowRoughnessBlend * FlowResponse) / SafeWeight;
float Specular =
    (StaticSpecular * StaticResponse + FlowSpecular * FlowResponse) / SafeWeight;
float Response = 1.0 - (1.0 - StaticResponse) * (1.0 - FlowResponse);
return float4(
    saturate(TargetRoughness),
    saturate(RoughnessBlend),
    saturate(Specular),
    saturate(Response));
""",
        [
            ("StaticCoverage", c.named_usage(mf, static_coverage_decl, 7950, 900), ("", "Result")),
            ("FlowCoverage", c.named_usage(mf, flow_coverage_decl, 7950, 1250), ("", "Result")),
            ("StaticTotalStrength", c.named_usage(mf, profile_declarations["Droplet1TotalStrength"], 7950, 1600), ("", "Result")),
            ("FlowTotalStrength", c.named_usage(mf, profile_declarations["Droplet2TotalStrength"], 7950, 1950), ("", "Result")),
            ("StaticTargetRoughness", c.named_usage(mf, profile_declarations["Droplet1TargetRoughness"], 7950, 2300), ("", "Result")),
            ("FlowTargetRoughness", c.named_usage(mf, profile_declarations["Droplet2TargetRoughness"], 7950, 2650), ("", "Result")),
            ("StaticRoughnessBlend", c.named_usage(mf, profile_declarations["Droplet1RoughnessBlend"], 7950, 3000), ("", "Result")),
            ("FlowRoughnessBlend", c.named_usage(mf, profile_declarations["Droplet2RoughnessBlend"], 7950, 3350), ("", "Result")),
            ("StaticSpecular", c.named_usage(mf, profile_declarations["Droplet1Specular"], 7950, 3700), ("", "Result")),
            ("FlowSpecular", c.named_usage(mf, profile_declarations["Droplet2Specular"], 7950, 4050), ("", "Result")),
        ],
        "float4", 9300, 2500,
        "Resolve independently authored Droplet1 and Droplet2 appearance into one response.",
    )

    surface_color_response = c.custom_expression(
        mf,
        """
float StaticResponse =
    saturate(StaticCoverage) * saturate(StaticTotalStrength) * saturate(StaticColorBlend);
float FlowResponse =
    saturate(FlowCoverage) * saturate(FlowTotalStrength) * saturate(FlowColorBlend);
return saturate(1.0 - (1.0 - StaticResponse) * (1.0 - FlowResponse));
""",
        [
            ("StaticCoverage", c.named_usage(mf, static_coverage_decl, 9650, 900), ("", "Result")),
            ("FlowCoverage", c.named_usage(mf, flow_coverage_decl, 9650, 1250), ("", "Result")),
            ("StaticTotalStrength", c.named_usage(mf, profile_declarations["Droplet1TotalStrength"], 9650, 1600), ("", "Result")),
            ("FlowTotalStrength", c.named_usage(mf, profile_declarations["Droplet2TotalStrength"], 9650, 1950), ("", "Result")),
            ("StaticColorBlend", c.named_usage(mf, profile_declarations["Droplet1ColorBlend"], 9650, 2300), ("", "Result")),
            ("FlowColorBlend", c.named_usage(mf, profile_declarations["Droplet2ColorBlend"], 9650, 2650), ("", "Result")),
        ],
        "float1", 10800, 1750,
        "Resolve independently authored Droplet1 and Droplet2 Base Color response.",
    )

    surface_normal = c.custom_expression(
        mf,
        """
float StaticWeight = saturate(StaticCoverage) * saturate(StaticTotalStrength);
float FlowWeight = saturate(FlowCoverage) * saturate(FlowTotalStrength);
float WeightSum = StaticWeight + FlowWeight;
float DropletVisualHeightBoost = 1.65;
float2 StaticXY =
    DropletNormal.xy * min(clamp(StaticNormalStrength, 0.0, 3.0) * DropletVisualHeightBoost, 12.0);
float2 FlowXY =
    FlowDropletNormal.xy * min(clamp(FlowNormalStrength, 0.0, 3.0) * DropletVisualHeightBoost, 12.0);
float2 DetailXY = StaticXY * StaticWeight + FlowXY * FlowWeight;
DetailXY /= max(WeightSum, 1.0e-5);
float DropletWeight = 1.0 - (1.0 - StaticWeight) * (1.0 - FlowWeight);
float2 CombinedXY = DetailXY * DropletWeight;
float3 Base = normalize(BaseNormal);
float3 DropletDetail = normalize(float3(CombinedXY, 1.0));
return normalize(float3(Base.xy + DropletDetail.xy, Base.z * DropletDetail.z));
""",
        [
            ("BaseNormal", c.named_usage(mf, wrinkle_decl, 4400, -500), ("", "Result")),
            ("DropletNormal", c.named_usage(mf, droplet_normal_decl, 4400, 50), ("", "Result")),
            ("FlowDropletNormal", c.named_usage(mf, flow_droplet_normal_decl, 4400, 600), ("", "Result")),
            ("StaticCoverage", c.named_usage(mf, static_coverage_decl, 4400, 1150), ("", "Result")),
            ("FlowCoverage", c.named_usage(mf, flow_coverage_decl, 4400, 1700), ("", "Result")),
            ("StaticNormalStrength", c.named_usage(mf, profile_declarations["Droplet1NormalStrength"], 4400, 2250), ("", "Result")),
            ("FlowNormalStrength", c.named_usage(mf, profile_declarations["Droplet2NormalStrength"], 4400, 2800), ("", "Result")),
            ("StaticTotalStrength", c.named_usage(mf, profile_declarations["Droplet1TotalStrength"], 4400, 3350), ("", "Result")),
            ("FlowTotalStrength", c.named_usage(mf, profile_declarations["Droplet2TotalStrength"], 4400, 3900), ("", "Result")),
        ],
        "float3", 6000, 2050,
        "Blend Droplet1 and Droplet2 normals.",
    )
    surface_normal_decl = c.named_declaration(mf, "SURFACE_NormalRaw", surface_normal, ("", "Result"), 7600, 2050)

    # Preserve the existing per-slot compile contract. When Surface Water is disabled,
    # the compiler removes the droplet RT and Texture2DArray sampling graph entirely.
    enabled_normal_use = c.named_usage(mf, surface_normal_decl, 8200, 200)
    enabled_coverage_use = c.named_usage(mf, surface_coverage_decl, 8200, 850)
    enabled_surface_pack = c.append_vector(
        mf, enabled_normal_use, ("", "Result"), enabled_coverage_use, ("", "Result"),
        9000, 500, "Pack the droplet normal and visible coverage for the enabled Surface Water path.",
    )
    disabled_normal_use = c.named_usage(mf, wrinkle_decl, 8200, 1450)
    disabled_zero = c.scalar_constant(mf, 0.0, 8200, 2000, "Disabled Surface Water coverage")
    disabled_surface_pack = c.append_vector(
        mf, disabled_normal_use, ("", "Result"), disabled_zero, ("", "Result"),
        9000, 1650, "Use the absorbed/wrinkle normal and zero coverage when Surface Water is compiled out.",
    )
    selected_surface_pack = c.static_switch_parameter(
        mf, "DWC_UseSurfaceWater", False,
        enabled_surface_pack, ("", "Result"),
        disabled_surface_pack, ("", "Result"),
        10000, 950,
        group="DWC Surface Water",
        description="Compile droplet RT, coverage, Texture2DArray sampling, and normal work only for slots that use Surface Water.",
    )
    selected_normal = c.custom_expression(
        mf, "return Packed.rgb;",
        [("Packed", selected_surface_pack, ("", "Result"))],
        "float3", 11100, 450,
        "Decode the statically selected final tangent normal.",
    )
    selected_coverage = c.custom_expression(
        mf, "return Packed.a;",
        [("Packed", selected_surface_pack, ("", "Result"))],
        "float1", 11100, 1450,
        "Decode the statically selected droplet coverage.",
    )
    selected_normal_decl = c.named_declaration(
        mf, "SURFACE_Normal", selected_normal, ("", "Result"), 12100, 450
    )
    selected_coverage_decl = c.named_declaration(
        mf, "SURFACE_SelectedCoverage", selected_coverage, ("", "Result"), 12100, 1450
    )
    disabled_appearance_rgb = c.vector_constant(
        mf, (0.0, 0.0, 0.0), 9000, 2150, "Disabled Surface Water appearance values"
    )
    disabled_appearance_alpha = c.scalar_constant(
        mf, 0.0, 9000, 2500, "Disabled Surface Water appearance response"
    )
    disabled_appearance = c.append_vector(
        mf, disabled_appearance_rgb, ("", "Result"), disabled_appearance_alpha, ("", "Result"),
        9800, 2300, "Pack disabled Surface Water appearance.",
    )
    selected_appearance = c.static_switch_parameter(
        mf, "DWC_UseSurfaceWater", False,
        surface_appearance, ("", "Result"),
        disabled_appearance, ("", "Result"),
        10700, 2250,
        group="DWC Surface Water",
        description="Compile independently authored Droplet1 and Droplet2 appearance only for slots that use Surface Water.",
    )
    selected_target_roughness = c.custom_expression(
        mf, "return Packed.r;",
        [("Packed", selected_appearance, ("", "Result"))],
        "float1", 11700, 2100,
        "Decode the coverage-weighted droplet target roughness.",
    )
    selected_roughness_blend = c.custom_expression(
        mf, "return Packed.g;",
        [("Packed", selected_appearance, ("", "Result"))],
        "float1", 11700, 2450,
        "Decode the coverage-weighted droplet roughness blend.",
    )
    selected_specular = c.custom_expression(
        mf, "return Packed.b;",
        [("Packed", selected_appearance, ("", "Result"))],
        "float1", 11700, 2800,
        "Decode the coverage-weighted droplet target specular.",
    )
    selected_appearance_response = c.custom_expression(
        mf, "return Packed.a;",
        [("Packed", selected_appearance, ("", "Result"))],
        "float1", 11700, 3150,
        "Decode the combined Droplet1 and Droplet2 appearance response.",
    )
    selected_target_roughness_decl = c.named_declaration(
        mf, "SURFACE_SelectedTargetRoughness", selected_target_roughness, ("", "Result"), 12700, 2100
    )
    selected_roughness_blend_decl = c.named_declaration(
        mf, "SURFACE_SelectedRoughnessBlend", selected_roughness_blend, ("", "Result"), 12700, 2450
    )
    selected_specular_decl = c.named_declaration(
        mf, "SURFACE_SelectedSpecular", selected_specular, ("", "Result"), 12700, 2800
    )
    selected_appearance_response_decl = c.named_declaration(
        mf, "SURFACE_SelectedAppearanceResponse", selected_appearance_response, ("", "Result"), 12700, 3150
    )
    disabled_color_response = c.scalar_constant(
        mf, 0.0, 10700, 3500, "Disabled Surface Water Base Color response"
    )
    selected_color_response = c.static_switch_parameter(
        mf, "DWC_UseSurfaceWater", False,
        surface_color_response, ("", "Result"),
        disabled_color_response, ("", "Result"),
        11700, 3500,
        group="DWC Surface Water",
        description="Compile independently authored Droplet1 and Droplet2 Base Color response only for slots that use Surface Water.",
    )
    selected_color_response_decl = c.named_declaration(
        mf, "SURFACE_SelectedColorResponse", selected_color_response, ("", "Result"), 12700, 3500
    )
    enabled_amount_use = c.custom_expression(
        mf,
        "return max(saturate(Droplet1Amount), saturate(Droplet2Amount));",
        [
            ("Droplet1Amount", c.named_usage(mf, effective_coverage_decl, 10100, 2050), ("", "Result")),
            ("Droplet2Amount", c.named_usage(mf, effective_flow_coverage_decl, 10100, 2400), ("", "Result")),
        ],
        "float1", 10900, 2300,
        "Expose the union of independent Droplet1 and Droplet2 RT wetness values.",
    )
    disabled_amount_zero = c.scalar_constant(mf, 0.0, 10600, 2750, "Disabled Surface Water droplet amount")
    selected_amount = c.static_switch_parameter(
        mf, "DWC_UseSurfaceWater", False,
        enabled_amount_use, ("", "Result"),
        disabled_amount_zero, ("", "Result"),
        11600, 2450,
        group="DWC Surface Water",
        description="Compile Surface Water droplet wetness only for slots that use Surface Water.",
    )
    selected_amount_decl = c.named_declaration(
        mf, "SURFACE_SelectedDropletWetness", selected_amount, ("", "Result"), 12600, 2450
    )
    enabled_brush_use = c.named_usage(mf, visual_brush_decl, 10600, 3300)
    disabled_brush_zero = c.scalar_constant(mf, 0.0, 10600, 3850, "Disabled Surface Water visual droplet brush")
    selected_brush = c.static_switch_parameter(
        mf, "DWC_UseSurfaceWater", False,
        enabled_brush_use, ("", "Result"),
        disabled_brush_zero, ("", "Result"),
        11600, 3550,
        group="DWC Surface Water",
        description="Compile visual Surface Water droplet brush only for slots that use Surface Water.",
    )
    selected_brush_decl = c.named_declaration(
        mf, "SURFACE_SelectedDropletBrush", selected_brush, ("", "Result"), 12600, 3550
    )
    # Final outputs.
    final_color = c.custom_expression(
        mf,
        """
float C = saturate(ColorResponse);
float Brush = saturate(DropletBrush);
float Metal = saturate(BaseMetallic);
float NonMetal = 1.0 - Metal;
float SpecularCue = saturate(WaterSpecular);
float BaseLuminance = dot(BaseColor, float3(0.299, 0.587, 0.114));
float DarkSurfaceDamp = lerp(0.28, 1.0, smoothstep(0.05, 0.55, BaseLuminance));
float Edge = smoothstep(0.08, 0.48, Brush) * (1.0 - smoothstep(0.55, 0.96, Brush));
float Center = smoothstep(0.50, 1.0, Brush);
float CenterLift = 0.008 * Center * DarkSurfaceDamp;
float EdgeLift = (0.08 + 0.09 * SpecularCue) * Edge * DarkSurfaceDamp;
float3 NonMetalClearColor = lerp(AbsorbedColor, BaseColor, 0.38 + 0.16 * SpecularCue);
float3 NonMetalGlint = CenterLift + EdgeLift;
float NonMetalBlend = C * (0.58 + 0.18 * SpecularCue);
float3 NonMetalWater = lerp(AbsorbedColor, NonMetalClearColor + NonMetalGlint, NonMetalBlend);

float MetalEdgeLift = (0.015 + 0.035 * SpecularCue) * Edge * DarkSurfaceDamp;
float MetalCenterLift = 0.002 * Center * DarkSurfaceDamp;
float MetalBlend = C * (0.08 + 0.05 * SpecularCue);
float3 MetalWater = lerp(AbsorbedColor, BaseColor + MetalEdgeLift + MetalCenterLift, MetalBlend);

return saturate(lerp(NonMetalWater, MetalWater, Metal));
""",
        [
            ("BaseColor", c.named_usage(mf, declarations["BaseColor"], 9550, -5350), ("", "Result")),
            ("AbsorbedColor", c.named_usage(mf, absorbed_color_decl, 9550, -4800), ("", "Result")),
            ("BaseMetallic", c.named_usage(mf, declarations["BaseMetallic"], 9550, -4250), ("", "Result")),
            ("ColorResponse", c.named_usage(mf, selected_color_response_decl, 9550, -3700), ("", "Result")),
            ("DropletBrush", c.named_usage(mf, selected_brush_decl, 9550, -3150), ("", "Result")),
            ("WaterSpecular", c.named_usage(mf, selected_specular_decl, 9550, -2600), ("", "Result")),
        ],
        "float3", 11200, -4550,
        "Add a subtle clear-water film response inside final droplet coverage while preserving the source color.",
    )
    final_color_decl = c.named_declaration(mf, "FINAL_BaseColor", final_color, ("", "Result"), 12600, -4550)

    final_roughness = c.custom_expression(
        mf,
        "return saturate(lerp(AbsorbedRoughness, TargetRoughness, saturate(AppearanceResponse * RoughnessBlend)));",
        [
            ("AbsorbedRoughness", c.named_usage(mf, absorbed_rough_decl, 9550, -3000), ("", "Result")),
            ("TargetRoughness", c.named_usage(mf, selected_target_roughness_decl, 9550, -2450), ("", "Result")),
            ("AppearanceResponse", c.named_usage(mf, selected_appearance_response_decl, 9550, -1900), ("", "Result")),
            ("RoughnessBlend", c.named_usage(mf, selected_roughness_blend_decl, 9550, -1350), ("", "Result")),
        ],
        "float1", 11200, -2200,
        "Blend absorbed roughness toward the surface-water target.",
    )
    final_rough_decl = c.named_declaration(mf, "FINAL_Roughness", final_roughness, ("", "Result"), 12600, -2200)
    final_specular = c.custom_expression(
        mf,
        "return saturate(lerp(BaseSpecular, TargetSpecular, saturate(AppearanceResponse)));",
        [
            ("BaseSpecular", c.named_usage(mf, declarations["BaseSpecular"], 9550, -900), ("", "Result")),
            ("TargetSpecular", c.named_usage(mf, selected_specular_decl, 9550, -350), ("", "Result")),
            ("AppearanceResponse", c.named_usage(mf, selected_appearance_response_decl, 9550, 200), ("", "Result")),
        ],
        "float1", 11200, -350,
        "Blend source specular toward the surface-water target inside final droplet coverage.",
    )
    final_specular_decl = c.named_declaration(mf, "FINAL_Specular", final_specular, ("", "Result"), 12600, -350)
    final_normal_decl = c.named_declaration(
        mf, "FINAL_Normal", c.named_usage(mf, selected_normal_decl, 12800, -250), ("", "Result"), 13600, -250
    )

    outputs = [
        ("BaseColor", final_color_decl, "Final wet Base Color."),
        ("Roughness", final_rough_decl, "Final wet Roughness."),
        ("Specular", final_specular_decl, "Final wet Specular."),
        ("Normal", final_normal_decl, "Final tangent-space Normal."),
        ("SurfaceCoverage", selected_coverage_decl, "Visible mask-gated droplet coverage, or zero when compiled out."),
        ("DropletCoverage", selected_coverage_decl, "Visible mask-gated droplet coverage, or zero when compiled out."),
        ("DropletWetness", selected_amount_decl, "Single-channel droplet Wetness, or zero when Surface Water is compiled out."),
        ("DropletBrush", selected_brush_decl, "Visual mask-shaped droplet brush, or zero when Surface Water is compiled out."),
    ]
    for i, (name, declaration, description) in enumerate(outputs):
        use = c.named_usage(mf, declaration, 13700, -4200 + i * 1100)
        c.function_output(mf, name, use, ("", "Result"), i, 15100, -4200 + i * 1100, description)

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
