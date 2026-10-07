"""Generate repository-owned PBR materials, tangent-space normals and sample scene."""
import hashlib
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "Content"


def write(path, content):
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(content if isinstance(content, bytes) else content.encode("utf-8"))
    meta = target.with_name(target.name + ".meta")
    if not meta.exists():
        identity = hashlib.sha256(("WP1/" + path).encode()).hexdigest()[:32]
        meta.write_text(json.dumps(dict(version=1, id=identity, scale=1, flipV=False, previousPaths=[]), indent=2) + "\n", encoding="utf-8")


def component(kind, **properties):
    return dict(id=kind.lower(), type=kind, enabled=True, **properties)


def obj(identity, position, scale, components, rotation=None):
    return dict(id=identity, name=identity, parent="", position=position, rotation=rotation or [0, 0, 0], scale=scale, components=components)


def main():
    # Analytic periodic relief: normals encode (-dh/du,-dh/dv,1) in linear RGB.
    size = 64
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            dx = .6 * math.cos(x / size * math.tau * 4)
            dy = .6 * math.cos(y / size * math.tau * 4)
            inverse = 1 / math.sqrt(dx * dx + dy * dy + 1)
            rgb = [round((n * inverse * .5 + .5) * 255) for n in (-dx, -dy, 1)]
            pixels.extend(reversed(rgb))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
    # Top-down rows agree with texture UVs, and 64*3 is already DWORD aligned.
    header += struct.pack("<IiiHHIIiiII", 40, size, -size, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    write("Assets/Textures/PbrRelief.bmp", header + pixels)
    scene = dict(version=4, settings=dict(background=[.025, .035, .055, 1], mainCamera="Camera"), objects=[
        obj("Camera", [0, 6, -13], [1, 1, 1], [component("Camera", verticalFov=45, nearClip=.1, farClip=100, referenceAspect=16/9, preserveHorizontal=True)], [.3, 0, 0]),
        obj("Light", [0, 0, 0], [1, 1, 1], [component("DirectionalLight", direction=[.3, -.6, 1], color=[1, 1, 1], intensity=2, ambient=.08, specular=.4, shininess=32, shadowsEnabled=True)]),
        obj("Floor", [0, -.5, 0], [10, .3, 7], [component("MeshRenderer", model="Assets/Models/Cube.obj", material="Assets/Materials/PbrFloor.mat")]),
    ])
    materials = [("PbrFloor", [.4, .43, .48, 1], 1, 0, False)]
    for row, metallic in enumerate((0, 1)):
        for column, roughness in enumerate((.2, .5, .9)):
            name = f"Pbr{'Metal' if metallic else 'Dielectric'}{column}"
            materials.append((name, [.85, .52, .16, 1], roughness, metallic, False))
            scene["objects"].append(obj(name, [(column-1)*2.6, .8, row*2.4], [2, 2, 2], [component("MeshRenderer", model="Assets/Models/PhysicsSphere.obj", material=f"Assets/Materials/{name}.mat")]))
    for index, mapped in enumerate((False, True)):
        name = "PbrRelief" if mapped else "PbrSmooth"
        materials.append((name, [.3, .55, .75, 1], .65, 0, mapped))
        scene["objects"].append(obj(name, [(index-.5)*3, .65, -3], [1.2, 1.2, 1.2], [component("MeshRenderer", model="Assets/Models/Cube.obj", material=f"Assets/Materials/{name}.mat")], [0, .2, 0]))
    for name, color, roughness, metallic, normal in materials:
        write(f"Assets/Materials/{name}.mat", json.dumps(dict(color=color, roughness=roughness, metallic=metallic, physicallyBased=True, transparent=False, texture="", normalTexture="Assets/Textures/PbrRelief.bmp" if normal else "", normalFlipY=False), indent=2) + "\n")
    write("Assets/Scenes/RenderingPlayground.json", json.dumps(scene, indent=2) + "\n")


if __name__ == "__main__":
    main()
