"""Author the street stage and its START connection."""
import copy,json,math
from AuthorSunriseStreet import GAME,model,script
from GenerateParkAdventure import obj,component,coin,character,write,box,mesh
from AuthorEntranceCafe import material
from StreetArchitecture import StreetArchitecture,StreetArtBake,PAVING,NORMAL,BRICK,BRICK_NORMAL,prepare_art_imports

FOLDER='Assets/Models/StreetJourney'
SCENE='Assets/Scenes/StreetJourney.json'

class Journey(StreetArchitecture):
 def __init__(self):
  super().__init__()
  self.b=StreetArtBake(FOLDER,'Assets/Materials/StreetJourney','Assets/Textures/StreetJourney','journey')
  self.limestone=material(PAVING,NORMAL,roughness=.88)
  self.paving=self.limestone
  self.gardenstone=material(color=(.58,.64,.58,1),roughness=.94)
  self.rooftile=material(color=(.64,.43,.30,1),roughness=.94)
  self.mortar=material(color=(.43,.44,.40,1),roughness=.94)
 def floor(self,key,x0,x1,z0,z1,h=0,surface=None,foundation=None):
  self.cube(((x0+x1)/2,(h-3.6)/2,(z0+z1)/2),(x1-x0,h+3.6,z1-z0),foundation or material(BRICK,BRICK_NORMAL,roughness=.88))
  self.solid(key,((x0+x1)/2,h-.35,(z0+z1)/2),(x1-x0,.7,z1-z0))
  self.cube(((x0+x1)/2,h+.015,(z0+z1)/2),(x1-x0,.03,z1-z0),surface or self.paving)
  if surface and not surface['texture']:
   # Whole-area construction joints, rather than a painted route or arrows.
   for x in range(math.ceil(x0/3)*3,math.ceil(x1),3):
    self.cube((x,h+.032,(z0+z1)/2),(.025,.004,z1-z0),self.mortar)
   for z in range(math.ceil(z0/3)*3,math.ceil(z1),3):
    self.cube(((x0+x1)/2,h+.032,z),(x1-x0,.004,.025),self.mortar)
 def rail(self,key,x,z,w,d,h=.85,y=0):
  self.cube((x,y+h/2,z),(w,h,d),self.cream,key)
  self.cube((x,y+h+.03,z),(w+.05,.07,d+.05),self.teal)
 def landing(self,key,x,z,width,y=0,across=False):
  # Paired masonry returns belong to the terrace, never a floating sign.
  for side in (-1,1):
   px,pz=(x,z+side*width/2) if across else (x+side*width/2,z)
   self.cube((px,y+.38,pz),(.5,.76,.5),self.cream,key+str(side))
   self.cube((px,y+.8,pz),(.62,.08,.62),self.teal)
 def bank(self,key,x,z0,z1,opening,y=0):
  # The opening is a real gap in a continuous parapet, not a row of cue props.
  lo,hi=opening
  for suffix,a,b in (('south',z0,lo),('north',hi,z1)):
   self.rail(key+'-'+suffix,x,(a+b)/2,.28,b-a,.76,y)
  self.landing(key,x,(lo+hi)/2,hi-lo,y,True)
 def build(self):
  from StreetJourneyTown import build_town
  return build_town(self)


def box_hero(objects):
 # The visual rig is independent of the capsule and movement controller.
 # A plain cube, square eyes and a short mouth replace the round silhouette.
 objects[:]=[o for o in objects if o['id'] not in ('hero-backpack','hero-backpack-buckle','hero-mouth')
             and not o['id'].startswith(('park-player-eye-','park-player-foot-'))]
 body=next(o for o in objects if o['id']=='hero-body')
 body.update(position=[0,-.22,0],scale=[.9,.9,.9],components=mesh('Player'))
 for side in (-1,1):
  objects.append(obj('park-player-eye-'+str(side),[side*.18,-.1,.457],[.105,.13,.025],mesh('Eye'),parent='hero-rig'))
 objects.append(obj('hero-mouth',[0,-.35,.457],[.2,.035,.025],mesh('Eye'),parent='hero-rig'))


