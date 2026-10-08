"""Generate owned translating/turning root clips and an in-place/moving comparison."""
import base64
import copy
import hashlib
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "Content"


def save(relative, value):
    path = ROOT / relative
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + "\n").replace("\n", "\r\n").encode("utf-8"))
    metadata = path.with_name(path.name + ".meta")
    if not metadata.exists():
        identity = hashlib.sha256(("WP1/" + relative).encode()).hexdigest()[:32]
        metadata.write_bytes((json.dumps(dict(version=1, id=identity, scale=1, flipV=False, previousPaths=[]), indent=2) + "\n").replace("\n", "\r\n").encode("utf-8"))


def main():
    model = json.loads((ROOT / "Assets/Models/IkBox.gltf").read_text(encoding="utf-8"))
    binary = bytearray(base64.b64decode(model["buffers"][0]["uri"].split(",")[1]))

    def accessor(values, kind, count, limits=None):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        binary.extend(struct.pack("<" + "f" * len(values), *values))
        view = len(model["bufferViews"])
        model["bufferViews"].append(dict(buffer=0, byteOffset=offset, byteLength=len(values) * 4))
        index = len(model["accessors"])
        item = dict(bufferView=view, componentType=5126, count=count, type=kind)
        if limits:
            item.update(min=limits[0], max=limits[1])
        model["accessors"].append(item)
        return index

    times = accessor([0, 1], "SCALAR", 2, ([0], [1]))
    positions = accessor([0, 0, 0, 1, 0, 0], "VEC3", 2)
    rotations = accessor([0, 0, 0, 1, 0, math.sin(math.pi / 4), 0, math.cos(math.pi / 4)], "VEC4", 2)
    root = next(index for index, node in enumerate(model["nodes"]) if node["name"] == "Root")
    advance = dict(name="Advance", samplers=[dict(input=times, output=positions, interpolation="LINEAR")], channels=[dict(sampler=0, target=dict(node=root, path="translation"))])
    turn = copy.deepcopy(advance)
    turn["name"] = "Turn"
    turn["samplers"].append(dict(input=times, output=rotations, interpolation="LINEAR"))
    turn["channels"].append(dict(sampler=1, target=dict(node=root, path="rotation")))
    model["animations"] = [advance, turn]
    model["buffers"][0] = dict(byteLength=len(binary), uri="data:application/octet-stream;base64," + base64.b64encode(binary).decode())
    save("Assets/Models/RootMotionBox.gltf", model)
    scene = json.loads((ROOT / "Assets/Scenes/IkPlayground.json").read_text(encoding="utf-8"))
    template = copy.deepcopy(scene["objects"][3])
    scene["objects"] = scene["objects"][:3]
    scene["assetReferences"].pop("Assets/Models/IkBox.gltf", None)
    scene["assetReferences"]["Assets/Models/RootMotionBox.gltf"] = json.loads((ROOT / "Assets/Models/RootMotionBox.gltf.meta").read_text(encoding="utf-8"))["id"]
    for index, (enabled, clip) in enumerate(((False, "Advance"), (True, "Advance"), (True, "Turn"))):
        actor = copy.deepcopy(template)
        actor.update(id="Root" + str(index), name="Root motion " + str(enabled) + " " + clip, position=[(index - 1) * 3, 0, 0])
        actor["components"] = [item for item in actor["components"] if item["type"] in ("MeshRenderer", "Animator")]
        mesh, animator = actor["components"]
        mesh["model"] = "Assets/Models/RootMotionBox.gltf"
        animator.update(initialState="Move", states=[dict(name="Move", clip=clip, speed=1, loop=True)], transitions=[], ik=[], rootMotion=enabled, rootBone="Root")
        scene["objects"].append(actor)
    save("Assets/Scenes/RootMotionPlayground.json", scene)


if __name__ == "__main__":
    main()
