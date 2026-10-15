"""A connected waterside neighbourhood, authored in metres.

Route metadata is for offline validation only; none of it is rendered as guidance.
"""
import math
from AuthorSunriseStreet import model
from GenerateParkAdventure import component, coin, character, obj, box
from AuthorEntranceCafe import material


def build_town(t):
 b=t.b
 t.spawn=[-14,1.05,0]
 t.floors=[]
 def ground(key,x0,x1,z0,z1,h=0,surface=None,foundation=None):
  t.floor(key,x0,x1,z0,z1,h,surface,foundation)
  t.floors.append(key)
 def bench(key,x,z,y=0,width=2.8):
  t.cube((x,y+.38,z),(width,.76,.6),t.wood,key)
  t.cube((x,y+.87,z+.22),(width,.65,.16),t.wood)
 def bed(key,x,z,w,d,y):
  t.cube((x,y+.35,z),(w,.7,d),t.cream,key)
  t.cube((x,y+.71,z),(w-.22,.02,d-.22),t.leaf)
 def crate(key,x,z,y):
  b.place('p','wooden_crate_02',(x,y,z),1.15,.08)
  t.solid(key,(x,y+.267,z),(.65,.534,1.34))
 # A cafe is an island in a small neighbourhood square. Walking straight
 # ahead meets its occupied facade; both sides visibly wrap around it.
 ground('arrival-square',-22,0,-6,16)
 ground('quayside',-22,0,16,28)
 t.building('entry-west',(-30,-22,-8,28),9)
 t.building('entry-east',(0,8,-8,13),9)
 t.building('cafe',(-16,-8,8,15),5)
 t.awning(-12,8,7,3.6)
 t.solid('cafe-weather-roof',(-12,3.49,7.1),(7,.67,1.95))
 t.seating(-18.8,6.2,0,.85)
 t.seating(-5.2,10,0,.85)
 t.planter(-20.5,13);t.planter(-2.2,5)
 t.rail('arrival-back',-11,-6,22,.3,1)
 t.rail('quay-north',-11,28,22,.3,1)
 t.rail('quay-west',-22,22,.3,12,1)
 bench('quay-bench',-16,26)
 # The canal runs across the town, with two real banks and a broad break in
 # the coping. The facing market terrace is the first attainable destination.
 t.cube((1,-.8,23),(2,.08,25),t.water)
 ground('market-apron',2,20,16,32,.56)
 t.bank('canal-west',-.15,16.2,27.8,(19.6,26.4))
 t.bank('canal-east',2.15,16.2,31.8,(19.6,26.4),.56)
 t.rail('market-south',11,16,18,.3,.9,.56)
 t.building('east-shop',(20,28,16,45),9)
 t.awning(15.4,31,5.5,4.0,.56)
 t.solid('market-weather-roof',(15.4,3.89,30.1),(5.5,.67,1.95))
 t.cube((15.4,1.1,30.4),(4.7,1.08,.8),t.wood,'market-counter')
 b.place('k','kitchenCoffeeMachine',(16.4,1.64,30.4),1.7)
 t.seating(4.7,18.9,.56,.85)
 crate('market-stock',18.5,28,.59)
 t.planter(18.6,18.1,.56)
 # An offset market hall separates genuinely different public and service
 # streets. Both terminate in the same garden; neither is a disguised dead end.
 b.zone=1
 ground('market-streets',-6,20,32,48,.56)
 t.building('market-hall',(3,10,33,40),5)
 t.building('garden-west',(-14,-6,32,48),7)
 # The public arcade repeats a construction, not an arbitrary cue object.
 for side,x in (('west',11.2),('east',19)):
  t.cube((x,4.5,38),(.42,.2,12),t.cream)
  for i,z in enumerate((32.5,37.5,42.5)):
   t.cube((x,2.56,z),(.32,4,.4),t.teal,'arcade-'+side+'-'+str(i))
   t.cube((x,.76,z),(.68,.4,.72),t.cream)
   t.cube((x,4.35,z),(.65,.15,.7),t.cream)
 # Lower timber storage bays contrast with the tall, uncluttered arcade.
 # The shelter covers stock, leaving the player/camera aisle open.
 t.cube((-4.7,3.05,38),(2,.16,9),t.wood)
 for i,z in enumerate((34,38,42)):
  for j,x in enumerate((-5.5,-3.9)):
   t.cube((x,1.82,z),(.14,2.46,.14),t.wood,'warehouse-post-'+str(i)+'-'+str(j))
  crate('warehouse-stock-'+str(i),-4.7,z,.59)
 t.cube((-2,1.01,38),(1,.9,2),t.wood,'packing-table')
 # A lateral street reveals the stair building before entering the court.
 t.rail('market-west-return',-6,46,.3,4,.8,.56)
 t.planter(5,44.8,.56);bench('market-bench',-2,46.5,.56)
 ground('garden-court',-6,20,48,57,.56,t.gardenstone)
 t.building('garden-east',(20,28,45,68),10)
 t.rail('court-west',-6,52.5,.3,9,.85,.56)
 t.rail('court-north',2.5,57,19,.35,.85,.56)
 t.rail('court-north-east',18.8,57,4.4,.35,.85,.56)
 bed('court-planting',2.5,52,4,2.7,.56)
 t.seating(-2,54.5,.56,.85)
 # Sheltered cafe -> open trellis court -> open sky roof garden. The repeated
 # use is resting, with a progressively more open enclosure.
 for x in (-4.8,.8):
  for z in (53.2,55.9):
   t.cube((x,2.15,z),(.16,3.12,.16),t.wood,'trellis-'+str(x)+'-'+str(z))
  t.cube((x,3.78,54.55),(.18,.14,3),t.wood)
 for i in range(6):t.cube((-2,3.9,53.2+i*.54),(5.9,.1,.13),t.wood)
 t.planter(7.3,55.5,.56)
 # A working stair shutter: the attached gear and drive shaft describe how
 # the barrier operates without new UI, labels or floating symbols.
 lever=model('journey-lever',(17,1.86,54),(1.7,1.7,1.7),'Assets/Models/Title/Factory/cog-a.obj',(math.pi/2,0,0))
 lever['components'].append(component('Script',behaviour='StreetGear',parameters={}))
 t.objects.append(lever)
 t.cube((17,1.0,54),(.38,.9,.4),t.teal)
 t.cube((17,2.45,54),(.16,1,.16),t.teal)
 t.cube((17,2.9,55.5),(.16,.16,3.2),t.teal)
 t.cube((15.5,2.9,57),(3.1,.16,.16),t.teal)
 t.objects.append(box('journey-shutter',(14,2.1,57),(4,3.1,.24),'Gate',True))
 for x in (11.8,16.2):t.cube((x,2.3,57),(.35,3.5,.5),t.teal)
 t.cube((14,4.1,57),(4.8,.3,.7),t.orange)
 b.zone=2
 for i in range(12):
  h=.56+.28*(i+1)
  t.cube((14,h/2,57.5+i),(4,h,1),t.cream,'roof-step-'+str(i))
  for x in (11.75,16.25):
   t.cube((x,h/2+.3,57.5+i),(.26,h+.6,1),t.plaster,'stair-cheek-'+str(i)+'-'+str(x))
   t.cube((x,h+.64,57.5+i),(.34,.08,1),t.teal)
 roof=3.92
 ground('east-roof',4,20,69,82,roof,t.rooftile,t.plaster)
 ground('west-roof',-16,1.4,69,82,roof,t.rooftile,t.plaster)
 ground('catch-balcony',1.4,4,70,79,2.8,t.orange,t.plaster)
 ground('clock-lookout',-16,-8,82,89,roof,t.rooftile,t.plaster)
 t.bank('roof-east',4.12,69.15,81.85,(70.1,78.9),roof)
 t.bank('roof-west',1.28,69.15,81.85,(70.1,78.9),roof)
 for key,x,z,w,d in (('east-end',20,75.5,.3,13),('east-north',12,82,16,.3),
                    ('east-south-left',8,69,8,.3),('east-south-right',18,69,4,.3),
                    ('west-end',-16,75.5,.3,13),('west-south',-7.3,69,17.4,.3),
                    ('west-north',-3.3,82,9.4,.3),('lookout-left',-16,85.5,.3,7),
                    ('lookout-right',-8,85.5,.3,7),('lookout-end',-12,89,8,.3)):
  t.rail(key,x,z,w,d,.85,roof)
 # The roof is a garden with a second loop, not a line of platforms.
 bed('roof-east-bed',10,79,5,2,roof)
 bed('roof-west-bed',-5.5,76,5,2.5,roof)
 t.seating(17,78,roof,.85);t.planter(18.5,71,roof)
 bench('roof-garden-bench',-4,81.25,roof)
 t.gate('lookout-entrance',-12,82,7,3.5,roof,False)
 bench('clock-bench',-12,87.3,roof,4)
 t.goal=[-12,roof+.85,84]
 # There is exactly one clock, attached to the final accessible lookout.
 t.clock(-19,85,13)
 t.cube((-17.5,.16,85),(3,7.52,4),t.plaster)
 for i,(x,z,h) in enumerate(((-32,35,11),(35,28,12),(-27,65,10),(32,84,14),(8,99,9))):
  t.building('distant-'+str(i),(x-4,x+4,z-4,z+4),h,False,False)
 t.cube((0,-4,42),(110,.8,150),material(color=(.34,.39,.34,1)))
 # Everyday details belong to the places they serve; discoveries stay in
 # pockets, never arranged as a trail leading to the exit.
 b.zone=0
 for x,z,y in ((-18,1,.04),(-4,22,.04),(12,27,.6),(15,45,.6)):
  b.place('q','Prop_ManholeCover',(x,y,z),1)
 for x,z,y in ((-.55,18,.03),(2.55,28,.59),(19.5,38,.59)):
  b.place('q','Prop_Drain',(x,y,z),1.2)
 for x,z in ((-27,18),(30,52),(-23,72)):
  b.place('n','tree_oak',(x,-3.5,z),6)
  t.cube((x,-3.8,z),(3,.6,3),t.cream)
 positions=[[-3,1.1,9],[-19,1.1,24],[4.5,1.65,19],[-1,1.65,36],
            [-3,1.65,50],[18,1.65,50],[17,4.92,75],[-1,4.92,80]]
 for i,p in enumerate(positions):
  c=coin('stage-coin-'+str(i),p);c['components']=[x for x in c['components'] if x['type']!='Script'];t.objects.append(c)
 for i,(x,z,y) in enumerate(((9,25,1.26),(15,42,1.26),(10,52,1.26))):
  e=character('stage-enemy-'+str(i),(x,y,z),'Enemy','ParkEnemy')
  e[0]['components'][-1]['behaviour']='SunriseEnemy';t.objects+=e
 t.objects.append(obj('stage-checkpoint',(8,1.26,49)))
 t.objects+=t.collisions
 t.route=[[-18.5,2],[-18.5,18],[-15,23],[-1,23],[3,23],[10,26],[10,31.8],
          [15,31.8],[15,43.5],[12,46],[8,49],[8,53],[17,54],
          [14,54],[14,66],[14,74],[5,74],[0,74],[-11.5,71.5],[-12,80],[-12,84]]
 t.jumps=[dict(start=[-1,23],end=[3,23]),dict(start=[5,74],end=[0,74])]
 t.detour=[[10,32.4],[0,32.4],[0,43.5],[8,46],[12,46]]
 t.entry_loop=[[-18.5,2],[-3,2],[-3,17],[-18.5,18]]
 t.roof_loop=[[-11.5,71.5],[-12,80],[-1,80],[-1,71.5],[-11.5,71.5]]
 t.shots=[[-14,0],[-18.5,18],[-1,23],[10,31.8],[15,43.5],[8,49],[14,66],[5,74],[-12,80]]
 return t
