"""Author editable title clips. Runtime builds do not execute this script."""
import json
from pathlib import Path


def track(property, clock, keys, easing="smooth", delay=0, loop=False):
    def vector(value):
        if isinstance(value, (int, float)):
            value = [value]
        return list(value) + [0] * (4 - len(value))
    return dict(property=property, clock=clock, easing=easing, delay=delay, loop=loop,
                keys=[dict(time=time, value=vector(value)) for time, value in keys])


def apply_animation(layout):
    nodes = {node["id"]: node for node in layout["objects"]}

    def animate(identifier, tracks):
        node = nodes[identifier]
        node["components"] = [component for component in node["components"] if component["type"] != "Animation"]
        node["components"].append(dict(type="Animation", id="animation", enabled=True, tracks=tracks))

    canvas = next(c for c in nodes["title-canvas"]["components"] if c["type"] == "Canvas")
    canvas["stateDefaults"].update(introDuration=2.2, startDuration=0.8, sceneTime=3, motionTime=3)
    camera = nodes["scene-camera"]
    position, rotation = camera["position"], camera["rotation"]
    animate("scene-camera", [
        track("position", "motionTime", [(0, position), (8, [-0.5, 2.28, -8.25]), (16, [-1.05, 2.15, -7.8]), (24, position)], delay=2.4, loop=True),
        track("rotation", "motionTime", [(0, rotation), (8, [-0.145, 0.038, -0.06]), (16, [-0.135, 0.02, -0.07]), (24, rotation)], delay=2.4, loop=True),
        track("position", "startTime", [(0, position), (0.24, [-0.8, 2.2, -7.5]), (0.8, [0.8, 2.8, 2])], "smooth"),
        track("rotation", "startTime", [(0, rotation), (0.8, [-0.075, 0.11, 0.025])], "smooth"),
    ])
    animate("world-logo", [
        track("uiPosition", "sceneTime", [(0, [1370, 58]), (0.42, [1370, 58]), (1.12, [674, 58])], "outCubic"),
        track("uiRotation", "sceneTime", [(0, -0.16), (0.42, -0.16), (1.0, 0.012), (1.25, 0)], "smooth"),
        track("uiSize", "sceneTime", [(0, [620, 332]), (0.42, [620, 332]), (1.10, [532, 285]), (1.3, [540, 289])], "smooth"),
        track("opacity", "sceneTime", [(0, 0), (0.42, 0), (0.55, 1)]),
        track("uiRotation", "motionTime", [(0, 0), (3, 0.004), (6, 0), (9, -0.004), (12, 0)], delay=2.4, loop=True),
        track("uiSize", "startTime", [(0, [540, 289]), (0.16, [564, 302]), (0.8, [680, 364])]),
        track("uiPosition", "startTime", [(0, [674, 58]), (0.8, [590, -40])]),
    ])
    animate("morning-caption", [
        track("uiPosition", "sceneTime", [(0, [1320, 325]), (0.75, [1320, 325]), (1.4, [638, 325])], "outCubic"),
        track("opacity", "sceneTime", [(0, 0), (0.75, 0), (1.1, 1)]),
        track("opacity", "startTime", [(0, 1), (0.2, 0)]),
    ])
    animate("world-start", [
        track("uiPosition", "sceneTime", [(0, [-520, 590]), (1.25, [-520, 590]), (1.9, [432, 563])], "outCubic"),
        track("opacity", "sceneTime", [(0, 0), (1.25, 0), (1.4, 1)]),
        track("uiRotation", "sceneTime", [(0, -0.12), (1.25, -0.12), (1.9, 0)], "outCubic"),
        track("uiPosition", "motionTime", [(0, [432, 563]), (0.7, [435, 561]), (1.4, [432, 563]), (2.1, [429, 565]), (2.8, [432, 563])], delay=2.4, loop=True),
        track("uiSize", "startTime", [(0, [455, 104]), (0.1, [492, 112]), (0.3, [455, 104])], "outCubic"),
        track("opacity", "startTime", [(0, 1), (0.08, 0.45), (0.16, 1), (0.32, 0)]),
    ])
    for identifier in ["world-footer", "morning-help"]:
        animate(identifier, [track("opacity", "sceneTime", [(0, 0), (1.65, 0), (2.1, 1)]),
                             track("opacity", "startTime", [(0, 1), (0.15, 0)])])


if __name__ == "__main__":
    path = Path(__file__).resolve().parent.parent / "Content/Assets/Scenes/TitleStreet.json"
    scene = json.loads(path.read_text(encoding="utf-8"))
    apply_animation(scene)
    path=Path(__file__).resolve().parent.parent / "generated/authoring/TitleAnimation.json"
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes((json.dumps(scene, ensure_ascii=False, indent=2) + "\n").replace("\n", "\r\n").encode("utf-8"))
    print("Authored editable intro, idle and start clips")
