"""Generate a repository-owned scene comparing nested 1D, Cartesian 2D and Direct blending."""
import copy
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "Content"


def motion(clip="", tree="", threshold=0, position=(0, 0), parameter="weight", speed=1):
    return dict(clip=clip, blendTree=tree, threshold=threshold, position=list(position), parameter=parameter, speed=speed)


def tree(name, mode, children, x="speed", y="moveY"):
    return dict(name=name, type=mode, parameterX=x, parameterY=y, children=children)


def main():
    scene = json.loads((ROOT / "Assets/Scenes/AnimatorPlayground.json").read_text(encoding="utf-8"))
    template = scene["objects"].pop()
    scene["objects"] = [item for item in scene["objects"] if item["id"] not in ("Wall", "Platform")]
    for identity, x, definitions in [
        ("Player", -3, [
            tree("Locomotion", "1D", [motion("Idle", threshold=0), motion(tree="Gait", threshold=1)]),
            tree("Gait", "1D", [motion("Walk", threshold=0), motion("Walk", threshold=1, speed=2)]),
        ]),
        ("Cartesian", 0, [tree("Locomotion", "2D", [
            motion("Idle"), motion("Walk", position=(1, 0)), motion("Walk", position=(-1, 0)),
            motion("Jump", position=(0, 1)), motion("Jump", position=(0, -1)),
        ], x="moveX")]),
        ("Direct", 3, [tree("Locomotion", "Direct", [
            motion("Idle", parameter="MoveRight"), motion("Walk", parameter="MoveForward"), motion("Jump", parameter="speed"),
        ])]),
    ]:
        character = copy.deepcopy(template)
        character["id"] = identity
        character["name"] = identity + " Blend Tree"
        character["position"][0] = x
        animator = next(component for component in character["components"] if component["type"] == "Animator")
        animator.update(initialState="Locomotion", states=[dict(name="Locomotion", clip="", blendTree="Locomotion", speed=1, loop=True)], transitions=[], blendTrees=definitions)
        scene["objects"].append(character)
    path = ROOT / "Assets/Scenes/BlendTreePlayground.json"
    path.write_bytes((json.dumps(scene, ensure_ascii=False, indent=2) + "\n").replace("\n", "\r\n").encode("utf-8"))
    metadata = path.with_name(path.name + ".meta")
    if not metadata.exists():
        identity = hashlib.sha256(b"WP1/Assets/Scenes/BlendTreePlayground.json").hexdigest()[:32]
        metadata.write_bytes((json.dumps(dict(version=1, id=identity, scale=1, flipV=False, previousPaths=[]), indent=2) + "\n").replace("\n", "\r\n").encode("utf-8"))


if __name__ == "__main__":
    main()
