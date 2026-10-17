"""近接カメラで遊ぶ短い街区を作成します。タイトルと旧ステージは編集しません。"""
import copy
import json
import math
from AuthorSunriseStreet import GAME, material_box, model, script
from GenerateParkAdventure import component, obj, ball, character, coin, nature, write
from ParkAuthoring import configure_scene
from SunriseBatching import batch_static
from AuthorEntranceCafe import scene_objects as cafe_objects
from SunriseDistrictBake import scene_objects as district_objects
from SunriseBlockPlan import BUILDINGS, PLAZA_GARDEN

SCENE = 'Assets/Scenes/SunriseBlock.json'
BATCHES = 'Assets/Models/Sunrise/BlockBatches'


def assemble(batched=True):
    title=json.loads((GAME/'Assets/Scenes/TitleStreet.json').read_text(encoding='utf-8'))
    objects=[]
    for kind in ('Sky','DirectionalLight'):
        item=copy.deepcopy(next(o for o in title['objects'] if any(c['type']==kind for c in o['components'])))
        item.update(id='block-'+kind.lower(),parent='')
        if kind=='DirectionalLight':
            next(c for c in item['components'] if c['type']==kind)['ambient']=.4
        objects.append(item)
    objects.append(obj('park-camera',[0,4.9,-6.8],rotation=[.30,0,0],components=[
        component('Camera',verticalFov=49,nearClip=.1,farClip=220,referenceAspect=16/9,preserveHorizontal=True),
        component('Script',behaviour='ParkCamera',parameters={'height':4.2,'distance':7.8,'lookAhead':4.5,
            'horizontalFollow':1,'response':7,'plazaExitX':8,'plazaFollowSlack':.85,
            'plazaFocusWeight':.22,'plazaCameraLeft':-3,'plazaBlendStart':18,'plazaBlendEnd':34},data={'target':{'kind':'reference','value':'park-player'}})]))
    objects.append(material_box('block-foundation',[0,-3.6,60],[180,6,240],'Grass'))
    objects.append(obj('block-ground',[1,-.6,44],[48,1.2,112],
        [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    # 近景の外壁と家具で、同じ CC0 のマテリアル体系とメートル単位のモジュールを共有します。
    objects.extend(cafe_objects())
    objects.extend(district_objects())
    for z in (3,11.7):
        objects.append(obj(f'cafe-table-collision-{z}',[-4.25,.45,z],[1.2,.8,1.2],
            [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    objects.append(obj('collision-entry-left',[-10.9,4.8,8],[10,9.6,20],
        [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    objects.append(obj('collision-cafe-awning',[-5.05,3.0,7.25],[1.65,.4,14.4],
        [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    for key,(x0,x1,z0,z1,height,zone) in BUILDINGS.items():
        if key=='roof':continue
        objects.append(obj('collision-'+key,[(x0+x1)/2,height/2,(z0+z1)/2],
            [x1-x0,height,z1-z0],[component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    x0,x1,z0,z1,h=PLAZA_GARDEN
    objects.append(obj('plaza-garden-collision',[(x0+x1)/2,h/2,(z0+z1)/2],[x1-x0,h,z1-z0],
        [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    objects.append(obj('plaza-table-collision',[10,.45,11.8],[1.2,.8,1.2],
        [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    objects.append(obj('courtyard-garden',[-4.5,.25,49.5],[5,.5,5],
        [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    # 次の地区は、数百個の小物ではなく街の輪郭として見せます。
    for i,(x,z) in enumerate([(-29,18),(35,44),(-29,57),(35,72),(-25,113),(24,125),(-10,145)]):
        objects.append(model(f'skyline-{i}',[x,0,z],[8+i%2]*3,
            'Assets/Models/Title/Surface/Commercial/building-skyscraper-a.obj'))
    for i,(x,z) in enumerate([(-26,76),(-26,91),(-26,106),(32,78),(32,94),(32,110),(-13,114),(18,115)]):
        objects.append(model(f'outer-neighborhood-{i}',[x,0,z],[6.7]*3,
            'Assets/Models/Title/Surface/Commercial/building-e.obj'))
    for i,(x,z) in enumerate([(-16,69),(-14,79),(-22,84),(-22,99),(24,75),(24,91)]):
        objects.append(nature(f'outer-tree-{i}',[x,.01,z],'tree_oak',3.5))
    # 建築の開口部とオレンジ色の歯車で、文字を使わずに曲がる方向を示します。
    objects.append(model('corner-sign-gear',[1.15,4.8,28.3],[2.1]*3,
        'Assets/Models/Title/Factory/cog-a.obj',[0,0,math.pi/2]))
    # 中庭には小規模な遭遇・安全な場所・見える機械を配置します。
    for i,(x,z) in enumerate([(5,46),(11,50)]):
        enemy=character(f'enemy-{i}',[x,.7,z],'Enemy','ParkEnemy')
        enemy[0]['components'][-1]['behaviour']='SunriseEnemy'
        objects.extend(enemy)
    objects.append(material_box('roof-machine',[7,.55,56],[2.2,1.1,1.5],'Teal',True))
    objects.append(ball('switch-roof',[7,1.5,54.9],[.12]*3,'Switch',name='ParkSwitch'))
    gear=model('gear-roof',[7,1.5,54.87],[2.7]*3,'Assets/Models/Title/Factory/cog-a.obj',[math.pi/2,0,0])
    gear['components'].append(script('SunriseMechanism',switch='switch-roof'))
    objects.append(gear)
    # ケーブルで機械とシャッターのつながりを見せ、シャッターを開くと階段が現れるようにします。
    objects.append(material_box('machine-conduit',[.5,.045,57],[13,.09,.15],'Orange'))
    objects.append(material_box('shutter-frame',[-6,3.3,61],[5.2,.3,.5],'Teal'))
    shutter=obj('roof-shutter',[-6,1.6,61],components=[
        component('BoxCollider',center=[0,0,0],size=[4.6,3.2,.3])])
    shutter['components'].append(script('SunriseShutter',switch='switch-roof'))
    objects.append(shutter)
    for i,x in enumerate((-.725,.725,2.175)):
        objects.append(obj(f'shutter-panel-{i}',[x,-1.6,0],[1.45]*3,[
            component('MeshRenderer',model='Assets/Models/Sunrise/District/Baked/SlidingDoor.obj',instancing=True),
            component('Material',asset='Assets/Materials/Sunrise/District/SlidingDoor.mat')],parent='roof-shutter'))
    for i in range(18):
        h=.3*(i+1)
        objects.append(material_box(f'roof-step-{i}',[-6,h/2,62+i*1.05],[3.8,h,1.1],'Cream',True))
    for x in (-7.9,-4.1):
        objects.append(obj(f'stair-side-{x}',[x,3.4,70.95],[.2,1.05,19.65],
            [component('BoxCollider',center=[0,0,0],size=[1,1,1])],rotation=[-math.atan2(.3,1.05),0,0]))
    # 衝突形状は建築の平面形状と一致させ、細かな表面は CC0 のバッチで表現します。
    x0,x1,z0,z1,height,zone=BUILDINGS['roof']
    objects.append(obj('roof-building',[(x0+x1)/2,height/2,(z0+z1)/2],[x1-x0,height,z1-z0],
        [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    for x in (-9,9):
        objects.append(obj(f'roof-rail-{x}',[x,5.95,87],[.2,1.1,14],
            [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    objects.append(obj('roof-balcony-rail',[0,5.95,94],[18,1.1,.2],
        [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    # 低い道路の障壁は、空の平面に並べず、プレイ可能なすべての曲がり角の外側に置きます。
    for key,pos,size in [('west',[-20,1,44],[.5,2,112]),('east',[25,1,44],[.5,2,112]),
            ('end',[2,1,100],[46,2,.5]),('entry',[0,1,-10],[46,2,.5])]:
        objects.append(material_box('boundary-'+key,pos,size,'Cream',True))
    for i,pos in enumerate([(-4.5,1,15),(10.3,1,14.2),(11,1,38),(15,1,54),(-6,3.5,71),(-7,6.3,89)]):
        objects.append(coin(f'block-flower-{i}',pos))
    for i,(x,z) in enumerate([(0,2),(8,43),(-6,58)]):
        objects.append(ball(f'checkpoint-{i}',[x,.15,z],[.5,.15,.5],'White',name='ParkCheckpoint'))
    objects+=character('park-player',[0,1.05,1],'Player','Player')
    player=next(o for o in objects if o['id']=='park-player')
    for c in player['components']:
        if c['type']=='PlayerController':c['moveSpeed']=4.5
    body=player['components'][:2];player['components']=player['components'][2:]
    objects.append(obj('hero-rig',[0,0,0],components=[script('SunriseHero')],parent='park-player'))
    objects.append(obj('hero-body',[0,0,0],[1.1,1,1],body,parent='hero-rig'))
    for o in objects:
        if o['parent']=='park-player' and o['id']!='hero-rig':o['parent']='hero-rig'
    objects.append(ball('hero-backpack',[0,-.02,-.43],[.57,.65,.26],'Petal',parent='hero-rig'))
    objects.append(ball('hero-backpack-buckle',[0,-.02,-.57],[.17,.17,.04],'White',parent='hero-rig'))
    player['components'].append(component('AudioListener',volume=1))
    objects.append(obj('park-puff',[0,0,1],[2.8,1,2.8],[
        component('MeshRenderer',model='Assets/Models/PhysicsSphere.obj',visibleWhen='parkAttack=1'),
        component('Material',asset='Assets/Materials/Sunrise/Attack.mat')],parent='park-player'))
    objects.append(ball('park-goal',[0,6.5,91],[1.5]*3,'Petal'))
    # 次の目印は背景です。この短いプレイ区間は屋上で終わります。
    objects.append(model('next-tower',[4,0,127],[6]*3,
        'Assets/Models/Title/Surface/Commercial/building-skyscraper-a.obj'))
    objects.append(material_box('next-tower-roof',[4,18.2,127],[9,.4,8],'Orange'))
    objects.append(model('next-tower-clock',[4,14,123.5],[5]*3,
        'Assets/Models/Title/Factory/cog-a.obj',[math.pi/2,0,0]))
    objects.append(material_box('next-district-ground',[0,-.5,132],[70,.2,64],'Paving'))
    for z in (104,112,120,128,136,144,152,160):
        objects.append(model(f'next-road-{z}',[4,-.39,z],[8,1,6],
            'Assets/Models/Title/Surface/Roads/road-straight.obj',[0,math.pi/2,0]))
    for x in (-8,18):
        objects.append(model(f'next-paving-{x}',[x,-.38,132],[12,1,60],
            'Assets/Models/Title/Surface/Roads/sidewalk.obj'))
        for z in (108,145):
            objects.append(model(f'next-shop-{x}-{z}',[x,-.4,z],[6]*3,
                'Assets/Models/Title/Surface/Commercial/building-e.obj'))
            objects.append(nature(f'next-tree-{x}-{z}',[x-3,-.4,z+5],'tree_oak',3.5))
    objects.append(obj('music',[0,0,0],components=[component('AudioSource',clip='Assets/Audio/Title/Bgm.wav',
        volume=.3,loop=True,playOnAwake=True,volumeBinding='volumeGain')]))
    for cue in ('Collect','Attack','Hit','Damage','Jump','Mechanism','Clear'):
        objects.append(obj('audio-'+cue,[0,0,0],components=[component('Script',behaviour='SunriseSound',
            parameters={'volume':.35},data={'clip':{'kind':'string','value':cue}})]))
    objects.append(obj('park-ui',[0,0,0],components=[component('Canvas',referenceSize=[1280,720],scaleWithScreen=True,
        stateDefaults={'parkAttack':0,'parkClear':0,'sunriseHurt':0})]))
    scene=dict(version=4,settings=copy.deepcopy(title['settings']),objects=objects)
    scene['settings']['mainCamera']='park-camera'
    scene['settings']['fog'].update(start=60,end=190)
    configure_scene(scene)
    for o in objects:
        for c in o['components']:
            if c['type']!='Script':continue
            if c['behaviour']=='ParkCamera':c['behaviour']='SunriseBlockCamera'
            if c['behaviour']=='ParkPlayer':
                c['behaviour']='SunriseBlockPlayer'
                c['parameters'].update(requiredSwitches=1,coinTotal=6,switchDistance=2.9,leftLimit=-20,rightLimit=25,
                    hintBoundary1=20,hintBoundary2=43,hintBoundary3=60)
                values=c['data']['value']
                values['gate']={'kind':'reference','value':''}
                values['scene']={'kind':'string','value':SCENE}
                for key in ('stats','hint','result'):
                    values[key]={'kind':'reference','value':''}
                for key in ('statsTemplate','resultTemplate','hint1','hint2','hint3','hint4'):
                    values[key]={'kind':'string','value':''}
    return batch_static(scene,BATCHES)[0] if batched else scene


if __name__=='__main__':
    scene,assets=batch_static(assemble(False),BATCHES)
    for relative,content in assets.items():write(GAME/relative,content)
    write(GAME/SCENE,scene)
    print(f'Authored {SCENE}: {len(scene["objects"])} objects')
