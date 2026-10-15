"""Six complete plaza adventures: branching objectives, play and return routes."""
import math
from GuidanceDistrict import City
from GenerateParkAdventure import obj, component, box, ball, coin, character
from AuthorSunriseStreet import model


class Adventure(City):
    def bell(self,index,x,z,y=0):
        self.bells.append([x,y+1.25,z])
        self.cube((x,y+.06,z),(2,.12,2),self.wood)
        for dx in (-.8,.8):self.cube((x+dx,y+.95,z+.2),(.14,1.9,.18),self.teal)
        self.cube((x,y+1.95,z+.2),(1.9,.18,.3),self.cream)
        bell=model('stage-bell-'+str(index),(x,y+1.3,z),(1.8,1.8,1.8),
                   'Assets/Models/Title/Factory/cog-a.obj',(math.pi/2,0,0))
        bell['components'].append(component('Script',behaviour='GuidanceBell',parameters={}))
        self.objects.append(bell)
        self.objects.append(ball('bell-seal-'+str(index),(x,y+.2,z),(1.2,.16,1.2),'Switch'))

    def exit(self,x,z,y=0,reverse=False):
        self.goal=[x,y+.85,z]
        front=z+(2 if reverse else -2)
        if self.mode!=5:self.gate('exit',x,front,8,3.9,y,False)
        # A real alcove with one operable entrance; its two missing seals are
        # visible before entry. Reaching the back of the world is not a goal.
        for side in (-1,1):
            self.cube((x+side*4.3,y+1.6,z),(.4,3.2,6),self.plaster,'exit-side-'+str(side))
        back=z+(-3 if reverse else 3)
        self.cube((x,y+1.6,back),(8.6,3.2,.4),self.plaster,'exit-back')
        door=box('stage-exit-lock',(x,y+1.45,front),(4.4,2.9,.24),'Gate',True)
        self.objects.append(door)
        for i,dx in enumerate((-1.1,1.1)):
            self.objects.append(ball('stage-lock-token-'+str(i),(x+dx,y+3.1,front-.2),(.48,.48,.18),'Wood'))
            self.objects.append(ball('stage-lit-token-'+str(i),(x+dx,y+3.1,front+.04),(.50,.50,.18),'Switch'))
        if self.mode!=5:self.sign(x,y+3.7,front-.22,.65)
        self.objects.append(obj('lab-preview',(x,y+3.7,front)))

    def optional_roof(self,x,z):
        self.cube((x,.7,z),(10,1.4,8),self.plaster,'optional-roof')
        self.cube((x,1.42,z),(10,.04,8),self.paving)
        for i in range(5):
            h=.28*(i+1)
            self.cube((x,h/2,z-8.5+i),(3.5,h,1.03),self.cream,'optional-step-'+str(i))
        self.cube((x+8,.7,z),(2.4,1.4,3),self.plaster,'jump-ledge')
        self.cube((x+8,1.42,z),(2.4,.04,3),self.paving)
        self.planter(x-3,z+2,1.44)
        self.seating(x-2,z,1.44)
        self.coin_positions.extend([[x,2.25,z],[x+8,2.25,z]])
        self.optional_route=[[x,z-10],[x,z],[x+3.5,z]]
        self.jump_target=[x+8,z]

    def build(self):
        self.bells=[];self.coin_positions=[];b=self.b
        self.cube((0,-.045,28),(72,.08,76),self.paving)
        self.solid('floor',(0,-.6,28),(70,1.2,76))
        for side in (-1,1):
            self.cube((side*35,.45,28),(.3,.9,76),self.cream)
            self.solid('edge-'+str(side),(side*35,.8,28),(.3,1.6,76))
            for z in (3,35,66):
                self.building('skyline-'+str(side)+'-'+str(z),(side*40-4,side*40+4,z-7,z+7),9,side=False)
        for key,z in (('back',-10),('end',66)):
            self.cube((0,.45,z),(70,.9,.3),self.cream)
            self.solid(key,(0,.8,z),(70,1.6,.3))
        # Street corners frame the plaza at human scale instead of leaving a
        # tiled testing plane exposed all the way to the world boundary.
        self.building('entry-west',(-34,-21,-4,9),8)
        self.building('entry-east',(21,34,-4,9),8)
        self.building('north-west',(-18,-5,61,66),8)
        self.building('north-east',(5,18,61,66),8)
        for side in (-1,1):
            for z in (13,51):
                self.planter(side*30,z,size=1.5)
                self.seating(side*30,z+3)
        # An inhabited central square, with three attractive approaches.
        # This low island creates circulation, not a corridor toward an exit.
        if self.mode not in (3,5):
            self.cube((0,.32,25),(9,.64,7),self.cream,'plaza-garden')
            self.cube((0,.66,25),(8.7,.04,6.7),self.leaf)
            for x in (-2.6,0,2.6):self.planter(x,25,.68,size=.85)
        self.spawn=[0,1.05,1]
        if self.mode==0:
            self.family='鐘を鳴らす迎えの広場'
            for i,(x,z) in enumerate(((-15,30),(15,44))):
                b.zone=i;self.welcome('host-'+str(i),x,z)
                self.building('host-wing-'+str(i),(x-11,x-5,z,z+7),6)
                self.seating(x+6,z+2)
            self.building('quiet-workshop',(-3,5,49,59),6)
            self.bell(0,-15,28);self.bell(1,15,42)
            self.exit(-25,23)
            self.route=[[0,12],[-15,19],[-15,28],[-7,36],[15,36],[15,42],[9,36],[-9,36],[-9,17],[-25,17],[-25,23]]
            self.reverse_route=[[0,12],[10,18],[15,35],[15,42],[9,36],[-7,36],[-15,28],[-15,19],[-25,17],[-25,23]]
            self.anchors=[[-15,6,30],[15,6,44],[-25,3,23],[0,2,56]]
            self.renderShots=[[0,1.05,9],[-15,1.05,20],[15,1.05,35],[-25,1.05,17]]
        elif self.mode==1:
            self.family='回廊を選んで戻る三つの中庭'
            for i,(x,z) in enumerate(((-16,28),(0,47),(22,22))):
                b.zone=i
                self.gate('repeat-'+str(i),x,z-3,8,4.4)
                self.building('repeat-west-'+str(i),(x-10,x-5,z,z+7),6)
                self.seating(x+6,z)
                self.planter(x+6,z+4)
                self.cube((x,.008,z),(10,.02,8),self.cream)
            self.bell(0,-16,28);self.bell(1,0,47)
            self.exit(23,18)
            self.route=[[0,12],[-16,19],[-16,28],[-9,35],[0,39],[0,47],[9,39],[30,30],[30,12],[23,12],[23,18]]
            self.reverse_route=[[0,12],[9,18],[9,39],[0,39],[0,47],[-9,35],[-16,28],[-16,19],[0,12],[23,12],[23,18]]
            self.anchors=[[-16,3,28],[0,3,47],[23,3,18],[0,2,56]]
            self.renderShots=[[0,1.05,10],[-16,1.05,20],[0,1.05,38],[23,1.05,12]]
        elif self.mode==2:
            self.family='カフェの営みを探す回遊街区'
            self.building('roastery',(-30,-22,36,49),9)
            self.building('workshop',(20,30,35,48),9)
            for i,(x,z) in enumerate(((14,25),(-13,43))):
                b.zone=i;self.awning(x,z+1,8)
                self.cube((x, .65,z+2),(4,1.3,.9),self.wood,'counter-'+str(i))
                self.b.place('k','kitchenCoffeeMachine',(x+1,1.3,z+2),1.7)
                self.seating(x+6,z);self.planter(x-6,z+2)
            self.bell(0,14,24);self.bell(1,-13,42)
            self.exit(-25,24)
            self.route=[[0,12],[14,17],[14,24],[9,24],[9,34],[-6,35],[-13,35],[-13,42],[-16,35],[-16,17],[-25,17],[-25,24]]
            self.reverse_route=[[0,12],[-9,18],[-9,35],[-13,35],[-13,42],[-6,35],[9,34],[9,24],[14,24],[14,17],[0,12],[-25,17],[-25,24]]
            self.anchors=[[14,2.5,25],[-13,2.5,43],[-25,3,24],[0,2,56]]
            self.renderShots=[[0,1.05,10],[14,1.05,17],[-13,1.05,35],[-25,1.05,18]]
        elif self.mode==3:
            self.family='広場を巡って高みへ上る段階の庭'
            for i,(x,z,w,l,h) in enumerate(((-16,25,8,8,.84),(-4,43,12,8,1.68),(16,43,18,8,2.52))):
                b.zone=i;self.terrace('terrace-'+str(i),x,z,w,l,h)
            self.building('low-neighbour',(-29,-23,18,32),3)
            self.building('middle-neighbour',(-17,-11,40,53),6)
            self.building('high-neighbour',(28,34,35,55),12)
            self.bell(0,-16,27,.84);self.bell(1,16,45,2.52)
            self.cube((20,1.26,36),(4,2.52,14),self.plaster,'upper-return-walk')
            self.cube((20,1.26,25),(9,2.52,8),self.plaster,'exit-terrace')
            self.cube((20,2.54,32),(4,.04,22),self.paving)
            self.exit(20,24,2.52,True)
            self.route=[[0,10],[-16,17],[-16,27],[-16,20],[-4,32],[-4,46],[4,46],[4,32],[16,32],[16,45],[20,45],[20,30],[20,24]]
            self.reverse_route=[]
            self.anchors=[[-16,3,27],[-4,5,46],[16,7,45],[20,6,24]]
            self.renderShots=[[0,1.05,10],[-16,1.65,26],[-4,2.5,45],[16,3.3,45],[20,3.3,31]]
        elif self.mode==4:
            self.family='時計塔から温室へつなぐ街の環'
            self.building('corner',(-31,-23,9,18),9)
            self.building('arcade',(25,33,15,36),9)
            self.clock(17,31,12)
            self.awning(29,16,6)
            b.zone=1;self.greenhouse(-12,44)
            self.gate('conservatory-entry',-12,39.5,8,3.9,planters=False)
            self.building('garden-wall',(-32,-24,40,57),6)
            self.bell(0,15,27);self.bell(1,-12,43)
            self.exit(-25,27)
            self.route=[[0,12],[15,20],[15,27],[10,35],[-5,35],[-12,38],[-12,43],[-12,35],[-18,20],[-25,20],[-25,27]]
            self.reverse_route=[[0,12],[-12,20],[-12,38],[-12,43],[-12,35],[-5,35],[10,35],[15,27],[15,20],[0,12],[-25,20],[-25,27]]
            self.anchors=[[17,10,31],[-12,3.5,44],[-25,3.5,27],[0,2,56]]
            self.renderShots=[[0,1.05,9],[15,1.05,21],[4,1.05,35],[-12,1.05,36],[-25,1.05,20]]
        else:
            self.family='同じ入口へ帰って扉を開く環状街区'
            self.motif(0,10)
            self.building('entry-frame-left',(-21,-11,6,19),6)
            self.building('entry-frame-right',(11,21,6,19),6)
            b.zone=1
            self.building('workshop-left',(-31,-23,30,51),12)
            self.building('workshop-right',(23,32,35,54),9)
            self.cube((0,.32,33),(11,.64,10),self.cream,'middle-garden')
            self.cube((0,.66,33),(10.7,.04,9.7),self.leaf)
            for x in (-3,0,3):self.planter(x,33,.68,size=1.2)
            self.bell(0,-16,27);self.bell(1,15,44)
            # Same physical portal seen at the beginning, not a duplicate at
            # the far end of a long strip. The final action changes its meaning.
            self.exit(0,12)
            self.route=[[0,1],[-7,1],[-7,22],[-16,22],[-16,27],[-8,43],[15,36],[15,44],[21,29],[7,22],[7,1],[0,1],[0,12]]
            self.reverse_route=[[0,1],[7,1],[7,22],[21,29],[15,36],[15,44],[-8,43],[-16,27],[-16,22],[-7,22],[-7,1],[0,1],[0,12]]
            self.anchors=[[-16,3,27],[15,3,44],[0,3,12],[0,2,56]]
            self.renderShots=[[0,1.05,2],[-16,1.05,21],[15,1.05,36],[7,1.05,20],[0,1.05,4]]
        b.zone=2
        self.optional_roof(-27,52) if self.mode==3 else self.optional_roof(0,56)
        self.coin_positions.extend([[-8,1.1,17],[8,1.1,18],[-7,1.1,36],[8,1.1,38]])
        for i,p in enumerate(self.coin_positions):
            c=coin('stage-coin-'+str(i),p)
            c['components']=[part for part in c['components'] if part['type']!='Script']
            self.objects.append(c)
        for i,(x,z) in enumerate(((8,32),(-7,40))):
            enemy=character('stage-enemy-'+str(i),(x,.7,z),'Enemy','ParkEnemy')
            enemy[0]['components'][-1]['behaviour']='SunriseEnemy'
            self.objects+=enemy
        self.objects.append(obj('stage-checkpoint',(0,.7,13)))
        for i,p in enumerate(self.anchors):self.objects.append(obj('lab-focus-'+str(i),p))
        self.objects+=self.collisions
        self.alternative=self.reverse_route
        self.shots=[[x,z] for x,y,z in self.renderShots]
        return self


def build_city(mode):return Adventure(mode).build()
