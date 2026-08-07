# Copyright 2026 Team Tofunut. All Rights Reserved.
"""Run every DWC material-function authoring script in dependency order.

Execute this inside Unreal Python, for example:

    UnrealEditor-Cmd.exe Project.uproject -run=pythonscript -script=.../run_all_dwc_material_function_scripts.py

or from the Unreal Python console:

    exec(open(r".../run_all_dwc_material_function_scripts.py", encoding="utf-8").read())
"""
from __future__ import annotations

import os
import runpy
import stat
import sys

import unreal


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PLUGIN_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))

if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)


MATERIAL_FUNCTION_SCRIPTS = (
    (
        "MF_DWC_GetRenderProfile",
        "create_mf_dwc_get_render_profile.py",
        os.path.join("Content", "Materials", "Functions", "MF_DWC_GetRenderProfile.uasset"),
    ),
    (
        "MF_DWC_SampleSurfaceWaterNormals",
        "create_mf_dwc_sample_surface_water_normals.py",
        os.path.join("Content", "Materials", "Functions", "MF_DWC_SampleSurfaceWaterNormals.uasset"),
    ),
    (
        "MF_DWC_EvaluateSurfaceAppearance",
        "create_mf_dwc_evaluate_surface_appearance.py",
        os.path.join("Content", "Materials", "Functions", "MF_DWC_EvaluateSurfaceAppearance.uasset"),
    ),
    (
        "MF_DWC_DebugWetPartColor",
        "create_mf_dwc_debug_wet_part_color.py",
        os.path.join("Content", "Materials", "Functions", "MF_DWC_DebugWetPartColor.uasset"),
    ),
)


def _log(message: str) -> None:
    unreal.log(f"[DWC MF Runner] {message}")


def _clear_read_only(path: str) -> None:
    if not os.path.exists(path):
        return

    current_mode = os.stat(path).st_mode
    os.chmod(path, current_mode | stat.S_IWRITE)


def main() -> None:
    _log(f"Script directory: {SCRIPT_DIR}")
    _log(f"Plugin root: {PLUGIN_ROOT}")

    for display_name, script_name, relative_asset_path in MATERIAL_FUNCTION_SCRIPTS:
        script_path = os.path.join(SCRIPT_DIR, script_name)
        asset_path = os.path.join(PLUGIN_ROOT, relative_asset_path)

        if not os.path.isfile(script_path):
            raise FileNotFoundError(f"Missing DWC material-function script: {script_path}")

        _clear_read_only(asset_path)

        _log(f"Running {display_name}")
        runpy.run_path(script_path, run_name="__main__")

    _log("All material function scripts completed successfully.")


if __name__ == "__main__":
    main()
