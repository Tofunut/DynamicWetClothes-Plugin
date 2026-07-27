"""Create or recreate MF_DWC_EvaluateSurfaceAppearance with nested spacious comments."""
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
        c.fail(
            f"{c.asset_path(name)} is missing. Run its DWC Python creation script first."
        )
    return value


def build() -> None:
    data_fallback, _ = c.ensure_default_textures()
    profile_function = require_helper_function(PROFILE_FUNCTION_NAME)
    normal_function = require_helper_function(NORMAL_FUNCTION_NAME)
    mf = c.create_or_replace_material_function(ASSET_NAME, OVERWRITE_EXISTING)

    # Four large processing stages.  Every child comment is separated from its
    # neighbors by 500+ graph units, and parent comments contain only children.
    c.create_comment(mf, "1. Inputs & Shared Data", -21000, -6000, 10500, 9000, parent=True)
    c.create_comment(mf, "1-1. Original & Authored Material Inputs", -20500, -5500, 4700, 5200)
    c.create_comment(mf, "1-2. Runtime & UV Inputs", -15200, -5500, 4000, 5200)
    c.create_comment(mf, "1-3. Render Profile Lookup", -20500, 300, 4700, 2200)
    c.create_comment(mf, "1-4. Shared Constants", -15200, 300, 4000, 2200)

    c.create_comment(mf, "2. Absorbed Wetness Appearance", -9500, -6000, 8500, 9000, parent=True)
    c.create_comment(mf, "2-1. Absorbed Wetness", -9000, -5500, 3500, 3500)
    c.create_comment(mf, "2-2. Wet Base Color", -5000, -5500, 3500, 3500)
    c.create_comment(mf, "2-3. Wrinkle Response", -9000, -1500, 3500, 4000)
    c.create_comment(mf, "2-4. Transparency Response", -5000, -1500, 3500, 4000)

    c.create_comment(mf, "3. Surface Water Appearance", 0, -6000, 11500, 9000, parent=True)
    c.create_comment(mf, "3-1. Surface State Sampling", 500, -5500, 3300, 3500)
    c.create_comment(mf, "3-2. Droplet Lifetime & Coverage", 4300, -5500, 3000, 3500)
    c.create_comment(mf, "3-3. Rivulet Lifetime & Coverage", 7800, -5500, 3200, 3500)
    c.create_comment(mf, "3-4. Detail Normal Sampling & Composition", 500, -1500, 6000, 4000)
    c.create_comment(mf, "3-5. Surface Selection & Roughness", 7000, -1500, 4000, 4000)

    c.create_comment(mf, "4. Final Composition", 12500, -6000, 9500, 9000, parent=True)
    c.create_comment(mf, "4-1. Final Base Color", 13000, -5500, 3800, 3500)
    c.create_comment(mf, "4-2. Final Roughness", 17300, -5500, 4200, 3500)
    c.create_comment(mf, "4-3. Final Normal", 13000, -1500, 5000, 4000)
    c.create_comment(mf, "4-4. Function Outputs", 18500, -1500, 3000, 4000)

    declarations: dict[str, object] = {}

    # 1-1 Original and authored material inputs.
    authored_specs = [
        ("BaseColor", "vector3", (1.0, 1.0, 1.0), "Original material Base Color."),
        ("BaseRoughness", "scalar", (0.5,), "Original material Roughness."),
        ("BaseNormal", "vector3", (0.0, 0.0, 1.0), "Original tangent-space Normal."),
        ("WetDarkeningStrength", "scalar", (0.35,), "Absorbed wetness darkening strength."),
        ("WetRoughness", "scalar", (0.12,), "Target roughness at full absorbed wetness."),
        ("WrinkleNormal", "vector3", (0.0, 0.0, 1.0), "Baked wrinkle tangent normal."),
        ("UseWrinkleNormalMap", "scalar", (0.0,), "Wrinkle map enable weight."),
        ("WrinkleStrength", "scalar", (1.0,), "Wrinkle detail-normal strength."),
        ("WrinkleWetnessMin", "scalar", (0.25,), "Wetness where wrinkle response starts."),
        ("WrinkleWetnessMax", "scalar", (1.0,), "Wetness where wrinkle response reaches full strength."),
        ("TransparencyColor", "vector3", (1.0, 1.0, 1.0), "Color revealed by wet transparency."),
        ("TransparencyAlpha", "scalar", (0.0,), "Baked transparency region/strength."),
        ("UseTransparencyMap", "scalar", (0.0,), "Transparency map enable weight."),
        ("TransparencyWetnessMin", "scalar", (0.25,), "Wetness where transparency starts."),
        ("TransparencyWetnessMax", "scalar", (1.0,), "Wetness where transparency reaches full strength."),
    ]
    sort_priority = 0
    for i, (name, kind, preview, desc) in enumerate(authored_specs):
        column = i // 8
        row = i % 8
        x = -20200 + column * 2150
        y = -5050 + row * 560
        node = c.function_input(mf, name, kind, preview, sort_priority, x, y, desc)
        declarations[name] = c.named_declaration(
            mf, f"IN_{name}", node, ("", "Result"), x + 900, y
        )
        sort_priority += 1

    # 1-2 Runtime and UV inputs.
    runtime_specs = [
        ("Wetness", "scalar", (0.0,), "Resolved CPU/GPU absorbed wetness."),
        ("DWCDataUV", "vector2", (0.0, 0.0), "DWC Data UV used by Wet Part data and runtime state textures."),
        ("SurfaceWaterNormalUV", "vector2", (0.0, 0.0), "Repeated detail-normal mesh UV."),
        ("SurfaceTime", "scalar", (0.0,), "Surface-water lifetime and scroll time."),
        ("SurfaceWaterTargetRoughness", "scalar", (0.02,), "Common visible surface-water target roughness."),
    ]
    for i, (name, kind, preview, desc) in enumerate(runtime_specs):
        y = -5050 + i * 650
        node = c.function_input(mf, name, kind, preview, sort_priority, -14900, y, desc)
        declarations[name] = c.named_declaration(
            mf, f"IN_{name}", node, ("", "Result"), -13600, y
        )
        sort_priority += 1

    # 1-3 Render Profile Lookup.
    data_uv_use = c.named_usage(mf, declarations["DWCDataUV"], -20150, 950)
    profile_call = c.function_call(
        mf, profile_function, -19550, 950,
        "Resolve the current pixel's runtime Render Profile."
    )
    c.try_connect(data_uv_use, ("", "Result"), profile_call, "DWCDataUV")
    profile_outputs = [
        "AbsorbedDarkeningStrength", "AbsorbedGlossinessStrength",
        "DropletNormalSlice", "RivuletNormalSlice",
        "SurfaceWaterNormalStrength", "SurfaceWaterRoughnessStrength",
        "SurfaceVisibilityThreshold", "RivuletUVScrollSpeed",
        "DropletMaskSlice", "RivuletMaskSlice",
        "DropletDetailSize", "RivuletDetailSize",
    ]
    profile_declarations: dict[str, object] = {}
    for i, name in enumerate(profile_outputs):
        x = -18800 + (i % 4) * 780
        y = 650 + (i // 4) * 850
        profile_declarations[name] = c.named_declaration(
            mf, f"PROFILE_{name}", profile_call, name, x, y
        )

    # 1-4 Shared Constants.
    lifetime_epsilon = c.scalar_constant(mf, 0.01, -14800, 850, "Avoid zero lifetime division")
    visibility_feather = c.scalar_constant(mf, 0.4, -14800, 1450, "CL145 default coverage smoothstep feather: Surface Water ramps from amount threshold 0.25 to 0.65.")
    flat_normal = c.vector_constant(mf, (0.0, 0.0, 1.0), -14800, 2050, "Flat tangent normal")
    shared_lifetime = c.named_declaration(mf, "SHARED_LifetimeEpsilon", lifetime_epsilon, ("", "Result"), -13700, 850)
    shared_feather = c.named_declaration(mf, "SHARED_VisibilityFeather", visibility_feather, ("", "Result"), -13700, 1450)
    shared_flat = c.named_declaration(mf, "SHARED_FlatNormal", flat_normal, ("", "Result"), -13700, 2050)

    # 2-1 Absorbed Wetness.
    wetness_use = c.named_usage(mf, declarations["Wetness"], -8650, -4050)
    absorbed_wetness = c.custom_expression(
        mf, "return saturate(Wetness);",
        [("Wetness", wetness_use, ("", "Result"))],
        "float1", -8000, -4050, "Clamp resolved wetness to 0..1."
    )
    absorbed_wetness_decl = c.named_declaration(
        mf, "ABSORBED_Wetness", absorbed_wetness, ("", "Result"), -6900, -4050
    )

    # 2-2 Wet Base Color.
    base_color_use = c.named_usage(mf, declarations["BaseColor"], -4650, -4900)
    absorbed_use = c.named_usage(mf, absorbed_wetness_decl, -4650, -4250)
    darkening_use = c.named_usage(mf, declarations["WetDarkeningStrength"], -4650, -3600)
    profile_darkening_use = c.named_usage(mf, profile_declarations["AbsorbedDarkeningStrength"], -4650, -2950)
    wet_base_color = c.custom_expression(
        mf,
        "float Amount = saturate(Wetness * DarkeningStrength * ProfileDarkeningStrength);\nreturn BaseColor * (1.0 - Amount);",
        [
            ("BaseColor", base_color_use, ("", "Result")),
            ("Wetness", absorbed_use, ("", "Result")),
            ("DarkeningStrength", darkening_use, ("", "Result")),
            ("ProfileDarkeningStrength", profile_darkening_use, ("", "Result")),
        ],
        "float3", -3500, -4050, "Apply absorbed-wetness darkening to original Base Color."
    )
    wet_base_decl = c.named_declaration(
        mf, "ABSORBED_WetBaseColor", wet_base_color, ("", "Result"), -2150, -4050
    )

    # 2-3 Wrinkle Response.
    wrinkle_wet_use = c.named_usage(mf, absorbed_wetness_decl, -8650, -550)
    wrinkle_min_use = c.named_usage(mf, declarations["WrinkleWetnessMin"], -8650, 0)
    wrinkle_max_use = c.named_usage(mf, declarations["WrinkleWetnessMax"], -8650, 550)
    wrinkle_strength_use = c.named_usage(mf, declarations["WrinkleStrength"], -8650, 1100)
    wrinkle_enabled_use = c.named_usage(mf, declarations["UseWrinkleNormalMap"], -8650, 1650)
    wrinkle_weight = c.custom_expression(
        mf,
        "float T = saturate((Wetness - WetnessMin) / max(WetnessMax - WetnessMin, 0.0001));\nreturn saturate(T * Strength * Enabled);",
        [
            ("Wetness", wrinkle_wet_use, ("", "Result")),
            ("WetnessMin", wrinkle_min_use, ("", "Result")),
            ("WetnessMax", wrinkle_max_use, ("", "Result")),
            ("Strength", wrinkle_strength_use, ("", "Result")),
            ("Enabled", wrinkle_enabled_use, ("", "Result")),
        ],
        "float1", -7350, 550, "Compute wetness-driven wrinkle normal weight."
    )
    wrinkle_weight_decl = c.named_declaration(
        mf, "ABSORBED_WrinkleWeight", wrinkle_weight, ("", "Result"), -6300, 300
    )
    wrinkle_normal_use = c.named_usage(mf, declarations["WrinkleNormal"], -7350, 1700)
    wrinkle_normal_decl = c.named_declaration(
        mf, "ABSORBED_WrinkleNormal", wrinkle_normal_use, ("", "Result"), -6300, 1700
    )

    # 2-4 Transparency Response.
    wet_base_use = c.named_usage(mf, wet_base_decl, -4650, -650)
    transparency_color_use = c.named_usage(mf, declarations["TransparencyColor"], -4650, -100)
    transparency_alpha_use = c.named_usage(mf, declarations["TransparencyAlpha"], -4650, 450)
    transparency_enabled_use = c.named_usage(mf, declarations["UseTransparencyMap"], -4650, 1000)
    transparency_min_use = c.named_usage(mf, declarations["TransparencyWetnessMin"], -4650, 1550)
    transparency_max_use = c.named_usage(mf, declarations["TransparencyWetnessMax"], -4650, 2100)
    transparency_wet_use = c.named_usage(mf, absorbed_wetness_decl, -3600, 2100)
    absorbed_base_color = c.custom_expression(
        mf,
        """
float T = saturate((Wetness - WetnessMin) / max(WetnessMax - WetnessMin, 0.0001));
float Weight = saturate(TransparencyAlpha * T * Enabled);
return lerp(WetBaseColor, TransparencyColor, Weight);
""",
        [
            ("WetBaseColor", wet_base_use, ("", "Result")),
            ("TransparencyColor", transparency_color_use, ("", "Result")),
            ("TransparencyAlpha", transparency_alpha_use, ("", "Result")),
            ("Enabled", transparency_enabled_use, ("", "Result")),
            ("WetnessMin", transparency_min_use, ("", "Result")),
            ("WetnessMax", transparency_max_use, ("", "Result")),
            ("Wetness", transparency_wet_use, ("", "Result")),
        ],
        "float3", -3300, 700, "Blend the wet base color toward the baked transparency reveal color."
    )
    absorbed_base_decl = c.named_declaration(
        mf, "ABSORBED_BaseColor", absorbed_base_color, ("", "Result"), -2050, 700
    )

    # 3-1 Surface State Sampling.
    rt_uv_droplet = c.named_usage(mf, declarations["DWCDataUV"], 750, -4700)
    rt_uv_rivulet = c.named_usage(mf, declarations["DWCDataUV"], 750, -3100)
    droplet_rt = c.texture2d_parameter(
        mf, "DWC_SurfaceDropletRT", data_fallback, 1300, -4700,
        sampler_type=c.linear_color_sampler(), group="DWC Surface Water",
        description="Droplet state RT: R=amount, G=spawn time, B=lifetime."
    )
    rivulet_rt = c.texture2d_parameter(
        mf, "DWC_SurfaceRivuletRT", data_fallback, 1300, -3100,
        sampler_type=c.linear_color_sampler(), group="DWC Surface Water",
        description="Rivulet state RT: R=amount, G=spawn time, B=lifetime, A=encoded flow angle."
    )
    c.try_connect(rt_uv_droplet, ("", "Result"), droplet_rt, ("Coordinates", "UVs"))
    c.try_connect(rt_uv_rivulet, ("", "Result"), rivulet_rt, ("Coordinates", "UVs"))

    surface_decls: dict[str, object] = {}
    droplet_channels = [("DropletAmount", "R"), ("DropletSpawnTime", "G"), ("DropletLifetime", "B")]
    for i, (name, channel) in enumerate(droplet_channels):
        mask = c.component_mask(mf, droplet_rt, channel, channel, 2100, -5000 + i * 550)
        surface_decls[name] = c.named_declaration(mf, f"SURFACE_{name}", mask, ("", "Result"), 2950, -5000 + i * 550)
    rivulet_channels = [("RivuletAmount", "R"), ("RivuletSpawnTime", "G"), ("RivuletLifetime", "B"), ("RivuletEncodedFlowAngle", "A")]
    for i, (name, channel) in enumerate(rivulet_channels):
        mask = c.component_mask(mf, rivulet_rt, channel, channel, 2100, -3200 + i * 500)
        surface_decls[name] = c.named_declaration(mf, f"SURFACE_{name}", mask, ("", "Result"), 2950, -3200 + i * 500)

    # Shared coverage helper builder.
    def create_coverage(prefix: str, x0: int, y0: int, comment: str):
        amount_use = c.named_usage(mf, surface_decls[f"{prefix}Amount"], x0, y0)
        spawn_use = c.named_usage(mf, surface_decls[f"{prefix}SpawnTime"], x0, y0 + 500)
        lifetime_use = c.named_usage(mf, surface_decls[f"{prefix}Lifetime"], x0, y0 + 1000)
        time_use = c.named_usage(mf, declarations["SurfaceTime"], x0, y0 + 1500)
        threshold_use = c.named_usage(mf, profile_declarations["SurfaceVisibilityThreshold"], x0, y0 + 2000)
        epsilon_use = c.named_usage(mf, shared_lifetime, x0 + 650, y0 + 1500)
        feather_use = c.named_usage(mf, shared_feather, x0 + 650, y0 + 2000)
        coverage = c.custom_expression(
            mf,
            """
float Age = max(SurfaceTime - SpawnTime, 0.0);
float LifetimeFade = 1.0 - saturate(Age / max(Lifetime, LifetimeEpsilon));
float VisibleAmount = Amount * LifetimeFade;
float ThresholdMin = saturate(VisibilityThreshold);
float ThresholdMax = min(ThresholdMin + max(VisibilityFeather, 1.0e-4), 1.0);
return smoothstep(ThresholdMin, ThresholdMax, VisibleAmount);
""",
            [
                ("Amount", amount_use, ("", "Result")),
                ("SpawnTime", spawn_use, ("", "Result")),
                ("Lifetime", lifetime_use, ("", "Result")),
                ("SurfaceTime", time_use, ("", "Result")),
                ("VisibilityThreshold", threshold_use, ("", "Result")),
                ("LifetimeEpsilon", epsilon_use, ("", "Result")),
                ("VisibilityFeather", feather_use, ("", "Result")),
            ],
            "float1", x0 + 1300, y0 + 900, comment,
        )
        return c.named_declaration(
            mf, f"SURFACE_{prefix}Coverage", coverage, ("", "Result"), x0 + 2250, y0 + 900
        )

    # 3-2 and 3-3 coverage blocks.
    droplet_coverage_decl = create_coverage(
        "Droplet", 4500, -5150, "Compute droplet lifetime fade and visible coverage."
    )
    rivulet_coverage_decl = create_coverage(
        "Rivulet", 8000, -5150, "Compute rivulet lifetime fade and visible coverage."
    )

    # 3-4 Detail Normal Sampling & Weight.
    normal_call = c.function_call(
        mf, normal_function, 3200, -150,
        "Sample raw droplet/rivulet detail normals using mesh UV and stored flow angle."
    )
    normal_call_inputs = [
        ("SurfaceWaterNormalUV", declarations["SurfaceWaterNormalUV"]),
        ("SurfaceTime", declarations["SurfaceTime"]),
        ("DropletMaskSlice", profile_declarations["DropletMaskSlice"]),
        ("DropletNormalSlice", profile_declarations["DropletNormalSlice"]),
        ("RivuletMaskSlice", profile_declarations["RivuletMaskSlice"]),
        ("RivuletNormalSlice", profile_declarations["RivuletNormalSlice"]),
        ("DropletDetailSize", profile_declarations["DropletDetailSize"]),
        ("RivuletDetailSize", profile_declarations["RivuletDetailSize"]),
        ("RivuletEncodedFlowAngle", surface_decls["RivuletEncodedFlowAngle"]),
        ("RivuletUVScrollSpeed", profile_declarations["RivuletUVScrollSpeed"]),
    ]
    for i, (input_name, declaration) in enumerate(normal_call_inputs):
        usage = c.named_usage(mf, declaration, 800 + (i % 2) * 1100, -900 + (i // 2) * 700)
        c.try_connect(usage, ("", "Result"), normal_call, input_name)

    droplet_mask_decl = c.named_declaration(
        mf, "SURFACE_DropletMask", normal_call, "DropletMask", 4100, -1250
    )
    droplet_normal_decl = c.named_declaration(
        mf, "SURFACE_DropletNormal", normal_call, "DropletNormal", 4100, -750
    )
    rivulet_mask_decl = c.named_declaration(
        mf, "SURFACE_RivuletMask", normal_call, "RivuletMask", 4100, -450
    )
    rivulet_normal_decl = c.named_declaration(
        mf, "SURFACE_RivuletNormal", normal_call, "RivuletNormal", 4100, 50
    )
    # 3-5 Surface selection and roughness. The reference graph gates visible
    # detail by the authored mask instead of applying the whole RT stamp as one
    # flat glossy/dark patch.
    droplet_cov_use2 = c.named_usage(mf, droplet_coverage_decl, 7350, -650)
    rivulet_cov_use2 = c.named_usage(mf, rivulet_coverage_decl, 7350, 50)
    droplet_mask_use2 = c.named_usage(mf, droplet_mask_decl, 7350, 650)
    rivulet_mask_use2 = c.named_usage(mf, rivulet_mask_decl, 7350, 950)
    surface_coverage = c.custom_expression(
        mf,
        """
float D = saturate(DropletCoverage) * saturate(DropletMask);
float F = saturate(RivuletCoverage) * saturate(RivuletMask);
return saturate(D + F - D * F);
""",
        [
            ("DropletCoverage", droplet_cov_use2, ("", "Result")),
            ("RivuletCoverage", rivulet_cov_use2, ("", "Result")),
            ("DropletMask", droplet_mask_use2, ("", "Result")),
            ("RivuletMask", rivulet_mask_use2, ("", "Result")),
        ],
        "float1", 8200, -300, "CL145-style Surface Water combined mask: D + F - D * F after authored mask gating."
    )
    surface_coverage_decl = c.named_declaration(mf, "SURFACE_Coverage", surface_coverage, ("", "Result"), 9000, -300)
    surface_coverage_use = c.named_usage(mf, surface_coverage_decl, 7350, 1000)
    rough_strength_use = c.named_usage(mf, profile_declarations["SurfaceWaterRoughnessStrength"], 7350, 1650)
    surface_roughness_weight = c.custom_expression(
        mf, "return saturate(Coverage * RoughnessStrength);",
        [("Coverage", surface_coverage_use, ("", "Result")), ("RoughnessStrength", rough_strength_use, ("", "Result"))],
        "float1", 8200, 1300, "Reference-style Surface Water roughness weight: visible masked coverage scaled by the simplified profile strength."
    )
    surface_roughness_raw_decl = c.named_declaration(
        mf, "SURFACE_RoughnessWeightRaw", surface_roughness_weight, ("", "Result"), 9300, 1300
    )

    droplet_normal_use2 = c.named_usage(mf, droplet_normal_decl, 7200, 2050)
    rivulet_normal_use2 = c.named_usage(mf, rivulet_normal_decl, 7200, 2500)
    droplet_cov_use3 = c.named_usage(mf, droplet_coverage_decl, 8000, 2050)
    rivulet_cov_use3 = c.named_usage(mf, rivulet_coverage_decl, 8000, 2500)
    droplet_mask_use3 = c.named_usage(mf, droplet_mask_decl, 8400, 2050)
    rivulet_mask_use3 = c.named_usage(mf, rivulet_mask_decl, 8400, 2500)
    normal_strength_use3 = c.named_usage(mf, profile_declarations["SurfaceWaterNormalStrength"], 8000, 2950)
    combined_surface_normal = c.custom_expression(
        mf,
        """
float D = saturate(DropletCoverage) * saturate(DropletMask);
float F = saturate(RivuletCoverage) * saturate(RivuletMask);
float Strength = clamp(NormalStrength, 0.0, 8.0);

// CL145 strengthens only XY, preserves each texture normal's Z/B profile, then normalizes.
float3 DropletN = normalize(DropletNormal);
DropletN = normalize(float3(DropletN.xy * Strength, DropletN.z));

float3 RivuletN = normalize(RivuletNormal);
RivuletN = normalize(float3(RivuletN.xy * Strength, RivuletN.z));

float FlowRatio = saturate(F / max(D + F, 0.001));
float3 CombinedWaterNormal = normalize(lerp(DropletN, RivuletN, FlowRatio));
float CombinedMask = saturate(D + F - D * F);
return normalize(lerp(float3(0.0, 0.0, 1.0), CombinedWaterNormal, CombinedMask));
""",
        [
            ("DropletNormal", droplet_normal_use2, ("", "Result")),
            ("RivuletNormal", rivulet_normal_use2, ("", "Result")),
            ("DropletCoverage", droplet_cov_use3, ("", "Result")),
            ("RivuletCoverage", rivulet_cov_use3, ("", "Result")),
            ("DropletMask", droplet_mask_use3, ("", "Result")),
            ("RivuletMask", rivulet_mask_use3, ("", "Result")),
            ("NormalStrength", normal_strength_use3, ("", "Result")),
        ],
        "float3", 9000, 2400,
        "CL145-style Surface Water normal: strengthen XY while preserving sampled Z, blend Droplet/Flow by F/(D+F), then lerp from flat by the combined mask."
    )
    combined_surface_decl = c.named_declaration(
        mf, "SURFACE_CombinedNormalRaw", combined_surface_normal, ("", "Result"), 10000, 2400
    )

    # One shared static switch controls the entire Surface Water branch. Packing
    # normal.xyz + roughness weight in A avoids duplicate static parameters and
    # lets the compiler remove RT and Texture2DArray sampling for CPU/no-surface slots.
    combined_surface_use = c.named_usage(mf, combined_surface_decl, 10100, 1700)
    roughness_raw_use = c.named_usage(mf, surface_roughness_raw_decl, 10100, 2200)
    enabled_surface_pack = c.append_vector(
        mf, combined_surface_use, ("", "Result"), roughness_raw_use, ("", "Result"),
        10700, 1900, "Pack Surface Water normal and roughness contribution."
    )
    disabled_flat_use = c.named_usage(mf, shared_flat, 10100, 2850)
    disabled_zero = c.scalar_constant(mf, 0.0, 10100, 3300, "Disabled Surface Water roughness weight")
    disabled_surface_pack = c.append_vector(
        mf, disabled_flat_use, ("", "Result"), disabled_zero, ("", "Result"),
        10700, 2950, "Flat normal and zero roughness when Surface Water is compiled out."
    )
    selected_surface_pack = c.static_switch_parameter(
        mf, "DWC_UseSurfaceWater", False,
        enabled_surface_pack, ("", "Result"),
        disabled_surface_pack, ("", "Result"),
        11300, 2400,
        group="DWC Surface Water",
        description="Compile Surface Water RT, coverage, detail-normal, and roughness work only for slots that use it.",
    )
    selected_surface_decl = c.named_declaration(
        mf, "SURFACE_SelectedPack", selected_surface_pack, ("", "Result"), 12100, 2400
    )
    selected_surface_use1 = c.named_usage(mf, selected_surface_decl, 12600, 2050)
    selected_surface_use2 = c.named_usage(mf, selected_surface_decl, 12600, 2850)
    selected_surface_normal = c.custom_expression(
        mf, "return Packed.rgb;",
        [("Packed", selected_surface_use1, ("", "Result"))],
        "float3", 13300, 2050, "Decode selected Surface Water tangent normal."
    )
    selected_surface_roughness = c.custom_expression(
        mf, "return Packed.a;",
        [("Packed", selected_surface_use2, ("", "Result"))],
        "float1", 13300, 2850, "Decode selected Surface Water roughness weight."
    )
    surface_normal_decl = c.named_declaration(
        mf, "SURFACE_Normal", selected_surface_normal, ("", "Result"), 14200, 2050
    )
    surface_roughness_decl = c.named_declaration(
        mf, "SURFACE_RoughnessWeight", selected_surface_roughness, ("", "Result"), 14200, 2850
    )

    # 4-1 Final Base Color.
    absorbed_base_use2 = c.named_usage(mf, absorbed_base_decl, 13400, -4000)
    out_base_decl = c.named_declaration(
        mf, "OUT_BaseColor", absorbed_base_use2, ("", "Result"), 15500, -4000
    )

    # 4-2 Final Roughness.
    base_rough_use = c.named_usage(mf, declarations["BaseRoughness"], 17700, -4900)
    wet_rough_use = c.named_usage(mf, declarations["WetRoughness"], 17700, -4250)
    absorbed_wet_use2 = c.named_usage(mf, absorbed_wetness_decl, 17700, -3600)
    absorbed_gloss_use = c.named_usage(mf, profile_declarations["AbsorbedGlossinessStrength"], 17700, -2950)
    surface_target_rough_use = c.named_usage(mf, declarations["SurfaceWaterTargetRoughness"], 18500, -2300)
    surface_rough_weight_use = c.named_usage(mf, surface_roughness_decl, 18500, -1750)
    final_roughness = c.custom_expression(
        mf,
        """
float AbsorbedRoughness = lerp(
    BaseRoughness,
    WetRoughness,
    saturate(Wetness * AbsorbedGlossinessStrength));
float SafeSurfaceTarget = saturate(SurfaceWaterTargetRoughness);
return lerp(AbsorbedRoughness, SafeSurfaceTarget, saturate(SurfaceRoughnessWeight));
""",
        [
            ("BaseRoughness", base_rough_use, ("", "Result")),
            ("WetRoughness", wet_rough_use, ("", "Result")),
            ("Wetness", absorbed_wet_use2, ("", "Result")),
            ("AbsorbedGlossinessStrength", absorbed_gloss_use, ("", "Result")),
            ("SurfaceWaterTargetRoughness", surface_target_rough_use, ("", "Result")),
            ("SurfaceRoughnessWeight", surface_rough_weight_use, ("", "Result")),
        ],
        "float1", 19400, -3300,
        "Reference-style Surface Water roughness: absorbed wetness first, then visible masked Surface Water drives roughness toward the glossy target."
    )
    out_roughness_decl = c.named_declaration(
        mf, "OUT_Roughness", final_roughness, ("", "Result"), 20700, -3600
    )

    # 4-3 Final Normal.
    normal_inputs = [
        ("BaseNormal", declarations["BaseNormal"]),
        ("WrinkleNormal", wrinkle_normal_decl),
        ("WrinkleWeight", wrinkle_weight_decl),
        ("SurfaceNormal", surface_normal_decl),
        ("SurfaceCoverage", surface_coverage_decl),
        ("FlatNormal", shared_flat),
    ]
    normal_usages = []
    for i, (name, declaration) in enumerate(normal_inputs):
        x = 13400 + (i % 2) * 1400
        y = -900 + (i // 2) * 750
        normal_usages.append((name, c.named_usage(mf, declaration, x, y), ("", "Result")))
    final_normal = c.custom_expression(
        mf,
        """
float3 W = normalize(lerp(FlatNormal, WrinkleNormal, saturate(WrinkleWeight)));
float3 S = normalize(SurfaceNormal);
float SurfaceWeight = saturate(SurfaceCoverage);
float3 Result = normalize(BaseNormal);

if (saturate(WrinkleWeight) > 0.0001)
{
    float3 WrinkleBlended;
    WrinkleBlended.xy = Result.xy * W.z + W.xy * Result.z;
    WrinkleBlended.z = Result.z * W.z - dot(Result.xy, W.xy);
    Result = normalize(WrinkleBlended);
}

if (SurfaceWeight > 0.001)
{
    float3 SurfaceBlended;
    SurfaceBlended.xy = Result.xy * S.z + S.xy * Result.z;
    SurfaceBlended.z = Result.z * S.z - dot(Result.xy, S.xy);
    Result = normalize(SurfaceBlended);
}
return Result;
""",
        normal_usages,
        "float3", 16300, 300, "Surface-safe angle-correct normal blend: Base + Wrinkle, flatten cloth detail under visible water, then Surface Water."
    )
    out_normal_decl = c.named_declaration(
        mf, "OUT_Normal", final_normal, ("", "Result"), 17500, 300
    )

    # 4-4 Function Outputs.
    coverage_zero = c.scalar_constant(mf, 0.0, 17800, 2700, "Disabled Surface Water coverage")
    droplet_coverage_use_out = c.named_usage(mf, droplet_coverage_decl, 17800, 1700)
    rivulet_coverage_use_out = c.named_usage(mf, rivulet_coverage_decl, 17800, 2200)
    selected_droplet_coverage = c.static_switch_parameter(
        mf, "DWC_UseSurfaceWater", False,
        droplet_coverage_use_out, ("", "Result"), coverage_zero, ("", "Result"),
        18400, 1700, group="DWC Surface Water",
        description="Output zero coverage when the Surface Water branch is compiled out.",
    )
    selected_rivulet_coverage = c.static_switch_parameter(
        mf, "DWC_UseSurfaceWater", False,
        rivulet_coverage_use_out, ("", "Result"), coverage_zero, ("", "Result"),
        18400, 2300, group="DWC Surface Water",
        description="Output zero coverage when the Surface Water branch is compiled out.",
    )
    out_droplet_coverage_decl = c.named_declaration(
        mf, "OUT_DropletCoverage", selected_droplet_coverage, ("", "Result"), 19000, 1700
    )
    out_rivulet_coverage_decl = c.named_declaration(
        mf, "OUT_RivuletCoverage", selected_rivulet_coverage, ("", "Result"), 19000, 2300
    )

    final_outputs = [
        ("BaseColor", out_base_decl, "Final DWC Base Color."),
        ("Roughness", out_roughness_decl, "Final DWC Roughness."),
        ("Normal", out_normal_decl, "Final DWC tangent-space Normal."),
        ("DropletCoverage", out_droplet_coverage_decl, "Visible Droplet coverage for debug visualization."),
        ("RivuletCoverage", out_rivulet_coverage_decl, "Visible Rivulet coverage for debug visualization."),
    ]
    for i, (name, declaration, desc) in enumerate(final_outputs):
        usage = c.named_usage(mf, declaration, 19000, -650 + i * 1100)
        c.function_output(mf, name, usage, ("", "Result"), i, 20700, -650 + i * 1100, desc)

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
