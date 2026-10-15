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
 def test_floor_patches_have_no_horizontal_overlap(self):
  floors=[self.objects['street-collision-'+k] for k in self.route['floors']]
  for i,a in enumerate(floors):
   for b in floors[i+1:]:
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
 def test_cover_changes_on_the_walkable_route(self):
  # Measure the actual baked roof polygons. A name or a material change alone
  # cannot satisfy the solid -> slatted -> open sequence.
  def coverage(spec):
   x,z0,z1,floor=[spec[k] for k in ('x','z0','z1','floor')]
   faces=[]
   for polygons in self.city.b.groups.values():
    for face in polygons:
     p=[(v[0],v[1],-v[2]) for v,uv,n in face]
     if min(v[1] for v in p)<floor+2 or max(v[1] for v in p)>floor+7:continue
     if x<min(v[0] for v in p) or x>max(v[0] for v in p):continue
     if z1<min(v[2] for v in p) or z0>max(v[2] for v in p):continue
     a,b,c=p[:3]
     area=(b[0]-a[0])*(c[2]-a[2])-(b[2]-a[2])*(c[0]-a[0])
     if abs(area)>1e-8:faces.append(p)
   hits=0
   for i in range(201):
    z=z0+(z1-z0)*(i+.5)/201
    for p in faces:
     signs=[(b[0]-a[0])*(z-a[2])-(b[2]-a[2])*(x-a[0]) for a,b in zip(p,p[1:]+p[:1])]
     if min(signs)>=-1e-7 or max(signs)<=1e-7:hits+=1;break
   return hits/201
  market,garden,roof=[coverage(s) for s in self.city.cover_samples]
  self.assertGreater(market,.98)
  self.assertGreater(garden,.2);self.assertLess(garden,.55)
  self.assertEqual(roof,0)
  self.assertGreater(coverage(dict(self.city.cover_samples[0],x=1)),.98)
  for x in (-2,15):
   value=coverage(dict(self.city.cover_samples[1],x=x))
   self.assertGreater(value,.2);self.assertLess(value,.55)
 def test_both_branches_share_the_roof_and_garden(self):
  roof=self.objects['street-collision-market-common-roof']
  self.assertAlmostEqual(roof['position'][0]-roof['scale'][0]/2,-6)
  self.assertAlmostEqual(roof['position'][0]+roof['scale'][0]/2,20)
  # The hall is recessed enough for a common decision area, with real floor
  # on both flanks. These are not separate optional showcases of a technique.
  hall=self.objects['street-collision-market-hall']
  self.assertGreaterEqual(hall['position'][2]-hall['scale'][2]/2,38)
  for x in (1,14):
   self.assertLess(abs(x-roof['position'][0]),roof['scale'][0]/2)
  garden=self.objects['street-collision-garden-court']
  self.assertEqual(garden['position'][0]-garden['scale'][0]/2,-6)
  self.assertEqual(garden['position'][0]+garden['scale'][0]/2,20)
 def test_wheel_clears_the_ground_and_cannot_overlap_the_player(self):
  wheel=self.objects['journey-lever'];housing=self.objects['street-collision-gear-wheel']
  vertices=[tuple(map(float,line.split()[1:4])) for line in
            (GAME/'Assets/Models/Title/Factory/cog-a.obj').read_text().splitlines() if line.startswith('v ')]
  # The existing model is rotated 90 degrees about X. Its local Z becomes Y.
  bottom=wheel['position'][1]-max(v[2] for v in vertices)*wheel['scale'][2]
  self.assertGreaterEqual(bottom,.59)
  for axis,local in ((0,0),(1,2)):
   extent=max(abs(v[local]) for v in vertices)*wheel['scale'][local]
   self.assertGreaterEqual(housing['scale'][axis]/2,extent)
  approach=[17,54]
  self.assertGreater(housing['position'][0]-housing['scale'][0]/2,approach[0]+.35)

if __name__=='__main__':unittest.main()
