# Copyright 2026 Team Tofunut. All Rights Reserved.
"""Rebuild every DWC material function, then repair all generated wet materials.

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


def _unpack_repair_result(result: object) -> tuple[bool, str]:
    if isinstance(result, tuple):
        succeeded = next((value for value in result if isinstance(value, bool)), False)
        report = next((value for value in result if isinstance(value, str)), "")
        return succeeded, report
    return bool(result), ""


def _repair_all_generated_wet_materials() -> None:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    class_path = unreal.TopLevelAssetPath("/Script/DWC", "WetClothingAsset")
    asset_data_items = registry.get_assets_by_class(class_path, True)
    asset_data_items = sorted(asset_data_items, key=lambda item: str(item.package_name))

    repaired_assets = 0
    failures: list[str] = []
    for asset_data in asset_data_items:
        wet_clothing_asset = asset_data.get_asset()
        if wet_clothing_asset is None:
            failures.append(f"Could not load {asset_data.package_name}.")
            continue

        result = unreal.DWCMaterialSetupEditorLibrary.repair_generated_wet_materials(
            wet_clothing_asset
        )
        succeeded, report = _unpack_repair_result(result)
        report_lines = [line.strip() for line in report.splitlines() if line.strip()]
        failed_lines = [
            line for line in report_lines
            if " failed:" in line.lower() or " skipped:" in line.lower()
        ]

        if succeeded:
            if not unreal.EditorAssetLibrary.save_loaded_asset(
                wet_clothing_asset, only_if_is_dirty=False
            ):
                failures.append(f"Could not save repaired asset {asset_data.package_name}.")
                continue
            repaired_assets += 1
            _log(f"Repaired generated materials for {asset_data.package_name}")

        if failed_lines or (not succeeded and report_lines):
            failures.append(
                f"{asset_data.package_name}: " + " | ".join(failed_lines or report_lines)
            )

    _log(
        f"Generated-material repair completed: {repaired_assets} Wet Clothing Asset(s) repaired."
    )
    if failures:
        raise RuntimeError(
            "One or more generated DWC material sets could not be fully repaired:\n"
            + "\n".join(failures)
        )


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

    _repair_all_generated_wet_materials()
    _log("All material functions and generated wet materials completed successfully.")


if __name__ == "__main__":
    main()
