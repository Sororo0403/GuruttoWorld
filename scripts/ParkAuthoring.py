"""Materialize native script defaults and stable object references into authored scenes."""
from pathlib import Path
import json
import re


def configure_scene(scene):
    source = (Path(__file__).resolve().parents[1] / "Content/Assets/Scripts/ParkAdventure.cpp").read_text(encoding="utf-8-sig")
    definitions = {}
    for name, block in re.findall(r"ScriptDefinition (Player|Camera|Patrol)\(\)(.*?)(?=\n    ScriptDefinition|\n}\n)", source, re.S):
        fields = re.search(r"result.fields=\{(.*?)\};", block, re.S)
        parameters = {key: float(value.rstrip("f")) for key, value in re.findall(r'\{"([^"]+)",\{([\d.\-]+f?),', fields.group(1))}
        data = {}
        defaults = re.search(r"result.dataFields=\{(.*?)\};", block, re.S)
        if defaults:
            for key, value in re.findall(r'\{"([^"]+)",\{(true|false)\}\}', defaults.group(1)):
                data[key] = {"kind": "bool", "value": value == "true"}
            for key, value in re.findall(r'\{"([^"]+)",\{std::string\("((?:\\.|[^"\\])*)"\)\}\}', defaults.group(1)):
                data[key] = {"kind": "string", "value": json.loads('"' + value + '"')}
            for key, value in re.findall(r'\{"([^"]+)",\{ScriptObjectReference\{"([^"]*)"\}\}\}', defaults.group(1)):
                data[key] = {"kind": "reference", "value": value}
            for key in re.findall(r'\{"([^"]+)",\{ScriptValue::Array\{\}\}\}', defaults.group(1)):
                data[key] = {"kind": "array", "value": []}
        definitions["Park" + name] = parameters, data
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
                values.setdefault(key, value)
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
