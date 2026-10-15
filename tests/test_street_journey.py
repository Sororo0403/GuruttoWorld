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

 def test_street_geometry_stays_within_runtime_budget(self):
  scene=json.loads((GAME/'Assets/Scenes/StreetJourney.json').read_text(encoding='utf-8'))
  models=[c['model'] for o in scene['objects'] for c in o['components']
          if c['type']=='MeshRenderer' and c.get('model','').startswith('Assets/Models/StreetJourney/Baked/')]
  self.assertLessEqual(len(models),34)
  polygons=sum(sum(line.startswith('f ') for line in (GAME/path).read_text(encoding='utf-8').splitlines()) for path in models)
  self.assertLessEqual(polygons,170000)

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
class TownTopologyTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  import sys
  sys.path.insert(0,str(GAME.parents[1]/'scripts'))
  from AuthorStreetJourney import Journey
  cls.city=Journey().build()
  cls.objects={o['id']:o for o in cls.city.objects}
  cls.route=json.loads((GAME/'Assets/Models/StreetJourney/Route.json').read_text(encoding='utf-8'))
 def test_floor_patches_have_no_coplanar_overlap(self):
  floors=[self.objects['street-collision-'+k] for k in self.route['floors']]
  for i,a in enumerate(floors):
   for b in floors[i+1:]:
    if abs(a['position'][1]-b['position'][1])>1e-5:continue
    overlap=[min(a['position'][j]+a['scale'][j]/2,b['position'][j]+b['scale'][j]/2)-max(a['position'][j]-a['scale'][j]/2,b['position'][j]-b['scale'][j]/2) for j in (0,2)]
    self.assertLessEqual(min(overlap),1e-5,(a['id'],b['id']))
 def test_jump_openings_match_the_real_banks(self):
  for west,east in (('canal-west','canal-east'),('roof-west','roof-east')):
   openings=[]
   for bank in (west,east):
    a,b=[self.objects['street-collision-'+bank+'-'+suffix] for suffix in ('south','north')]
    lo=a['position'][2]+a['scale'][2]/2;hi=b['position'][2]-b['scale'][2]/2
    self.assertGreater(hi-lo,6);openings.append((lo,hi))
   self.assertEqual(openings[0],openings[1])
  self.assertEqual(len(self.route['jumps']),2)
 def test_stairs_join_the_roof_without_overlapping_steps(self):
  steps=[self.objects['street-collision-roof-step-'+str(i)] for i in range(12)]
  for a,b in zip(steps,steps[1:]):
   self.assertAlmostEqual(a['position'][2]+a['scale'][2]/2,b['position'][2]-b['scale'][2]/2)
   self.assertAlmostEqual(b['scale'][1]-a['scale'][1],.28)
  roof=self.objects['street-collision-east-roof']
  self.assertAlmostEqual(steps[-1]['scale'][1],roof['position'][1]+roof['scale'][1]/2)
 def test_loops_and_destination_have_real_space(self):
  self.assertEqual(len(self.route['entryLoop']),4)
  self.assertEqual(self.route['roofLoop'][0],self.route['roofLoop'][-1])
  cafe=self.objects['street-collision-cafe']
  # The cafe lies inside the square, with full walking width on both flanks.
  self.assertGreater(cafe['position'][0]-cafe['scale'][0]/2-(-22),5)
  self.assertGreater(0-(cafe['position'][0]+cafe['scale'][0]/2),5)
  lookout=self.objects['street-collision-clock-lookout']
  goal=self.city.goal
  for j in (0,2):self.assertLess(abs(goal[j]-lookout['position'][j]),lookout['scale'][j]/2-.5)
 def test_furniture_and_stock_are_supported(self):
  placements=self.city.b.sources
  def height(p):return max(v[1] for _,face in self.city.b.cache[(p['kind'],p['model'])] for v,uv,n in face)*p['scale']
  for table in (p for p in placements if p['model']=='tableRound'):
   plant=next(p for p in placements if p['model']=='plantSmall1' and p['position'][0]==table['position'][0] and p['position'][2]==table['position'][2])
   self.assertAlmostEqual(plant['position'][1],table['position'][1]+height(table))
  for p in placements:
   if p['model']=='wooden_crate_02':self.assertAlmostEqual(p['position'][1],.59)
  posts=[o for k,o in self.objects.items() if k.startswith('street-collision-warehouse-post-')]
  self.assertEqual(len(posts),6)
  for o in posts:self.assertAlmostEqual(o['position'][1]-o['scale'][1]/2,.59)

if __name__=='__main__':unittest.main()
