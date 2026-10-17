"""保存済み SunriseStreet シーンを明示的に編集するツールです。ビルドでは実行しません。"""
import copy
import json
import math
import struct
import wave
from pathlib import Path
from GenerateParkAdventure import component, obj, box, ball, character, coin, nature, ui
from ParkAuthoring import configure_scene
from SunriseBatching import batch_static

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / 'Games/GuruttoWorld'
SCENE = 'Assets/Scenes/SunriseStreet.json'


def script(behaviour, **refs):
    return component('Script', behaviour=behaviour, parameters={},
                     data={k: {'kind': 'reference', 'value': v} for k, v in refs.items()})


def material_box(identity, position, size, material, solid=False):
    item = box(identity, position, size, 'Stone', solid)
    item['components'][1]['asset'] = f'Assets/Materials/Sunrise/{material}.mat'
    return item


def model(identity, position, scale, path, rotation=(0, 0, 0)):
    return obj(identity, position, scale, [component('MeshRenderer', model=path, instancing=True)], rotation=rotation)


def assemble(batched=True):
    title = json.loads((GAME / 'Assets/Scenes/TitleStreet.json').read_text(encoding='utf-8'))
    objects = []
    for kind in ('Sky', 'DirectionalLight'):
        source = copy.deepcopy(next(o for o in title['objects'] if any(c['type'] == kind for c in o['components'])))
        source.update(id='sunrise-' + kind.lower(), name='朝の空' if kind == 'Sky' else 'タイトルと同じ朝の光', parent='')
        objects.append(source)
    objects.append(obj('park-camera', [0, 8.5, -12], rotation=[.46, 0, 0], components=[
        component('Camera', verticalFov=49, nearClip=.1, farClip=240, referenceAspect=16/9, preserveHorizontal=True),
        component('Script', behaviour='ParkCamera', parameters={'height': 10.5, 'distance': 14, 'minZ': 1, 'maxZ': 115,
            'lookAhead': 3, 'horizontalFollow': .95}, data={'target': {'kind': 'reference', 'value': 'park-player'}})]))

    # 歩ける街区を、広場 → 2 本の経路 → 中庭 → 運河 → 公園の順に構成します。
    objects.append(material_box('scenery-foundation', [0, -4.5, 60], [220, 3, 240], 'Grass'))
    for identity, start, end in [('south', -18, 52), ('north', 60, 145)]:
        objects.append(material_box('ground-' + identity, [0, -.6, (start+end)/2], [120, 1.2, end-start], 'Paving', True))
    # タイトルのテクスチャ付き舗装を道路向けのタイル寸法で再利用し、路面の下に置きます。
    for x in (-15,-9,-3,3,9,15):
        for z in (-6,2,10,18,26,34,42,48):
            objects.append(model(f'plaza-paving-{x}-{z}',[x,-.012,z],[6,1,8],
                'Assets/Models/Title/Surface/Roads/sidewalk.obj'))
    for x in (-7.5,-2.5,2.5,7.5):
        for z in (64,72,80,88,96,104,112,120):
            objects.append(model(f'park-paving-{x}-{z}',[x,-.012,z],[5,1,8],
                'Assets/Models/Title/Surface/Roads/sidewalk.obj'))
    objects.append(material_box('canal-water', [0, -1.5, 56], [120, .12, 8], 'Water'))
    for side in (-1, 1):
        objects.append(material_box(f'boundary-south-{side}', [side*19, .65, 23], [.4, 1.3, 58], 'Cream', True))
        objects.append(material_box(f'boundary-north-{side}', [side*10, .65, 100], [.4, 1.3, 78], 'Cream', True))
        for i, z in enumerate((-8, 12, 35, 75, 98, 122)):
            x = side*(24 if z<52 else 15)
            objects.append(model(f'building-{side}-{i}', [x, 0, z], [5.2]*3,
                f'Assets/Models/Title/Surface/Commercial/building-{("e", "c", "k")[i%3]}.obj', [0, 0 if side<0 else math.pi, 0]))
            objects.append(model(f'skyline-{side}-{i}', [side*36, 0, z+18], [8+i%2]*3,
                'Assets/Models/Title/Surface/Commercial/building-skyscraper-a.obj'))
        for i, z in enumerate((3, 12, 44, 66, 92, 110)):
            x = side*(15 if z<52 else 8)
            objects.append(material_box(f'planter-{side}-{i}', [x, .25, z], [2, .5, 3], 'Cream', True))
            objects.append(nature(f'flowers-{side}-{i}', [x, .51, z], 'flower_yellowC' if side>0 else 'flower_purpleC', 3))
            objects.append(nature(f'tree-{side}-{i}', [x+side*2, 0, z+2], 'tree_oak', 3.4))

    for i, (x,z) in enumerate([(0,-6),(0,2),(0,10),(7,19),(7,27),(7,35),(7,43),
                               (0,48),(0,64),(-4,72),(0,80),(4,87),(0,96),(0,104),(0,112)]):
        objects.append(model(f'road-{i}', [x,.008,z], [8,1,6],
            'Assets/Models/Title/Surface/Roads/road-straight.obj', [0,math.pi/2,0]))
    # 直線道路を飾る代わりに、中央の店舗区画で早い段階に進路を選ばせます。
    objects.append(material_box('market-island', [0,2,29], [5.6,4,20], 'Cream', True))
    objects.append(material_box('market-fascia', [0,3.5,18.8], [6,.65,.25], 'Orange'))
    for x in (-1.7,1.7):
        objects.append(material_box(f'market-window-{x}', [x,1.8,18.85], [1.2,1.8,.16], 'Teal'))
    objects.append(material_box('market-door', [0,1.2,18.8], [1,2.4,.18], 'Teal'))
    for z in (23,28,33,38):
        objects.append(material_box(f'shop-window-{z}',[2.84,1.65,z],[.12,1.7,2.8],'Teal'))
        objects.append(material_box(f'shop-window-sill-{z}',[2.94,.76,z],[.3,.12,3],'Orange'))
        objects.append(material_box(f'shop-window-bar-{z}',[2.93,1.65,z],[.13,1.7,.1],'Cream'))
    objects.append(material_box('fork-mark', [0,.025,13], [7,.05,.35], 'Orange'))

    # 左の分岐はカフェを上り、屋上を渡って中庭へ下ります。
    for i in range(10):
        h=.3*(i+1)
        objects.append(material_box(f'terrace-step-{i}', [-7,h/2,16+i*1.05], [3.6,h,1.1], 'Cream', True))
    objects.append(material_box('terrace-deck', [-7,1.5,31.5], [7.4,3,12.6], 'Cream', True))
    objects.append(model('roof-paving',[-7,3.002,31.5],[7.2,1,12.5],
        'Assets/Models/Title/Surface/Roads/sidewalk.obj'))
    objects.append(material_box('cafe-roof-table',[-5,3.72,31.8],[1.4,.14,1.4],'Orange'))
    objects.append(material_box('cafe-roof-table-leg',[-5,3.35,31.8],[.18,.7,.18],'Teal'))
    for z in (30.6,33):
        objects.append(material_box(f'cafe-roof-seat-{z}',[-5,3.3,z],[.8,.6,.6],'Teal'))
    objects.append(nature('cafe-roof-flowers',[-9.6,3.05,27.5],'flower_yellowC',2.8))
    objects.append(material_box('roof-walk', [-7,2.85,39.3], [3.6,.3,3.4], 'Cream', True))
    for i in range(10):
        h=3-.3*i
        objects.append(material_box(f'roof-descent-{i}', [-7,h/2,41.5+i*.8], [3.6,h,.85], 'Cream', True))
    objects.append(material_box('roof-trim', [-10.65,3.08,31.5], [.18,.16,12.6], 'Orange'))
    objects.append(material_box('roof-rail', [-10.65,3.5,31.5], [.16,1,12.6], 'Teal', True))
    for x,z in [(-8.8,29),(-5.2,35)]:
        objects.append(material_box(f'roof-vent-{z}', [x,3.4,z], [1.3,.8,1.2], 'Teal', True))
    objects.append(material_box('roof-sign', [-10,4.2,35], [.2,2.4,2], 'Orange'))
    objects.append(material_box('roof-secret-ledge', [-11.5,2.85,35], [2,.3,3], 'Cream', True))

    # 地上の分岐は開放的なアーケードを通り、横のドアの奥には実際の部屋を設けます。
    objects.append(material_box('arcade-back', [9.8,1.6,24], [.35,3.2,6], 'Cream', True))
    objects.append(material_box('arcade-awning', [9.3,3.5,24], [1.5,.2,6], 'Orange'))
    for z in (21,27):
        objects.append(material_box(f'arcade-post-{z}', [4.8,1.7,z], [.18,3.4,.18], 'Teal', True))
    objects.append(material_box('secret-floor', [14,.03,33], [8,.06,10], 'Cream'))
    objects.append(material_box('secret-carpet', [14,.08,33], [4,.04,6], 'Orange'))
    for key,pos,size in [('back',[14,1.8,38],[8,3.6,.35]),('east',[18,1.8,33],[.35,3.6,10]),
                         ('south',[14,1.8,28],[8,3.6,.35]),('entry-upper',[10,1.8,35.5],[.35,3.6,5])]:
        objects.append(material_box('secret-wall-'+key,pos,size,'Cream',True))
    objects.append(material_box('secret-lintel', [10,3.5,30.5], [.4,.4,5], 'Orange'))
    crate=material_box('crate', [10,1.15,30.6], [.7,2.3,4.7], 'Orange', True)
    crate['name']='ParkEnemy'
    objects.append(crate)
    objects.append(nature('secret-flowers',[16,.1,36],'flower_purpleC',3))
    objects.append(material_box('secret-bench',[15,.4,36],[2.6,.8,.8],'Teal'))

    # 植栽のある中庭で、2 本の経路が合流するまで次の眺めを隠します。
    objects.append(material_box('courtyard-bed', [1.5,.35,44], [5,.7,2], 'Cream', True))
    objects.append(nature('courtyard-tree',[1.5,.7,44],'tree_oak',3.5))
    for key,x,z in [('canal',3.8,49),('vista',-3.8,86)]:
        objects.append(material_box(f'machine-{key}', [x,.5,z], [1.7,1,1.2], 'Teal', True))
        objects.append(ball('switch-'+key,[x,1.45,z-.75],[.12]*3,'Switch',name='ParkSwitch'))
        gear=model('gear-'+key,[x,1.45,z-.73],[2.4]*3,'Assets/Models/Title/Factory/cog-a.obj',[math.pi/2,0,0])
        gear['components'].append(script('SunriseMechanism',switch='switch-'+key))
        objects.append(gear)
        objects.append(material_box('machine-mark-'+key,[x,.03,z-2],[1.6,.06,1],'Orange'))
    bridge=material_box('canal-bridge',[-6,-.08,56],[4,.16,8.4],'Orange',True)
    bridge['components'].append(script('SunriseBridge',switch='switch-canal'))
    objects.append(bridge)
    for x in (-2.2,2.2):
        objects.append(material_box(f'bridge-track-{x}',[x,-.35,56],[.15,.15,9],'Teal'))
    # 橋とは別に、運河を横に跳び渡る任意の経路を設けます。
    for i,(x,z) in enumerate([(10,52.8),(12,55.3),(10.5,58),(10,60.2)]):
        objects.append(material_box(f'canal-stone-{i}',[x,-.2,z],[2.4,.4,2],'Cream',True))
    # 花壇をずらして北側の経路を曲げ、迷路にはしません。
    for key,x,z in [('west',-5,70),('east',5,80)]:
        objects.append(material_box('garden-bed-'+key,[x,.45,z],[9,.9,2.4],'Cream',True))
        for dx in (-2.5,0,2.5):
            objects.append(nature(f'garden-bush-{key}-{dx}',[x+dx,.9,z],'tree_oak',1.4))
    gate=obj('park-gate',[0,1.2,95],components=[component('BoxCollider',center=[0,0,0],size=[19,2.4,.35])])
    objects.append(gate)
    for i in range(-4,5):
        bar=material_box(f'gate-bar-{i}',[i*2,0,0],[.12,2.4,.2],'Teal')
        bar['parent']='park-gate'
        objects.append(bar)
    for x in (-9.4,9.4):
        objects.append(material_box(f'gate-pillar-{x}',[x,1.5,95],[.8,3,.8],'Cream',True))

    placements=[(-2,1,7),(2,1,14),(-7,3.9,29),(-11.5,3.9,35),
                (7,1,25),(15,1,33),(0,1,49),(12,1,55.3),
                (4,1,70),(-4,1,80),(3,1,90),(0,1.5,108)]
    for i,position in enumerate(placements):
        objects.append(coin(f'flower-piece-{i}',position))
    for i,(x,z) in enumerate([(7,36),(-2,49),(4,74)]):
        enemy=character(f'enemy-{i}',[x,.7,z],'Enemy','ParkEnemy')
        enemy[0]['components'][-1]['behaviour']='SunriseEnemy'
        objects.extend(enemy)
    for i,(x,z) in enumerate([(0,4),(0,49),(0,63),(0,98)]):
        objects.append(ball(f'checkpoint-{i}',[x,.15,z],[.6,.2,.6],'White',name='ParkCheckpoint'))

    objects += character('park-player', [0, 1.05, 1], 'Player', 'Player')
    player = next(o for o in objects if o['id'] == 'park-player')
    body_parts = player['components'][:2]
    player['components'] = player['components'][2:]
    rig = obj('hero-rig', [0, 0, 0], components=[script('SunriseHero')], parent='park-player')
    objects.append(rig)
    objects.append(obj('hero-body', [0, 0, 0], [1.1, 1, 1], body_parts, parent='hero-rig'))
    backpack = ball('hero-backpack', [0, -.02, -.43], [.57, .65, .26], 'Petal', parent='hero-rig')
    objects.append(backpack)
    objects.append(ball('hero-backpack-buckle', [0, -.02, -.57], [.17, .17, .04], 'White', parent='hero-rig'))
    for o in objects:
        if o['parent'] == 'park-player':
            o['parent'] = 'hero-rig' if o['id'] != 'hero-rig' else 'park-player'
    player['components'].append(component('AudioListener', volume=1))
    objects.append(obj('park-puff', [0, 0, 1], [2.8, 1, 2.8], [
        component('MeshRenderer', model='Assets/Models/PhysicsSphere.obj', visibleWhen='parkAttack=1'),
        component('Material', asset='Assets/Materials/Sunrise/Attack.mat')], parent='park-player'))

    # ゴールの塔にもオレンジ色の意匠を使い、通りの上に見えるようにします。
    objects += [material_box('goal-plinth', [0, .25, 111], [6, .5, 6], 'Cream', True),
                material_box('tower-base', [0, 2.5, 119], [5, 5, 4], 'Cream'),
                material_box('tower-column', [0, 7, 119], [2, 5, 2], 'Teal'),
                material_box('tower-roof', [0, 10, 119], [4.8, .3, 4.8], 'Orange'),
                ball('park-goal', [0, 1.5, 111], [1.7]*3, 'Petal')]
    objects.append(model('tower-clock', [0, 7, 116.85], [3.2]*3,
        'Assets/Models/Title/Factory/cog-a.obj', [math.pi/2, 0, 0]))
    for x in (-1.8, 1.8):
        objects.append(material_box(f'tower-trim-{x}', [x, 3.1, 116.9], [.2, 3.6, .2], 'Orange'))
    goal = next(o for o in objects if o['id'] == 'park-goal')
    goal['components'].append(component('Rotator', angularVelocity=[0, 50, 0]))
    for side in (-1, 1):
        for z in (102, 113):
            objects.append(nature(f'vista-tree-{side}-{z}', [side*7, 0, z], 'tree_oak', 4))
    objects.append(obj('music', [0, 0, 0], components=[component('AudioSource', clip='Assets/Audio/Title/Bgm.wav',
        volume=.3, loop=True, playOnAwake=True, volumeBinding='volumeGain')]))
    for cue in ('Collect', 'Attack', 'Hit', 'Damage', 'Jump', 'Mechanism', 'Clear'):
        objects.append(obj('audio-'+cue, [0, 0, 0], components=[component('Script', behaviour='SunriseSound',
            parameters={'volume': .35}, data={'clip': {'kind': 'string', 'value': cue}})]))

    objects.append(obj('park-ui', [0, 0, 0], components=[component('Canvas', referenceSize=[1280, 720],
        scaleWithScreen=True, stateDefaults={'parkAttack': 0, 'parkClear': 0, 'sunriseHurt': 0})]))
    objects += [ui('hud-bg', [22, 20], [720, 76], color=[.035, .09, .14, .82]),
        ui('stage-title', [38, 25], [690, 30], text='01  あさひの街とこもれび遊歩道'),
        ui('park-stats', [38, 60], [690, 30], text='ライフ 3    花のかけら 0 / 12    歯車 0 / 2'),
        ui('hint-bg', [22, 638], [1000, 58], color=[.035, .09, .14, .82]),
        ui('park-hint', [38, 649], [970, 35], text='左右の街区を探索しながら、展望塔を目指そう'),
        ui('park-clear-bg', [210, 245], [860, 230], color=[.035, .09, .14, .96], visible='parkClear=1'),
        ui('park-result', [252, 285], [790, 170], text='ステージクリア！', visible='parkClear=1')]
    scene = dict(version=4, settings=copy.deepcopy(title['settings']), objects=objects)
    scene['settings']['mainCamera'] = 'park-camera'
    scene['settings']['fog'].update(start=48, end=170)
    configure_scene(scene)
    for o in objects:
        for c in o['components']:
            if c['type'] != 'Script':
                continue
            if c['behaviour'] == 'ParkCamera':
                c['behaviour'] = 'SunriseCamera'
            if c['behaviour'] == 'ParkPlayer':
                c['behaviour'] = 'SunrisePlayer'
                c['parameters'].update(switchDistance=2.9, hintBoundary1=16, hintBoundary2=44, hintBoundary3=67, leftLimit=-19, rightLimit=19)
                values = c['data']['value']
                texts = dict(scene=SCENE,
                    statsTemplate='ライフ {hp}   花 {coins}/{coinTotal}   歯車 {switches}/{requiredSwitches}   発見 {discoveries}/2',
                    resultTemplate='あさひの街 クリア！\n花のかけら {coins} / {coinTotal}   発見 {discoveries} / 2   タイム {elapsed} 秒\nR / Start：再挑戦    Esc / B：タイトル',
                    hint1='WASD / 左スティック：移動   Space / A：ジャンプ   J / X：攻撃',
                    hint2='左はカフェの屋上、右は商店の通り抜け。好きな道を探そう',
                    hint3='オレンジの歯車を J / X でたたくと、橋が動く！',
                    hint4='広場の歯車をたたいて展望塔へ！  R：再挑戦  Esc：タイトル')
                for key, value in texts.items():
                    values[key] = {'kind': 'string', 'value': value}
    return batch_static(scene)[0] if batched else scene


