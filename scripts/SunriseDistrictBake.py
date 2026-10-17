"""ゲーム空間で CC0 の街区を空間ごとにバッチ化し、マテリアルを保持します。"""
import json
import math
from collections import defaultdict

from AuthorEntranceCafe import quaternius, kenney, material
from SunriseBatching import GAME, read_model, rotate, normalize
from GenerateParkAdventure import component, obj, write as write_source


def write(path,value):
    # エンジンのメッシュ読み込みで大きなファイルが開かれたままになる場合があるため、
    # 変更のないアセットは、演出の変更ごとに切り詰めて書き直さず、そのまま保持します。
    text=value if isinstance(value,str) else json.dumps(value,ensure_ascii=False,indent=2)+'\n'
    encoded=text.replace('\r\n','\n').replace('\n','\r\n').encode('utf-8')
    if path.is_file() and path.read_bytes()==encoded and path.with_name(path.name+'.meta').is_file():return
    write_source(path,value)

FOLDER='Assets/Models/Sunrise/District'
MATERIALS='Assets/Materials/Sunrise/District'
TEXTURES='Assets/Textures/Sunrise/District'


class DistrictBake:
    def __init__(self,folder=FOLDER,materials=MATERIALS,textures=TEXTURES,identity='district'):
        self.folder,self.materials,self.textures,self.identity=folder,materials,textures,identity
        self.zone=0
        self.groups=defaultdict(list)
        self.cache={}
        self.sources=[]

    def place(self,kind,name,position,scale=1,angle=0,pitch=0,origin=(0,0,0),side=0,spread=0):
        key=(kind,name)
        if key not in self.cache:self.cache[key]=quaternius(name) if kind=='q' else kenney(name)
        self.sources.append(dict(zone=self.zone,kind=kind,model=name,position=position,scale=scale,
                                 angle=angle,pitch=pitch,origin=origin,side=side,spread=spread))
        for surface,polygon in self.cache[key]:
            if side and sum(p[0] for p,uv,n in polygon)*side<0:continue
            world=[]
            for p,uv,n in polygon:
                local=[(p[i]-origin[i])*scale*(-1 if i==2 else 1) for i in range(3)]
                if spread:local[0]+=math.copysign(spread,p[0])
                transformed=rotate(local,(pitch,angle,0))
                normal=rotate((n[0],n[1],-n[2]),(pitch,angle,0))
                world.append(((transformed[0]+position[0],transformed[1]+position[1],-transformed[2]-position[2]),
                              uv,(normal[0],normal[1],-normal[2])))
            self.groups[(self.zone,json.dumps(surface,sort_keys=True))].append(world)

    def cube(self,position,size,surface):
        key=('box','unit')
        if key not in self.cache:self.cache[key]=read_model(GAME/'Assets/Models/Park/UnitBox.obj')
        vertices,uvs,normals,faces,_=self.cache[key]
        for _,corners in faces:
            a=[vertices[corners[1][0]][i]-vertices[corners[0][0]][i] for i in range(3)]
            b=[vertices[corners[2][0]][i]-vertices[corners[0][0]][i] for i in range(3)]
            normal=normalize((a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]))
            axis=max(range(3),key=lambda i:abs(normal[i]));axes=((2,1),(0,2),(0,1))[axis]
            polygon=[]
            for vi,ti,ni in corners:
                p=vertices[vi]
                world=[position[i]+p[i]*size[i]*(-1 if i==2 else 1) for i in range(3)]
                polygon.append(((world[0],world[1],-world[2]),tuple(world[i]/1.2 for i in axes),normal))
            self.groups[(self.zone,json.dumps(surface,sort_keys=True))].append(polygon)

    def finish(self):
        from PIL import Image
        colors=sorted({tuple(json.loads(s)['color'][:3]) for z,s in self.groups if not json.loads(s)['texture']})
        palette=Image.new('RGB',(32*len(colors),32))
        for i,color in enumerate(colors):palette.paste(tuple(round(c*255) for c in color),(i*32,0,(i+1)*32,32))
        (GAME/self.textures).mkdir(parents=True,exist_ok=True)
        palette.save(GAME/self.textures/'Palette.png')
        groups=defaultdict(list)
        for (zone,signature),polygons in self.groups.items():
            surface=json.loads(signature);tint=surface['color']
            uv=None
            if not surface['texture']:
                surface['color']=[1,1,1,1]
                uv=((colors.index(tuple(tint[:3]))+.5)/len(colors),.5)
                surface['texture']=self.textures+'/Palette.png'
                if surface['roughness']>=.6:surface['roughness']=.82
            if surface['texture'].endswith('/Wood.png'):surface['roughness']=.75
            groups[(zone,json.dumps(surface,sort_keys=True))].extend(
                [[(p,uv or tex,n) for p,tex,n in polygon] for polygon in polygons])
        surfaces=sorted({s for z,s in groups})
        for i,s in enumerate(surfaces):write(GAME/self.materials/f'Surface-{i}.mat',json.loads(s))
        write(GAME/self.folder/'Baked/District.mtl','newmtl white\nKd 1 1 1\n')
        objects=[]
        for (zone,signature),polygons in sorted(groups.items()):
            index=1;unique={}
            lines=['# CC0 district geometry; SunriseDistrictBake.py','mtllib District.mtl','usemtl white']
            for polygon in polygons:
                face=[]
                for p,uv,n in polygon:
                    key=tuple(f'{x:.7g}' for x in (*p,*uv,*n))
                    if key not in unique:
                        unique[key]=index;index+=1
                        lines+=['v '+' '.join(key[:3]),'vt '+' '.join(key[3:5]),'vn '+' '.join(key[5:])]
                    value=unique[key];face.append(f'{value}/{value}/{value}')
                lines.append('f '+' '.join(face))
            surface=surfaces.index(signature);name=f'Block-{zone}-{surface}'
            write(GAME/self.folder/'Baked'/(name+'.obj'),'\n'.join(lines)+'\n')
            objects.append(obj(self.identity+'-'+name.lower(),(0,0,0),components=[
                component('MeshRenderer',model=self.folder+'/Baked/'+name+'.obj',instancing=False),
                component('Material',asset=self.materials+f'/Surface-{surface}.mat')]))
        write(GAME/self.folder/'Objects.json',objects)
        write(GAME/self.folder/'Placements.json',self.sources)
        active={next(c['model'] for c in o['components'] if c['type']=='MeshRenderer') for o in objects}
        for path in (GAME/self.folder/'Baked').glob('Block-*.obj'):
            if path.relative_to(GAME).as_posix() not in active:
                path.unlink();path.with_suffix(path.suffix+'.meta').unlink(missing_ok=True)
        for path in (GAME/self.materials).glob('Surface-*.mat'):
            if int(path.stem.split('-')[1])>=len(surfaces):
                path.unlink();path.with_suffix(path.suffix+'.meta').unlink(missing_ok=True)
        print(f'District: {len(objects)} material/zone batches; {sum(len(p) for p in groups.values())} polygons')
        return objects


def scene_objects():
    return json.loads((GAME/FOLDER/'Objects.json').read_text(encoding='utf-8'))