def box_enemies(objects):
 # Keep the existing enemy roots and their anticipation/lunge animation.
 # All visual parts share that root, including removal when it is defeated.
 enemies=[o for o in objects if o['name']=='ParkEnemy']
 old_parts={root['id']+suffix for root in enemies for suffix in
            ('-eye--1','-eye-1','-foot--1','-foot-1')}
 objects[:]=[o for o in objects if o['id'] not in old_parts]
 for root in enemies:
  key=root['id']
  root['components']=[c for c in root['components'] if c['type'] not in ('MeshRenderer','Material')]
  objects.append(obj(key+'-body',[0,-.08,0],[.86,1,.86],mesh('Enemy'),parent=key))
  for side in (-1,1):
   objects.append(obj(key+'-eye-'+str(side),[side*.16,.04,.437],[.1,.12,.025],mesh('Eye'),parent=key))
   objects.append(obj(key+'-brow-'+str(side),[side*.16,.17,.44],[.18,.045,.025],mesh('Eye'),parent=key,rotation=[0,0,side*.35]))
   objects.append(obj(key+'-mouth-'+str(side),[side*.055,-.22,.44],[.125,.035,.025],mesh('Eye'),parent=key,rotation=[0,0,-side*.32]))


def assemble(city,geometry):
 old=json.loads((GAME/'Assets/Scenes/SunriseBlock.json').read_text(encoding='utf-8'))
 keep={'block-sky','block-directionallight','music','park-player','hero-rig','hero-body','hero-backpack','hero-backpack-buckle','park-puff','park-ui'}
 objects=[copy.deepcopy(o) for o in old['objects'] if o['id'] in keep or o['id'].startswith(('park-player-','audio-'))]
 objects+=copy.deepcopy(geometry)+copy.deepcopy(city.objects)
 box_hero(objects)
 box_enemies(objects)
 player=next(o for o in objects if o['id']=='park-player');player['position']=list(city.spawn)
 for c in player['components']:
  if c['type']=='PlayerController':c.update(moveSpeed=4.6,stepHeight=.36)
  if c['type']=='Script':
   c['behaviour']='StreetPlayer';c['parameters'].update(leftLimit=-34,rightLimit=34,coinTotal=8)
   data=c['data']['value'];data['scene']={'kind':'string','value':SCENE}
   data['returnScene']={'kind':'string','value':'Assets/Scenes/TitleStreet.json'}
   for key in ('goal','gate','stats','hint','result'):data[key]={'kind':'reference','value':''}
   for key in ('coins','switches','enemies','checkpoints'):data[key]={'kind':'array','value':[]}
   for key,prefix in (('coins','stage-coin-'),('enemies','stage-enemy-'),('checkpoints','stage-checkpoint')):
    data[key]={'kind':'array','value':[{'kind':'reference','value':o['id']} for o in objects if o['id'].startswith(prefix) and (key!='enemies' or o['name']=='ParkEnemy')]}
 camera=script('StreetCamera',target='park-player');camera['parameters']={'height':4.5,'distance':7.6,'lead':3.0}
 objects.append(obj('park-camera',[-13.5,4.4,-7.6],rotation=[.24,0,0],components=[
  component('Camera',verticalFov=58,nearClip=.1,farClip=150,referenceAspect=16/9,preserveHorizontal=True),camera]))
 objects.append(obj('street-destination',city.goal))
 return dict(version=4,settings=copy.deepcopy(old['settings']),objects=objects)

def author():
 prepare_art_imports()
 city=Journey().build();geometry=city.b.finish();scene=assemble(city,geometry)
 scene['objects'].append(obj('journey-controller',(0,0,0),components=[component('Script',behaviour='StreetJourney',parameters={})]))
 write(GAME/SCENE,scene)
 write(GAME/(FOLDER+'/Route.json'),dict(version=1,scene=SCENE,route=city.route,shots=city.shots,
  jumps=city.jumps,detour=city.detour,entryLoop=city.entry_loop,roofLoop=city.roof_loop,
  floors=city.floors,coverSamples=city.cover_samples,
  stages=['カフェを囲む入口広場','水路と市場のテラス','市場の表通りと裏通り','屋上へ続く庭','空中庭園','時計台の展望所']))
 title=json.loads((GAME/'Assets/Scenes/TitleStreet.json').read_text(encoding='utf-8'))
 for o in title['objects']:
  for c in o['components']:
   if o['id']=='world-start' and c['type']=='Button':c['target']=SCENE
   if c['type']=='Canvas' and 'menu' in c:c['menu']['entries'][0]['target']=SCENE
 compact=lambda value:json.dumps(value,ensure_ascii=False,separators=(',',':'))
 text='{\n  "version": '+str(title['version'])+',\n  "settings": '+compact(title['settings'])+',\n  "objects": [\n'
 text+=',\n'.join('    '+compact(o) for o in title['objects'])+'\n  ]'
 for key,value in title.items():
  if key not in ('version','settings','objects'):text+=',\n  '+json.dumps(key)+': '+compact(value)
 text+='\n}\n';write(GAME/'Assets/Scenes/TitleStreet.json',text)
 print('Authored the street journey; START enters StreetJourney.')

if __name__=='__main__':author()
