"""Guard play-critical layout, title connection and resolved stage resources."""
import importlib.util
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
SPEC = importlib.util.spec_from_file_location('sunrise', ROOT / 'scripts/AuthorSunriseStreet.py')
author = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(author)


class SunriseAuthoringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scene = json.loads((author.GAME / author.SCENE).read_text(encoding='utf-8'))
        cls.objects = {o['id']: o for o in cls.scene['objects']}

    def test_saved_scene_matches_authoring_and_valid_references(self):
        self.assertEqual(self.scene, author.assemble())
        self.assertEqual(len(self.objects), len(self.scene['objects']))

        def references(value):
            if isinstance(value, dict):
                if value.get('kind') == 'reference' and value.get('value'):
                    self.assertIn(value['value'], self.objects)
                for child in value.values():
                    references(child)
            elif isinstance(value, list):
                for child in value:
                    references(child)

        references(self.scene)
        for o in self.objects.values():
            if o['parent']:
                self.assertIn(o['parent'], self.objects)
            for c in o['components']:
                for key in ('model', 'asset', 'clip', 'texture'):
                    if c.get(key):
                        self.assertTrue((author.GAME / c[key]).is_file(), c[key])

    def test_title_keyboard_and_pointer_reach_stage(self):
        title = json.loads((author.GAME / 'Assets/Scenes/TitleStreet.json').read_text(encoding='utf-8'))
        canvas = next(c for o in title['objects'] for c in o['components'] if c['type'] == 'Canvas' and 'menu' in c)
        self.assertEqual(canvas['menu']['entries'][0]['target'], 'Assets/Scenes/StreetJourney.json')
        start = next(o for o in title['objects'] if o['id'] == 'world-start')
        self.assertEqual(next(c for c in start['components'] if c['type'] == 'Button')['target'], 'Assets/Scenes/StreetJourney.json')

    def test_batching_preserves_all_collision_and_bounds_object_count(self):
        original = author.assemble(False)
        self.assertLessEqual(len(self.objects), 160)
        for item in original['objects']:
            colliders = [c for c in item['components'] if c['type'] == 'BoxCollider']
            if not colliders:
                continue
            saved = self.objects[item['id']]
            self.assertEqual([c for c in saved['components'] if c['type'] == 'BoxCollider'], colliders)
            for key in ('parent', 'position', 'rotation', 'scale'):
                self.assertEqual(saved[key], item[key])
        self.assertEqual(sum(o['id'].startswith('audio-') for o in self.objects.values()), 7)

    def test_required_gears_and_safe_route_geometry(self):
        self.assertEqual(sum(o['name'] == 'ParkCoin' for o in self.objects.values()), 12)
        switches = [o for o in self.objects.values() if o['name'] == 'ParkSwitch']
        self.assertEqual(len(switches), 2)
        self.assertTrue(all(o['position'][2] < self.objects['park-gate']['position'][2] for o in switches))
        bridge = self.objects['canal-bridge']
        self.assertGreaterEqual(bridge['scale'][2], 8)
        self.assertTrue(any(c['type'] == 'BoxCollider' for c in bridge['components']))
        steps = [self.objects[f'terrace-step-{i}'] for i in range(10)]
        self.assertTrue(all(abs(b['scale'][1] - a['scale'][1]) < .32 for a, b in zip(steps, steps[1:])))
        self.assertEqual(steps[-1]['scale'][1], 3)
        self.assertTrue(all(f'roof-descent-{i}' in self.objects for i in range(10)))
        self.assertEqual(self.objects['crate']['name'], 'ParkEnemy')
        self.assertGreater(self.objects['flower-piece-5']['position'][0], 11)
        self.assertTrue(all(f'canal-stone-{i}' in self.objects for i in range(4)))


if __name__ == '__main__':
    unittest.main()
