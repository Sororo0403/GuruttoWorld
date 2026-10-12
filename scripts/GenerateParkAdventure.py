"""Author and assemble the park's bounded gameplay modules; never run during builds."""
import argparse
import copy
import hashlib
import json
import math
import random
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CONTENT = REPO / "Content"
MODULES = CONTENT / "Assets/Prefabs/Park"
ORDER = ("Entrance", "Terraces", "Canal", "SwitchGarden", "Vista")
ORIGINS = (0, 16, 32, 48, 64)
PALETTE = {
    "Grass": [0.24, 0.58, 0.32, 1], "Path": [0.88, 0.79, 0.57, 1],
    "Stone": [0.56, 0.65, 0.62, 1], "Water": [0.18, 0.63, 0.78, 1],
    "Wood": [0.46, 0.27, 0.14, 1], "Leaf": [0.16, 0.43, 0.24, 1],
    "LeafLight": [0.48, 0.72, 0.30, 1], "Petal": [1, 0.68, 0.30, 1],
    "Player": [0.42, 0.78, 0.92, 1], "Enemy": [0.91, 0.37, 0.24, 1],
    "Eye": [0.025, 0.06, 0.10, 1], "White": [0.98, 0.98, 0.90, 1],
    "Switch": [0.93, 0.70, 0.16, 1], "Gate": [0.18, 0.40, 0.47, 1],
}


def component(kind, **values):
    if kind == "Script" and "data" in values:
        values["data"] = {"kind": "object", "value": values["data"]}
    return dict(type=kind, id=kind.lower(), enabled=True, **values)


def obj(identity, position, scale=(1, 1, 1), components=(), name=None, parent="", rotation=(0, 0, 0)):
    return dict(id=identity, name=name or identity, parent=parent, position=list(position),
                rotation=list(rotation), scale=list(scale), components=list(components))


def mesh(material, sphere=False, visible=""):
    return [component("MeshRenderer", model="Assets/Models/PhysicsSphere.obj" if sphere else
                      "Assets/Models/Park/UnitBox.obj", visibleWhen=visible, instancing=True),
            component("Material", asset=f"Assets/Materials/Park/{material}.mat")]


def box(identity, position, size, material, solid=False):
    parts = mesh(material)
    if solid:
        parts.append(component("BoxCollider", center=[0, 0, 0], size=[1, 1, 1]))
    return obj(identity, position, size, parts)


def ball(identity, position, size, material, **values):
    return obj(identity, position, size, mesh(material, True), **values)


def character(identity, position, material, name):
    root = obj(identity, position, components=mesh(material, True), name=name)
    if name == "ParkEnemy":
        root["scale"] = [1.3, 1.2, 1.3]
        root["components"].append(component("Script", behaviour="ParkPatrol", parameters={}))
    else:
        root["components"] += [
            component("BoxCollider", shape="capsule", center=[0, 0, 0], size=[1, 1, 1], radius=0.35, halfHeight=0.35),
            component("PlayerController", moveSpeed=5, useGravity=True, gravity=20, jumpSpeed=7.5,
                      usePhysics=True, maxSlopeDegrees=45, stepHeight=0.32),
            component("Script", behaviour="ParkPlayer", parameters={},
                      data={"scene": {"kind": "string", "value": "Assets/Scenes/ParkAdventure.json"}}),
        ]
    result = [root]
    for side in (-1, 1):
        eye = ball(f"{identity}-eye-{side}", [side * .16, .12, .43], [.14, .24, .08], "Eye", parent=identity)
        result.append(eye)
        result.append(ball(f"{identity}-foot-{side}", [side * .32, -.43, .12], [.42, .20, .55], "White", parent=identity))
    return result


def coin(identity, position):
    result = ball(identity, position, [.65, .65, .25], "Petal", name="ParkCoin")
    result["components"] += [component("Rotator", angularVelocity=[0, 95, 0]),
                             component("Script", behaviour="Bob", parameters={"amplitude": .12, "frequency": .7})]
    return result


