"""Author the street stage and its START connection."""
import copy,json,math
from StreetDistrict import StreetDistrict
from SunriseDistrictBake import DistrictBake
from AuthorSunriseStreet import GAME,model,script
from GenerateParkAdventure import obj,component,coin,character,write,box

FOLDER='Assets/Models/StreetJourney'
SCENE='Assets/Scenes/StreetJourney.json'

class Journey(StreetDistrict):
 def __init__(self):
  super().__init__()
  self.b=DistrictBake(FOLDER,'Assets/Materials/StreetJourney','Assets/Textures/StreetJourney','journey')
 def floor(self,key,x0,x1,z0,z1,h=0):
  self.cube(((x0+x1)/2,h-.35,(z0+z1)/2),(x1-x0,.7,z1-z0),self.plaster,key)
  self.cube(((x0+x1)/2,h+.015,(z0+z1)/2),(x1-x0,.03,z1-z0),self.paving)
 def rail(self,key,x,z,w,d,h=.85,y=0):
  self.cube((x,y+h/2,z),(w,h,d),self.cream,key)
  self.cube((x,y+h+.03,z),(w+.05,.07,d+.05),self.teal)
 def build(self):
  b=self.b
  self.spawn=[-14,1.05,0];self.goal=[-3,4.21,75]
  # Entry street: full-size architecture on both sides, an open bend ahead.
  self.floor('entry-floor',-18,-10,-7,18)
  self.building('entry-west',(-26,-18,-8,16),10)
  self.building('entry-east',(-10,-2,-8,14),8)
  self.rail('entry-back',-14,-7,8,.3,1.4)
  self.awning(-14,14,6,4.1)
  self.planter(-17,11);self.planter(-11,14)
  # Cafe apron separated by a real narrow drainage channel: first jump.
  self.floor('cafe-west-floor',-18,-8,17,30)
  self.floor('cafe-east-floor',-6,9,17,31,.56)
  self.cube((-7,-.9,23),(2,.08,14),self.water)
  for x in (-8.15,-5.85):self.cube((x,.08 if x<-7 else .64,23),(.22,.12,12.8),self.orange)
  self.rail('cafe-west-edge',-18,23.5,.3,13)
  self.rail('cafe-south',1.5,17,15,.3,.9,.56)
  self.building('cafe',(-25,-7,30,44),9)
  self.awning(-14,30,8,3.7);self.sign(-14,3.05,29.7,.9)
  self.awning(-3.7,29,4,4.0)
  self.cube((-3.7,1.2,29),(4,1.28,.8),self.wood,'cafe-counter')
  b.place('k','kitchenCoffeeMachine',(-2.7,1.84,29),1.7)
  self.seating(5.4,22,.56);self.seating(-15.5,26)
  self.planter(-17,29);self.planter(7.5,28,.56)
  # Two entrances around a kiosk. Both are visible before the choice.
  b.zone=1
  self.floor('fork-floor',-6,9,30,48,.56)
  self.building('kiosk',(0,2,34,41),4)
  self.awning(-3.4,35,4,3.6);self.awning(5.4,35,4,3.6)
  self.sign(-3.4,2.9,34.65,.5)
  self.cube((-5.4,1.1,40),(1,.9,3),self.wood,'service-counter')
  b.place('k','kitchenCoffeeMachine',(-5.4,1.55,40),1.4)
  self.planter(-5,44,.56);self.planter(8,42,.56)
  self.building('east-shop',(9,18,30,47),10)
  # Backyard is a compact encounter and a visible route up the building.
  self.floor('backyard-floor',-6,21,47,57,.56)
  self.building('garden-west',(-14,-6,45,59),6)
  self.building('garden-east',(21,29,47,68),11)
  self.rail('garden-north-left',2.5,57,19,.4,2.2,.56)
  self.rail('garden-north-right',19,57,4,.4,2.2,.56)
  self.rail('garden-east-rail',21,52,.3,10,.9,.56)
  self.seating(-2,52,.56)
  for x in (-4,3,19):self.planter(x,55,.56,1.1)
  self.cube((4,.9,49),(4,.68,3),self.cream,'garden-bed')
  self.cube((4,1.25,49),(3.8,.04,2.8),self.leaf)
  # A single attack-operated shutter connects the action to a new place.
  lever=model('journey-lever',(17,1.86,54),(1.7,1.7,1.7),'Assets/Models/Title/Factory/cog-a.obj',(math.pi/2,0,0))
  lever['components'].append(component('Script',behaviour='StreetGear',parameters={}))
  self.objects.append(lever)
  self.objects.append(box('journey-shutter',(14,2.1,57),(4,3.1,.24),'Gate',True))
  for x in (11.8,16.2):self.cube((x,2.3,57),(.35,3.5,.5),self.teal)
  self.cube((14,4.1,57),(4.8,.3,.7),self.orange)
  # Climb the back of the cafe block. Twelve shallow physical steps.
  b.zone=2
  for i in range(12):
   h=.56+.28*(i+1)
   self.cube((14,h/2,57.5+i),(4,h,1.03),self.cream,'roof-step-'+str(i))
  roof=.56+3.36
  self.floor('east-roof',9,20,69,78,roof)
  self.floor('west-roof',-7,6.4,69,79,roof)
  # The roof surface is at the collider height; visible walls support it.
  for key,x,w,z,d in [('east-roof-body',14.5,11,73.5,9),('west-roof-body',-.3,13.4,74,10)]:
   self.cube((x,(roof-.7)/2,z),(w,roof-.7,d),self.plaster,key)
  for key,x,z,w,d in [('east-north',14.5,78,11,.3),('east-side',20,73.5,.3,9),('west-north',-.3,79,13.4,.3),('west-side',-7,74,.3,10)]:self.rail(key,x,z,w,d,.85,roof)
  # Low awning below the gap lets a missed jump land visibly rather than in void.
  self.floor('catch-balcony',6.4,9,70,77,2.8)
  self.cube((7.7,2.83,73.5),(2.6,.06,7),self.orange)
  self.awning(14,77,7,roof+3.0);self.seating(17,74,roof)
  self.planter(18.7,70,roof);self.planter(-5,70,roof)
  self.gate('roof-exit',-3,76,6,3.5,roof,False)
  self.sign(-3,roof+2.7,75.6,.55)
  self.goal=[-3,roof+.85,77]
  # Beyond the reachable roof, scenery sells a larger continuing city.
  self.clock(-16,87,14)
  for i,(x,z,h) in enumerate(((-30,28,12),(30,24,11),(-24,65,12),(30,82,14),(3,91,9))):
   self.building('distant-'+str(i),(x-5,x+5,z-5,z+5),h)
  self.floor('distant-street',-20,20,84,96,-.5)
  positions=[[-14,1.1,10],[-7,1.5,23],[3,1.65,25],[-3.8,1.65,40],[5.5,1.65,44],[14,2.8,62],[11,roof+1.0,73],[4,roof+1.0,73]]
  for i,p in enumerate(positions):
   c=coin('stage-coin-'+str(i),p);c['components']=[x for x in c['components'] if x['type']!='Script'];self.objects.append(c)
  for i,(x,z,y) in enumerate(((-12,22,.7),(3.5,26,1.26),(9,52,1.26))):
   e=character('stage-enemy-'+str(i),(x,y,z),'Enemy','ParkEnemy');e[0]['components'][-1]['behaviour']='SunriseEnemy';self.objects+=e
  self.objects.append(obj('stage-checkpoint',(-1,1.26,48)))
  self.objects+=self.collisions
  self.route=[[-14,10],[-14,22],[-9,23],[-5,23],[3,25],[5,32],[5,44],[-1,44],[-1,48],[-1,52],[9,53],[17,54],[14,54],[14,66],[14,73],[10,73],[5,73],[-3,73],[-3,77]]
  self.shots=[[-14,5],[-12,22],[3,31],[2,48],[14,65],[12,73],[-3,75]]
  return self

