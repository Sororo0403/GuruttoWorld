"""Bake static street scenery into spatial OBJ batches, preserving collision objects."""
import copy
import json
import math
import os
from collections import defaultdict
from pathlib import Path

GAME = Path(__file__).resolve().parents[1] / 'Games/GuruttoWorld'
FOLDER = 'Assets/Models/Sunrise/Batches'


def rotate(vector, angles):
    x, y, z = vector
    for axis, angle in enumerate(angles):
        c, s = math.cos(angle), math.sin(angle)
        if axis == 0:
            y, z = y*c-z*s, y*s+z*c
        elif axis == 1:
            x, z = x*c+z*s, -x*s+z*c
        else:
            x, y = x*c-y*s, x*s+y*c
    return x, y, z


def normalize(vector):
    length = math.sqrt(sum(v*v for v in vector)) or 1
    return tuple(v/length for v in vector)


def read_model(path):
    vertices, uvs, normals, faces, materials = [], [], [], [], {}
    selected = None
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        words = line.split()
        if not words or words[0].startswith('#'):
            continue
        kind = words[0]
        if kind == 'mtllib':
            for library in words[1:]:
                mtl_path = path.parent / library
                name = None
                for record in mtl_path.read_text(encoding='utf-8-sig').splitlines():
                    parts = record.split()
                    if not parts:
                        continue
                    if parts[0] == 'newmtl':
                        name = parts[1]
                        materials[name] = {'color': [1, 1, 1], 'texture': ''}
                    elif name and parts[0] == 'Kd':
                        materials[name]['color'] = list(map(float, parts[1:4]))
                    elif name and parts[0] == 'map_Kd':
                        materials[name]['texture'] = str((mtl_path.parent / ' '.join(parts[1:])).resolve())
        elif kind == 'v':
            vertices.append(list(map(float, words[1:])))
        elif kind == 'vt':
            uvs.append(tuple(map(float, words[1:3])))
        elif kind == 'vn':
            normals.append(tuple(map(float, words[1:4])))
        elif kind == 'usemtl':
            selected = words[1]
        elif kind == 'f':
            corners = []
            for token in words[1:]:
                indices = token.split('/')
                corner = []
                for i, count in enumerate((len(vertices), len(uvs), len(normals))):
                    value = int(indices[i]) if i < len(indices) and indices[i] else 0
                    corner.append(value-1 if value > 0 else count+value if value < 0 else None)
                corners.append(corner)
            faces.append((selected, corners))
    return vertices, uvs, normals, faces, materials


