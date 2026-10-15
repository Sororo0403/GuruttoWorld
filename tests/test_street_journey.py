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
 def test_street_geometry_stays_within_runtime_budget(self):
  scene=json.loads((GAME/'Assets/Scenes/StreetJourney.json').read_text(encoding='utf-8'))
  models=[c['model'] for o in scene['objects'] for c in o['components']
          if c['type']=='MeshRenderer' and c.get('model','').startswith('Assets/Models/StreetJourney/Baked/')]
  self.assertLessEqual(len(models),34)
  polygons=sum(sum(line.startswith('f ') for line in (GAME/path).read_text(encoding='utf-8').splitlines()) for path in models)
  self.assertLessEqual(polygons,170000)
 def test_spatial_openings_and_stair_edges_remain_usable(self):
  scene=json.loads((GAME/'Assets/Scenes/StreetJourney.json').read_text(encoding='utf-8'))
  objects={o['id']:o for o in scene['objects']}
  bounds=lambda key:objects['street-collision-'+key]
  # The approach opens before the cafe, rather than funneling to a marked lane.
  self.assertGreaterEqual(bounds('entry-floor')['scale'][0],9)
  self.assertLessEqual(bounds('entry-east')['position'][2]+bounds('entry-east')['scale'][2]/2,11)
  self.assertGreaterEqual(bounds('entry-apron')['scale'][2],7)
  # Matching terrace returns leave generous openings; neither bank narrows the jump.
  for key in ('cafe-bank-west','cafe-bank-east','roof-bank-east','roof-bank-west'):
   ends=[bounds(key+str(side)) for side in (-1,1)]
   clear=ends[1]['position'][2]-ends[0]['position'][2]-(ends[0]['scale'][2]+ends[1]['scale'][2])/2
   self.assertGreater(clear,6)
  for i in range(12):
   west,east=[bounds('stair-cheek-'+str(i)+'-'+str(x)) for x in (11.75,16.25)]
   self.assertGreater(east['position'][0]-west['position'][0]-.26,4)
   if i:self.assertGreater(west['scale'][1],bounds('stair-cheek-'+str(i-1)+'-11.75')['scale'][1])
  # Both arms remain open and meet in the same courtyard; no new mandatory interaction.
  self.assertLess(bounds('service-loading-plinth')['position'][0]+bounds('service-loading-plinth')['scale'][0]/2,-4.8)
  self.assertIn('street-collision-court-mouth-1',objects)
 def test_imported_street_textures_match_cc0_sources(self):
  import hashlib
  folder=GAME/'Assets/Textures/StreetJourney/PolyHaven'
  for provenance in folder.glob('*Source.json'):
   source=json.loads(provenance.read_text(encoding='utf-8'))
   self.assertEqual(source['license'],'CC0-1.0')
   for entry in source['files']:
    path=folder/entry['file']
    self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),entry['sha256'])
    self.assertTrue(json.loads(path.with_name(path.name+'.meta').read_text(encoding='utf-8'))['texture']['mipmaps'])
   for entry in source.get('originalFiles',[]):
    self.assertEqual(hashlib.sha256((GAME/entry['file']).read_bytes()).hexdigest(),entry['sha256'])
if __name__=='__main__':unittest.main()