def write_assets():
    colors = {'Cream': [.86, .85, .77, 1], 'Paving': [.62, .67, .68, 1], 'Orange': [1, .48, .12, 1],
              'Teal': [.13, .31, .38, 1], 'Grass': [.32, .55, .32, 1], 'Water': [.12, .57, .72, 1], 'Attack': [1, .85, .45, .32]}
    for key, color in colors.items():
        path = GAME / f'Assets/Materials/Sunrise/{key}.mat'
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(dict(version=1, color=color, roughness=.78, metallic=0, transparent=key=='Attack', texture=''), indent=2)+'\n', encoding='utf-8')
    rate = 24000
    cues = {'Collect': [84, 91], 'Attack': [55, 67], 'Hit': [60, 72], 'Damage': [49, 43],
            'Jump': [72, 79], 'Mechanism': [60, 64, 67, 72], 'Clear': [72, 76, 79, 84, 91]}
    folder = GAME / 'Assets/Audio/Sunrise'
    folder.mkdir(parents=True, exist_ok=True)
    for name, pitches in cues.items():
        samples = []
        duration = .11 if name != 'Clear' else .18
        for pitch in pitches:
            hz = 440*2**((pitch-69)/12)
            for i in range(round(duration*rate)):
                t = i/rate
                env = min(1, t/.006)*max(0, 1-t/duration)**2
                samples.append(round(11000*env*(math.sin(2*math.pi*hz*t)+.15*math.sin(4*math.pi*hz*t))))
        with wave.open(str(folder / (name+'.wav')), 'wb') as out:
            out.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
            out.writeframes(struct.pack('<'+'h'*len(samples), *samples))


if __name__ == '__main__':
    write_assets()
    scene, assets = batch_static(assemble(False))
    for relative, content in assets.items():
        target = GAME / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding='utf-8')
    (GAME / SCENE).write_text(json.dumps(scene, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'Authored {SCENE}: {len(scene["objects"])} objects')