def nature(identity, position, model, scale=1):
    return obj(identity, position, [scale] * 3, [component("MeshRenderer",
        model=f"Assets/Models/Park/KenneyNature/{model}.obj", instancing=True)])


def tree(identity, x, z):
    return [nature(identity, [x, 0, z], "tree_oak", 3)]


def platform(identity, start, end, width=20):
    center = (start + end) / 2
    return [box(identity, [0, -.6, center], [width, 1.2, end - start], "Grass", True),
            box(identity + "-path", [0, .02, center], [4.6, .04, end - start], "Path")]


def write(path, value, overwrite=True):
    path = Path(path)
    if path.exists() and not overwrite:
        raise FileExistsError(f"Existing file preserved: {path}; use --overwrite explicitly")
    path.parent.mkdir(parents=True, exist_ok=True)
    text = value if isinstance(value, str) else json.dumps(value, ensure_ascii=False, indent=2) + "\n"
    path.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\r\n")
    if path.is_relative_to(CONTENT):
        meta = path.with_name(path.name + ".meta")
        if path.suffix != ".meta" and not meta.exists():
            key = "WP1/" + path.relative_to(CONTENT).as_posix()
            write(meta, dict(version=1, id=hashlib.sha256(key.encode()).hexdigest()[:32],
                             scale=1, flipV=False, previousPaths=[]))


def author_library():
    cube = "# Park unit cube; authored in WP1\n"
    vertices = [(-.5, -.5, -.5), (.5, -.5, -.5), (.5, .5, -.5), (-.5, .5, -.5),
                (-.5, -.5, .5), (.5, -.5, .5), (.5, .5, .5), (-.5, .5, .5)]
    cube += "".join("v " + " ".join(map(str, v)) + "\n" for v in vertices)
    cube += "".join("f " + " ".join(map(str, f)) + "\n" for f in
                    ((5, 6, 7, 8), (2, 1, 4, 3), (1, 5, 8, 4), (6, 2, 3, 7), (4, 8, 7, 3), (1, 2, 6, 5)))
    write(CONTENT / "Assets/Models/Park/UnitBox.obj", cube)
    for name, color in PALETTE.items():
        write(CONTENT / f"Assets/Materials/Park/{name}.mat",
              dict(version=1, color=color, roughness=.8, metallic=0, transparent=False, texture=""))
    chunks = {}
    chunks["Entrance"] = platform("lawn", -4, 16) + [coin("coin-a", [-2, 1, 6]), coin("coin-b", [2, 1, 11])]
    chunks["Entrance"] += character("enemy", [0, .7, 12], "Enemy", "ParkEnemy")
    chunks["Terraces"] = platform("lawn", 0, 16) + [
        box("step-low", [-3.5, .35, 5], [3, .7, 3], "Stone", True),
        box("step-high", [-3.5, .9, 9], [3, 1.8, 3], "Stone", True),
        coin("coin-a", [-3.5, 1.5, 5]), coin("coin-b", [-3.5, 2.6, 9]), coin("coin-c", [2, 1, 12]),
        ball("checkpoint", [0, .6, 14], [.7, .7, .7], "White", name="ParkCheckpoint"),
    ]
    chunks["Terraces"] += character("enemy", [1.5, .7, 8], "Enemy", "ParkEnemy")
    chunks["Canal"] = platform("bank-a", 0, 5.5) + platform("bank-b", 7.5, 16) + [
        box("water", [0, -.45, 6.5], [20, .15, 2], "Water"),
        box("takeoff", [0, .08, 5.1], [4, .16, .35], "Switch"),
        box("landing", [0, .08, 7.9], [4, .16, .35], "Switch"),
        coin("coin-a", [0, 1, 3]), coin("coin-b", [0, 2, 6.5]), coin("coin-c", [0, 1, 11]),
        ball("checkpoint", [0, .6, 14], [.7, .7, .7], "White", name="ParkCheckpoint"),
    ]
    chunks["SwitchGarden"] = platform("lawn", 0, 16) + [
        ball("switch-left", [-4, .5, 5], [1.2, .8, 1.2], "Switch", name="ParkSwitch"),
        ball("switch-right", [4, .5, 10], [1.2, .8, 1.2], "Switch", name="ParkSwitch"),
        coin("coin-a", [-4, 1, 3]), coin("coin-b", [4, 1, 8]), coin("coin-c", [0, 1, 13]),
        obj("park-gate", [0, 1.5, 15], components=[component("BoxCollider", center=[0, 0, 0], size=[24, 3, .5])]),
    ]
    chunks["SwitchGarden"] += character("enemy", [0, .7, 6], "Enemy", "ParkEnemy")
    for index in range(-11, 12):
        bar = box(f"gate-bar-{index}", [index, 0, 0], [.14, 3, .22], "Gate")
        bar["parent"] = "park-gate"
        chunks["SwitchGarden"].append(bar)
    for height in (-1, 1):
        rail = box(f"gate-rail-{height}", [0, height, 0], [24, .15, .25], "Gate")
        rail["parent"] = "park-gate"
        chunks["SwitchGarden"].append(rail)
    chunks["Vista"] = platform("lawn", 0, 16) + [
        coin("coin-a", [0, 1, 5]),
        ball("park-goal", [0, 1, 11], [2, 2, 2], "Petal"),
        box("goal-plinth", [0, .25, 11], [3, .5, 3], "Stone", True),
    ]
    for name, objects in chunks.items():
        root = obj("module", [0, 0, 0], name=name)
        for item in objects:
            if not item["parent"]:
                item["parent"] = "module"
        write(MODULES / f"{name}.prefab", dict(version=4,
              settings=dict(background=[.58, .82, .93, 1], mainCamera=""), objects=[root] + objects))
    write(MODULES / "Assembly.json", dict(version=1, order=list(ORDER), origins=list(ORIGINS),
        gap=2, coinCount=12, switchCount=2, speed=5, jumpSpeed=7.5, gravity=20,
        rules={"mainRoute": "fixed", "mirrorTerraces": True, "decorationExclusionWidth": 12}))


