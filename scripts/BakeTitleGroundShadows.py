"""Bake directional shadows into a world-space ground atlas, retaining source UV detail.

Explicit authoring tool, Python + NumPy + Pillow. Static title geometry only.
Run after editing street geometry/light; builds never run this or change a scene.
"""
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from AuthorTitleMenu import save_scene

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "Content"
SCENE = CONTENT / "Assets/Scenes/TitleStreet.json"
OUT = CONTENT / "Assets/Models/Title/Shadows"
SIZE = 2048
BOUNDS = (-40, -20, 40, 100)


def read_obj(path):
    vertices, uvs, faces, materials = [], [], [], {}
    material = ""
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v":
            vertices.append(list(map(float, fields[1:4])))
        elif fields[0] == "vt":
            uvs.append(list(map(float, fields[1:3])))
        elif fields[0] == "usemtl":
            material = fields[1]
        elif fields[0] == "mtllib":
            current = ""
            for m in (path.parent / fields[1]).read_text(encoding="utf-8").splitlines():
                f = m.split()
                if not f:
                    continue
                if f[0] == "newmtl":
                    current = f[1]; materials[current] = ([1,1,1], None)
                elif f[0] == "Kd":
                    materials[current] = (list(map(float, f[1:4])), materials[current][1])
                elif f[0] == "map_Kd":
                    materials[current] = (materials[current][0], path.parent / f[1])
        elif fields[0] == "f":
            corners = [tuple(int(n) if n else 0 for n in c.split("/")) for c in fields[1:]]
            for i in range(1, len(corners)-1):
                faces.append((material, [corners[0], corners[i], corners[i+1]]))
    return np.array(vertices), np.array(uvs), faces, materials


def world_vertices(vertices, node):
    # Source OBJ is right handed; title placements are DirectX left handed.
    v = vertices * np.array([1,1,-1]) * np.array(node["scale"])
    pitch, yaw, roll = node["rotation"]
    cx,sx,cy,sy,cz,sz = math.cos(pitch),math.sin(pitch),math.cos(yaw),math.sin(yaw),math.cos(roll),math.sin(roll)
    rx=np.array([[1,0,0],[0,cx,sx],[0,-sx,cx]])
    ry=np.array([[cy,0,-sy],[0,1,0],[sy,0,cy]])
    rz=np.array([[cz,sz,0],[-sz,cz,0],[0,0,1]])
    return v @ (rz @ rx @ ry) + np.array(node["position"])


def pixels(points):
    return np.column_stack(((points[:,0]-BOUNDS[0])/(BOUNDS[2]-BOUNDS[0])*(SIZE-1),
                            (points[:,2]-BOUNDS[1])/(BOUNDS[3]-BOUNDS[1])*(SIZE-1)))


