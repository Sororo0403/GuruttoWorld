"""プレイ可能な短い街区・カメラの構図・タイトルからの引き継ぎを検証します。"""
import json
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import AuthorSunriseBlock as author


class SunriseBlockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scene=json.loads((author.GAME/author.SCENE).read_text(encoding='utf-8'))
        cls.objects={o['id']:o for o in cls.scene['objects']}

    def test_saved_resources_and_references(self):
        self.assertEqual(self.scene,author.assemble())
        self.assertEqual(len(self.objects),len(self.scene['objects']))
        def visit(value):
            if isinstance(value,dict):
                if value.get('kind')=='reference' and value.get('value'):self.assertIn(value['value'],self.objects)
                for child in value.values():visit(child)
            elif isinstance(value,list):
                for child in value:visit(child)
        visit(self.scene)
        for o in self.objects.values():
            if o['parent']:self.assertIn(o['parent'],self.objects)
            for c in o['components']:
                for key in ('model','asset','clip'):
                    if c.get(key):self.assertTrue((author.GAME/c[key]).is_file(),c[key])

    def test_collision_survives_static_baking(self):
        self.assertLessEqual(len(self.objects),142)
        for o in author.assemble(False)['objects']:
            colliders=[c for c in o['components'] if c['type']=='BoxCollider']
            if colliders:
                saved=self.objects[o['id']]
                self.assertEqual(colliders,[c for c in saved['components'] if c['type']=='BoxCollider'])
                for key in ('position','rotation','scale','parent'):self.assertEqual(o[key],saved[key])
        self.assertEqual(sum(o['id'].startswith('audio-') for o in self.objects.values()),7)

    def test_text_free_stage_and_camera_space(self):
        self.assertFalse(any(c['type'] in ('Text','Image') for o in self.objects.values() for c in o['components']))
        for identity in ('park-hint','park-stats','stage-title','park-result','hint-bg','hud-bg','park-clear-bg'):
            self.assertNotIn(identity,self.objects)
        building=self.objects['collision-entry-right']
        self.assertLessEqual(building['position'][2]+building['scale'][2]/2,10)
        from SunriseDistrictBake import scene_objects
        for original in scene_objects():
            self.assertEqual(original,self.objects[original['id']])
            renderer=next(c for c in original['components'] if c['type']=='MeshRenderer')
            path=author.GAME/renderer['model']
            for line in path.read_text().splitlines():
                if line.startswith('mtllib '):self.assertTrue((path.parent/line[7:]).is_file())

    def test_mechanism_and_short_route(self):
        self.assertEqual(sum(o['name']=='ParkCoin' for o in self.objects.values()),6)
        self.assertEqual(sum(o['name']=='ParkSwitch' for o in self.objects.values()),1)
        self.assertEqual(sum(o['id'].startswith('roof-step-') for o in self.objects.values()),18)
        self.assertGreater(self.objects['park-goal']['position'][1],6)
        shutter=self.objects['roof-shutter']
        self.assertTrue(any(c['type']=='BoxCollider' for c in shutter['components']))
        control=next(c for c in shutter['components'] if c['type']=='Script')
        self.assertEqual(control['data']['value']['switch']['value'],'switch-roof')

    def test_cafe_keeps_normal_maps_and_native_materials(self):
        from AuthorEntranceCafe import scene_objects
        normal_mapped=0
        for original in scene_objects():
            self.assertEqual(original,self.objects[original['id']])
            renderer=next(c for c in original['components'] if c['type']=='MeshRenderer')
            mesh=(author.GAME/renderer['model']).read_text(encoding='utf-8')
            self.assertIn('mtllib Cafe.mtl',mesh)
            mat=next(c for c in original['components'] if c['type']=='Material')
            values=json.loads((author.GAME/mat['asset']).read_text(encoding='utf-8'))
            self.assertTrue(values['physicallyBased'])
            for key in ('texture','normalTexture'):
                if values[key]:
                    self.assertTrue(values[key].startswith('Assets/Textures/'))
                    self.assertTrue((author.GAME/values[key]).is_file())
            normal_mapped+=bool(values['normalTexture'])
        self.assertGreater(normal_mapped,0)
        self.assertFalse(any(c['type']=='PointLight' for o in self.objects.values() for c in o['components']))

    def test_title_atmosphere_and_start(self):
        title=json.loads((author.GAME/'Assets/Scenes/TitleStreet.json').read_text(encoding='utf-8'))
        for kind in ('Sky','DirectionalLight'):
            original=next(c for o in title['objects'] for c in o['components'] if c['type']==kind)
            current=next(c for o in self.objects.values() for c in o['components'] if c['type']==kind)
            if kind=='DirectionalLight':
                original=dict(original,ambient=.4)
            self.assertEqual(original,current)
        canvas=next(c for o in title['objects'] for c in o['components'] if c['type']=='Canvas' and 'menu' in c)
        self.assertEqual(canvas['menu']['entries'][0]['target'],'Assets/Scenes/StreetJourney.json')
        start=next(o for o in title['objects'] if o['id']=='world-start')
        self.assertEqual(next(c for c in start['components'] if c['type']=='Button')['target'],'Assets/Scenes/StreetJourney.json')


if __name__=='__main__':unittest.main()
