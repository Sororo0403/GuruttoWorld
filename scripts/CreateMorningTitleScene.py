"""Rebuild the editable morning title from CC0 models and original vector UI.

Run once to reset the composition. Subsequent art direction belongs in the Editor;
normal builds never run this script or overwrite an edited scene.
"""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCENE = ROOT / "Content/Assets/Scenes/TitleStreet.json"
objects = []


def item(identifier, name, position=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1), parent="", components=None):
    result = dict(id=identifier, name=name, parent=parent, position=list(position),
                  rotation=list(rotation), scale=list(scale), components=components or [])
    objects.append(result)
    return result


def mesh(identifier, model, position, scale=(4, 4, 4), yaw=0, parent="city"):
    return item(identifier, identifier, position, (0, yaw, 0), scale, parent,
                [dict(type="MeshRenderer", id="mesh", enabled=True, model="Assets/Models/Title/" + model + ".obj")])


def ui(identifier, name, position, size, texture="", color=(1, 1, 1, 1), parent="title-canvas", rotation=0, pivot=(0, 0)):
    return item(identifier, name, parent=parent, components=[
        dict(type="RectTransform", id="rect", enabled=True, anchorMin=[0, 0], anchorMax=[0, 0],
             pivot=list(pivot), position=list(position), size=list(size), rotation=rotation,
             introDelay=0, introOffset=0, offsetBinding="", opacityBinding="", widthBinding="", visibleWhen=""),
        dict(type="Image", id="image", enabled=True,
             texture="Assets/Textures/Title/Morning/" + texture + ".png" if texture else "",
             color=list(color), uv=[0, 0, 1, 1])])


def text(identifier, content, position, size, color, font_size=16):
    node = ui(identifier, content, position, size, color=color)
    node["components"][1] = dict(type="Text", id="text", enabled=True, text=content,
                                font="Yu Gothic UI", fontSize=font_size, color=list(color))
    return node


INK = [0.043, 0.106, 0.173, 1]
CREAM = [1, 0.988, 0.956, 1]
PINK = [1, 0.447, 0.29, 1]
MINT = [0.235, 0.875, 1, 1]

item("city", "街 / 朝の交差点")
mesh("ground", "Surface/Roads/ground", (0, -0.12, 60), (140, 4, 180), parent="")
for index in range(24):
    z = -8 + index * 4
    mesh(f"road-{index:02}", "Surface/Roads/road-straight", (0, 0, z), yaw=math.pi / 2)
    for side, x in [("left", -2.8), ("right", 2.8)]:
        mesh(f"sidewalk-{index:02}-{side}", "Surface/Roads/sidewalk", (x, 0.08, z), (1.6, 4, 4))

# A clear foreground, open central plaza and varied skyline: three readable depths.
for index, (model, x, z, scale, yaw) in enumerate([
    ("k", -7.2, 2, (4.2, 4.5, 4), -math.pi / 2),
    ("e", -6.8, 13, (4, 5.5, 4), -math.pi / 2),
    ("h", -6.2, 27, (4.3, 4.6, 4), -math.pi / 2),
    ("c", -5.8, 37, (4, 6.0, 4), -math.pi / 2),
    ("k", 19.5, 4, (4, 5.0, 4), math.pi / 2),
    ("e", 21, 15, (4, 5.0, 4), math.pi / 2),
    ("c", 17.5, 30, (4.5, 5.5, 4), math.pi / 2),
    ("h", 5.2, 38, (4, 4, 4), math.pi / 2),
]):
    mesh(f"street-building-{index}", "Surface/Commercial/building-" + model, (x, 0.08, z), scale, yaw)

item("plaza", "広場 / 朝の街角")
for x in range(3):
    for z in range(4):
        mesh(f"plaza-paving-{x}-{z}", "Surface/Roads/sidewalk", (4.8 + x * 4, 0.08, 6 + z * 4), parent="plaza")
for index in range(3):
    mesh(f"plaza-step-{index + 1}", "Roads/tile-low", (9, 0.16 * (index + 1), 17.5 + index * 0.5), (7.5, 8, 1), parent="plaza")
mesh("plaza-plinth", "Roads/tile-low", (9, 0.08, 22), (8.5, 28, 7), parent="plaza")
mesh("plaza-landmark", "Surface/Commercial/building-h", (9, 0.64, 22), (8, 7.5, 6), math.pi, "plaza")
mesh("plaza-cafe", "Surface/Commercial/building-c", (15.5, 0.08, 24), (4, 4, 4), -0.18, "plaza")
mesh("landmark-awning", "Commercial/detail-awning-wide", (9, 1.0, 18.7), (5, 5, 5), math.pi, "plaza")
for index, (x, z) in enumerate([(5, 11), (13.5, 12.5)]):
    mesh(f"cafe-table-{index}", "Commercial/detail-parasol-" + ("a" if index == 0 else "b"), (x, 0.17, z), (2.8, 2.8, 2.8), 0.3, "plaza")
for index, (x, z) in enumerate([(3.3, 5), (16, 8), (16, 17), (-2.8, 1), (-2.8, 15)]):
    mesh(f"street-light-{index}", "Roads/light-square-double", (x, 0.12, z), (4, 4, 4), parent="plaza")

