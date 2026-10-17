"""同梱の CC0 建築モジュールを使い、近距離で見る街を組み立てます。"""
import json
import math
from StreetDistrict import StreetDistrict
from SunriseDistrictBake import DistrictBake
from AuthorEntranceCafe import material,accessor
from SunriseBatching import GAME, read_model, rotate
from SunriseDistrictBake import write

PAVING='Assets/Textures/StreetJourney/PolyHaven/pavement_05_diff_1k.png'
NORMAL='Assets/Textures/StreetJourney/PolyHaven/pavement_05_nor_dx_1k.png'
BRICK='Assets/Textures/Sunrise/Cafe/T_RedBrick_BaseColor.png'
BRICK_NORMAL='Assets/Textures/Sunrise/Cafe/T_RedBrick_Normal.png'

def prepare_art_imports():
 # 読み込み時にミップマップを生成します。これがないと、細かな CC0 の表面は静止画で鮮明でも
 # 移動中にちらつきます。元の画像のバイト列は変更しません。
 textures=[PAVING,NORMAL,BRICK,BRICK_NORMAL]
 textures += [f'Assets/Textures/StreetJourney/PolyHaven/wooden_crate_02_{channel}_1k.png' for channel in ('diff','nor_gl')]
 for name in ('Trim','MetalConcrete','Ornaments','Dirt'):
  textures += [f'Assets/Textures/Sunrise/Cafe/T_{name}_{kind}.png' for kind in ('BaseColor','Normal')]
 for rel in textures:
  path=(GAME/rel).with_name((GAME/rel).name+'.meta')
  data=json.loads(path.read_text(encoding='utf-8'))
  data['texture']=dict(compression='none',maxSize=1024,mipmaps=True,srgb='Normal' not in rel and 'nor_' not in rel)
  write(path,data)


class StreetArtBake(DistrictBake):
 def cube(self,position,size,surface):
  key=(self.zone,json.dumps(surface,sort_keys=True));start=len(self.groups[key])
  super().cube(position,size,surface)
  meters=3.4 if surface['texture']==PAVING else 2.4 if surface['texture']==BRICK else None
  if meters:
   for face in self.groups[key][start:]:
    for i,(p,uv,n) in enumerate(face):face[i]=(p,tuple(v*1.2/meters for v in uv),n)

 def place(self,kind,name,position,scale=1,angle=0,tint=None,**kwargs):
  if kind=='p' and (kind,name) not in self.cache:
   folder=GAME/'Assets/Models/StreetJourney/Source/PolyHaven'
   d=json.loads((folder/'wooden_crate_02_1k.gltf').read_text(encoding='utf-8'))
   buffers=[(folder/b['uri']).read_bytes() for b in d['buffers']];polygons=[]
   surface=material('Assets/Textures/StreetJourney/PolyHaven/wooden_crate_02_diff_1k.png',
                    'Assets/Textures/StreetJourney/PolyHaven/wooden_crate_02_nor_gl_1k.png',roughness=.82)
   for node in d['nodes']:
    if any(k in node for k in ('matrix','scale','rotation')):raise ValueError('Unsupported crate node transform')
    translation=node.get('translation',(0,0,0))
    for primitive in d['meshes'][node['mesh']]['primitives']:
     positions=accessor(d,buffers,primitive['attributes']['POSITION'])
     normals=accessor(d,buffers,primitive['attributes']['NORMAL'])
     uvs=accessor(d,buffers,primitive['attributes']['TEXCOORD_0'])
     indices=[v[0] for v in accessor(d,buffers,primitive['indices'])]
     for start in range(0,len(indices),3):
      polygons.append((surface,[(tuple(positions[i][j]+translation[j] for j in range(3)),
                                (uvs[i][0],1-uvs[i][1]),normals[i]) for i in indices[start:start+3]]))
   base=min(p[1] for _,face in polygons for p,uv,n in face)
   self.cache[(kind,name)]=[(m,[((p[0],p[1]-base,p[2]),uv,n) for p,uv,n in face]) for m,face in polygons]
  if kind=='n' and (kind,name) not in self.cache:
   vertices,uvs,normals,faces,materials=read_model(GAME/'Assets/Models/Park/KenneyNature'/(name+'.obj'))
   base=min(v[1] for v in vertices);polygons=[]
   for name_,corners in faces:
    color=materials[name_]['color']
    if 'leaf' in name_.lower():color=[.24,.43,.30]
    elif 'wood' in name_.lower():color=[.40,.28,.18]
    polygons.append((material(color=(*color,1)),[
     ((vertices[vi][0],vertices[vi][1]-base,vertices[vi][2]),uvs[ti] if ti is not None else (0,0),
      normals[ni] if ni is not None else (0,1,0)) for vi,ti,ni in corners]))
   self.cache[(kind,name)]=polygons
  if tint:
   from AuthorEntranceCafe import quaternius
   if (kind,name) not in self.cache:self.cache[(kind,name)]=quaternius(name)
   original=self.cache[(kind,name)]
   self.cache[(kind,name)]=[(dict(m,color=list(tint)) if m['texture'].endswith('T_Trim_BaseColor.png') else m,face) for m,face in original]
   try:super().place(kind,name,position,scale,angle,**kwargs)
   finally:self.cache[(kind,name)]=original
   self.sources[-1]['tint']=tint
  else:super().place(kind,name,position,scale,angle,**kwargs)

 def tilted_box(self,position,size,surface,pitch):
  key=(self.zone,json.dumps(surface,sort_keys=True));start=len(self.groups[key])
  self.cube((0,0,0),size,surface)
  for face in self.groups[key][start:]:
   for i,(p,uv,n) in enumerate(face):
    v=rotate((p[0],p[1],-p[2]),(pitch,0,0));normal=rotate((n[0],n[1],-n[2]),(pitch,0,0))
    face[i]=((v[0]+position[0],v[1]+position[1],-v[2]-position[2]),uv,(normal[0],normal[1],-normal[2]))


