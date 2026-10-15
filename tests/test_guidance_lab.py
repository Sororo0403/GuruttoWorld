"""Spatial studies must have playable routes, authored focus and valid assets."""
import copy
import json
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
GAME=ROOT/'Games/GuruttoWorld'
sys.path.insert(0,str(ROOT/'scripts'))
from AuthorGuidanceLab import CASES,GALLERY


def load(path):return json.loads((GAME/path).read_text(encoding='utf-8'))
def items(scene):return {o['id']:o for o in scene['objects']}


class GuidanceTests(unittest.TestCase):
    def test_twelve_accessible_layouts_with_resolved_resources(self):
        experiments=load('Assets/Models/GuidanceLab/Experiments.json')['cases']
        self.assertEqual(len(experiments),12)
        for case in experiments:
            scene=load(case['scene']);objects=items(scene)
            self.assertEqual(len(objects),len(scene['objects']))
            self.assertLessEqual(len(objects),180)
            for o in objects.values():
                if o['parent']:self.assertIn(o['parent'],objects)
                for c in o['components']:
                    self.assertNotIn(c['type'],('Text','Image','PointLight'))
                    for key in ('model','asset','clip'):
                        if c.get(key):self.assertTrue((GAME/c[key]).is_file(),c[key])
                    if c['type']=='Script':
                        def visit(v):
                            if isinstance(v,dict):
                                if v.get('kind')=='reference' and v.get('value'):self.assertIn(v['value'],objects)
                                for x in v.values():visit(x)
                            elif isinstance(v,list):
                                for x in v:visit(x)
                        visit(c.get('data',{}))

    def test_studies_have_distinct_architecture_and_authored_camera_focus(self):
        for mirror in ('A','B'):
            base=items(load(f'Assets/Scenes/GuidanceLab/Course-0{mirror}.json'))
            camera_data=[]
            footprints=[]
            for mode in range(6):
                current=items(load(f'Assets/Scenes/GuidanceLab/Course-{mode}{mirror}.json'))
                camera=next(c for c in current['park-camera']['components'] if c['type']=='Script')
                self.assertEqual(camera['behaviour'],'GuidanceCamera')
                refs=camera['data']['value']
                self.assertTrue(all(refs['focus'+str(i)]['value']=='lab-focus-'+str(i) for i in range(4)))
                camera_data.append([current[refs['focus'+str(i)]['value']]['position'] for i in range(4)])
                footprints.append([(o['id'],o['position'],o['scale']) for o in current.values() if o['id'].startswith('lab-collision-')])
                self.assertIn('lab-collision-step-14',current)
                self.assertEqual(current['lab-collision-step-14']['scale'][1],4.2)
                self.assertIn('lab-destination',current)
                self.assertEqual(current['lab-destination']['position'][1],5.25)
                self.assertNotIn('lab-collision-island',current)
            self.assertEqual(len({json.dumps(v) for v in camera_data}),6)
            self.assertEqual(len({json.dumps(v) for v in footprints}),6)
            for mode in range(1,6):
                current=items(load(f'Assets/Scenes/GuidanceLab/Course-{mode}{mirror}.json'))
                self.assertEqual(current['block-sky'],base['block-sky'])
                self.assertEqual(current['block-directionallight'],base['block-directionallight'])

    def test_reflection_swaps_destination_and_colliders(self):
        a=items(load('Assets/Scenes/GuidanceLab/Course-0A.json'))
        b=items(load('Assets/Scenes/GuidanceLab/Course-0B.json'))
        for key in [k for k in a if k.startswith(('lab-collision-','lab-focus-'))]+['lab-goal','lab-destination']:
            expected=copy.deepcopy(a[key]);expected['position'][0]*=-1
            expected['rotation'][1]*=-1
            self.assertEqual(expected,b[key])

    def test_six_figures_split_into_three_pairs(self):
        self.assertEqual(len(CASES),6)
        cases=load('Assets/Models/GuidanceLab/Experiments.json')['cases']
        groups=[c['group'] for c in cases if not c['mirror']]
        self.assertEqual(groups,['定番','定番','珍しい','珍しい','激レア','激レア'])
        for mode in range(6):
            scene=items(load(f'Assets/Scenes/GuidanceLab/Course-{mode}A.json'))
            self.assertTrue(any(k.startswith(f'lab-City{mode}') for k in scene))
        self.assertEqual(CASES[5][0],'Epanalepsis')

    def test_title_and_gallery_entry(self):
        title=items(load('Assets/Scenes/TitleStreet.json'))
        self.assertTrue(load('Assets/Scenes/TitleStreet.json').get('assetReferences'))
        start=next(c for c in title['world-start']['components'] if c['type']=='Button')
        self.assertEqual(start['target'],'Assets/Scenes/GuidanceLab/Course-4A.json')
        canvas=next(c for o in title.values() for c in o['components'] if c['type']=='Canvas' and 'menu' in c)
        self.assertEqual(canvas['menu']['entries'][0]['target'],start['target'])
        entry=next(c for c in title['guidance-entry']['components'] if c['type']=='Button')
        self.assertEqual(entry['event'],'guidance:gallery')
        gallery=items(load(GALLERY))
        self.assertTrue(all('lab-name-'+str(i) in gallery for i in range(6)))
        keys=load('Assets/Project.json')['inputActions']
        self.assertEqual(keys['GuidanceGallery']['keys'],[64])
        self.assertEqual(keys['GuidanceMirror']['keys'],[50])


if __name__=='__main__':unittest.main()
