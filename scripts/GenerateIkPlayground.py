"""Create an owned three-node IK chain and a zero/half/full-weight comparison scene."""
import copy
import hashlib
import json
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
    model = json.loads((ROOT / "Assets/Models/AnimatedBox.gltf").read_text(encoding="utf-8"))
    upper = next(node for node in model["nodes"] if node["name"] == "Upper")
    upper["children"] = [len(model["nodes"])]
    model["nodes"].append(dict(name="Tip", translation=[0, 1, 0]))
    save("Assets/Models/IkBox.gltf", model)
    scene = json.loads((ROOT / "Assets/Scenes/BlendTreePlayground.json").read_text(encoding="utf-8"))
    template = scene["objects"][3]
    scene["objects"] = scene["objects"][:3]
    scene["assetReferences"].pop("Assets/Models/AnimatedBox.gltf", None)
    scene["assetReferences"]["Assets/Models/IkBox.gltf"] = json.loads((ROOT / "Assets/Models/IkBox.gltf.meta").read_text(encoding="utf-8"))["id"]
    for index, weight in enumerate((0, .5, 1)):
        actor = copy.deepcopy(template)
        actor.update(id="Ik" + str(index), name="IK weight " + str(weight), position=[(index - 1) * 3, 0, 0])
        actor["components"] = [item for item in actor["components"] if item["type"] in ("MeshRenderer", "Animator")]
        mesh, animator = actor["components"]
        mesh["model"] = "Assets/Models/IkBox.gltf"
        world = index == 2
        target = [.75 + (actor["position"][0] if world else 0), 1.3, 0]
        hint = [2 + (actor["position"][0] if world else 0), 1, 0]
        animator.update(initialState="Idle", states=[dict(name="Idle", clip="Idle", speed=1, loop=True)], transitions=[], blendTrees=[],
                        ik=[dict(name="reach", root="Root", middle="Upper", tip="Tip", enabled=True, target=target, hint=hint, weight=weight, worldSpace=world)])
        actor["components"].append(dict(id="orbit", type="Script", enabled=True, behaviour="IkOrbit", parameters=dict(radius=.25, frequency=.3, weight=weight)))
        scene["objects"].append(actor)
    save("Assets/Scenes/IkPlayground.json", scene)


if __name__ == "__main__":
    main()
