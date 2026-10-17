"""同梱の CC0 モジュールから入口のカフェを焼き込みます。エンジンの変更は行いません。

Quaternius の glTF モジュールは基本色・法線・元の UV を保持します。
Kenney の家具には木材・陶器の表面を合わせます。AuthorSunriseBlock.py より前に、
同梱の Python と Pillow で実行してください。素材の取り込み後はネットワーク不要です。"""
import json
import math
import struct
from collections import defaultdict
from pathlib import Path

from GenerateParkAdventure import component, obj, write
from SunriseBatching import GAME, read_model, rotate, normalize

FOLDER = 'Assets/Models/Sunrise/Cafe'
BASE = GAME / FOLDER
MATERIALS = 'Assets/Materials/Sunrise/Cafe'
TEXTURES = 'Assets/Textures/Sunrise/Cafe'


def material(texture='', normal='', color=(1, 1, 1, 1), roughness=.82, metallic=0):
    return dict(color=list(color), texture=texture, normalTexture=normal,
                roughness=roughness, metallic=metallic, transparent=False,
                physicallyBased=True, normalFlipY=False)


def accessor(document, buffers, index):
    a = document['accessors'][index]
    assert 'sparse' not in a, 'Sparse data requires explicit conversion'
    view = document['bufferViews'][a['bufferView']]
    count = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
    code = {5126: 'f', 5125: 'I', 5123: 'H', 5121: 'B'}[a['componentType']]
    fmt = '<' + code * count
    stride = view.get('byteStride', struct.calcsize(fmt))
    offset = view.get('byteOffset', 0) + a.get('byteOffset', 0)
    return [struct.unpack_from(fmt, buffers[view['buffer']], offset+i*stride)
            for i in range(a['count'])]


def quaternius(name):
    source = BASE / 'Source/Quaternius'
    d = json.loads((source/(name+'.gltf')).read_text())
    assert len(d['nodes']) == 1 and not any(k in d['nodes'][0] for k in ('matrix','translation','rotation','scale'))
    buffers = [(source/b['uri']).read_bytes() for b in d['buffers']]

    def image(index):
        return TEXTURES+'/'+d['images'][d['textures'][index]['source']]['uri']

    result = []
    for mesh in d['meshes']:
        for p in mesh['primitives']:
            assert p.get('mode', 4) == 4
            m = d['materials'][p['material']]
            pbr = m['pbrMetallicRoughness']
            texture = image(pbr['baseColorTexture']['index']) if 'baseColorTexture' in pbr else ''
            normal = image(m['normalTexture']['index']) if 'normalTexture' in m else ''
            surface = material(texture, normal, pbr.get('baseColorFactor', [1,1,1,1]))
            # これらの面は静的な色付きガラスとして扱い、Source 専用の視差シェーダーは使いません。
            if 'Glass' in m['name']:
                surface = material(color=(.10,.24,.28,1), roughness=.28, metallic=.12)
            elif 'FakeInterior' in m['name']:
                surface = material(color=(.25,.32,.30,1))
            elif 'InteriorWall' in m['name']:
                surface = material(TEXTURES+'/Plaster.png')
            positions = accessor(d,buffers,p['attributes']['POSITION'])
            normals = accessor(d,buffers,p['attributes']['NORMAL'])
            uvs = accessor(d,buffers,p['attributes']['TEXCOORD_0'])
            indices = [i[0] for i in accessor(d,buffers,p['indices'])]
            for start in range(0,len(indices),3):
                result.append((surface,[(positions[i],(uvs[i][0],1-uvs[i][1]),normals[i])
                                       for i in indices[start:start+3]]))
    return result


