"""街のステージで共有する CC0 建築用の補助処理です。"""
import math
from AuthorEntranceCafe import material, TEXTURES as CAFE_TEXTURES
from SunriseDistrictBake import TEXTURES
from GenerateParkAdventure import component, obj

class StreetDistrict:
    def __init__(self):
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

    def solid(self,key,pos,size):
        self.collisions.append(obj('street-collision-'+key,pos,size,[component('BoxCollider',center=[0,0,0],size=[1,1,1])]))

    def cube(self,pos,size,surface=None,key=None):
        self.b.cube(pos,size,surface or self.cream)
        if key:self.solid(key,pos,size)

    def building(self,key,bounds,height=9,front=True,side=True):
        x0,x1,z0,z1=bounds
        self.cube(((x0+x1)/2,height/2,(z0+z1)/2),(x1-x0-.3,height,z1-z0-.3),self.plaster)
        self.solid(key,((x0+x1)/2,height/2,(z0+z1)/2),(x1-x0,height,z1-z0))
        # 背景の建物には閉じた屋根を付けます。プレイ可能なテラスには、
        # 実際の衝突形状の高さに合わせた専用の舗装と胸壁を設けます。
        if key!='roof':self.cube(((x0+x1)/2,height+.10,(z0+z1)/2),(x1-x0+.35,.20,z1-z0+.35),self.teal)
        # 建築モジュールは元の比率を保ちます。上部の窓はまばらにして、
        # 全面を埋めずに街の輪郭を読み取りやすくします。
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

