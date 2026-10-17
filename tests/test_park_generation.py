"""再現可能な変化・必須目標・壊れたモジュールの拒否を検証します。"""
import copy
import importlib.util
import unittest
import subprocess
import sys
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("park", Path(__file__).resolve().parents[1] / "scripts/GenerateParkAdventure.py")
park = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(park)


class ParkGenerationTests(unittest.TestCase):
    def test_reproducible_and_varied(self):
        first, report = park.assemble(20261012)
        again, repeated = park.assemble(20261012)
        other, _ = park.assemble(7)
        self.assertEqual(first, again)
        self.assertEqual(report, repeated)
        self.assertNotEqual(first, other)

    def test_seed_batch(self):
        for seed in range(100):
            with self.subTest(seed=seed):
                scene, report = park.assemble(seed)
                self.assertEqual(report["validation"]["counts"]["ParkCoin"], 12)
                self.assertTrue(report["validation"]["requiredSwitchesBeforeGate"])
                self.assertLess(report["validation"]["mainRouteGap"], report["validation"]["conservativeJumpRange"])
                self.assertEqual(scene["settings"]["mainCamera"], "park-camera")

    def test_objective_beyond_gate_is_rejected(self):
        scene, _ = park.assemble(1)
        switch = next(item for item in scene["objects"] if item["name"] == "ParkSwitch")
        switch["position"][2] = 70
        with self.assertRaisesRegex(ValueError, "behind its gate"):
            park.validate(scene)

    def test_disconnected_route_is_rejected(self):
        scene, _ = park.assemble(1)
        scene["objects"] = [item for item in scene["objects"] if not item["id"].startswith("Canal-bank-b")]
        with self.assertRaisesRegex(ValueError, "exceeds conservative jump"):
            park.validate(scene)

    def test_missing_parent_and_duplicate_ids_are_rejected(self):
        scene, _ = park.assemble(1)
        broken = copy.deepcopy(scene)
        broken["objects"][0]["parent"] = "missing"
        with self.assertRaisesRegex(ValueError, "Missing parent"):
            park.validate(broken)
        scene["objects"].append(copy.deepcopy(scene["objects"][0]))
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            park.validate(scene)

    def test_output_cannot_replace_project_settings(self):
        settings = park.CONTENT / "Assets/Project.json"
        before = settings.read_bytes()
        result = subprocess.run([sys.executable, str(park.REPO / "scripts/GenerateParkAdventure.py"),
                                 "--output", str(settings), "--overwrite", "--author-library"],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(".json scene under", result.stderr)
        self.assertEqual(settings.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
