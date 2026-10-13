"""Materialize native script defaults and stable object references into authored scenes."""
from pathlib import Path
import json
from copy import deepcopy


def configure_scene(scene):
    schema_path = Path(__file__).resolve().parents[1] / "Content/Assets/Scripts/ParkScriptSchema.schema.json"
    schema = json.loads(schema_path.read_text(encoding="utf-8-sig"))
    if schema.get("version") != 1:
        raise ValueError("Unsupported park script schema version")
    definitions = {
        name: ({key: field["initial"] for key, field in definition["parameters"].items()}, definition["data"])
        for name, definition in schema["scripts"].items()
    }
    for obj in scene["objects"]:
        for component in obj.get("components", []):
            if component.get("type") != "Script" or component.get("behaviour") not in definitions:
                continue
            parameters, data = definitions[component["behaviour"]]
            for key, value in parameters.items():
                component.setdefault("parameters", {}).setdefault(key, value)
            encoded = component.setdefault("data", {"kind": "object", "value": {}})
            if encoded.get("kind") == "object":
                values = encoded["value"]
                for key, value in encoded.items():
                    if key not in ("kind", "value"):
                        values.setdefault(key, value)
            else:
                values = encoded
            component["data"] = {"kind": "object", "value": values}
            for key, value in data.items():
                values.setdefault(key, deepcopy(value))
            if component["behaviour"] == "ParkPlayer":
                for key in ("coins", "switches", "enemies", "checkpoints"):
                    if not values[key]["value"]:
                        name = values[key + "Name"]["value"]
                        values[key] = {"kind": "array", "value": [{"kind": "reference", "value": target["id"]} for target in scene["objects"] if target["name"] == name]}
                values["useNameLookup"] = {"kind": "bool", "value": False}
    return scene


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    path = root / "Content/Assets/Scenes/ParkAdventure.json"
    scene = configure_scene(json.loads(path.read_text(encoding="utf-8-sig")))
    path.write_text(json.dumps(scene, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