def bake():
    scene=json.loads(SCENE.read_text(encoding="utf-8"))
    nodes={n["id"]:n for n in scene["objects"]}
    # Re-baking uses the retained original placements, rather than an already baked mesh.
    manifest=OUT / "GroundSources.json"
    grounds=[n for n in scene["objects"] if n["id"].startswith(("road-","sidewalk-","plaza-paving-"))]
    if manifest.exists():
        sources={n["id"]:n for n in json.loads(manifest.read_text(encoding="utf-8"))}
        if not grounds:
            grounds=list(sources.values())
            scene["objects"]=[n for n in scene["objects"] if n["id"]!="shadowed-ground"]
            scene["objects"][1:1]=grounds
        for n in grounds:
            mesh=next(c for c in n["components"] if c["type"]=="MeshRenderer")
            if "/Shadows/" in mesh["model"]:
                mesh["model"]=next(c["model"] for c in sources[n["id"]]["components"] if c["type"]=="MeshRenderer")
    else:
        OUT.mkdir(parents=True,exist_ok=True)
        manifest.write_text(json.dumps(grounds,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    light=next(c for n in scene["objects"] for c in n["components"] if c["type"]=="DirectionalLight")
    direction=np.array(light["direction"])
    shadow=Image.new("L",(SIZE,SIZE),0)
    draw=ImageDraw.Draw(shadow)
    cache={}
    for n in scene["objects"]:
        if not ("building" in n["id"] or n["id"].startswith(("skyline-","street-light-","config-cog"))):
            continue
        mesh=next((c for c in n["components"] if c["type"]=="MeshRenderer"),None)
        if not mesh:
            continue
        path=CONTENT / mesh["model"]
        if path not in cache:
            cache[path]=read_obj(path)
        vertices,_,faces,_=cache[path]
        points=world_vertices(vertices,n)
        projected=points.copy()
        projected += ((.13-points[:,1])/direction[1])[:,None]*direction
        p=pixels(projected)
        for _,corners in faces:
            draw.polygon([tuple(p[c[0]-1]) for c in corners],fill=255)
    mask=np.asarray(shadow.filter(ImageFilter.GaussianBlur(.65)),dtype=float)/255
    atlas=np.zeros((SIZE,SIZE,3),dtype=np.uint8)
    atlas[:]=[150,155,159]
    height=np.full((SIZE,SIZE),-100.0)
    count=0
    for n in grounds:
        lines=["# Kenney CC0 ground with authored static directional shadows", "mtllib Ground.mtl", "usemtl ground"]
        local_count=0
        mesh=next(c for c in n["components"] if c["type"]=="MeshRenderer")
        vertices,uvs,faces,materials=read_obj(CONTENT / mesh["model"])
        points=world_vertices(vertices,n)
        textures={name: (np.asarray(Image.open(p).convert("RGB")) if p else np.full((1,1,3),255))
                  for name,(_,p) in materials.items()}
        for material,corners in faces:
            v=points[[c[0]-1 for c in corners]]
            # Retain source triangulation and winding through the RH/LH round trip.
            uv=pixels(v)/(SIZE-1)
            for xyz in vertices[[corner[0]-1 for corner in corners]]:
                lines.append(f"v {xyz[0]:.7f} {xyz[1]:.7f} {xyz[2]:.7f}")
            for t in uv:
                lines.append(f"vt {t[0]:.8f} {1-t[1]:.8f}")
            ids=[local_count+i+1 for i in range(3)]
            lines.append("f "+" ".join(f"{i}/{i}" for i in ids)); count+=3; local_count+=3
            p=pixels(v)
            low=np.maximum(np.floor(p.min(axis=0)).astype(int),0)
            high=np.minimum(np.ceil(p.max(axis=0)).astype(int),SIZE-1)
            denominator=(p[1,1]-p[2,1])*(p[0,0]-p[2,0])+(p[2,0]-p[1,0])*(p[0,1]-p[2,1])
            if abs(denominator)<1e-6 or np.any(high<low):
                continue
            xx,yy=np.meshgrid(np.arange(low[0],high[0]+1),np.arange(low[1],high[1]+1))
            a=((p[1,1]-p[2,1])*(xx-p[2,0])+(p[2,0]-p[1,0])*(yy-p[2,1]))/denominator
            b=((p[2,1]-p[0,1])*(xx-p[2,0])+(p[0,0]-p[2,0])*(yy-p[2,1]))/denominator
            c=1-a-b
            h=a*v[0,1]+b*v[1,1]+c*v[2,1]
            valid=(a>=-.001)&(b>=-.001)&(c>=-.001)&(h>=height[yy,xx])
            if not valid.any():
                continue
            old=uvs[[corner[1]-1 for corner in corners]]
            u=a*old[0,0]+b*old[1,0]+c*old[2,0]
            vt=a*old[0,1]+b*old[1,1]+c*old[2,1]
            tex=textures[material]
            tx=(u%1*(tex.shape[1]-1)).astype(int)
            ty=((1-vt)%1*(tex.shape[0]-1)).astype(int)
            color=tex[ty,tx].astype(float)*np.array(materials[material][0])
            # Blue ambient remains in shadow; street markings and paving detail survive.
            shade=1-mask[yy,xx,None]*np.array([.48,.40,.29])
            color=np.clip(color*shade,0,255).astype(np.uint8)
            atlas[yy[valid],xx[valid]]=color[valid]
            height[yy[valid],xx[valid]]=h[valid]
        (OUT/(n["id"]+".obj")).write_text("\n".join(lines)+"\n",encoding="utf-8")
        mesh["model"]="Assets/Models/Title/Shadows/"+n["id"]+".obj"
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/"Ground.mtl").write_text("newmtl ground\nKd 1 1 1\nmap_Kd Ground.png\n",encoding="utf-8")
    Image.fromarray(atlas).save(OUT/"Ground.png")
    save_scene(scene)
    print(f"Baked {len(grounds)} ground placements; {count//3} source triangles")


if __name__ == "__main__":
    bake()