def batch_static(source, folder=FOLDER):
    scene = copy.deepcopy(source)
    batches = defaultdict(lambda: defaultdict(list))
    cached, signatures, preserved, baked = {}, {}, [], []
    referenced = set()
    def references(value):
        if isinstance(value, dict):
            if value.get('kind') == 'reference':
                referenced.add(value.get('value'))
            for child in value.values():
                references(child)
        elif isinstance(value, list):
            for child in value:
                references(child)
    references(source)
    for item in scene['objects']:
        components = {c['type']: c for c in item['components']}
        renderer = components.get('MeshRenderer')
        if (item['parent'] or item['id'] in referenced or not renderer or renderer.get('visibleWhen') or
                item['name'] == 'ParkEnemy' or
                set(components) - {'MeshRenderer', 'Material', 'BoxCollider'}):
            preserved.append(item)
            continue
        material = None
        if 'Material' in components:
            material = json.loads((GAME / components['Material']['asset']).read_text(encoding='utf-8'))
            if material.get('transparent'):
                preserved.append(item)
                continue
        path = GAME / renderer['model']
        if path not in cached:
            cached[path] = read_model(path)
        vertices, uvs, normals, faces, materials = cached[path]
        zone = math.floor((item['position'][2]+20)/32)
        for name, corners in faces:
            # Assimp's default OBJ material uses diffuse 0.6 when there is no MTL.
            surface = copy.deepcopy(materials.get(name, {'color': [.6, .6, .6], 'texture': ''}))
            if material:
                surface['color'] = [surface['color'][i]*material['color'][i] for i in range(3)]
            signature = json.dumps(surface, sort_keys=True)
            if signature not in signatures:
                signatures[signature] = (f'surface{len(signatures)}', surface)
            world = []
            for vi, ti, ni in corners:
                vertex = vertices[vi]
                # Importer converts OBJ to left-handed space before applying scene SRT.
                # Bake that exact transform, then convert back for the exported OBJ.
                transformed = rotate([vertex[i]*item['scale'][i]*(-1 if i == 2 else 1) for i in range(3)], item['rotation'])
                position = tuple((transformed[i]+item['position'][i])*(-1 if i == 2 else 1) for i in range(3))
                normal = None
                if ni is not None:
                    rotated = rotate([normals[ni][i]/item['scale'][i]*(-1 if i == 2 else 1) for i in range(3)], item['rotation'])
                    normal = normalize([rotated[i]*(-1 if i == 2 else 1) for i in range(3)])
                color = vertex[3:6] if len(vertex) >= 6 else [1, 1, 1]
                world.append((position, uvs[ti] if ti is not None else (0, 0), normal, color))
            if any(c[2] is None for c in world):
                a = [world[1][0][i]-world[0][0][i] for i in range(3)]
                b = [world[2][0][i]-world[0][0][i] for i in range(3)]
                normal = normalize((a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]))
                world = [(p, uv, n or normal, color) for p, uv, n, color in world]
            batches[zone][signature].append(world)
        baked.append(item['id'])
        if 'BoxCollider' in components:
            item['components'] = [components['BoxCollider']]
            preserved.append(item)

    assets = {}
    mtl = ['# Derived from the existing title CC0 models and original Sunrise materials.']
    for name, surface in signatures.values():
        mtl += [f'newmtl {name}', 'Kd '+' '.join(map(str, surface['color']))]
        if surface['texture']:
            relative = os.path.relpath(surface['texture'], GAME / folder).replace('\\', '/')
            mtl.append('map_Kd '+relative)
    assets[f'{folder}/Street.mtl'] = '\n'.join(mtl)+'\n'
    for zone, surfaces in sorted(batches.items()):
        lines = ['# Static geometry baked by AuthorSunriseStreet.py', 'mtllib Street.mtl']
        index = 1
        unique = {}
        for signature, polygons in surfaces.items():
            name = signatures[signature][0]
            lines += [f'g {name}', f'usemtl {name}']
            for polygon in polygons:
                face = []
                for p, uv, normal, color in polygon:
                    key = tuple(f'{v:.8g}' for v in (*p, *uv, *normal))
                    if key not in unique:
                        unique[key] = index
                        lines += ['v '+' '.join(key[:3]), 'vt '+' '.join(key[3:5]), 'vn '+' '.join(key[5:])]
                        index += 1
                    vertex = unique[key]
                    face.append(f'{vertex}/{vertex}/{vertex}')
                lines.append('f '+' '.join(face))
        path = f'{folder}/Street-{zone}.obj'
        assets[path] = '\n'.join(lines)+'\n'
        preserved.append(dict(id=f'static-street-{zone}', name=f'街の静的描画 {zone}', parent='',
            position=[0, 0, 0], rotation=[0, 0, 0], scale=[1, 1, 1], components=[
                dict(type='MeshRenderer', id='mesh', enabled=True, model=path, instancing=False)]))
    scene['objects'] = preserved
    assets[f'{folder}/Sources.json'] = json.dumps(dict(originalObjects=len(source['objects']),
        optimizedObjects=len(preserved), bakedObjects=baked,
        sourceModels=[p.relative_to(GAME).as_posix() for p in cached]), ensure_ascii=False, indent=2)+'\n'
    return scene, assets
