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
        ("SurfaceTime", "scalar", (0.0,), "Runtime time used for droplet lifetime fade."),
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
        "DropletNormalSlice",
        "SurfaceWaterNormalStrength",
        "SurfaceWaterRoughnessBlend",
        "SurfaceWaterSpecular",
        "SurfaceWaterTargetRoughness",
        "SurfaceWaterTotalStrength",
        "DropletMaskSlice",
        "DropletDetailSize",
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

    wrinkle_inputs = []
    for i, name in enumerate((
        "BaseNormal", "WrinkleNormal", "Wetness", "UseWrinkleNormalMap",
        "WrinkleStrength", "WrinkleWetnessMin", "WrinkleWetnessMax",
    )):
        wrinkle_inputs.append((name, c.named_usage(mf, declarations[name], -5450, -5400 + i * 650), ("", "Result")))
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
        "float3", -3900, -3350,
        "Blend the baked wrinkle normal into the original tangent normal.",
    )
    wrinkle_decl = c.named_declaration(mf, "ABSORBED_Normal", wrinkle_normal, ("", "Result"), -2550, -3350)

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

    # Droplet state RT. There is one state texture and no feature-type switch.
    rt_uv = c.named_usage(mf, declarations["DWCDataUV"], -850, -5250)
    droplet_rt = c.texture2d_parameter(
        mf, "DWC_SurfaceDropletRT", data_fallback, -250, -5250,
        sampler_type=c.linear_color_sampler(), group="DWC Surface Water",
        description="Droplet state RT: R=stamp region amount, G=spawn time, B=lifetime, A=reserved.",
    )
    c.try_connect(rt_uv, ("", "Result"), droplet_rt, ("Coordinates", "UVs"))
    droplet_amount = c.component_mask(mf, droplet_rt, "R", "R", 450, -5500)
    droplet_spawn = c.component_mask(mf, droplet_rt, "G", "G", 450, -4850)
    droplet_lifetime = c.component_mask(mf, droplet_rt, "B", "B", 450, -4200)
    amount_decl = c.named_declaration(mf, "SURFACE_DropletAmount", droplet_amount, ("", "Result"), 1200, -5500)
    spawn_decl = c.named_declaration(mf, "SURFACE_DropletSpawnTime", droplet_spawn, ("", "Result"), 1200, -4850)
    lifetime_decl = c.named_declaration(mf, "SURFACE_DropletLifetime", droplet_lifetime, ("", "Result"), 1200, -4200)

    lifetime_fade_inputs = [
        ("SpawnTime", c.named_usage(mf, spawn_decl, 1900, -5000), ("", "Result")),
        ("Lifetime", c.named_usage(mf, lifetime_decl, 1900, -4450), ("", "Result")),
        ("SurfaceTime", c.named_usage(mf, declarations["SurfaceTime"], 1900, -3900), ("", "Result")),
    ]
    lifetime_fade = c.custom_expression(
        mf,
        """
float Age = max(SurfaceTime - SpawnTime, 0.0);
float SafeLifetime = max(Lifetime, 0.01);
float FadeDuration = min(0.35, SafeLifetime * 0.15);
float FadeStart = max(SafeLifetime - FadeDuration, 0.0);
return 1.0 - smoothstep(FadeStart, SafeLifetime, Age);
""",
        lifetime_fade_inputs,
        "float1", 3100, -5000,
        "Keep droplets fully visible for most of their lifetime and fade only near the end.",
    )
    lifetime_fade_decl = c.named_declaration(mf, "SURFACE_DropletLifetimeFade", lifetime_fade, ("", "Result"), 4300, -5000)

    visible_amount = c.custom_expression(
        mf,
        "return saturate(Amount * LifetimeFade);",
        [
            ("Amount", c.named_usage(mf, amount_decl, 3100, -5550), ("", "Result")),
            ("LifetimeFade", c.named_usage(mf, lifetime_fade_decl, 3100, -5000), ("", "Result")),
        ],
        "float1", 4300, -5550,
        "Compute lifetime-faded RT amount before visibility threshold or authored mask gating.",
    )
    visible_amount_decl = c.named_declaration(mf, "SURFACE_DropletVisibleAmount", visible_amount, ("", "Result"), 5400, -5550)

    coverage_inputs = [
        ("VisibleAmount", c.named_usage(mf, visible_amount_decl, 1900, -5550), ("", "Result")),
    ]
    raw_coverage = c.custom_expression(
        mf,
        """
return VisibleAmount > 1.0e-4 ? 1.0 : 0.0;
""",
        coverage_inputs,
        "float1", 3100, -4450,
        "Treat any live droplet state as visible coverage; Total Strength controls visual intensity.",
    )
    raw_coverage_decl = c.named_declaration(mf, "SURFACE_RawDropletCoverage", raw_coverage, ("", "Result"), 4300, -4450)

    preview_override = c.scalar_parameter(
        mf, "DWC_PreviewSurfaceWaterOverride", 0.0, 1900, -2700,
        group="DWC Surface Water",
        description="Editor preview only: replace RT droplet coverage when greater than 0.5.",
    )
    preview_amount = c.scalar_parameter(
        mf, "DWC_PreviewSurfaceWaterAmount", 0.0, 1900, -2300,
        group="DWC Surface Water",
        description="Editor preview only: direct droplet amount.",
    )
    raw_cov_use = c.named_usage(mf, raw_coverage_decl, 3000, -2450)
    effective_coverage = c.custom_expression(
        mf,
        "return PreviewOverride > 0.5 ? (PreviewAmount > 1.0e-4 ? 1.0 : 0.0) : saturate(Coverage);",
        [
            ("PreviewOverride", preview_override, ("", "Result")),
            ("PreviewAmount", preview_amount, ("", "Result")),
            ("Coverage", raw_cov_use, ("", "Result")),
        ],
        "float1", 4200, -2450,
        "Select preview or runtime droplet coverage before authored mask gating.",
    )
    effective_coverage_decl = c.named_declaration(mf, "SURFACE_EffectiveDropletCoverage", effective_coverage, ("", "Result"), 5400, -2450)

    normal_call = c.function_call(
        mf, normal_function, 800, -900,
        "Sample the droplet mask and detail normal.",
    )
    normal_inputs = [
        ("SurfaceWaterNormalUV", declarations["SurfaceWaterNormalUV"]),
        ("DropletDetailSize", profile_declarations["DropletDetailSize"]),
        ("DropletMaskSlice", profile_declarations["DropletMaskSlice"]),
        ("DropletNormalSlice", profile_declarations["DropletNormalSlice"]),
    ]
    for i, (input_name, declaration) in enumerate(normal_inputs):
        usage = c.named_usage(mf, declaration, -800 + (i % 2) * 850, -1450 + (i // 2) * 700)
        c.try_connect(usage, ("", "Result"), normal_call, input_name)
    droplet_mask_decl = c.named_declaration(mf, "SURFACE_DropletMask", normal_call, "DropletMask", 2100, -1150)
    droplet_normal_decl = c.named_declaration(mf, "SURFACE_DropletNormal", normal_call, "DropletNormal", 2100, -450)

    visible_coverage = c.custom_expression(
        mf,
        "return saturate(Coverage) * saturate(Mask);",
        [
            ("Coverage", c.named_usage(mf, effective_coverage_decl, 5700, -1900), ("", "Result")),
            ("Mask", c.named_usage(mf, droplet_mask_decl, 5700, -1300), ("", "Result")),
        ],
        "float1", 6600, -1600,
        "Gate droplet state coverage by the authored droplet mask.",
    )
    surface_coverage_decl = c.named_declaration(mf, "SURFACE_Coverage", visible_coverage, ("", "Result"), 7600, -1600)

    visual_brush = c.custom_expression(
        mf,
        """
float C = saturate(Coverage);
float M = saturate(Mask);
return C * M;
""",
        [
            ("Coverage", c.named_usage(mf, effective_coverage_decl, 5700, -700), ("", "Result")),
            ("Mask", c.named_usage(mf, droplet_mask_decl, 5700, -100), ("", "Result")),
        ],
        "float1", 6600, 250,
        "Use the authored droplet mask and live stamp region as the visual water brush.",
    )
    visual_brush_decl = c.named_declaration(mf, "SURFACE_VisualBrush", visual_brush, ("", "Result"), 7600, 250)

    surface_normal = c.custom_expression(
        mf,
        """
// Preserve the established WP custom-mesh response. Texture-path parity is
// handled in C++ by uploading the authored 512 texture directly.
float C = saturate(Coverage);
float Strength = clamp(NormalStrength, 0.0, 3.0);
float DropletVisualHeightBoost = 1.65;
float DropletWeight = C * saturate(TotalStrength);
float2 CombinedXY = DropletNormal.xy * DropletWeight * min(Strength * DropletVisualHeightBoost, 12.0);
return normalize(float3(CombinedXY, 1.0));
""",
        [
            ("BaseNormal", c.named_usage(mf, wrinkle_decl, 4400, -500), ("", "Result")),
            ("DropletNormal", c.named_usage(mf, droplet_normal_decl, 4400, 50), ("", "Result")),
            ("Coverage", c.named_usage(mf, surface_coverage_decl, 4400, 600), ("", "Result")),
            ("NormalStrength", c.named_usage(mf, profile_declarations["SurfaceWaterNormalStrength"], 4400, 1150), ("", "Result")),
            ("TotalStrength", c.named_usage(mf, profile_declarations["SurfaceWaterTotalStrength"], 4400, 1700), ("", "Result")),
        ],
        "float3", 6000, 450,
        "Match the Wetness Profile preview surface-normal formula for droplet normals.",
    )
    surface_normal_decl = c.named_declaration(mf, "SURFACE_NormalRaw", surface_normal, ("", "Result"), 7600, 450)

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
    enabled_amount_use = c.named_usage(mf, amount_decl, 10600, 2200)
    disabled_amount_zero = c.scalar_constant(mf, 0.0, 10600, 2750, "Disabled Surface Water droplet amount")
    selected_amount = c.static_switch_parameter(
        mf, "DWC_UseSurfaceWater", False,
        enabled_amount_use, ("", "Result"),
        disabled_amount_zero, ("", "Result"),
        11600, 2450,
        group="DWC Surface Water",
        description="Compile raw Surface Water droplet amount only for slots that use Surface Water.",
    )
    selected_amount_decl = c.named_declaration(
        mf, "SURFACE_SelectedDropletAmount", selected_amount, ("", "Result"), 12600, 2450
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
    enabled_lifetime_fade_use = c.named_usage(mf, lifetime_fade_decl, 10600, 4400)
    disabled_lifetime_fade_zero = c.scalar_constant(mf, 0.0, 10600, 4950, "Disabled Surface Water lifetime fade")
    selected_lifetime_fade = c.static_switch_parameter(
        mf, "DWC_UseSurfaceWater", False,
        enabled_lifetime_fade_use, ("", "Result"),
        disabled_lifetime_fade_zero, ("", "Result"),
        11600, 4650,
        group="DWC Surface Water",
        description="Compile raw Surface Water droplet lifetime fade only for slots that use Surface Water.",
    )
    selected_lifetime_fade_decl = c.named_declaration(
        mf, "SURFACE_SelectedDropletLifetimeFade", selected_lifetime_fade, ("", "Result"), 12600, 4650
    )

    # Final outputs.
    final_color = c.custom_expression(
        mf,
        """
float C = saturate(Coverage);
float Brush = saturate(DropletBrush);
float Metal = saturate(BaseMetallic);
float NonMetal = 1.0 - Metal;
float SpecularCue = saturate(WaterSpecular);
float Total = saturate(TotalStrength);
float BaseLuminance = dot(BaseColor, float3(0.299, 0.587, 0.114));
float DarkSurfaceDamp = lerp(0.28, 1.0, smoothstep(0.05, 0.55, BaseLuminance));
float Edge = smoothstep(0.08, 0.48, Brush) * (1.0 - smoothstep(0.55, 0.96, Brush));
float Center = smoothstep(0.50, 1.0, Brush);
float CenterLift = 0.008 * Center * DarkSurfaceDamp;
float EdgeLift = (0.08 + 0.09 * SpecularCue) * Edge * DarkSurfaceDamp;
float3 NonMetalClearColor = lerp(AbsorbedColor, BaseColor, 0.38 + 0.16 * SpecularCue);
float3 NonMetalGlint = CenterLift + EdgeLift;
float NonMetalBlend = C * Total * (0.58 + 0.18 * SpecularCue);
float3 NonMetalWater = lerp(AbsorbedColor, NonMetalClearColor + NonMetalGlint, NonMetalBlend);

float MetalEdgeLift = (0.015 + 0.035 * SpecularCue) * Edge * DarkSurfaceDamp;
float MetalCenterLift = 0.002 * Center * DarkSurfaceDamp;
float MetalBlend = C * Total * (0.08 + 0.05 * SpecularCue);
float3 MetalWater = lerp(AbsorbedColor, BaseColor + MetalEdgeLift + MetalCenterLift, MetalBlend);

return saturate(lerp(NonMetalWater, MetalWater, Metal));
""",
        [
            ("BaseColor", c.named_usage(mf, declarations["BaseColor"], 9550, -5350), ("", "Result")),
            ("AbsorbedColor", c.named_usage(mf, absorbed_color_decl, 9550, -4800), ("", "Result")),
            ("BaseMetallic", c.named_usage(mf, declarations["BaseMetallic"], 9550, -4250), ("", "Result")),
            ("Coverage", c.named_usage(mf, selected_coverage_decl, 9550, -3700), ("", "Result")),
            ("DropletBrush", c.named_usage(mf, selected_brush_decl, 9550, -3150), ("", "Result")),
            ("WaterSpecular", c.named_usage(mf, profile_declarations["SurfaceWaterSpecular"], 9550, -2600), ("", "Result")),
            ("TotalStrength", c.named_usage(mf, profile_declarations["SurfaceWaterTotalStrength"], 9550, -2050), ("", "Result")),
        ],
        "float3", 11200, -4550,
        "Add a subtle clear-water film response inside final droplet coverage while preserving the source color.",
    )
    final_color_decl = c.named_declaration(mf, "FINAL_BaseColor", final_color, ("", "Result"), 12600, -4550)

    final_roughness = c.custom_expression(
        mf,
        "return saturate(lerp(AbsorbedRoughness, TargetRoughness, saturate(Coverage * RoughnessBlend * TotalStrength)));",
        [
            ("AbsorbedRoughness", c.named_usage(mf, absorbed_rough_decl, 9550, -3000), ("", "Result")),
            ("TargetRoughness", c.named_usage(mf, profile_declarations["SurfaceWaterTargetRoughness"], 9550, -2450), ("", "Result")),
            ("Coverage", c.named_usage(mf, selected_coverage_decl, 9550, -1900), ("", "Result")),
            ("RoughnessBlend", c.named_usage(mf, profile_declarations["SurfaceWaterRoughnessBlend"], 9550, -1350), ("", "Result")),
            ("TotalStrength", c.named_usage(mf, profile_declarations["SurfaceWaterTotalStrength"], 9550, -800), ("", "Result")),
        ],
        "float1", 11200, -2200,
        "Blend absorbed roughness toward the surface-water target.",
    )
    final_rough_decl = c.named_declaration(mf, "FINAL_Roughness", final_roughness, ("", "Result"), 12600, -2200)
    final_specular = c.custom_expression(
        mf,
        "return saturate(lerp(BaseSpecular, TargetSpecular, saturate(Coverage * TotalStrength)));",
        [
            ("BaseSpecular", c.named_usage(mf, declarations["BaseSpecular"], 9550, -900), ("", "Result")),
            ("TargetSpecular", c.named_usage(mf, profile_declarations["SurfaceWaterSpecular"], 9550, -350), ("", "Result")),
            ("Coverage", c.named_usage(mf, selected_coverage_decl, 9550, 200), ("", "Result")),
            ("TotalStrength", c.named_usage(mf, profile_declarations["SurfaceWaterTotalStrength"], 9550, 750), ("", "Result")),
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
        ("DropletAmount", selected_amount_decl, "Raw droplet RT amount, or zero when Surface Water is compiled out."),
        ("DropletBrush", selected_brush_decl, "Visual mask-shaped droplet brush, or zero when Surface Water is compiled out."),
        ("DropletLifetimeFade", selected_lifetime_fade_decl, "Raw droplet lifetime fade, or zero when Surface Water is compiled out."),
    ]
    for i, (name, declaration, description) in enumerate(outputs):
        use = c.named_usage(mf, declaration, 13700, -4200 + i * 1100)
        c.function_output(mf, name, use, ("", "Result"), i, 15100, -4200 + i * 1100, description)

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
