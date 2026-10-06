import copy
import importlib.util
from pathlib import Path
import unittest
import json
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location("conversion", Path(__file__).parents[1] / "scripts/ConvertSceneToLocal.py")
conversion = importlib.util.module_from_spec(spec)
spec.loader.exec_module(conversion)


def placement(name, parent=""):
    return {"id": name, "name": name, "model": "Assets/Models/Title/triangle.obj",
            "parent": parent, "position": [1, 2, 3], "rotation": [0, 0, 0], "scale": [1, 1, 1]}


class SceneConversionTests(unittest.TestCase):
    def test_world_pose_preserved_with_mirror_and_forward_references(self):
        parent, child, grandchild = placement("parent"), placement("child", "parent"), placement("grandchild", "child")
        parent.update(position=[10, 20, 30], rotation=[0, conversion.math.pi/2, 0], scale=[-2, 2, 2])
        child["rotation"][2] = 0.37
        source = {"version": 1, "objects": [grandchild, child, parent]}
        snapshot = copy.deepcopy(source)
        result = conversion.convert(source)
        self.assertEqual(source, snapshot)
        self.assertEqual(result["version"], 2)
        self.assertNotIn("transformSpace", result)
        for before, after in zip(map(conversion.compose, source["objects"]), conversion.resolve(result["objects"])):
            self.assertTrue(conversion.matches(before, after))

    def test_local_v1_is_not_converted_twice(self):
        source = {"version": 1, "transformSpace": "local", "objects": [placement("parent"), placement("child", "parent")]}
        result = conversion.convert(source)
        self.assertEqual(result["objects"], source["objects"])
        self.assertEqual(conversion.convert(result), result)

    def test_shear_refuses_conversion_without_mutation(self):
        parent, child = placement("parent"), placement("child", "parent")
        parent["scale"] = [2, 3, 4]
        child["rotation"][2] = 0.37
        source = {"version": 1, "objects": [child, parent]}
        snapshot = copy.deepcopy(source)
        with self.assertRaisesRegex(ValueError, "child.*shear"):
            conversion.convert(source)
        self.assertEqual(source, snapshot)

    def test_invalid_graphs_and_transforms_are_rejected(self):
        fixtures = [[placement("same"), placement("same")], [placement("child", "missing")],
                    [placement("self", "self")], [placement("a", "b"), placement("b", "a")]]
        zero, nonfinite = placement("zero"), placement("nonfinite")
        zero["scale"][0], nonfinite["position"][0] = 0, float("nan")
        fixtures += [[zero], [nonfinite]]
        for objects in fixtures:
            with self.subTest(objects=objects), self.assertRaises(ValueError):
                conversion.convert({"version": 1, "objects": objects})

    def test_format_preserves_unchanged_lines_and_metadata(self):
        obj = placement("root")
        obj["position"][1] = 0.079999998211860657
        line = json.dumps(obj).replace("0.07999999821186066", "0.079999998211860657")
        original = '{"version":1,"note":"keep","objects":[\n' + line + '\n]}'
        result = conversion.convert(json.loads(original))
        formatted = conversion.format_document(result, original)
        self.assertIn("0.079999998211860657", formatted)
        self.assertEqual(json.loads(formatted), result)
        self.assertEqual(result["note"], "keep")

    def test_cli_validates_all_inputs_before_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory)/"first.json", Path(directory)/"second.json"
            original = json.dumps({"version": 1, "objects": [placement("root")]})
            first.write_text(original, encoding="utf-8")
            second.write_text(json.dumps({"version": 1, "objects": [placement("child", "missing")]}), encoding="utf-8")
            result = subprocess.run([sys.executable, str(Path(conversion.__file__)), str(first), str(second), "--write"],
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(first.read_text(encoding="utf-8"), original)
            second.write_text(original, encoding="utf-8")
            result = subprocess.run([sys.executable, str(Path(conversion.__file__)), str(first), str(second), "--write"],
                                    capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(first.read_text(encoding="utf-8"))["version"], 2)
            self.assertEqual(json.loads(second.read_text(encoding="utf-8"))["version"], 2)

    def test_empty_and_deep_scenes(self):
        self.assertEqual(conversion.convert({"version": 1, "objects": []}), {"version": 2, "objects": []})
        objects = [placement(str(i), str(i-1) if i else "") for i in range(2000)]
        source = {"version": 1, "objects": objects[::-1]}
        result = conversion.convert(source)
        self.assertTrue(conversion.matches(conversion.compose(objects[-1]), conversion.resolve(result["objects"])[0]))


if __name__ == "__main__":
    unittest.main()