def assemble(city,geometry):
 old=json.loads((GAME/'Assets/Scenes/SunriseBlock.json').read_text(encoding='utf-8'))
 keep={'block-sky','block-directionallight','music','park-player','hero-rig','hero-body','hero-backpack','hero-backpack-buckle','park-puff','park-ui'}
 objects=[copy.deepcopy(o) for o in old['objects'] if o['id'] in keep or o['id'].startswith(('park-player-','audio-'))]
 objects+=copy.deepcopy(geometry)+copy.deepcopy(city.objects)
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
 objects.append(obj('park-camera',[-14.4,3.3,-6.2],rotation=[.18,0,0],components=[
  component('Camera',verticalFov=53,nearClip=.1,farClip=150,referenceAspect=16/9,preserveHorizontal=True),camera]))
 objects.append(obj('street-destination',city.goal))
 return dict(version=4,settings=copy.deepcopy(old['settings']),objects=objects)

def author():
 city=Journey().build();geometry=city.b.finish();scene=assemble(city,geometry)
 scene['objects'].append(obj('journey-controller',(0,0,0),components=[component('Script',behaviour='StreetJourney',parameters={})]))
 write(GAME/SCENE,scene)
 write(GAME/(FOLDER+'/Route.json'),dict(version=1,scene=SCENE,route=city.route,shots=city.shots,
  jumps=[dict(start=[-9,23],end=[-5,23]),dict(start=[10,73],end=[5,73])],
  detour=[[5,32],[-3.5,32],[-3.5,40],[-3.5,44],[5,44]],
  stages=['入口の路地','カフェ前の跳び越し','店脇の分岐','裏庭の仕掛け','屋上の跳び移り','次の街区を望む出口']))
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