def ui(identity, position, size, text=None, color=None, visible="", parent="park-ui"):
    parts = [component("RectTransform", anchorMin=[0, 0], anchorMax=[0, 0], pivot=[0, 0],
                       position=position, size=size, rotation=0, introDelay=0, introOffset=0,
                       visibleWhen=visible, offsetBinding="", opacityBinding="")]
    if color:
        parts.append(component("Image", texture="", uv=[0, 0, 1, 1], color=color))
    if text:
        parts.append(component("Text", text=text, font="Yu Gothic UI", fontSize=24, color=[1, 1, .95, 1]))
    return obj(identity, [0, 0, 0], components=parts, parent=parent)


def assemble(seed, scene_path="Assets/Scenes/ParkAdventure.json"):
    rng = random.Random(seed)
    objects = [obj("park-camera", [0, 10, -11], rotation=[.55, 0, 0], components=[
        component("Camera", verticalFov=52, nearClip=.1, farClip=160, referenceAspect=16 / 9, preserveHorizontal=True),
        component("Script", behaviour="ParkCamera", parameters={}, data={"target": {"kind": "reference", "value": "park-player"}})]),
        obj("park-light", [0, 0, 0], components=[component("DirectionalLight", direction=[.35, -1, .3],
            color=[1, .96, .84], intensity=1, ambient=.38, specular=.15, shininess=32, shadowsEnabled=True)])]
    title = json.loads((CONTENT / "Assets/Scenes/TitleStreet.json").read_text(encoding="utf-8"))
    sky = copy.deepcopy(next(item for item in title["objects"] if any(part["type"] == "Sky" for part in item["components"])))
    sky.update(id="park-sky", name="公園の空", parent="")
    objects.append(sky)
    manifest = []
    for kind, origin in zip(ORDER, ORIGINS):
        source = json.loads((MODULES / f"{kind}.prefab").read_text(encoding="utf-8"))
        sign = rng.choice((-1, 1)) if kind == "Terraces" else 1
        ids = {item["id"]: f"{kind}-{item['id']}" for item in source["objects"]}
        for special in ("park-gate", "park-goal"):
            ids[special] = special
        # Bake the module root into world positions; child character visuals retain their hierarchy.
        for item in copy.deepcopy(source["objects"]):
            if item["id"] == "module":
                continue
            parent = item["parent"]
            item["id"] = ids[item["id"]]
            if parent == "module":
                item["parent"] = ""
                item["position"][0] *= sign
                item["position"][2] += origin
            else:
                item["parent"] = ids[parent]
            objects.append(item)
        manifest.append(dict(module=kind, origin=origin, mirrored=sign < 0))
        for side in (-1, 1):
            for number, local_z in enumerate((2, 10)):
                x = side * rng.uniform(8, 9.5)
                z = origin + local_z + rng.uniform(-1, 1)
                objects.extend(tree(f"tree-{kind}-{side}-{number}", x, z))
                objects.append(nature(f"flower-{kind}-{side}-{number}",
                    [side * 6.6, 0, z + 1.5], "flower_yellowC" if side > 0 else "flower_purpleC", 4))
        for side in (-1, 1):
            objects.append(box(f"bench-{kind}-{side}", [side * 5.1, .6, origin + 2], [.9, .25, 2], "Wood"))
            objects.append(box(f"bench-back-{kind}-{side}", [side * 5.55, 1, origin + 2], [.15, 1.1, 2], "Wood"))
    objects += character("park-player", [0, 1.05, 1], "Player", "Player")
    player = next(item for item in objects if item["id"] == "park-player")
    player["components"][-1]["data"]["value"]["scene"]["value"] = scene_path
    player["components"].append(component("AudioListener", volume=1))
    objects.append(obj("park-music", [0, 0, 0], components=[component("AudioSource",
        clip="Assets/Audio/Title/Bgm.wav", volume=.3, loop=True, playOnAwake=True,
        cue="parkBgm", volumeBinding="volumeGain")]))
    objects.append(obj("park-puff", [0, 0, 1.7], [3.4, 1.7, 3.4],
                       mesh("White", True, "parkAttack=1"), parent="park-player"))
    # A distant landmark makes the destination readable throughout the course.
    objects += [box("tower-base", [0, 2, 85], [6, 4, 5], "Stone"),
                box("tower-column", [0, 6, 85], [3.5, 6, 3.5], "Gate"),
                ball("tower-crown", [0, 10, 85], [5, 3, 5], "Petal")]
    for side in (-1, 1):
        objects.append(box(f"distant-building-{side}", [side * 18, 5, 58], [7, 10, 10], "Stone"))
        objects.extend(tree(f"distant-tree-{side}", side * 14, 70))
    objects.append(obj("park-ui", [0, 0, 0], components=[component("Canvas", referenceSize=[1280, 720],
        scaleWithScreen=True, stateDefaults={"parkAttack": 0, "parkClear": 0})]))
    objects += [ui("park-hud", [20, 20], [920, 102], color=[.06, .18, .19, .88]),
                ui("park-title", [36, 29], [800, 32], text="こもれび公園 — 花のかけらを探して"),
                ui("park-stats", [36, 65], [860, 35], text="ライフ 3   花のかけら 0 / 12   スイッチ 0 / 2"),
                ui("park-help-bg", [20, 604], [1240, 96], color=[.06, .18, .19, .88]),
                ui("park-hint", [36, 612], [1200, 32], text="公園の奥へ進もう"),
                ui("park-help", [36, 654], [1200, 32], text="WASD / 左スティック：移動   Space / A：ジャンプ   J / X：攻撃   R：再挑戦   Esc：タイトル"),
                ui("park-clear-bg", [240, 270], [800, 160], color=[.08, .24, .25, .96], visible="parkClear=1"),
                ui("park-result", [275, 300], [730, 110], text="公園をクリア！", visible="parkClear=1")]
    scene = dict(version=4, settings=dict(background=[.58, .82, .93, 1], mainCamera="park-camera",
        fog=dict(enabled=True, color=[.58, .82, .93], start=65, end=150, strength=.65)), objects=objects)
    report = validate(scene)
    return scene, dict(version=1, seed=seed, modules=manifest, validation=report)


