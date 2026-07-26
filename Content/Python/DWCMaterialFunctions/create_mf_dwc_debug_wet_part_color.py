"""Create or recreate MF_DWC_DebugWetPartColor with Wet Part and Surface Water overlays."""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import dwc_mf_common as c

OVERWRITE_EXISTING = True
ASSET_NAME = "MF_DWC_DebugWetPartColor"


def build() -> None:
    mf = c.create_or_replace_material_function(ASSET_NAME, OVERWRITE_EXISTING)

    c.create_comment(mf, "1. Inputs", -6200, -3300, 3400, 6200, parent=True)
    c.create_comment(mf, "2. Wet Part Debug", -2400, -3300, 3000, 2700, parent=True)
    c.create_comment(mf, "3. Surface Water Debug", -2400, 0, 4200, 2900, parent=True)
    c.create_comment(mf, "4. Final Output", 2300, -3300, 3000, 6200, parent=True)

    specs = [
        ("BaseColor", "vector3", (1.0, 1.0, 1.0), "Evaluated base color before debug overlays."),
        ("VertexColorRGB", "vector3", (0.0, 0.0, 0.0), "Packed Wet Part debug color channels."),
        ("VertexColorAlpha", "scalar", (0.0,), "Packed Wet Part debug blue channel."),
        ("WetnessMask", "scalar", (0.0,), "Resolved absorbed-wetness amount."),
        ("WetPartDebugStrength", "scalar", (0.0,), "Runtime Wet Part debug strength."),
        ("DropletCoverage", "scalar", (0.0,), "Visible Droplet coverage from the Appearance MF."),
        ("RivuletCoverage", "scalar", (0.0,), "Visible Rivulet coverage from the Appearance MF."),
        ("SurfaceWaterDebugStrength", "scalar", (0.0,), "Runtime Surface Water debug strength."),
        ("DropletDebugColor", "vector3", (1.0, 0.85, 0.0), "Droplet debug color."),
        ("RivuletDebugColor", "vector3", (0.72, 0.45, 1.0), "Rivulet debug color."),
    ]
    declarations: dict[str, object] = {}
    for i, (name, kind, preview, desc) in enumerate(specs):
        node = c.function_input(mf, name, kind, preview, i, -5850, -2750 + i * 520, desc)
        declarations[name] = c.named_declaration(
            mf, f"IN_{name}", node, ("", "Result"), -4550, -2750 + i * 520
        )

    wet_inputs = []
    for i, name in enumerate(("BaseColor", "VertexColorRGB", "VertexColorAlpha", "WetnessMask", "WetPartDebugStrength")):
        wet_inputs.append((name, c.named_usage(mf, declarations[name], -2000 + (i % 2) * 750, -2700 + (i // 2) * 650), ("", "Result")))
    wet_result = c.custom_expression(
        mf,
        """
float3 WetPartColor = saturate(float3(VertexColorRGB.g, VertexColorRGB.b, VertexColorAlpha));
float WetPartAlpha = saturate(WetnessMask * WetPartDebugStrength);
return lerp(BaseColor, WetPartColor, WetPartAlpha);
""",
        wet_inputs,
        "float3", -650, -1800,
        "Apply the authored Wet Part color overlay, gated by absorbed wetness.",
    )
    wet_result_decl = c.named_declaration(mf, "DEBUG_WetPartResult", wet_result, ("", "Result"), 250, -1800)

    surface_names = (
        "DropletCoverage", "RivuletCoverage", "SurfaceWaterDebugStrength",
        "DropletDebugColor", "RivuletDebugColor",
    )
    surface_inputs = []
    for i, name in enumerate(surface_names):
        surface_inputs.append((name, c.named_usage(mf, declarations[name], -2000 + (i % 2) * 900, 350 + (i // 2) * 650), ("", "Result")))
    surface_color = c.custom_expression(
        mf,
        """
float D = saturate(DropletCoverage * SurfaceWaterDebugStrength);
float R = saturate(RivuletCoverage * SurfaceWaterDebugStrength);
return D >= R ? DropletDebugColor : RivuletDebugColor;
""",
        surface_inputs,
        "float3", -400, 850,
        "Choose the dominant visible Surface Water type for an unambiguous debug color.",
    )
    surface_alpha_inputs = []
    for i, name in enumerate(("DropletCoverage", "RivuletCoverage", "SurfaceWaterDebugStrength")):
        surface_alpha_inputs.append((name, c.named_usage(mf, declarations[name], -1900 + i * 750, 2000), ("", "Result")))
    surface_alpha = c.custom_expression(
        mf,
        "return saturate(max(DropletCoverage, RivuletCoverage) * SurfaceWaterDebugStrength);",
        surface_alpha_inputs,
        "float1", 600, 1950,
        "Compute the final Surface Water debug overlay alpha.",
    )
    surface_color_decl = c.named_declaration(mf, "DEBUG_SurfaceColor", surface_color, ("", "Result"), 1100, 850)
    surface_alpha_decl = c.named_declaration(mf, "DEBUG_SurfaceAlpha", surface_alpha, ("", "Result"), 1100, 1950)

    wet_use = c.named_usage(mf, wet_result_decl, 2700, -800)
    surface_color_use = c.named_usage(mf, surface_color_decl, 2700, 0)
    surface_alpha_use = c.named_usage(mf, surface_alpha_decl, 2700, 800)
    final_color = c.lerp(
        mf, wet_use, ("", "Result"), surface_color_use, ("", "Result"), surface_alpha_use, ("", "Result"),
        3600, 0, "Place Surface Water debug colors above the Wet Part overlay.",
    )
    final_decl = c.named_declaration(mf, "OUT_BaseColor", final_color, ("", "Result"), 4400, 0)

    final_use = c.named_usage(mf, final_decl, 5650, -700)
    color_use = c.named_usage(mf, surface_color_decl, 5650, 0)
    alpha_use = c.named_usage(mf, surface_alpha_decl, 5650, 700)
    c.function_output(mf, "BaseColor", final_use, ("", "Result"), 0, 6500, -700, "Final debug-overlaid Base Color.")
    c.function_output(mf, "DebugColor", color_use, ("", "Result"), 1, 6500, 0, "Dominant Surface Water debug color.")
    c.function_output(mf, "DebugAlpha", alpha_use, ("", "Result"), 2, 6500, 700, "Surface Water debug alpha.")

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
