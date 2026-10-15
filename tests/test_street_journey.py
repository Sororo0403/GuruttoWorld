"""Resource integrity and playable topology of the authored START stage."""
import json,unittest
from pathlib import Path
GAME=Path(__file__).resolve().parents[1]/'Games/GuruttoWorld'
class StreetJourneyTests(unittest.TestCase):
 def test_resources_and_scene_references(self):
  scene=json.loads((GAME/'Assets/Scenes/StreetJourney.json').read_text(encoding='utf-8'))
  objects={o['id']:o for o in scene['objects']}
  self.assertEqual(len(objects),len(scene['objects']))
  for o in objects.values():
   if o['parent']:self.assertIn(o['parent'],objects)
   for c in o['components']:
    self.assertNotIn(c['type'],('Text','Image','PointLight'))
    for key in ('asset','model','clip'):
     if c.get(key):self.assertTrue((GAME/c[key]).is_file(),c[key])
    def visit(v):
     if isinstance(v,dict):
      if v.get('kind')=='reference' and v.get('value'):self.assertIn(v['value'],objects)
      for x in v.values():visit(x)
     elif isinstance(v,list):
      for x in v:visit(x)
    visit(c.get('data',{}))
 def test_title_and_current_stage_have_no_comparison_mode(self):
  title=json.loads((GAME/'Assets/Scenes/TitleStreet.json').read_text(encoding='utf-8'))
  project=json.loads((GAME/'Assets/Project.json').read_text(encoding='utf-8'))
  self.assertFalse(any(k.startswith('Guidance') for k in project['inputActions']))
  objects={o['id']:o for o in title['objects']}
  self.assertNotIn('guidance-entry',objects);self.assertNotIn('lab-controller',objects)
  button=next(c for c in objects['world-start']['components'] if c['type']=='Button')
  self.assertEqual(button['target'],'Assets/Scenes/StreetJourney.json')
  current=(GAME/'Assets/Scenes/StreetJourney.json').read_text(encoding='utf-8')
  self.assertNotIn('Guidance',current)
  self.assertFalse((GAME/'Assets/Scenes/GuidanceGallery.json').exists())
  self.assertFalse((GAME/'Assets/Scenes/GuidanceLab').exists())
 def test_presentation_terms_never_enter_game_content(self):
  import re
  forbidden=re.compile(r'修辞|擬人法|反復法|漸層法|対比法|rhetoric|anaphora|antithesis|climax',re.I)
  for rel in ('Assets/Scenes/StreetJourney.json','Assets/Scenes/TitleStreet.json','Assets/Project.json','Assets/Scripts/StreetJourney.cpp'):
   self.assertIsNone(forbidden.search((GAME/rel).read_text(encoding='utf-8')),rel)
 def test_disconnected_jump_surfaces_and_local_floors(self):
  scene=json.loads((GAME/'Assets/Scenes/StreetJourney.json').read_text(encoding='utf-8'))
  objects={o['id']:o for o in scene['objects']}
  floor=lambda key:objects['street-collision-'+key]
  west,east=floor('cafe-west-floor'),floor('cafe-east-floor')
  edge=west['position'][0]+west['scale'][0]/2
  self.assertGreater(east['position'][0]-east['scale'][0]/2-edge,1.5)
  self.assertNotIn('street-collision-floor',objects)
  self.assertGreater(floor('east-roof')['position'][1],3)
  self.assertGreater(floor('roof-step-11')['scale'][1],floor('roof-step-0')['scale'][1])
  self.assertIn('journey-shutter',objects)
  route=json.loads((GAME/'Assets/Models/StreetJourney/Route.json').read_text(encoding='utf-8'))
  self.assertTrue(route['detour']);self.assertEqual(len(route['jumps']),2)
if __name__=='__main__':unittest.main()
