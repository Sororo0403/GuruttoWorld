"""Rebuild the repository-owned physics sample and round primitive meshes."""
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "Content"


def write(path, text):
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    meta = target.with_name(target.name + ".meta")
    if not meta.exists():
        identity = hashlib.sha256(("WP1/" + path).encode()).hexdigest()[:32]
        meta.write_text(json.dumps(dict(version=1, id=identity, scale=1, flipV=False, previousPaths=[]), indent=2) + "\n", encoding="utf-8")


def round_model(path, half_height=0):
    rings, sides = 24, 32
    output = ["# Repository-authored round physics primitive"]
    points, normals = [], []
    for ring in range(rings + 1):
        latitude = math.pi * (ring / rings - 0.5)
        for side in range(sides + 1):
            longitude = math.tau * side / sides
            normal = (math.cos(latitude) * math.cos(longitude), math.sin(latitude), math.cos(latitude) * math.sin(longitude))
            height = (-half_height if ring < rings / 2 else half_height)
            points.append((normal[0] * 0.5, normal[1] * 0.5 + height, normal[2] * 0.5))
            normals.append(normal)
    for point in points:
        output.append("v " + " ".join(f"{p:.8f}" for p in point))
    for normal in normals:
        output.append("vn " + " ".join(f"{p:.8f}" for p in normal))
    for ring in range(rings):
        for side in range(sides):
            a = ring * (sides + 1) + side + 1
            b, c, d = a + 1, a + sides + 1, a + sides + 2
            # Counter-clockwise outward faces; Assimp converts to the renderer's convention.
            for face in ((a, c, b), (b, c, d)):
                if ring == 0 and face == (a, c, b):
                    continue
                if ring == rings - 1 and face == (b, c, d):
                    continue
                output.append("f " + " ".join(f"{v}//{v}" for v in face))
    write(path, "\n".join(output) + "\n")


def component(kind, **properties):
    return dict(id=kind.lower(), type=kind, enabled=True, **properties)


def collider(shape="box", **properties):
    return component("BoxCollider", center=[0, 0, 0], size=[1.5, 1.5, 1.5], shape=shape, **properties)


def obj(identity, position, scale, components, rotation=None):
    return dict(id=identity, name=identity, parent="", position=position, rotation=rotation or [0, 0, 0], scale=scale, components=components)


def mesh(model="Cube.obj", material="PhysicsNeutral.mat"):
    return component("MeshRenderer", model="Assets/Models/" + model, material="Assets/Materials/" + material)


def main():
    round_model("Assets/Models/PhysicsSphere.obj")
    round_model("Assets/Models/PhysicsCapsule.obj", 0.5)
    for name, color, transparent in (
        ("PhysicsNeutral", [0.42, 0.48, 0.58, 1], False),
        ("PhysicsPlayer", [0.2, 0.65, 1, 1], False),
        ("PhysicsBall", [1, 0.35, 0.15, 1], False),
        ("PhysicsTrigger", [0.15, 0.9, 0.4, 0.2], True),
    ):
        write(f"Assets/Materials/{name}.mat", json.dumps(dict(version=1, color=color, roughness=0.55, metallic=0, transparent=transparent, texture=""), indent=2) + "\n")
    scene = dict(version=4, settings=dict(background=[0.025, 0.035, 0.055, 1], mainCamera="Camera"), objects=[
        obj("Camera", [0, 11, -19], [1, 1, 1], [component("Camera", verticalFov=45, nearClip=0.1, farClip=150, referenceAspect=16/9, preserveHorizontal=True)], [0.45, 0, 0]),
        obj("Light", [0, 0, 0], [1, 1, 1], [component("DirectionalLight", direction=[0.3, -1, 0.5], color=[1, 1, 1], intensity=0.85, ambient=0.25, specular=0.3, shininess=32, shadowsEnabled=True)]),
        obj("Floor", [0, -0.375, 0], [14, 0.5, 12], [mesh(), collider()]),
        obj("Ramp", [-4, 0.75, 3], [5, 0.5, 3], [mesh(), collider()], [0, 0, 0.3]),
        obj("MovingFloor", [4, 1.5, 3], [2.5, 0.35, 2.5], [mesh(), collider(), component("RigidBody", motion="kinematic"), component("Script", behaviour="Bob", parameters=dict(amplitude=1, frequency=0.2))]),
        obj("Player", [0, 2, -4], [1, 1, 1], [mesh("PhysicsCapsule.obj", "PhysicsPlayer.mat"), collider("capsule", radius=0.5, halfHeight=0.5), component("PlayerController", moveSpeed=5, useGravity=True, gravity=20, jumpSpeed=7, usePhysics=True, maxSlopeDegrees=45, stepHeight=0.3)]),
        obj("Trigger", [0, 1, 3], [2, 1.5, 1], [mesh(material="PhysicsTrigger.mat"), collider(isTrigger=True)]),
    ])
    for i in range(4):
        scene["objects"].append(obj(f"Ball{i+1}", [2 + i * 1.3, 4 + i, -1], [1, 1, 1], [mesh("PhysicsSphere.obj", "PhysicsBall.mat"), collider("sphere", radius=0.5), component("RigidBody", mass=1+i, restitution=0.6, friction=0.5, continuous=True)]))
    for i in range(3):
        scene["objects"].append(obj(f"Box{i+1}", [-3, 1 + i * 1.6, -2], [1, 1, 1], [mesh(), collider(), component("RigidBody", mass=2, friction=0.8)]))
    write("Assets/Scenes/RigidBodyPlayground.json", json.dumps(scene, indent=2) + "\n")


if __name__ == "__main__":
    main()