class StreetArchitecture(StreetDistrict):
 def building(self,key,bounds,height=9,front=True,side=True):
  if not front and not side:
   super().building(key,bounds,height,front,side);return
  x0,x1,z0,z1=bounds
  # 構造の芯は窓のくぼみを貫通させず、ガラスより奥へ下げます。
  # 元データで +Z を向く面は、glTF からワールドへの反転後に -Z を向きます。
  shell=material(BRICK,BRICK_NORMAL,roughness=.88)
  self.cube(((x0+x1)/2,height/2,(z0+z1)/2),(x1-x0-.65,height,z1-z0-.65),shell)
  self.solid(key,((x0+x1)/2,height/2,(z0+z1)/2),(x1-x0,height,z1-z0))
  self.cube(((x0+x1)/2,.18,(z0+z1)/2),(x1-x0+.06,.36,z1-z0+.06),self.cream)
  self.cube(((x0+x1)/2,-1.8,(z0+z1)/2),(x1-x0,3.6,z1-z0),shell)
  for y in range(3,int(height),3):
   self.cube(((x0+x1)/2,y,(z0+z1)/2),(x1-x0+.12,.12,z1-z0+.12),self.cream)
  self.cube(((x0+x1)/2,height+.12,(z0+z1)/2),(x1-x0+.35,.24,z1-z0+.35),self.teal)
  # 外壁はプレイ側から見えるものを選び、見えない外壁を飾ったり、
  # 遠景に同じ処理量を費やしたりしないようにします。
  faces=[]
  if front and key not in ('entry-west','entry-east'):
   faces.append((x0,x1,z0,0,(0,0,-1)))
  if side:
   if key not in ('entry-west','cafe','garden-west'):
    faces.append((z0,z1,x0,math.pi/2,(-1,0,0)))
   if key not in ('entry-east','east-shop','garden-east'):
    faces.append((z0,z1,x1,-math.pi/2,(1,0,0)))
  for lo,hi,edge,angle,out in faces:
   tint={'entry-west':(.80,.91,1,1),'cafe':(1,.73,.55,1),'east-shop':(.75,.85,.78,1)}.get(key)
   count=max(1,int((hi-lo)/2));offset=(lo+hi)/2-(count-1)
   for i in range(count):
    along=offset+2*i
    pos=lambda y:((along,y,edge) if out[2] else (edge,y,along))
    for y in range(0,int(height)-2,3):
     name='Trim_FirstFloor_Window_001' if y==0 else ('Trim_Plain_3' if i%4==3 else 'Trim_Window')
     self.b.place('q',name,pos(y),1,angle,tint=tint)
    cornice=pos(height-.8)
    self.b.place('q','Cornice_Trim_Center',cornice,1,angle)
   # くぼんだ店舗のガラス面・石造りの角柱・一部の壁付け設備を配置します。
   for along in (lo+.35,hi-.35):
    p=(along,0,edge) if out[2] else (edge,0,along)
    self.b.place('q','Trim_Column_Center',p,.9,angle)
   for i in range(1,count,4):
    p=(offset+2*i,3.4,edge-.32) if out[2] else (edge+out[0]*.32,3.4,offset+2*i)
    self.b.place('k','lampWall',p,1.2,angle)
  if key=='cafe':
   self.b.place('q','DoorFrame_Wooden',(-14,.02,z0-.3),1)
   for x in (-14,-13):self.b.place('q','Door_1',(x,.02,z0-.35),1)
  if key in ('cafe','east-shop','garden-east'):
   self.b.place('q','Prop_ACUnit',((x0+x1)/2,height-.8,z0-.32),1.3,math.pi)

 def awning(self,x,z,width=6,y=3.4,floor=0):
  self.b.tilted_box((x,y,z-.9),(width,.12,1.9),self.orange,-.16)
  self.cube((x,y-.3,z-1.83),(width,.35,.12),self.cream)
  self.cube((x,y+.07,z-.07),(width,.12,.14),self.wood)
  for dx in (-width/2+.18,width/2-.18):
   self.b.place('q','Metal_Column_Center',(x+dx,floor+.03,z-.24),(y+.10-floor-.03)/3)

 def seating(self,x,z,y=0,size=1):
  base=y+.03
  self.b.place('k','tableRound',(x,base,z),2.1*size)
  for dz,angle in ((-.95,0),(.95,math.pi)):
   self.b.place('k','chairRounded',(x,base,z+dz*size),2.2*size,angle)
  top=max(p[1] for _,face in self.b.cache[('k','tableRound')] for p,uv,n in face)
  self.b.place('k','plantSmall1',(x,base+top*2.1*size,z),.7*size)

 def planter(self,x,z,y=0,size=1):
  self.b.place('q','Prop_Planter_Single',(x,y+.03,z),.48*size)
  self.b.place('n','tree_oak',(x,y+.03+.26*size,z),1.1*size)

 def street_details(self):
  # カフェの接客部分の端に少数の PBR の小物を置き、素材のスケールを示します。
  self.b.place('p','wooden_crate_02',(-.8,.59,29),1.25,.15)
  height=max(p[1] for _,face in self.b.cache[('p','wooden_crate_02')] for p,uv,n in face)
  self.b.place('p','wooden_crate_02',(-1.0,.59+height*1.25,29),1.1,-.12)
  self.b.place('p','wooden_crate_02',(-16.8,.03,27.7),1.15,.4)
  self.solid('cafe-crate-stack',(-.9,1.1,29),(1.65,1.1,1))
  self.solid('cafe-crate-west',(-16.8,.30,27.7),(1.5,.54,1))
  for x,z,y in ((-16,8,0),(-11,12,0),(7.7,26,.56),(7.8,45,.56)):
   self.b.place('q','Prop_Bollard',(x,y+.03,z),1)
  for x,z,y in ((-14,5,.04),(2,22,.6),(6,37,.645)):
   self.b.place('q','Prop_ManholeCover',(x,y,z),1)
  for x,z,y in ((-8.45,20,.03),(-5.3,25.5,.59),(8.5,31,.59)):
   self.b.place('q','Prop_Drain',(x,y,z),1.2)
  # 大きな植栽は端に置き、ジャンプする場所と出口を空けます。
  for x,z,y in ((-20,20,-3.5),(12,23,-3.5),(24,41,-3.5)):
   self.b.place('n','tree_oak',(x,y,z),6)
   self.cube((x,y-.3,z),(3,.6,3),self.cream)
  # プレイ範囲外の低い通りは、落下時に戻す平面より下に置きます。
  self.cube((0,-4,42),(110,.8,150),material(color=(.34,.39,.34,1)))
