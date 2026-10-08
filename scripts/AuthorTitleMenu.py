"""Explicitly author the street menu; never run by a normal build."""
import copy
import json
import shutil
from pathlib import Path
from PIL import Image

from AuthorTitleAnimation import track

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "Content/Assets/Scenes/TitleStreet.json"


def save_scene(scene):
    (ROOT / "generated/authoring").mkdir(parents=True,exist_ok=True)
    rows=[]
    for key,value in scene.items():
        if key=="objects":
            body=",\n".join("    "+json.dumps(n,ensure_ascii=False,separators=(",",":")) for n in value)
            rows.append('  "objects": [\n'+body+'\n  ]')
        else:
            rows.append("  "+json.dumps(key)+": "+json.dumps(value,ensure_ascii=False,separators=(",",":")))
    (ROOT / "generated/authoring/TitleMenu.json").write_bytes(("{\n"+",\n".join(rows)+"\n}\n").replace("\n","\r\n").encode("utf-8"))


def author(scene):
    scene["objects"]=[n for n in scene["objects"] if not n["id"].startswith(("config-","quit-","menu-selection-","menu-shadow-")) and n["id"] not in ("world-config","world-quit")]
    nodes = {o["id"]: o for o in scene["objects"]}
    def component(node, kind):
        return next(c for c in node["components"] if c["type"] == kind)
    def add(identifier, model, position, scale, rotation=(0, 0, 0)):
        node = dict(id=identifier, name=identifier, parent="", position=list(position),
                    rotation=list(rotation), scale=list(scale), components=[
                        dict(type="MeshRenderer", id="mesh", enabled=True, model=model)])
        scene["objects"].append(node)
        return node

    # A genuine CC0 cog from Kenney Factory Kit; retain its material and licence.
    source = ROOT / "generated/title-assets/factory"
    dest = ROOT / "Content/Assets/Models/Title/Factory"
    dest.mkdir(parents=True, exist_ok=True)
    for name in ("cog-a.obj", "cog-a.mtl"):
        shutil.copy2(source / "Models/OBJ format" / name, dest / name)
    shutil.copy2(source / "License.txt", dest / "License.txt")
    (dest / "Textures").mkdir(exist_ok=True)
    shutil.copy2(source / "Models/OBJ format/Textures/colormap.png", dest / "Textures/colormap.png")
    # A palette-only derivative of the CC0 cog: orange reads clearly on the dark facade.
    palette=Image.open(dest / "Textures/colormap.png").convert("RGB")
    Image.new("RGB",palette.size,(255,151,69)).save(dest / "Textures/colormap.png")
    add("config-building", "Assets/Models/Title/Surface/Commercial/building-e.obj",
        (9, .08, 18), (4, 8, 4), (0, 0, 0))["name"] = "CONFIG / CC0の歯車付き施設"
    # The cog lies in XZ in its source file. Stand it upright on the street-facing facade.
    add("config-cog", "Assets/Models/Title/Factory/cog-a.obj",
        (9, 5.6, 15.65), (3.2, 3.2, 3.2), (1.5707963, 0, 0))["name"] = "CONFIG / Kenney CC0の歯車看板"
    add("quit-sign", "Assets/Models/Title/EmergencyExit/Sign.obj",
        (-7.2, 7.4, -2.8), (1, 1, 1))["name"] = "QUIT / 左のビル上部のCC0避難口看板"

    camera = nodes["scene-camera"]
    tracks = component(camera, "Animation")["tracks"]
    tracks[:]=[t for t in tracks if t["clock"] not in ("configFocusTime","quitFocusTime","homeFocusTime","sceneTime")]
    for t in tracks:
        if t["clock"] == "startTime":
            t["easing"] = "outCubic"
    for prop, home, target in (("position", camera["position"], [7.1, 2.9, 5.2]),
                               ("rotation", camera["rotation"], [-.19, .12, -.025])):
        tracks += [track(prop, "configFocusTime", [(0, home), (.62, target)], "outBack"),
                   track(prop, "homeFocusTime", [(0, target), (.56, home)], "outBack")]
    for prop, home, target in (("position",camera["position"],[-1.5,5.2,-11]),
                               ("rotation",camera["rotation"],[-.22,-.61,0])):
        tracks.append(track(prop,"quitFocusTime",[(0,home),(.58,target)],"outBack"))

    defaults = component(nodes["title-canvas"], "Canvas")["stateDefaults"]
    defaults.update(screen=0, selected=0, cameraFocus=0, configFocusTime=-1, homeFocusTime=-1,
                    quitFocusTime=-1,focusView=0,quitEmphasis=.6,quitTransition=0,
                    volume=7, motion=1, row=0, saveFailed=0, introDuration=1.3,startEmphasis=1,configEmphasis=.6)
    component(nodes["world-logo"],"RectTransform")["visibleWhen"]="cameraFocus=0"
    small_logo=copy.deepcopy(nodes["world-logo"])
    small_logo.update(id="config-logo",name="CONFIG / 小さなタイトルロゴ")
    small_logo["components"]=[c for c in small_logo["components"] if c["type"]!="Animation"]
    component(small_logo,"RectTransform").update(position=[900,42],size=[300,160],visibleWhen="focusView=1")
    scene["objects"].append(small_logo)
    start = nodes["world-start"]
    start["name"] = "START / ゲーム開始"
    start["components"] = [c for c in start["components"] if c["type"] != "Animation"]
    rect = component(start, "RectTransform")
    start["components"]=[c for c in start["components"] if c["type"] not in ("Image","Text")]
    start["components"].append(dict(type="Text",id="text",enabled=True,text="START",font="Segoe UI",fontSize=34,color=[1,1,1,1]))
    rect.update(position=[88, 450], size=[270, 58], rotation=0,visibleWhen="screen=0",opacityBinding="startEmphasis")
    component(start, "Button").update(action="click", target="", event="menu:0", sound="",hoverColor=[1,1,1,1],pressedColor=[.7,.7,.7,1])
    config = copy.deepcopy(start)
    config.update(id="world-config", name="CONFIG / 設定施設へ")
    component(config, "RectTransform")["position"] = [88, 518]
    component(config, "RectTransform")["opacityBinding"] = "configEmphasis"
    component(config, "Text")["text"] = "CONFIG"
    component(config, "Button")["event"] = "menu:1"
    scene["objects"].append(config)
    quit_button=copy.deepcopy(config)
    quit_button.update(id="world-quit",name="QUIT / ゲーム終了")
    component(quit_button,"RectTransform").update(position=[88,586],opacityBinding="quitEmphasis")
    component(quit_button,"Text")["text"]="QUIT"
    component(quit_button,"Button")["event"]="menu:2"
    scene["objects"].append(quit_button)
    for node, index in ((start, 0), (config, 1), (quit_button,2)):
        pos = component(node, "RectTransform")["position"]
        node["components"].append(dict(type="Animation", id="animation", enabled=True, tracks=[
            track("uiPosition", "sceneTime", [(0, [-380, pos[1]]), (.55+index*.1, [-380, pos[1]]),
                  (1.12+index*.1, pos)], "outBack"),
            track("opacity", "startTime", [(0, 1), (.18, 0)])]))
        shadow=copy.deepcopy(node)
        shadow.update(id=f"menu-shadow-{index}",name=f"文字の可読性 / {index}")
        shadow["components"]=[c for c in shadow["components"] if c["type"]!="Button"]
        component(shadow,"RectTransform")["position"]=[pos[0]+2,pos[1]+2]
        component(shadow,"Text")["color"]=[0,0,0,.7]
        for key in component(shadow,"Animation")["tracks"][0]["keys"]:
            key["value"][0]+=2
            key["value"][1]+=2
        scene["objects"].insert(scene["objects"].index(node),shadow)
        # A small neutral dot follows keyboard and pointer selection.
        marker = copy.deepcopy(node)
        marker.update(id=f"menu-selection-{index}", name=f"選択 / {index}")
        marker["components"] = [copy.deepcopy(component(node, "RectTransform")),
                                dict(type="Image", id="image", enabled=True, texture="", uv=[0,0,1,1],
                                     color=[1,1,1,1])]
        component(marker, "RectTransform").update(position=[pos[0]-21,pos[1]+23], size=[6,6],
                                                  rotation=0, opacityBinding="",visibleWhen=f"screen=0&selected={index}")
        scene["objects"].append(marker)

    # Existing saved UI nodes vary between authored versions; remove obsolete instructions only.
    for node in scene["objects"]:
        if node["id"] in ("morning-help", "world-help"):
            for c in node["components"]:
                if c["type"] == "Text":
                    c["text"] = "↑↓ 選択   ENTER 決定"
    template = copy.deepcopy(rect)
    for identifier in ("title-transition", "title-transitionPink"):
        component(nodes[identifier],"RectTransform")["visibleWhen"]="selected=0"
    fade=copy.deepcopy(nodes["title-transition"])
    fade.update(id="quit-fade",name="QUIT / 通常の暗転")
    component(fade,"RectTransform").update(widthBinding="",opacityBinding="quitTransition",visibleWhen="selected=2")
    component(fade,"Image")["color"]=[0,0,0,1]
    scene["objects"].append(fade)
    def ui(identifier, pos, size, text=None, color=None, condition="screen=1", event=None, font_size=28):
        r = copy.deepcopy(template)
        r.update(position=pos, size=size, rotation=0, opacityBinding="",visibleWhen=condition)
        parts=[r]
        if color:
            parts.append(dict(type="Image",id="image",enabled=True,texture="",uv=[0,0,1,1],color=color))
        if text:
            parts.append(dict(type="Text",id="text",enabled=True,text=text,font="Yu Gothic UI",fontSize=font_size,
                              color=[1,.98,.94,1]))
        if event:
            parts.append(dict(type="Button",id="button",enabled=True,action="click",target="",event=event,sound="",
                              hoverColor=[1,1,1,1],pressedColor=[.7,.7,.7,1]))
        scene["objects"].append(dict(id=identifier,name=identifier,parent="title-canvas",
                                     position=[0,0,0],rotation=[0,0,0],scale=[1,1,1],components=parts))
    ui("config-panel",[64,390],[410,282],color=[.025,.065,.11,.94])
    ui("config-heading",[90,407],[340,36],text="CONFIG / 設定")
    for i,label in enumerate(("音量", "背景演出", "保存して戻る")):
        ui(f"config-row-{i}",[95,460+i*57],[335,45],text=label,event=f"settings:{i}")
        ui(f"config-row-marker-{i}",[78,467+i*57],[5,29],color=[1,.4,.17,1],condition=f"screen=1&row={i}")
    for volume in range(11):
        ui(f"config-volume-{volume}",[300,460],[105,40],text=f"{volume*10}%",condition=f"screen=1&volume={volume}",event="volume")
    for enabled in (0,1):
        ui(f"config-motion-{enabled}",[300,517],[105,40],text="ON" if enabled else "OFF",
           condition=f"screen=1&motion={enabled}",event="settings:1")
    ui("config-back",[95,633],[335,27],text="ESC / B : 保存せず戻る",event="back",font_size=20)
    ui("config-save-error",[95,360],[600,28],text="保存できませんでした。もう一度お試しください。",condition="screen=1&saveFailed=1",font_size=18)
    ui("config-controls",[76,679],[500,24],text="↑↓ / W S 選択　ENTER / A 決定",condition="screen=0",font_size=16)
    light = component(nodes["scene-light"], "DirectionalLight")
    light.update(ambient=.28,intensity=1.15,color=[1,.93,.81],direction=[-.55,-.8,.28],specular=.12,
                 shadowsEnabled=True,shadowDistance=70,shadowBias=.00008)
    scene["settings"]["fog"].update(start=28,end=130,strength=.67,color=[.73,.85,.92])
    return scene


if __name__ == "__main__":
    scene = author(json.loads(PATH.read_text(encoding="utf-8")))
    save_scene(scene)