def kenney(name):
    v,uv,n,faces,materials = read_model(BASE/'Source/Kenney'/(name+'.obj'))
    origin=[(min(p[i] for p in v)+max(p[i] for p in v))/2 for i in range(3)]
    origin[1]=min(p[1] for p in v)
    result=[]
    for key,corners in faces:
        color=materials.get(key,{}).get('color',[1,1,1])
        wooden = 'wood' in (key or '').lower()
        surface=material(TEXTURES+'/Wood.png' if wooden else '',
                         color=(1,1,1,1) if wooden else (*color,1),roughness=.68 if wooden else .8)
        if 'leaf' in (key or '').lower():surface=material(color=(.22,.44,.25,1))
        polygon=[]
        for vi,ti,ni in corners:
            position=[v[vi][i]-origin[i] for i in range(3)]
            normal=n[ni] if ni is not None else (0,1,0)
            # 家具の元データは単色のマテリアルを使うため、木材にはメートル単位の UV スケールを設定します。
            axis=max(range(3),key=lambda i:abs(normal[i]))
            axes=((2,1),(0,2),(0,1))[axis]
            tex=tuple(position[i]*2 for i in axes) if wooden else (uv[ti] if ti is not None else (0,0))
            polygon.append((position,tex,normal))
        result.append((surface,polygon))
    return result


def generate_textures():
    from PIL import Image, ImageDraw
    import random
    folder=GAME/TEXTURES;folder.mkdir(parents=True,exist_ok=True)
    rng=random.Random(240)
    for name,base,wood in [('Wood',(144,100,64),True),('Plaster',(223,217,199),False)]:
        image=Image.new('RGB',(256,256));pixels=[]
        for y in range(256):
            for x in range(256):
                detail=(math.sin(y*.32+math.sin(x*.025)*1.6)*.035 if wood else 0)+rng.uniform(-.018,.018)
                pixels.append(tuple(round(c*(1+detail)) for c in base))
        image.putdata(pixels);image.save(folder/(name+'.png'))
    image=Image.new('RGB',(512,256),(29,65,68));draw=ImageDraw.Draw(image)
    draw.rounded_rectangle((8,8,503,247),radius=16,outline=(220,185,126),width=5)
    # 説明の文字を使わず、カップと朝日で店の用途を伝えます。
    draw.ellipse((110,36,190,116),fill=(220,185,126))
    draw.rounded_rectangle((198,104,322,196),radius=18,fill=(246,231,194))
    draw.arc((300,106,364,178),-90,90,fill=(246,231,194),width=12)
    draw.line((185,207,344,207),fill=(220,185,126),width=8)
    for x in (225,260,295):draw.arc((x-10,40,x+10,102),80,280,fill=(232,202,151),width=5)
    image.save(folder/'CafeSign.png')


