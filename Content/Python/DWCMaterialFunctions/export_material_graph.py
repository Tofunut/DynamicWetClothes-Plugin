import json
import os
import re
import unreal


# ============================================================
# 사용자 설정
# ============================================================

# Content Browser에서 에셋 우클릭 → Copy Reference로 얻은 경로를 사용한다.
#
# 예:
# /Game/Materials/M_Test
# /DynamicWetClothes/Materials/MF_DWC_Appearance
#
# Copy Reference 전체를 그대로 넣어도 된다.
# MaterialFunction'/DynamicWetClothes/Materials/MF_DWC_Appearance.MF_DWC_Appearance'
ASSET_PATH = "/DynamicWetClothes/Materials/Functions/Backups/MF_DWC_ApplyWetness_CPU"

# 결과는 기본적으로 프로젝트의 Saved/MaterialGraphJSON에 저장된다.
OUTPUT_DIRECTORY = os.path.join(
    unreal.Paths.project_saved_dir(),
    "MaterialGraphJSON"
)

# 연결 정보를 더 잘 읽기 위해 에셋 에디터를 자동으로 열 것인지 설정한다.
OPEN_ASSET_EDITOR = False


# ============================================================
# 유틸리티
# ============================================================

def log(message):
    unreal.log("[MaterialGraphExporter] {}".format(message))


def log_warning(message):
    unreal.log_warning("[MaterialGraphExporter] {}".format(message))


def log_error(message):
    unreal.log_error("[MaterialGraphExporter] {}".format(message))


def safe_get_editor_property(obj, property_name):
    """존재하는 Editor Property만 안전하게 읽는다."""
    if obj is None:
        return None

    try:
        return obj.get_editor_property(property_name)
    except Exception:
        return None


def object_reference(obj):
    """Unreal UObject를 JSON용 경로 참조로 변환한다."""
    if obj is None:
        return None

    try:
        return {
            "name": obj.get_name(),
            "class": obj.get_class().get_name(),
            "path": obj.get_path_name(),
        }
    except Exception:
        return str(obj)


def serialize_value(value, depth=0):
    """Unreal 값을 JSON 직렬화 가능한 값으로 바꾼다."""
    if value is None:
        return None

    if isinstance(value, (bool, int, float, str)):
        return value

    if depth >= 3:
        return str(value)

    # Name, Text, Enum 등은 문자열로 변환 가능하다.
    value_type_name = type(value).__name__

    if value_type_name in {
        "Name",
        "Text",
    }:
        return str(value)

    # UObject
    try:
        if isinstance(value, unreal.Object):
            return object_reference(value)
    except Exception:
        pass

    if isinstance(value, (list, tuple)):
        return [
            serialize_value(item, depth + 1)
            for item in value
        ]

    if isinstance(value, dict):
        return {
            str(key): serialize_value(item, depth + 1)
            for key, item in value.items()
        }

    # 자주 등장하는 Unreal 수학 구조체
    component_names = [
        ("x", "y", "z", "w"),
        ("x", "y", "z"),
        ("x", "y"),
        ("r", "g", "b", "a"),
    ]

    for components in component_names:
        if all(hasattr(value, component) for component in components):
            try:
                return {
                    component: serialize_value(
                        getattr(value, component),
                        depth + 1
                    )
                    for component in components
                }
            except Exception:
                pass

    # ExpressionInput 등의 Unreal 구조체
    struct_properties = [
        "expression",
        "output_index",
        "input_name",
        "mask",
        "mask_r",
        "mask_g",
        "mask_b",
        "mask_a",
    ]

    struct_result = {}

    for property_name in struct_properties:
        property_value = safe_get_editor_property(value, property_name)

        if property_value is not None:
            struct_result[property_name] = serialize_value(
                property_value,
                depth + 1
            )

    if struct_result:
        return struct_result

    return str(value)


def pin_name_to_property_candidates(pin_name):
    """
    머티리얼 핀 표시 이름을 Unreal Python 프로퍼티 이름 후보로 변환한다.

    예:
    Base Color -> base_color
    AGreaterThanB -> a_greater_than_b
    """
    if pin_name is None:
        return []

    pin_name = str(pin_name).strip()

    if not pin_name:
        return ["input"]

    snake_case = re.sub(
        r"(?<=[a-z0-9])(?=[A-Z])",
        "_",
        pin_name
    )

    snake_case = re.sub(
        r"[^a-zA-Z0-9]+",
        "_",
        snake_case
    )

    snake_case = snake_case.strip("_").lower()

    compact = re.sub(
        r"[^a-zA-Z0-9]+",
        "",
        pin_name
    ).lower()

    candidates = [
        snake_case,
        compact,
        pin_name.lower(),
    ]

    # 흔한 핀 이름에 대한 보조 후보
    aliases = {
        "true": ["a"],
        "false": ["b"],
        "input": ["input"],
        "coordinates": ["coordinates"],
        "textureobject": ["texture_object"],
    }

    candidates.extend(aliases.get(compact, []))

    # 중복 제거
    result = []

    for candidate in candidates:
        if candidate and candidate not in result:
            result.append(candidate)

    return result


