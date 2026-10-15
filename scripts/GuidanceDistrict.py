"""Route, architecture and shot plans for the fourth rhetoric study.

All coordinates are game-space metres. Colliders are authored beside the
geometry they describe; the camera reads visible landmark objects, not labels.
"""
import math
from AuthorEntranceCafe import material, TEXTURES as CAFE_TEXTURES
from SunriseDistrictBake import DistrictBake, TEXTURES
from GenerateParkAdventure import component, obj, box

FOLDER='Assets/Models/GuidanceLab'
CASES=[('擬人法','広場の建物が、近づいたあなたを迎える'),
       ('反復法','同じ入口とテラスを、歩くたびに読み取る'),
       ('換喩','店の営みを頼りに、左右の広場を巡る'),
       ('漸層法','小庭から大テラスへ上り、広場へ帰る'),
       ('Anadiplosis','時計塔から温室へ、景色の起点を引き継ぐ'),
       ('Epanalepsis','入口の門へ、街を巡って帰ってくる')]


def baker(name):
    return DistrictBake(FOLDER+'/'+name,'Assets/Materials/GuidanceLab/'+name,
                        'Assets/Textures/GuidanceLab/'+name,'lab-'+name)


class City:
    def __init__(self,mode):
        self.mode=mode
        self.b=baker(f'City{mode}A')
        self.objects=[]
        self.collisions=[]
        self.plaster=material(CAFE_TEXTURES+'/Plaster.png')
        self.wood=material(CAFE_TEXTURES+'/Wood.png')
        self.paving=material(TEXTURES+'/paving.png',roughness=.94)
        self.cream=material(color=(.94,.86,.69,1))
        self.teal=material(color=(.15,.32,.34,1))
        self.orange=material(color=(.80,.39,.16,1))
        self.leaf=material(color=(.28,.43,.30,1))
        self.water=material(color=(.30,.55,.56,1),roughness=.28)
        self.glass=material(color=(.30,.51,.51,1),roughness=.34)
        self.route=[(0,10),(7,20),(7,39),(-7,53),(-7,64),(-7,80),(-7,85),(0,88),(1,90)]
        self.alternative=[(-22,16),(-22,43),(-7,53)]
        self.anchors=[(-2,7,32),(10,3,54),(0,8,87),(1,5,90)]
        self.goal=[1,5.25,90]
        self.shots=[(0,1),(4,18),(7,37),(-7,59),(-7,78),(0,88)]

    def solid(self,key,pos,size):
        self.collisions.append(obj('lab-collision-'+key,pos,size,[component('BoxCollider',center=[0,0,0],size=[1,1,1])]))

    def cube(self,pos,size,surface=None,key=None):
        self.b.cube(pos,size,surface or self.cream)
        if key:self.solid(key,pos,size)

    def building(self,key,bounds,height=9,front=True,side=True):
        x0,x1,z0,z1=bounds
        self.cube(((x0+x1)/2,height/2,(z0+z1)/2),(x1-x0-.3,height,z1-z0-.3),self.plaster)
        self.solid(key,((x0+x1)/2,height/2,(z0+z1)/2),(x1-x0,height,z1-z0))
        # A solid roof cap belongs to scenery buildings. The playable terrace
        # has its own paving and parapets at the actual collider height.
        if key!='roof':self.cube(((x0+x1)/2,height+.10,(z0+z1)/2),(x1-x0+.35,.20,z1-z0+.35),self.teal)
        # Keep architectural modules at their native proportions. Sparse upper
        # windows leave the skyline readable instead of filling every surface.
        if front:
            for y in range(0,int(height)-1,3):
                for x in range(math.ceil(x0+1),math.floor(x1),2):
                    self.b.place('q','Trim_Window' if (x-math.ceil(x0+1))%4==0 else 'Trim_Plain_3',(x,y,z0),1)
        if side:
            for x,angle in ((x0,math.pi/2),(x1,-math.pi/2)):
                for y in range(0,int(height)-1,3):
                    for i,z in enumerate(range(math.ceil(z0+1),math.floor(z1),2)):
                        self.b.place('q','Trim_Window' if i%2==0 else 'Trim_Plain_3',(x,y,z),1,angle)

    def planter(self,x,z,y=0,size=1):
        self.b.place('q','Prop_Planter_Single',(x,y,z),.48*size)
        self.b.place('k','pottedPlant',(x,y+.2*size,z),2.2*size)

    def seating(self,x,z,y=0,size=1):
        self.b.place('k','tableRound',(x,y,z),2.1*size)
        for dz,angle in ((-.95,0),(.95,math.pi)):
            self.b.place('k','chairRounded',(x,y,z+dz*size),2.2*size,angle)
        self.b.place('k','plantSmall1',(x,y+.78*size,z),.7*size)

    def awning(self,x,z,width=6,y=3.4):
        self.cube((x,y,z-.8),(width,.14,1.9),self.orange)
        self.cube((x,y-.19,z-1.7),(width,.30,.12),self.cream)
        for dx in (-width/2+.15,width/2-.15):
            self.cube((x+dx,y-.6,z-.35),(.10,1.2,.12),self.wood)

    def sign(self,x,y,z,scale=1):
        w,h=2.2*scale,1.45*scale
        self.cube((x,y,z+.10),(w+.12,h+.12,.12),self.teal)
        surface=material(CAFE_TEXTURES+'/CafeSign.png')
        vertices=[(x-w/2,y-h/2,-z),(x+w/2,y-h/2,-z),(x+w/2,y+h/2,-z),(x-w/2,y+h/2,-z)]
        import json
        self.b.groups[(self.b.zone,json.dumps(surface,sort_keys=True))].append(
            [(p,uv,(0,0,1)) for p,uv in zip(vertices,((0,0),(1,0),(1,1),(0,1)))])

    def gate(self,key,x,z,width=8,height=4.4,y=0,planters=True):
        for side in (-1,1):
            self.cube((x+side*width/2,y+height/2,z),(.46,height,.55),self.teal,key+str(side))
            self.cube((x+side*width/2,y+.18,z),(.85,.36,.95),self.cream)
        self.cube((x,y+height,z),(width+.85,.3,.70),self.cream)
        self.cube((x,y+height+.20,z),(width+1.1,.12,.95),self.orange)
        if planters:
            for dx in (-width/2-1.1,width/2+1.1):self.planter(x+dx,z,y)

    def clock(self,x,z,height=12):
        self.building('clock',(x-1.7,x+1.7,z-1.7,z+1.7),height,front=False,side=False)
        self.cube((x,height-2.2,z-1.89),(2.55,2.55,.16),self.teal)
        self.cube((x,height-2.2,z-2.0),(2.2,2.2,.06),self.cream)
        self.cube((x,height-1.80,z-2.06),(.10,.9,.04),self.teal)
        self.cube((x+.32,height-2.2,z-2.07),(.72,.10,.04),self.teal)
        for dx,dy in ((0,1),(0,-1),(1,0),(-1,0)):
            self.cube((x+dx*.88,height-2.2+dy*.88,z-2.07),(.12,.12,.04),self.orange)
        self.cube((x,height+.34,z),(4,.5,4),self.orange)

    def greenhouse(self,x,z):
        # Walk-through conservatory: no invisible box blocking its open end.
        for dx in (-3.6,3.6):
            self.cube((x+dx,1.7,z),(.16,3.4,7),self.teal,'greenhouse-side-'+str(dx))
            for dz in (-3.5,0,3.5):self.cube((x+dx,1.8,z+dz),(.22,3.6,.22),self.cream)
            for dz in (-1.8,1.8):self.planter(x+dx*.73,z+dz,size=.8)
        for dz in (-3.5,0,3.5):
            self.cube((x,3.6,z+dz),(7.5,.15,.18),self.teal)
        for dx in (-2.8,-1.4,0,1.4,2.8):
            self.cube((x+dx,3.74,z),(.12,.12,7.5),self.cream)
        self.cube((x,-.005,z),(7.3,.04,7.4),self.cream)

    def motif(self,x,z,y=0):
        self.gate('echo-'+str(z),x,z,8,4.4,y)
        # A whole spatial phrase returns: portal, low reflecting basin, two
        # seats and a warm cup sign. It is a repeated composition, not the
        # claim that two different locations are the same physical place.
        self.cube((x+5.8,y+.22,z+2),(2.4,.44,3),self.cream)
        self.cube((x+5.8,y+.45,z+2),(2.05,.04,2.65),self.water)
        self.seating(x-5.6,z+2,y)
        self.sign(x,y+3.6,z-.45,.7)

    def welcome(self,key,x,z):
        self.gate(key,x,z,9,4.6,planters=False)
        self.cube((x,6.55,z),(9.3,3.4,.8),self.plaster,key+'-upper')
        self.cube((x,8.35,z),(10,.25,1.25),self.orange)
        # The two eyes and opening mouth belong to the building itself.
        for i,dx in enumerate((-1.8,1.8)):
            self.b.place('q','Trim_Window',(x+dx,4.7,z-.48),.82)
            eye=box(key+'-eye-'+str(i),(x+dx,6,z-.65),(1.15,1.05,.10),'White')
            self.objects.append(eye)
            pupil=box(key+'-pupil-'+str(i),(x+dx,6,z-.73),(.36,.64,.08),'Eye')
            pupil['components'].append(component('Script',behaviour='GuidanceWelcome',parameters={'kind':2},
                data={'target':{'kind':'reference','value':'park-player'}}))
            self.objects.append(pupil)
            lid=box(key+'-lid-'+str(i),(x+dx,6.3,z-.84),(1.28,.55,.14),'Wood')
            lid['components'].append(component('Script',behaviour='GuidanceWelcome',parameters={'kind':0},
                data={'target':{'kind':'reference','value':'park-player'}}))
            self.objects.append(lid)
        pivot=key+'-awning-pivot'
        self.objects.append(obj(pivot,(x,4.45,z-.5),components=[component('Script',behaviour='GuidanceWelcome',parameters={'kind':1},data={'target':{'kind':'reference','value':'park-player'}})]))
        awning=box(key+'-awning',(0,0,-.95),(8.5,.14,1.9),'Gate');awning['parent']=pivot;self.objects.append(awning)

    def terrace(self,key,x,z,width,length,height):
        self.cube((x,height/2,z+length/2),(width,height,length),self.plaster,key)
        self.cube((x,height+.02,z+length/2),(width,.04,length),self.paving)
        n=round(height/.28)
        for i in range(n):
            h=.28*(i+1)
            self.cube((x,h/2,z-n+i+.5),(4,h,1.03),self.cream,key+'-up-'+str(i))
        self.gate(key+'-frame',x,z+2,width-1,3.2+height*.55,height,False)
        for side in (-1,1):
            self.planter(x+side*(width/2-1),z+length-1,height,size=.7+height*.2)
        self.seating(x+width/2-2,z+length/2,height)

    def build(self):
        b=self.b
        self.cube((0,-.045,45),(52,.08,114),self.paving)
        self.solid('floor',(0,-.6,45),(50,1.2,114))
        # Shared world boundary only. Each study authors its own urban plan.
        for side in (-1,1):
            self.cube((side*25,.45,45),(.3,.9,114),self.cream)
            self.solid('edge-'+str(side),(side*25,.7,45),(.3,1.4,114))
            for z in (6,51,90):
                self.building('skyline-'+str(side)+'-'+str(z),(side*30-4,side*30+4,z-8,z+8),9,side=False)
        for key,z in (('back',-12),('end',102)):
            self.cube((0,.45,z),(50,.9,.3),self.cream)
            self.solid(key,(0,.7,z),(50,1.4,.3))
        self.alternative=[(-22,12),(-22,58),(-7,58),(-7,64)] if self.mode==0 else []
        tail=[(-7,64),(-7,80),(-7,85),(0,88),(1,90)]
        if self.mode==0:
            self.family='応答する商店街'
            for i,(x,z) in enumerate(((0,20),(9,42),(-5,61))):
                b.zone=i
                self.welcome('host-'+str(i),x,z)
                self.building('host-wing-'+str(i),(x-12,x-5,z,z+8),6)
                self.seating(x+7,z+4)
                self.planter(x+7,z-2)
            self.route=[(0,10),(0,24),(9,37),(9,46),(-5,57),(-5,63)]+tail
            self.anchors=[[0,6,20],[9,6,42],[-5,6,61],[1,7,90]]
            self.renderShots=[[0,1.05,9],[0,1.05,16],[9,1.05,35],[-5,1.05,55],[0,5.25,88]]
        elif self.mode==1:
            self.family='三つの反復中庭'
            for i,z in enumerate((17,39,61)):
                b.zone=i
                self.building('repeat-west-'+str(i),(-17,-10,z-4,z+7),6)
                self.building('repeat-east-'+str(i),(10,17,z-4,z+7),6)
                self.gate('repeat-'+str(i),0,z,8,4.4)
                self.seating(-7,z+4)
                self.planter(7,z+4)
                self.cube((0,.008,z+3),(18,.02,9),self.cream)
            self.route=[(0,10),(0,24),(0,46),(0,62)]+tail
            self.anchors=[[0,3.5,17],[0,3.5,39],[0,3.5,61],[1,7,90]]
            self.renderShots=[[0,1.05,9],[0,1.05,25],[0,1.05,47],[0,5.25,88]]
        elif self.mode==2:
            self.family='カフェの営みをたどる路地'
            self.building('service-west',(-20,-13,16,35),9)
            self.building('service-east',(16,24,28,48),9)
            self.building('roastery',(-21,-11,48,67),6)
            for i,(x,z) in enumerate(((-7,20),(9,39),(-5,59))):
                b.zone=i
                # A functioning outdoor service bay, not an icon on a dead wall.
                self.awning(x,z,8)
                self.cube((x,1,z+1),(4,2,.9),self.wood,'counter-'+str(i))
                self.b.place('k','kitchenCoffeeMachine',(x+1,2,z+1),1.7)
                self.seating(x+5,z-2)
                self.seating(x+5,z+3)
                self.planter(x-5,z+2)
                for dx in (-3.8,3.8):self.cube((x+dx,1.65,z+1),(.16,3.3,.16),self.teal)
                self.cube((x,.01,z),(9,.03,8),self.cream)
            self.route=[(0,10),(-3,18),(0,27),(5,37),(5,45),(-1,57),(-1,63)]+tail
            self.anchors=[[-7,2.8,20],[9,2.8,39],[-5,2.8,59],[1,7,90]]
            self.renderShots=[[-1,1.05,12],[-2,1.05,24],[4,1.05,33],[-1,1.05,53],[0,5.25,88]]
        elif self.mode==3:
            self.family='小庭から大テラスへ上る漸層'
            for i,(x,z,w,l,h) in enumerate(((0,18,8,6,.84),(0,38,16,8,1.68),(0,62,28,8,2.52))):
                b.zone=i
                self.terrace('terrace-'+str(i),x,z,w,l,h)
                if i<2:
                    for j in range(round(h/.28)-1):
                        down=h-.28*(j+1)
                        self.cube((x,down/2,z+l+j+.5),(4,down,1.03),self.cream,'descent-'+str(i)+'-'+str(j))
            self.building('low-neighbour',(-17,-11,13,25),3)
            self.building('middle-neighbour',(17,23,32,47),6)
            self.building('high-neighbour',(-24,-18,54,76),12)
            self.cube((0,1.26,72),(4,2.52,4),self.plaster,'terrace-link')
            for i in range(6):
                h=2.8+i*.28
                self.cube((0,h/2,74.5+i),(4,h,1.03),self.cream,'upper-stair-'+str(i))
            self.route=[(0,10),(0,21),(0,28),(0,41),(0,53),(0,65),(0,73),(0,81),(0,88),(1,90)]
            self.anchors=[[0,3.5,20],[0,5,40],[0,7,64],[1,8,90]]
            self.renderShots=[[0,1.05,10],[0,1.7,23],[0,2.6,43],[0,3.3,67],[0,5.25,88]]
        elif self.mode==4:
            self.family='時計塔・温室・屋上の連鎖'
            self.building('corner',(-19,-6,23,38),9)
            self.building('arcade',(16,24,10,42),9)
            self.awning(-11,23,7)
            self.clock(0,38,12)
            b.zone=1
            self.greenhouse(10,54)
            self.gate('conservatory-entry',10,49.5,8,3.9,planters=False)
            self.cube((10,.01,47),(9,.03,5),self.cream)
            self.building('garden-wall',(-24,-19,55,77),6)
            self.seating(20,55)
            self.planter(-15,53)
            self.route=[(0,10),(7,23),(7,37),(10,46),(10,56),(0,62)]+tail
            self.anchors=[[0,10,38],[10,3.5,54],[0,8,87],[1,7,90]]
            self.renderShots=[[0,1.05,1],[7,1.05,29],[7,1.05,37],[10,1.05,54],[-7,4.7,78],[0,5.25,88]]
        else:
            self.family='冒頭の景色へ帰る首尾反復'
            self.motif(1,16)
            self.building('entry-frame-left',(-19,-10,11,24),6)
            self.building('entry-frame-right',(12,23,11,24),6)
            b.zone=1
            # The intervening scene deliberately has a different silhouette,
            # material rhythm and use: a workshop garden instead of a portal.
            self.building('workshop-left',(-22,-12,34,58),12)
            self.building('workshop-right',(13,23,38,66),9)
            self.cube((0,.28,44),(11,.56,12),self.cream,'middle-garden')
            self.cube((0,.58,44),(10.7,.04,11.7),self.leaf)
            for x in (-3,0,3):
                for z in (41,47):self.planter(x,z,.6,size=1.4)
            for x in (-10,10):self.b.place('q','Prop_ACUnit',(x,.05,57),2)
            b.zone=2
            self.motif(1,90,4.24)
            self.route=[(0,10),(1,23),(9,32),(9,54),(-7,62)]+tail
            self.anchors=[[1,3.7,16],[9,2,39],[-7,2,62],[1,7.8,90]]
            self.renderShots=[[1,1.05,8],[9,1.05,36],[-7,1.05,60],[1,5.25,82],[0,5.25,88]]
        # Destination stays in the same world and uses the same movement rules;
        # the route and the way the player reads it are specific to each figure.
        self.b.zone=2
        # Physically walkable roof. Steps and the building are baked alongside
        # their exact colliders; no decorative stair floating above a ramp.
        self.building('roof',(-9,9,80,96),4.2,front=False,side=False)
        self.cube((0,4.215,88),(18,.04,16),self.paving)
        for i in range(15):
            h=.28*(i+1);z=65.5+i
            self.cube((-7,h/2,z),(4.2,h,1.03),self.cream,'step-'+str(i))
            self.cube((-7,h+.01,z-.44),(4.2,.025,.10),self.wood)
        for x in (-9.0,-5.0):
            for i in (0,4,8,12):
                self.cube((x,.28*(i+1)+.44,65.5+i),(.12,.88,.12),self.teal)
        for x in (-8.9,8.9):
            self.cube((x,4.55,88),(.22,.65,16),self.cream,'roof-edge-'+str(x))
        self.cube((0,4.55,95.9),(18,.65,.22),self.cream,'roof-back')
        # Front parapet leaves the stair landing and a broad arrival open.
        if self.mode!=3:self.cube((3,4.55,80.1),(12,.65,.22),self.cream,'roof-front')
        if self.mode!=5:
            self.gate('roof-pergola',1,91,10,3.6,4.24,False)
            for z in (87,89,93):self.cube((1,8.04,z),(11,.1,.16),self.wood)
            for x in (-4,6):self.seating(x,87,4.25)
        for x,z in ((-8,94),(7.8,94),(7.8,82)):self.planter(x,z,4.25)
        # The same destination is previewed at the beginning and confirmed on
        # arrival, rather than hiding every sign behind the central building.
        self.cube((7,9.2,93),(.22,2.7,.22),self.teal)
        self.sign(7,10.3,92.8,1.3)
        self.objects.append(obj('lab-preview',(7,10.3,92.8)))
        self.sign(1,6.1,90.6,.68)

        for i,p in enumerate(self.anchors):self.objects.append(obj('lab-focus-'+str(i),p))
        self.objects+=self.collisions
        return self


def build_city(mode):return City(mode).build()
