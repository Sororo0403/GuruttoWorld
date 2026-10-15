"""Bake the entrance cafe from the bundled CC0 source modules, without engine changes.

Quaternius glTF modules retain base colour, normals and their original UVs.
Kenney furniture receives matching wood/ceramic surfaces. Run with the bundled
Python + Pillow before AuthorSunriseBlock.py; no network is needed after import.
"""
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
            # These surfaces are static tinted panes, rather than Source-only parallax shaders.
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
            # Furniture source uses solid materials; give wood a metric UV scale.
            axis=max(range(3),key=lambda i:abs(normal[i]))
            axes=((2,1),(0,2),(0,1))[axis]
            tex=tuple(position[i]*2 for i in axes) if wooden else (uv[ti] if ti is not None else (0,0))
            polygon.append((position,tex,normal))
        result.append((surface,polygon))
    return result


def generate_textures():
    from PIL import Image, ImageDraw, ImageFont
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
    font=ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf',64)
    small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',23)
    draw.text((256,84),'MORNING',font=font,anchor='mm',fill=(246,231,194))
    draw.text((256,151),'COFFEE  /  BAKERY',font=small,anchor='mm',fill=(232,202,151))
    draw.text((256,202),'OPEN  7:00',font=small,anchor='mm',fill=(232,202,151))
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
                # Mirror glTF/OBJ to the engine's left-handed coordinates for SRT,
                # then mirror back when writing the derived OBJ.
                world=rotate([p[0]*scale,p[1]*scale,-p[2]*scale],(0,angle,0))
                normal=rotate([n[0],n[1],-n[2]],(0,angle,0))
                transformed.append(((world[0]+position[0],world[1]+position[1],-world[2]-position[2]),
                                    uv,(normal[0],normal[1],-normal[2])))
            groups[json.dumps(surface,sort_keys=True)].append(transformed)

    def cube(position,size,surface):
        # Closed geometry with metric UVs; used only for structural fill and the awning.
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
    # Three storeys use 2 x 3 metre modules with uniform scale, never stretched windows.
    # Recess the opaque structural fill behind the source window/door recesses.
    cube((-10.9,4.5,8),(9.4,9,19.2),cream)
    for floor in range(3):
        for z in range(0,18,2):
            name='DoorFrame_Wooden' if floor==0 and z==6 else ('Trim_FirstFloor_Window_001' if floor==0 else 'Trim_Window')
            place('q',name,(-5.84,floor*3,z),angle=-math.pi/2)
    # A visible end elevation gives the entry corner depth.
    for x in (-6.9,-8.9,-10.9,-12.9,-14.9):
        for y in (0,3,6):place('q','Trim_Window',(x,y,-2.04))
    for z in range(0,18,2):place('q','Cornice_Trim_Center',(-5.84,9,z),angle=-math.pi/2)
    for x in (-6.9,-8.9,-10.9,-12.9,-14.9):place('q','Cornice_Trim_Center',(x,9,-2.04))
    cube((-10.9,9.65,8),(10.2,.18,20.2),teal)
    # Fabric awning, narrow striped valance and real supports across the shopfront.
    for i in range(16):
        z=.5+i*.9
        cube((-5.05,3.12,z),(1.65,.13,.9),canvas if i%2 else orange)
        cube((-4.27,2.98,z),(.09,.3,.9),canvas if i%2 else orange)
    for z in (1,8,14):
        cube((-5.1,2.84,z),(1.2,.08,.07),teal)
        place('k','lampWall',(-5.7,2.7,z),1.3,-math.pi/2)
    # Two small seating groups leave the road and the collectible route clear.
    for z in (3,11.7):
        place('k','tableRound',(-4.25,.06,z),2.1)
        for dz,angle in [(-.9,0),(.9,math.pi)]:
            place('k','chairRounded',(-4.25,.06,z+dz),2.2,angle)
        place('k','plantSmall1',(-4.25,.84,z),.65)
    # A coffee service shelf by the open doorway and two planted ends.
    cube((-5.40,1.0,7.4),(.55,.12,1.6),material(TEXTURES+'/Wood.png'))
    place('k','kitchenCoffeeMachine',(-5.38,1.06,7.4),1.3,-math.pi/2)
    for z in (-.8,15.8):
        place('q','Prop_Planter_Single',(-4.8,.03,z),.47)
        place('k','pottedPlant',(-4.8,.2,z),2.2)
    for z in (2,14):place('q','Prop_Drain',(-3.1,.025,z),.6)
    # Projecting sign: a textured panel, wood frame and metal bracket.
    cube((-4.9,4.45,5.8),(1.6,.95,.12),teal)
    cube((-5.05,5.0,5.8),(1.5,.07,.07),teal)
    # Front-facing sign plane with explicit UVs, readable from the entrance camera.
    surface=material(TEXTURES+'/CafeSign.png')
    for ids in [(0,1,2),(0,2,3)]:
        verts=[(-5.65,4.02,-5.72),(-4.15,4.02,-5.72),(-4.15,4.88,-5.72),(-5.65,4.88,-5.72)]
        uvs=[(0,0),(1,0),(1,1),(0,1)]
        groups[json.dumps(surface,sort_keys=True)].append([(verts[i],uvs[i],(0,0,1)) for i in ids])

    # Solid colours do not each need a draw call. Use a palette atlas (the native
    # OBJ loader does not retain vertex colours), keeping the separate glass finish.
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
    # Assimp's implicit material is diffuse .6; explicit white avoids darkening
    # native material colours twice when the model is rendered with PBR.
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
    # Remove only obsolete files from this tool's explicitly owned output set.
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