def get_expression_input_struct(node, pin_name):
    """핀에 대응하는 FExpressionInput 프로퍼티를 찾는다."""
    for candidate in pin_name_to_property_candidates(pin_name):
        value = safe_get_editor_property(node, candidate)

        if value is None:
            continue

        source_expression = safe_get_editor_property(value, "expression")

        # 연결되지 않은 ExpressionInput도 반환한다.
        if (
            source_expression is not None
            or safe_get_editor_property(value, "output_index") is not None
        ):
            return value, candidate

    return None, None


def get_node_id(node):
    try:
        return node.get_name()
    except Exception:
        return str(node)


def get_node_position(node):
    """
    Material Editor Toolkit에 접근하지 않고
    UMaterialExpression 자체에 저장된 좌표를 읽는다.
    """
    return {
        "x": safe_get_editor_property(
            node,
            "material_expression_editor_x"
        ),
        "y": safe_get_editor_property(
            node,
            "material_expression_editor_y"
        ),
    }


def get_input_nodes(asset, node, is_function):
    """
    크래시 방지를 위해 활성 Material Editor에 의존하는 API를 사용하지 않는다.

    연결 정보는 각 노드의 FExpressionInput 프로퍼티를 통해 별도로 추출한다.
    """
    return []


def get_source_output_name(target_node, source_node):
    """
    get_input_node_output_name_for_material_expression()은
    활성 Material Editor 상태에 의존하므로 호출하지 않는다.
    """
    return None



# ============================================================
# 노드 추출
# ============================================================

COMMON_NODE_PROPERTIES = [
    "desc",
    "parameter_name",
    "group",
    "sort_priority",

    "default_value",
    "preview_value",
    "use_preview_value_as_default",

    "value",
    "const_a",
    "const_b",
    "const_alpha",

    "texture",
    "sampler_type",
    "mip_value_mode",

    "material_function",
    "function_inputs",
    "function_outputs",

    "input_name",
    "output_name",
    "input_type",
    "output_type",

    "size_x",
    "size_y",
    "text",

    "declaration",
    "variable_name",
]


def export_node(asset, node, is_function):
    node_id = get_node_id(node)

    try:
        node_class = node.get_class().get_name()
    except Exception:
        node_class = type(node).__name__

    try:
        input_names = list(
            unreal.MaterialEditingLibrary
            .get_material_expression_input_names(node)
        )
    except Exception:
        input_names = []

    try:
        input_types = list(
            unreal.MaterialEditingLibrary
            .get_material_expression_input_types(node)
        )
    except Exception:
        input_types = []

    try:
        output_names = list(
            unreal.MaterialEditingLibrary
            .get_material_expression_output_names(node)
        )
    except Exception:
        output_names = []

    properties = {}

    for property_name in COMMON_NODE_PROPERTIES:
        value = safe_get_editor_property(node, property_name)

        if value is not None:
            properties[property_name] = serialize_value(value)

    inputs = []

    for index, input_name in enumerate(input_names):
        input_type = (
            input_types[index]
            if index < len(input_types)
            else None
        )

        input_data = {
            "name": str(input_name),
            "type": input_type,
            "connection": None,
        }

        expression_input, property_name = get_expression_input_struct(
            node,
            input_name
        )

        if expression_input is not None:
            source_expression = safe_get_editor_property(
                expression_input,
                "expression"
            )

            output_index = safe_get_editor_property(
                expression_input,
                "output_index"
            )

            if source_expression is not None:
                try:
                    source_output_names = list(
                        unreal.MaterialEditingLibrary
                        .get_material_expression_output_names(
                            source_expression
                        )
                    )
                except Exception:
                    source_output_names = []

                source_output_name = None

                if (
                    isinstance(output_index, int)
                    and 0 <= output_index < len(source_output_names)
                ):
                    source_output_name = str(
                        source_output_names[output_index]
                    )

                if source_output_name is None:
                    source_output_name = get_source_output_name(
                        node,
                        source_expression
                    )

                input_data["connection"] = {
                    "source_node": get_node_id(source_expression),
                    "source_output": source_output_name,
                    "source_output_index": output_index,
                    "target_property": property_name,
                    "mask": {
                        "enabled": safe_get_editor_property(
                            expression_input,
                            "mask"
                        ),
                        "r": safe_get_editor_property(
                            expression_input,
                            "mask_r"
                        ),
                        "g": safe_get_editor_property(
                            expression_input,
                            "mask_g"
                        ),
                        "b": safe_get_editor_property(
                            expression_input,
                            "mask_b"
                        ),
                        "a": safe_get_editor_property(
                            expression_input,
                            "mask_a"
                        ),
                    },
                }

        inputs.append(input_data)

    upstream_nodes = get_input_nodes(
        asset,
        node,
        is_function
    )

    upstream_data = []

    for source_node in upstream_nodes:
        upstream_data.append({
            "source_node": get_node_id(source_node),
            "source_output": get_source_output_name(
                node,
                source_node
            ),
        })

    return {
        "id": node_id,
        "name": node_id,
        "class": node_class,
        "path": node.get_path_name(),
        "position": get_node_position(node),
        "input_pins": inputs,
        "output_pins": [
            {
                "name": str(name),
                "index": index,
            }
            for index, name in enumerate(output_names)
        ],
        "upstream_nodes": upstream_data,
        "properties": properties,
    }


