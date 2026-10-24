"""シーン変換の配置維持と、複数入力の事前検証を確認します。"""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import importlib.util


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/ConvertSceneToLocal.py"
SPEC = importlib.util.spec_from_file_location("migration_under_test", SCRIPT)
MIGRATION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MIGRATION)


class SceneMigrationTests(unittest.TestCase):
    def setUp(self):
        fixtures = ROOT / "generated/tests/scene-migration"
        fixtures.mkdir(parents=True, exist_ok=True)
        self.directory = Path(tempfile.mkdtemp(dir=fixtures))
        self.document = {"version": 1, "objects": [
            {"id": "parent", "position": [2, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
            {"id": "child", "parent": "parent", "position": [3, 0, 0],
             "rotation": [0, 0, 0], "scale": [1, 1, 1]}]}

    def write(self, name, document):
        path = self.directory / name
        path.write_text(json.dumps(document, ensure_ascii=True), encoding="utf-8")
        return path

    def invoke(self, *paths, write=True):
        return subprocess.run([sys.executable, str(SCRIPT), *(["--write"] if write else []),
                               *map(str, paths)], capture_output=True)

    def test_world_placement_and_dry_run(self):
        path = self.write("valid.json", self.document)
        original = path.read_bytes()
        self.assertEqual(self.invoke(path, write=False).returncode, 0)
        self.assertEqual(path.read_bytes(), original)
        result = self.invoke(path)
        self.assertEqual(result.returncode, 0, result.stderr)
        converted = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(converted["version"], 2)
        self.assertEqual(converted["objects"][1]["position"], [1, 0, 0])
        self.assertEqual(self.invoke(path).returncode, 0)
        self.assertEqual(json.loads(path.read_text(encoding="utf-8")), converted)

    def test_invalid_later_input_preserves_all_files(self):
        for invalid in (float("nan"), float("inf"), float("-inf"), "\ud800"):
            with self.subTest(value=repr(invalid)):
                first = self.write("first.json", self.document)
                document = copy.deepcopy(self.document)
                document["extra"] = invalid
                second = self.write("second.json", document)
                originals = [path.read_bytes() for path in (first, second)]
                self.assertNotEqual(self.invoke(first, second).returncode, 0)
                self.assertEqual([path.read_bytes() for path in (first, second)], originals)
                self.assertNotEqual(self.invoke(second, write=False).returncode, 0)
        second.write_text(second.read_text().replace('"\\ud800"', "1e999"), encoding="utf-8")
        self.assertNotEqual(self.invoke(first, second).returncode, 0)
        self.assertEqual(first.read_bytes(), originals[0])

    def test_invalid_hierarchy_is_rejected(self):
        for field, value in (("id", 1), ("parent", False), ("parent", "absent"), ("parent", "child")):
            with self.subTest(field=field, value=value):
                document = copy.deepcopy(self.document)
                document["objects"][1][field] = value
                path = self.write("hierarchy.json", document)
                original = path.read_bytes()
                self.assertNotEqual(self.invoke(path).returncode, 0)
                self.assertEqual(path.read_bytes(), original)

    def test_failed_output_removes_temporary_file(self):
        path = self.write("write-failure.json", self.document)
        original = path.read_bytes()
        original_temporary = tempfile.NamedTemporaryFile
        created = []

        def failing_temporary(*args, **kwargs):
            temporary = original_temporary(*args, **kwargs)
            created.append(Path(temporary.name))
            temporary.write = mock.Mock(side_effect=OSError("injected write failure"))
            return temporary

        for failure in ("write", "sync"):
            with self.subTest(failure=failure), mock.patch.object(sys, "argv", [str(SCRIPT), "--write", str(path)]):
                if failure == "write":
                    with mock.patch.object(MIGRATION.tempfile, "NamedTemporaryFile", side_effect=failing_temporary):
                        with self.assertRaisesRegex(OSError, "injected"):
                            MIGRATION.main()
                else:
                    with mock.patch.object(MIGRATION.os, "fsync", side_effect=OSError("injected sync failure")):
                        with self.assertRaisesRegex(OSError, "injected"):
                            MIGRATION.main()
                self.assertEqual(path.read_bytes(), original)
                self.assertEqual(set(self.directory.iterdir()), {path})
                self.assertTrue(all(not temporary.exists() for temporary in created))


if __name__ == "__main__":
    unittest.main()
