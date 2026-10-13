import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("park_authoring", ROOT / "scripts/ParkAuthoring.py")
authoring = importlib.util.module_from_spec(spec)
spec.loader.exec_module(authoring)


class ParkAuthoringTests(unittest.TestCase):
    def setUp(self):
        self.scene = json.loads((ROOT / "Content/Assets/Scenes/ParkAdventure.json").read_text(encoding="utf-8-sig"))

    def test_existing_authored_values_are_preserved(self):
        self.assertEqual(authoring.configure_scene(copy.deepcopy(self.scene)), self.scene)

    def test_authoring_does_not_read_cpp_sources(self):
        read = Path.read_text
        def checked(path, *args, **kwargs):
            self.assertNotEqual(path.suffix, ".cpp")
            return read(path, *args, **kwargs)
        with patch.object(Path, "read_text", checked):
            authoring.configure_scene(copy.deepcopy(self.scene))

    def test_defaults_and_references_are_materialized_without_overwriting_edits(self):
        player = next(obj for obj in self.scene["objects"] if obj["id"] == "park-player")
        script = next(c for c in player["components"] if c.get("behaviour") == "ParkPlayer")
        script["parameters"] = {"maxHp": 17}
        script["data"] = {"kind": "object", "value": {"returnScene": {"kind": "string", "value": "Assets/Scenes/UiCheck.json"}}}
        authoring.configure_scene(self.scene)
        self.assertEqual(script["parameters"]["maxHp"], 17)
        self.assertEqual(script["data"]["value"]["returnScene"]["value"], "Assets/Scenes/UiCheck.json")
        self.assertEqual(len(script["data"]["value"]["coins"]["value"]), 12)

    def test_generated_header_is_current_and_generation_is_idempotent(self):
        schema_path = ROOT / "Content/Assets/Scripts/ParkScriptSchema.schema.json"
        with tempfile.TemporaryDirectory() as directory:
            scripts = Path(directory) / "Assets/Scripts"
            scripts.mkdir(parents=True)
            (scripts / schema_path.name).write_bytes(schema_path.read_bytes())
            command = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ROOT / "scripts/GenerateScriptSchemas.ps1"), "-Content", directory]
            subprocess.run(command, check=True, capture_output=True)
            header = scripts / "ParkScriptSchema.generated.h"
            self.assertEqual(header.read_bytes(), (schema_path.parent / header.name).read_bytes())
            timestamp = header.stat().st_mtime_ns
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(header.stat().st_mtime_ns, timestamp)
            schema = json.loads(schema_path.read_text(encoding="utf-8-sig"))
            schema["scripts"]["ParkPlayer"]["parameters"]["maxHp"]["initial"] = 17
            schema["scripts"]["ParkPlayer"]["data"]["statsTemplate"]["value"] = "日本語's \"text\"\\path\n"
            (scripts / schema_path.name).write_text(json.dumps(schema, ensure_ascii=False, indent=1), encoding="utf-8")
            subprocess.run(command, check=True, capture_output=True)
            self.assertIn('{"maxHp",{17.0f,', header.read_text(encoding="utf-8"))
            self.assertIn("日本語's \\\"text\\\"\\\\path\\n", header.read_text(encoding="utf-8"))
            valid_header = header.read_bytes()
            schema["scripts"]["ParkPlayer"]["data"]["statsTemplate"]["value"] = "left\0right"
            (scripts / schema_path.name).write_text(json.dumps(schema), encoding="utf-8")
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(header.read_bytes(), valid_header)
            schema["scripts"]["ParkPlayer"]["data"]["statsTemplate"]["value"] = "valid"
            schema["scripts"]["ParkPlayer"]["purposes"]["maxHp"] = "sceneAsset"
            (scripts / schema_path.name).write_text(json.dumps(schema), encoding="utf-8")
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)


if __name__ == "__main__":
    unittest.main()