def validate(scene):
    objects = scene["objects"]
    ids = [item["id"] for item in objects]
    if len(ids) != len(set(ids)):
        raise ValueError("Duplicate object IDs")
    if any(item["parent"] and item["parent"] not in ids for item in objects):
        raise ValueError("Missing parent")
    counts = {name: sum(item["name"] == name for item in objects) for name in ("ParkCoin", "ParkSwitch", "ParkEnemy")}
    if counts != {"ParkCoin": 12, "ParkSwitch": 2, "ParkEnemy": 3}:
        raise ValueError(f"Invalid gameplay counts: {counts}")
    gate = next(item for item in objects if item["id"] == "park-gate")
    if any(item["position"][2] >= gate["position"][2] for item in objects if item["name"] == "ParkSwitch"):
        raise ValueError("A required switch is behind its gate")
    # Check the authored center route against actual solid box extents, excluding the removable gate.
    solids = [item for item in objects if not item["parent"] and item["id"] != "park-gate" and any(
        part["type"] == "BoxCollider" and not part.get("isTrigger", False) and part.get("shape", "box") == "box"
        for part in item["components"])]
    missing = []
    for step in range(741):
        z = step / 10
        support = any(abs(item["position"][0]) <= item["scale"][0] / 2 and
                      abs(z - item["position"][2]) <= item["scale"][2] / 2 and
                      abs(item["position"][1] + item["scale"][1] / 2) < .1 for item in solids)
        if not support:
            missing.append(z)
    gap = (max(missing) - min(missing) + .1) if missing else 0
    safe_jump = 5 * (2 * 7.5 / 20) * .7
    if gap > safe_jump:
        raise ValueError(f"Main route gap {gap:.2f} exceeds conservative jump range {safe_jump:.2f}")
    return dict(objects=len(objects), counts=counts, mainRouteGap=round(gap, 2),
                conservativeJumpRange=round(safe_jump, 2), requiredSwitchesBeforeGate=True,
                note="Structural checks only; engine playthrough is tested separately")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=20261012)
    parser.add_argument("--author-library", action="store_true", help="Rebuild repository-owned models, materials and module templates")
    parser.add_argument("--output", type=Path, default=REPO / "generated/park/ParkAdventure.json")
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()
    destination = args.output.resolve()
    if not destination.is_relative_to(REPO):
        parser.error("Output must stay within the WP1 workspace")
    if destination.suffix != ".json" or not (destination.is_relative_to(REPO / "generated") or
                                            destination.is_relative_to(CONTENT / "Assets/Scenes")):
        parser.error("Output must be a .json scene under generated or Content/Assets/Scenes")
    if destination.exists() and not args.overwrite:
        parser.error("Output exists; pass --overwrite to replace it")
    if args.author_library:
        author_library()
    scene_path = destination.relative_to(CONTENT).as_posix() if destination.is_relative_to(CONTENT) else "Assets/Scenes/ParkAdventure.json"
    scene, report = assemble(args.seed, scene_path)
    write(destination, scene, args.overwrite)
    report_path = REPO / "generated/park/reports" / f"{destination.stem}-{args.seed}.json"
    write(report_path, report)
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
