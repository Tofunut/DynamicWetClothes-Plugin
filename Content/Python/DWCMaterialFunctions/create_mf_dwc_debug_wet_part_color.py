"""Create or recreate MF_DWC_DebugWetPartColor with a compact debug overlay graph."""
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

    c.create_comment(mf, "1. Inputs", -4200, -1900, 2300, 3200, parent=True)
    c.create_comment(mf, "1-1. Material and Wet Part Data", -3900, -1500, 1700, 2500)
    c.create_comment(mf, "2. Debug Color", -1450, -1900, 2600, 3200, parent=True)
    c.create_comment(mf, "2-1. Decode Wet Part Color", -1150, -1500, 1900, 1100)
    c.create_comment(mf, "2-2. Debug Blend Weight", -1150, -150, 1900, 1100)
    c.create_comment(mf, "3. Outputs", 1450, -1900, 2300, 3200, parent=True)
    c.create_comment(mf, "3-1. Final Debug Overlay", 1750, -1500, 1500, 2500)

    declarations: dict[str, object] = {}
    specs = [
        ("BaseColor", "vector3", (1.0, 1.0, 1.0), "Base color before wet-part debug overlay."),
        ("VertexColorRGB", "vector3", (0.0, 0.0, 0.0), "Vertex color RGB. G/B encode wet-part debug color R/G."),
        ("VertexColorAlpha", "scalar", (0.0,), "Vertex color alpha. Encodes wet-part debug color B."),
        ("WetnessMask", "scalar", (0.0,), "CPU vertex wetness or GPU wetness-map amount used to gate the overlay."),
        ("DebugStrength", "scalar", (0.0,), "Runtime DWC_WetPartDebugStrength parameter."),
    ]
    for i, (name, kind, preview, desc) in enumerate(specs):
        node = c.function_input(mf, name, kind, preview, i, -3700, -1150 + i * 520, desc)
        declarations[name] = c.named_declaration(mf, f"IN_{name}", node, ("", "Result"), -2550, -1150 + i * 520)

    rgb_use = c.named_usage(mf, declarations["VertexColorRGB"], -1000, -1150)
    alpha_use = c.named_usage(mf, declarations["VertexColorAlpha"], -1000, -650)
    debug_color = c.custom_expression(
        mf,
        "return saturate(float3(VertexColorRGB.g, VertexColorRGB.b, VertexColorAlpha));",
        [
            ("VertexColorRGB", rgb_use, ("", "Result")),
            ("VertexColorAlpha", alpha_use, ("", "Result")),
        ],
        "float3",
        -300,
        -950,
        "Decode wet-part debug color from the packed vertex color channels.",
    )
    debug_color_decl = c.named_declaration(
        mf, "DEBUG_WetPartColor", debug_color, ("", "Result"), 550, -950
    )

    wetness_use = c.named_usage(mf, declarations["WetnessMask"], -1000, 250)
    strength_use = c.named_usage(mf, declarations["DebugStrength"], -1000, 750)
    debug_alpha = c.custom_expression(
        mf,
        "return saturate(WetnessMask * DebugStrength);",
        [
            ("WetnessMask", wetness_use, ("", "Result")),
            ("DebugStrength", strength_use, ("", "Result")),
        ],
        "float1",
        -300,
        500,
        "Compute the wet-part debug overlay alpha.",
    )
    debug_alpha_decl = c.named_declaration(
        mf, "DEBUG_WetPartAlpha", debug_alpha, ("", "Result"), 550, 500
    )

    base_use = c.named_usage(mf, declarations["BaseColor"], 1800, -1000)
    color_use = c.named_usage(mf, debug_color_decl, 1800, -350)
    alpha_use2 = c.named_usage(mf, debug_alpha_decl, 1800, 300)
    final_color = c.lerp(
        mf,
        base_use, ("", "Result"),
        color_use, ("", "Result"),
        alpha_use2, ("", "Result"),
        2500,
        -350,
        "Blend wet-part debug color over the evaluated base color.",
    )
    final_decl = c.named_declaration(
        mf, "OUT_BaseColor", final_color, ("", "Result"), 3200, -350
    )

    outputs = [
        ("BaseColor", final_decl, "Wet-part debug overlay result."),
        ("DebugColor", debug_color_decl, "Decoded wet-part debug color."),
        ("DebugAlpha", debug_alpha_decl, "Final wet-part debug blend alpha."),
    ]
    for i, (name, declaration, desc) in enumerate(outputs):
        usage = c.named_usage(mf, declaration, 4050, -900 + i * 520)
        c.function_output(mf, name, usage, ("", "Result"), i, 4850, -900 + i * 520, desc)

    c.finalize_material_function(mf)


if __name__ == "__main__":
    c.run_entry(build)
