"""Create or recreate MF_DWC_DebugWetPartColor with droplet-only Surface Water debug."""
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

    c.create_comment(mf, "1. Inputs", -6200, -3600, 3400, 6800, parent=True)
    c.create_comment(mf, "2. Wet Part Debug", -2400, -3000, 3000, 2500, parent=True)
    c.create_comment(mf, "3. Droplet Debug", -2400, 0, 3600, 2500, parent=True)
    c.create_comment(mf, "4. Final Output", 1800, -3000, 3000, 5600, parent=True)

    specs = [
        ("BaseColor", "vector3", (1.0, 1.0, 1.0), "Evaluated base color before debug overlays."),
        ("VertexColorRGB", "vector3", (0.0, 0.0, 0.0), "Packed Wet Part debug color channels."),
        ("VertexColorAlpha", "scalar", (0.0,), "Packed Wet Part debug blue channel."),
        ("WetnessMask", "scalar", (0.0,), "Resolved absorbed-wetness amount."),
        ("WetPartDebugStrength", "scalar", (0.0,), "Runtime Wet Part debug strength."),
        ("DropletAmount", "scalar", (0.0,), "Raw droplet RT amount from the Appearance MF."),
        ("DropletBrush", "scalar", (0.0,), "Visual mask-shaped droplet brush from the Appearance MF."),
        ("DropletLifetimeFade", "scalar", (0.0,), "Raw droplet lifetime fade from the Appearance MF."),
        ("SurfaceWaterDebugStrength", "scalar", (0.0,), "Runtime Surface Water debug strength."),
        ("DropletDebugColor", "vector3", (1.0, 0.85, 0.0), "Droplet debug color."),
    ]
    declarations: dict[str, object] = {}
    for i, (name, kind, preview, desc) in enumerate(specs):
        node = c.function_input(mf, name, kind, preview, i, -5850, -2500 + i * 580, desc)
        declarations[name] = c.named_declaration(
            mf, f"IN_{name}", node, ("", "Result"), -4550, -2500 + i * 580
        )

    wet_names = ("BaseColor", "VertexColorRGB", "VertexColorAlpha", "WetnessMask", "WetPartDebugStrength")
    wet_inputs = [
        (name, c.named_usage(mf, declarations[name], -2000 + (i % 2) * 750, -2450 + (i // 2) * 650), ("", "Result"))
        for i, name in enumerate(wet_names)
    ]
    wet_result = c.custom_expression(
        mf,
        """
float3 WetPartColor = saturate(float3(VertexColorRGB.g, VertexColorRGB.b, VertexColorAlpha));
float WetPartAlpha = saturate(WetnessMask * WetPartDebugStrength);
return lerp(BaseColor, WetPartColor, WetPartAlpha);
""",
        wet_inputs,
        "float3", -650, -1500,
        "Apply the authored Wet Part color overlay, gated by absorbed wetness.",
    )
    wet_decl = c.named_declaration(mf, "DEBUG_WetPartResult", wet_result, ("", "Result"), 250, -1500)

    droplet_inputs = []
    for i, name in enumerate(("DropletAmount", "DropletBrush", "DropletLifetimeFade", "SurfaceWaterDebugStrength", "DropletDebugColor")):
        droplet_inputs.append(
            (name, c.named_usage(mf, declarations[name], -2200 + (i % 3) * 800, 450 + (i // 3) * 550), ("", "Result"))
        )
    droplet_color = c.custom_expression(
        mf,
        "return DropletDebugColor;",
        droplet_inputs,
        "float3", -300, 850,
        "Use the configured droplet debug color.",
    )
    alpha_inputs = []
    for i, name in enumerate(("DropletAmount", "DropletBrush", "DropletLifetimeFade", "SurfaceWaterDebugStrength")):
        alpha_inputs.append(
            (name, c.named_usage(mf, declarations[name], -2200 + i * 700, 1550), ("", "Result"))
        )
    droplet_alpha = c.custom_expression(
        mf,
        """
float HasAmount = DropletAmount > 1.0e-4 ? 1.0 : 0.0;
return saturate(DropletBrush * HasAmount * DropletLifetimeFade * SurfaceWaterDebugStrength);
""",
        alpha_inputs,
        "float1", 150, 1550,
        "Compute droplet debug alpha from raw droplet state.",
    )
    color_decl = c.named_declaration(mf, "DEBUG_SurfaceColor", droplet_color, ("", "Result"), 850, 850)
    alpha_decl = c.named_declaration(mf, "DEBUG_SurfaceAlpha", droplet_alpha, ("", "Result"), 850, 1550)

    wet_use = c.named_usage(mf, wet_decl, 2200, -650)
    color_use = c.named_usage(mf, color_decl, 2200, 50)
    alpha_use = c.named_usage(mf, alpha_decl, 2200, 750)
    final_color = c.lerp(
        mf, wet_use, ("", "Result"), color_use, ("", "Result"), alpha_use, ("", "Result"),
        3100, 0, "Place the droplet debug color above the Wet Part overlay.",
    )
    final_decl = c.named_declaration(mf, "OUT_BaseColor", final_color, ("", "Result"), 3900, 0)

    final_use = c.named_usage(mf, final_decl, 5000, -650)
    color_out = c.named_usage(mf, color_decl, 5000, 50)
    alpha_out = c.named_usage(mf, alpha_decl, 5000, 750)
    c.function_output(mf, "BaseColor", final_use, ("", "Result"), 0, 5900, -650, "Final debug-overlaid Base Color.")
    c.function_output(mf, "DebugColor", color_out, ("", "Result"), 1, 5900, 50, "Droplet debug color.")
    c.function_output(mf, "DebugAlpha", alpha_out, ("", "Result"), 2, 5900, 750, "Droplet debug alpha.")

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