def bake():
    groups=defaultdict(list)
    cached={}

    def place(kind,name,position,scale=1,angle=0):
        key=(kind,name)
        if key not in cached:cached[key]=quaternius(name) if kind=='q' else kenney(name)
        for surface,polygon in cached[key]:
            transformed=[]
            for p,uv,n in polygon:
                # SRT の適用時は glTF/OBJ を反転してエンジンの左手座標系に合わせ、
                # 派生 OBJ の書き出し時に再び反転して戻します。
                world=rotate([p[0]*scale,p[1]*scale,-p[2]*scale],(0,angle,0))
                normal=rotate([n[0],n[1],-n[2]],(0,angle,0))
                transformed.append(((world[0]+position[0],world[1]+position[1],-world[2]-position[2]),
                                    uv,(normal[0],normal[1],-normal[2])))
            groups[json.dumps(surface,sort_keys=True)].append(transformed)

    def cube(position,size,surface):
        # メートル単位の UV を持つ閉じた形状です。構造の隙間埋めとひさしだけに使用します。
        v,uv,n,faces,_=read_model(GAME/'Assets/Models/Park/UnitBox.obj')
        for _,corners in faces:
            poly=[]
            a=[v[corners[1][0]][i]-v[corners[0][0]][i] for i in range(3)]
            b=[v[corners[2][0]][i]-v[corners[0][0]][i] for i in range(3)]
            face_normal=normalize((a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]))
            for vi,ti,ni in corners:
                p=v[vi][:3];normal=n[ni] if ni is not None else face_normal
                world=[position[i]+p[i]*size[i]*(-1 if i==2 else 1) for i in range(3)]
                axis=max(range(3),key=lambda i:abs(normal[i]));axes=((2,1),(0,2),(0,1))[axis]
                poly.append(((world[0],world[1],-world[2]),tuple(p[i]*size[i]/1.5 for i in axes),normal))
            groups[json.dumps(surface,sort_keys=True)].append(poly)

    cream=material(TEXTURES+'/Plaster.png')
    teal=material(color=(.19,.34,.36,1),roughness=.67)
    orange=material(color=(.80,.39,.16,1))
    canvas=material(color=(.94,.86,.66,1))
    # 3 階分を 2 × 3 メートルのモジュールと一様なスケールで構成し、窓を引き伸ばしません。
    # 不透明な構造の補完面は、元の窓やドアのくぼみより奥へ下げます。
    cube((-10.9,4.5,8),(9.4,9,19.2),cream)
    for floor in range(3):
        for z in range(0,18,2):
            name='DoorFrame_Wooden' if floor==0 and z==6 else ('Trim_FirstFloor_Window_001' if floor==0 else 'Trim_Window')
            place('q',name,(-5.84,floor*3,z),angle=-math.pi/2)
    # 端の立面を見せて、入口の角に奥行きを与えます。
    for x in (-6.9,-8.9,-10.9,-12.9,-14.9):
        for y in (0,3,6):place('q','Trim_Window',(x,y,-2.04))
    for z in range(0,18,2):place('q','Cornice_Trim_Center',(-5.84,9,z),angle=-math.pi/2)
    for x in (-6.9,-8.9,-10.9,-12.9,-14.9):place('q','Cornice_Trim_Center',(x,9,-2.04))
    cube((-10.9,9.65,8),(10.2,.18,20.2),teal)
    # 店先に布のひさし・細い縞の垂れ幕・実際の支柱を配置します。
    for i in range(16):
        z=.5+i*.9
        cube((-5.05,3.12,z),(1.65,.13,.9),canvas if i%2 else orange)
        cube((-4.27,2.98,z),(.09,.3,.9),canvas if i%2 else orange)
    for z in (1,8,14):
        cube((-5.1,2.84,z),(1.2,.08,.07),teal)
        place('k','lampWall',(-5.7,2.7,z),1.3,-math.pi/2)
    # 2 組の小さな座席を置き、道路と収集アイテムへの経路を空けます。
    for z in (3,11.7):
        place('k','tableRound',(-4.25,.06,z),2.1)
        for dz,angle in [(-.9,0),(.9,math.pi)]:
            place('k','chairRounded',(-4.25,.06,z+dz),2.2,angle)
        place('k','plantSmall1',(-4.25,.84,z),.65)
    # 開いた入口のそばにコーヒー用の棚を置き、両端に植栽を配置します。
    cube((-5.40,1.0,7.4),(.55,.12,1.6),material(TEXTURES+'/Wood.png'))
    place('k','kitchenCoffeeMachine',(-5.38,1.06,7.4),1.3,-math.pi/2)
    for z in (-.8,15.8):
        place('q','Prop_Planter_Single',(-4.8,.03,z),.47)
        place('k','pottedPlant',(-4.8,.2,z),2.2)
    for z in (2,14):place('q','Prop_Drain',(-3.1,.025,z),.6)
    # 突き出し看板は、テクスチャ付きの板・木枠・金属の支持金具で構成します。
    cube((-4.9,4.45,5.8),(1.6,.95,.12),teal)
    cube((-5.05,5.0,5.8),(1.5,.07,.07),teal)
    # 入口のカメラから読めるよう、明示的な UV を持つ正面向きの看板面を使います。
    surface=material(TEXTURES+'/CafeSign.png')
    for ids in [(0,1,2),(0,2,3)]:
        verts=[(-5.65,4.02,-5.72),(-4.15,4.02,-5.72),(-4.15,4.88,-5.72),(-5.65,4.88,-5.72)]
        uvs=[(0,0),(1,0),(1,1),(0,1)]
        groups[json.dumps(surface,sort_keys=True)].append([(verts[i],uvs[i],(0,0,1)) for i in ids])

    # 単色ごとに描画を分けず、パレットのアトラスを使います。エンジンの OBJ ローダーは
    # 頂点カラーを保持しないためです。ガラスの仕上げは別に保持します。
    from PIL import Image
    colors=sorted({tuple(json.loads(s)['color'][:3]) for s in groups if not json.loads(s)['texture']})
    palette=Image.new('RGB',(32*len(colors),32))
    for i,color in enumerate(colors):
        palette.paste(tuple(round(c*255) for c in color),(i*32,0,(i+1)*32,32))
    palette.save(GAME/TEXTURES/'Palette.png')
    merged=defaultdict(list)
    for signature,polygons in groups.items():
        surface=json.loads(signature)
        tint=surface['color'];surface['color']=[1,1,1,1]
        palette_uv=None
        if not surface['texture']:
            palette_uv=((colors.index(tuple(tint[:3]))+.5)/len(colors),.5)
            surface['texture']=TEXTURES+'/Palette.png'
            if surface['roughness']>=.6:surface['roughness']=.82
        if surface['texture'].endswith('/Wood.png'):surface['roughness']=.75
        for polygon in polygons:
            merged[json.dumps(surface,sort_keys=True)].append([(p,palette_uv or uv,n) for p,uv,n in polygon])
    groups=merged
    objects=[]
    # Assimp の暗黙のマテリアルの拡散色は .6 なので、白を明示し、
    # PBR 描画時に元のマテリアルの色が二重に暗くなるのを防ぎます。
    write(BASE/'Baked/Cafe.mtl','newmtl white\nKd 1 1 1\n')
    for i,(signature,polygons) in enumerate(sorted(groups.items())):
        surface=json.loads(signature)
        name=f'Surface-{i}'
        lines=['# Derived CC0 cafe geometry; AuthorEntranceCafe.py','mtllib Cafe.mtl','usemtl white']
        index=1
        for polygon in polygons:
            face=[]
            for p,uv,n in polygon:
                lines+=['v '+' '.join(f'{x:.7g}' for x in p),'vt '+' '.join(f'{x:.7g}' for x in uv),
                        'vn '+' '.join(f'{x:.7g}' for x in n)]
                face.append(f'{index}/{index}/{index}');index+=1
            lines.append('f '+' '.join(face))
        write(BASE/'Baked'/(name+'.obj'),'\n'.join(lines)+'\n')
        write(GAME/MATERIALS/(name+'.mat'),surface)
        objects.append(obj('cafe-surface-'+str(i),(0,0,0),components=[
            component('MeshRenderer',model=FOLDER+'/Baked/'+name+'.obj',instancing=False),
            component('Material',asset=MATERIALS+'/'+name+'.mat')]))
    write(BASE/'CafeObjects.json',objects)
    # このツールが明示的に管理する出力のうち、不要になったファイルだけを削除します。
    for directory,extension in [(BASE/'Baked','obj'),(GAME/MATERIALS,'mat')]:
        for path in directory.glob('Surface-*.'+extension):
            if int(path.stem.split('-')[1])>=len(objects):
                path.unlink()
                path.with_suffix(path.suffix+'.meta').unlink(missing_ok=True)
    print(f'Cafe: {len(objects)} material batches, {sum(len(p) for p in groups.values())} polygons')


def scene_objects():
    return json.loads((BASE/'CafeObjects.json').read_text(encoding='utf-8'))


if __name__=='__main__':
    generate_textures()
    bake()
