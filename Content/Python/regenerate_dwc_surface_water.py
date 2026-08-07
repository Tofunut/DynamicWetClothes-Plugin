"""Regenerate DWC surface-water functions and repair every generated WCA material.

Run this inside Unreal Editor. In the Output Log, switch the command executor
to Python and execute this file, or run:

    exec(open(r"<absolute path to this file>", encoding="utf-8").read())
"""
from __future__ import annotations

import os
import runpy
import stat
import sys

import unreal


try:
    SCRIPT_FILE = os.path.abspath(__file__)
except NameError:
    # Unreal Output Log's exec(open(...).read()) does not define __file__.
    SCRIPT_FILE = os.path.join(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_plugins_dir()),
        "DynamicWetClothes",
        "Content",
        "Python",
        "regenerate_dwc_surface_water.py",
    )

SCRIPT_DIR = os.path.dirname(SCRIPT_FILE)
PLUGIN_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
FUNCTION_SCRIPT_DIR = os.path.join(SCRIPT_DIR, "DWCMaterialFunctions")
FUNCTIONS = (
    (
        "create_mf_dwc_sample_surface_water_normals",
        "/DynamicWetClothes/Materials/Functions/MF_DWC_SampleSurfaceWaterNormals",
        os.path.join(
            PLUGIN_ROOT,
            "Content",
            "Materials",
            "Functions",
            "MF_DWC_SampleSurfaceWaterNormals.uasset",
        ),
    ),
    (
        "create_mf_dwc_evaluate_surface_appearance",
        "/DynamicWetClothes/Materials/Functions/MF_DWC_EvaluateSurfaceAppearance",
        os.path.join(
            PLUGIN_ROOT,
            "Content",
            "Materials",
            "Functions",
            "MF_DWC_EvaluateSurfaceAppearance.uasset",
        ),
    ),
)


def log(message: str) -> None:
    unreal.log(f"[DWC SURFACE WATER REGENERATE] {message}")


def unpack_result(value: object) -> tuple[bool, str]:
    if isinstance(value, tuple):
        succeeded = next((item for item in value if isinstance(item, bool)), False)
        report = next((item for item in value if isinstance(item, str)), "")
        return succeeded, report
    return bool(value), ""


def clear_read_only(filename: str) -> None:
    if not os.path.isfile(filename):
        return
    os.chmod(filename, os.stat(filename).st_mode | stat.S_IWRITE)


def close_asset_editor(asset_path: str) -> None:
    subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
    if subsystem is None:
        return
    asset = unreal.load_asset(asset_path)
    if asset is not None:
        subsystem.close_all_editors_for_asset(asset)


def close_generated_asset_editors(generated_root: str) -> None:
    subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
    if subsystem is None:
        return
    for object_path in unreal.EditorAssetLibrary.list_assets(
        generated_root, recursive=True, include_folder=False
    ):
        asset = unreal.load_asset(object_path)
        if asset is not None:
            subsystem.close_all_editors_for_asset(asset)


def regenerate_functions() -> None:
    if FUNCTION_SCRIPT_DIR not in sys.path:
        sys.path.insert(0, FUNCTION_SCRIPT_DIR)

    # Do not reuse a same-named helper imported from another DWC plugin copy.
    existing_common = sys.modules.get("dwc_mf_common")
    existing_common_file = getattr(existing_common, "__file__", "")
    if (
        existing_common_file
        and os.path.dirname(os.path.abspath(existing_common_file)) != FUNCTION_SCRIPT_DIR
    ):
        del sys.modules["dwc_mf_common"]

    # Close both dependent function editors before deleting either function.
    for _, asset_path, asset_filename in FUNCTIONS:
        close_asset_editor(asset_path)
        clear_read_only(asset_filename)

    for module_name, _, _ in FUNCTIONS:
        script_path = os.path.join(FUNCTION_SCRIPT_DIR, f"{module_name}.py")
        log(f"Running {script_path}")
        namespace = runpy.run_path(script_path, run_name=f"_dwc_{module_name}")
        namespace["c"].run_entry(namespace["build"])


def repair_generated_wca_materials() -> None:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(["/Game", "/DynamicWetClothes"], True)
    class_path = unreal.TopLevelAssetPath("/Script/DWC", "WetClothingAsset")
    asset_data_items = sorted(
        registry.get_assets_by_class(class_path, True),
        key=lambda item: str(item.package_name),
    )

    repaired_count = 0
    failures: list[str] = []
    for asset_data in asset_data_items:
        package_path = str(asset_data.package_name)
        asset_name = str(asset_data.asset_name)
        parent_path = package_path.rsplit("/", 1)[0]
        generated_root = f"{parent_path}/DWCGenerated/{asset_name}"
        if not unreal.EditorAssetLibrary.does_directory_exist(generated_root):
            continue

        close_generated_asset_editors(generated_root)
        wet_clothing_asset = asset_data.get_asset()
        if wet_clothing_asset is None:
            failures.append(f"Could not load {package_path}.")
            continue

        succeeded, report = unpack_result(
            unreal.DWCMaterialSetupEditorLibrary.repair_generated_wet_materials(
                wet_clothing_asset
            )
        )
        log(f"{asset_name}: {report or 'No repair report.'}")
        if not succeeded:
            failures.append(f"Could not repair {asset_name}: {report}")
            continue

        if not unreal.EditorAssetLibrary.save_loaded_asset(
            wet_clothing_asset, only_if_is_dirty=False
        ):
            failures.append(f"Could not save {package_path}.")
            continue
        if not unreal.EditorAssetLibrary.save_directory(
            generated_root, only_if_is_dirty=False, recursive=True
        ):
            failures.append(f"Could not save generated assets under {generated_root}.")
            continue

        repaired_count += 1

    log(f"Repaired {repaired_count} generated WCA asset(s).")
    if failures:
        raise RuntimeError(
            "One or more generated WCA material sets failed:\n" + "\n".join(failures)
        )


def main() -> None:
    log(f"Plugin root: {PLUGIN_ROOT}")
    regenerate_functions()
    repair_generated_wca_materials()
    log("SUCCESS")


main()