# ============================================================
# 에셋 추출
# ============================================================

def load_target_asset(asset_path):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)

    if asset is None:
        raise RuntimeError(
            "에셋을 불러오지 못했습니다: {}".format(asset_path)
        )

    return asset


def identify_asset_type(asset):
    if isinstance(asset, unreal.Material):
        return "Material", False

    if isinstance(asset, unreal.MaterialFunction):
        return "MaterialFunction", True

    raise TypeError(
        "Material 또는 MaterialFunction만 지원합니다. 실제 클래스: {}".format(
            asset.get_class().get_name()
        )
    )


def get_expressions(asset, is_function):
    if is_function:
        return list(
            unreal.MaterialEditingLibrary
            .get_material_function_expressions(asset)
        )

    return list(
        unreal.MaterialEditingLibrary
        .get_material_expressions(asset)
    )


def open_asset_editor(asset):
    if not OPEN_ASSET_EDITOR:
        return

    try:
        subsystem = unreal.get_editor_subsystem(
            unreal.AssetEditorSubsystem
        )

        subsystem.open_editor_for_assets([asset])

    except Exception as error:
        log_warning(
            "에셋 에디터를 열지 못했습니다: {}".format(error)
        )


def build_export_data(asset):
    asset_type, is_function = identify_asset_type(asset)

    # open_asset_editor(asset)

    expressions = get_expressions(
        asset,
        is_function
    )

    log(
        "{}개의 Expression을 찾았습니다.".format(
            len(expressions)
        )
    )

    nodes = []

    for index, expression in enumerate(expressions):
        try:
            nodes.append(
                export_node(
                    asset,
                    expression,
                    is_function
                )
            )
        except Exception as error:
            log_error(
                "노드 변환 실패: {} / {}".format(
                    get_node_id(expression),
                    error
                )
            )

            nodes.append({
                "id": get_node_id(expression),
                "class": expression.get_class().get_name(),
                "export_error": str(error),
            })

    node_ids = {
        node["id"]
        for node in nodes
        if "id" in node
    }

    connections = []

    for node in nodes:
        target_node_id = node.get("id")

        for input_pin in node.get("input_pins", []):
            connection = input_pin.get("connection")

            if connection is None:
                continue

            connections.append({
                "from_node": connection.get("source_node"),
                "from_output": connection.get("source_output"),
                "from_output_index": connection.get(
                    "source_output_index"
                ),
                "to_node": target_node_id,
                "to_input": input_pin.get("name"),
            })

    unresolved_upstream_connections = []

    resolved_pairs = {
        (
            connection.get("from_node"),
            connection.get("to_node"),
        )
        for connection in connections
    }

    for node in nodes:
        for upstream in node.get("upstream_nodes", []):
            pair = (
                upstream.get("source_node"),
                node.get("id"),
            )

            if pair not in resolved_pairs:
                unresolved_upstream_connections.append({
                    "from_node": upstream.get("source_node"),
                    "from_output": upstream.get("source_output"),
                    "to_node": node.get("id"),
                    "to_input": None,
                    "reason": (
                        "Unreal Python에서 대상 입력 핀을 "
                        "직접 식별하지 못함"
                    ),
                })

    return {
        "schema_version": 1,
        "asset": {
            "name": asset.get_name(),
            "class": asset.get_class().get_name(),
            "type": asset_type,
            "path": asset.get_path_name(),
            "package": asset.get_outermost().get_name(),
        },
        "statistics": {
            "node_count": len(nodes),
            "resolved_connection_count": len(connections),
            "unresolved_connection_count": len(
                unresolved_upstream_connections
            ),
        },
        "nodes": nodes,
        "connections": connections,
        "unresolved_connections": unresolved_upstream_connections,
        "known_node_ids": sorted(node_ids),
    }


def write_json(data, output_directory):
    os.makedirs(
        output_directory,
        exist_ok=True
    )

    asset_name = data["asset"]["name"]

    output_path = os.path.join(
        output_directory,
        "{}.material_graph.json".format(asset_name)
    )

    with open(
        output_path,
        "w",
        encoding="utf-8"
    ) as file:
        json.dump(
            data,
            file,
            ensure_ascii=False,
            indent=2
        )

    return output_path


def main():
    log("에셋 로드 시작: {}".format(ASSET_PATH))

    asset = load_target_asset(ASSET_PATH)
    export_data = build_export_data(asset)
    output_path = write_json(
        export_data,
        OUTPUT_DIRECTORY
    )

    log("JSON 변환 완료")
    log("출력 경로: {}".format(output_path))

    unreal.EditorDialog.show_message(
        "Material Graph Export",
        "JSON 변환이 완료되었습니다.\n\n{}".format(
            output_path
        ),
        unreal.AppMsgType.OK
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        log_error(str(error))

        unreal.EditorDialog.show_message(
            "Material Graph Export 실패",
            str(error),
            unreal.AppMsgType.OK
        )

        raise