towers = ["Commercial/building-skyscraper-" + name for name in ["a", "b", "d", "e"]]
item("skyline", "街並み / CC0の高層建築")
for index, (x, z, height_scale) in enumerate([
    (-16, 24, 6), (-10, 32, 7), (16, 35, 7), (22, 43, 8),
    (-19, 50, 8), (-9, 60, 9), (9, 62, 8), (29, 54, 7),
    (-30, 65, 9), (0, 83, 9), (21, 83, 10), (40, 75, 8),
]):
    mesh(f"skyline-{index}", towers[index % 4], (x, 0.08, z), (5, height_scale, 5), 0.12 * ((index % 3) - 1), "skyline")
for index, x in enumerate([-4, 0, 4]):
    mesh(f"bridge-{index}", "Roads/road-bridge", (x, 0, 42), (4, 8, 4), parent="skyline")

item("scene-camera", "タイトル / メインカメラ", (-0.8, 2.2, -8), (-0.14, 0.03, -0.065), components=[
    dict(type="Camera", id="camera", enabled=True, verticalFov=45, nearClip=0.1, farClip=220,
         preserveHorizontal=True, referenceAspect=1280 / 720),
    dict(type="CameraSway", id="sway", enabled=True, amplitude=[0.08, 0.03, 0], period=[24, 40, 24])])
item("scene-light", "朝 / 暖かな日差し", components=[
    dict(type="DirectionalLight", id="light", enabled=True, direction=[-0.45, -0.8, 0.35],
         color=[1, 0.94, 0.81], intensity=0.8, ambient=0.48, specular=0.16, shininess=32)])
item("scene-sky", "空 / 朝の青空", components=[
    dict(type="Sky", id="sky", enabled=True, horizon=[0.83, 0.9, 0.91], zenith=[0.22, 0.62, 0.83], horizonHeight=0.7,
         sunColor=[1, 0.89, 0.62], sunCenter=[0.82, 0.14], sunRadius=[0.25, 0.3], sunStrength=0.4,
         cloudLow=[0.88, 0.95, 0.98], cloudHigh=[1, 0.98, 0.91], cloudOpacity=0.8,
         clouds=[dict(center=[0.22, 0.26], size=1.0), dict(center=[0.54, 0.12], size=0.8), dict(center=[0.88, 0.3], size=1.1)],
         cloudVelocity=[0.0015, 0], referenceAspect=1280 / 720)])
item("scene-particles", "光 / 朝の街", (2, 1.2, 3), components=[
    dict(type="ParticleEmitter", id="particles", enabled=True, count=24, color=[1, 0.94, 0.7, 0.26],
         size=0.045, extent=[13, 0, 19], travel=[0.2, 3, 0], cycle=14, drift=0.12)])

item("title-canvas", "タイトル / UI", components=[
    dict(type="Canvas", id="canvas", enabled=True, referenceSize=[1280, 720], scaleWithScreen=True,
         stateDefaults=dict(screen=2, intro=1, sceneTime=3, motionTime=0, startTime=-1, startRequested=0, startDuration=0.8, musicVolume=0.3))])
ui("world-logo", "ぐるっとワールド / ロゴ", (674, 58), (540, 289), "Logo")
ui("morning-caption", "HELLO, NEW DAY.", (638, 325), (390, 43), "Caption")
ui("world-start", "PRESS ANY BUTTON / 開始", (432, 563), (455, 104), "StartBand")
objects[-1]["components"].append(dict(type="Button", id="button", enabled=True, action="setState",
    target="startRequested=1", event="start", sound="title-audio-confirm", hoverColor=CREAM, pressedColor=MINT))
text("world-footer", "GURUTTO WORLD", (35, 672), (250, 25), CREAM, 16)
text("morning-help", "任意のキー・ゲームパッドのボタンで開始", (440, 677), (480, 26), CREAM, 16)

# Final fullscreen ink guarantees coverage at any window aspect ratio.
item("title-transition-canvas", "開始 / 全画面", components=[
    dict(type="Canvas", id="canvas", enabled=True, referenceSize=[1280, 720], scaleWithScreen=False, stateDefaults={})])
for identifier, color, binding in [("title-transitionPink", PINK, "transitionPink"), ("title-transition", INK, "transition")]:
    node = ui(identifier, "開始 / " + identifier, (0, 0), (0, 0), color=color, parent="title-transition-canvas")
    node["components"][0].update(anchorMax=[1, 1], widthBinding=binding)

for name, volume, loop, awake in [("Bgm", 0.3, True, True), ("Select", 0.3, False, False), ("Confirm", 0.35, False, False), ("Back", 0.3, False, False), ("Error", 0.3, False, False)]:
    item("title-audio-" + name.lower(), "音 / " + name, components=[
        dict(type="AudioSource", id="audio", enabled=True, clip="Assets/Audio/Title/" + name + ".wav",
             volume=volume, loop=loop, playOnAwake=awake, cue="" if loop else name,
             volumeBinding="musicVolume" if loop else "volumeGain")])

layout = dict(version=4, settings=dict(background=[0.74, 0.86, 0.91, 1], mainCamera="scene-camera",
    fog=dict(enabled=True, color=[0.83, 0.9, 0.91], start=45, end=180, strength=0.5)), objects=objects)
objects[0], objects[1] = objects[1], objects[0]
SCENE.write_bytes((json.dumps(layout, ensure_ascii=False, indent=2) + "\n").replace("\n", "\r\n").encode("utf-8"))
print(f"Created editable title scene: {len(objects)} objects")
