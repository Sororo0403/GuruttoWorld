"""Author the short close-camera city block. Does not edit the title or the old stage."""
import copy
import json
import math
from AuthorSunriseStreet import GAME, material_box, model, script, write_assets
from GenerateParkAdventure import component, obj, ball, character, coin, nature, ui, write
from ParkAuthoring import configure_scene
from SunriseBatching import batch_static

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
            'horizontalFollow':1,'response':7},data={'target':{'kind':'reference','value':'park-player'}})]))
    objects.append(material_box('block-foundation',[0,-3.6,60],[180,6,240],'Grass'))
    objects.append(material_box('block-ground',[1,-.6,44],[48,1.2,112],'Paving',True))
    # Buildings have the title's real facades, scaled against the small hero.
    for key,x,z,kind,scale in [('entry-left',-10.8,9,'e',(6,13,13)),
            ('entry-right',10.8,8,'k',(5,9,13)),('corner',-3,31,'k',(7,8.5,15)),
            ('arcade',18,32,'c',(9,15,17)),('cafe',-15,54,'e',(6,9,14)),
            ('workshop',22,54,'k',(5,9,13))]:
        objects.append(model('building-'+key,[x,0,z],scale,
            f'Assets/Models/Title/Surface/Commercial/building-{kind}.obj'))
        bounds={'e':(1.64,.89,1),'c':(.88,.89,1.1),'k':(2.08,1.47,.94)}[kind]
        w,h,d=[bounds[i]*scale[i] for i in range(3)]
        objects.append(obj('collision-'+key,[x,h/2,z],[w-.25,h,d-.15],
            [component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    # The next district is seen as a skyline, not as hundreds of individual props.
    for i,(x,z) in enumerate([(-29,18),(29,18),(-29,57),(35,72),(-25,113),(24,125),(-10,145)]):
        objects.append(model(f'skyline-{i}',[x,0,z],[8,9+i%2*2,8],
            'Assets/Models/Title/Surface/Commercial/building-skyscraper-a.obj'))
    for x,z in [(0,-4),(0,4),(0,12),(0,20),(8,20),(8,28),(8,36),(8,44),(8,52)]:
        objects.append(model(f'road-{x}-{z}',[x,.008,z],[8,1,6],
            'Assets/Models/Title/Surface/Roads/road-straight.obj',[0,math.pi/2,0]))
    for x,z,w,d in [(-4.4,8,2.8,24),(4.4,8,2.8,24),(12,34,2.5,27),
                       (4.6,34,1.4,26),(3,53,8,16),(12,53,10,16)]:
        objects.append(model(f'paving-{x}-{z}',[x,.02,z],[w,1,d],
            'Assets/Models/Title/Surface/Roads/sidewalk.obj'))
    # Orange shop awnings and a projecting sign show the turn before the wall.
    objects.append(material_box('corner-awning',[1.2,3.7,23.7],[7,.18,2.4],'Orange'))
    objects.append(material_box('corner-sign',[4.5,4.8,24.5],[.25,2.2,2.6],'Orange'))
    objects.append(model('corner-sign-gear',[4.67,4.8,24.5],[2.1]*3,
        'Assets/Models/Title/Factory/cog-a.obj',[0,0,math.pi/2]))
    for z in (29,35,41):
        objects.append(material_box(f'arcade-canopy-{z}',[13.2,3.8,z],[2,.16,4],'Orange'))
        objects.append(material_box(f'arcade-post-{z}',[12.7,1.8,z],[.18,3.6,.18],'Teal',True))
    for i,(x,z) in enumerate([(-4.6,16),(12.7,23),(-1,48),(17,55)]):
        objects.append(material_box(f'planter-{i}',[x,.3,z],[1.5,.6,2.5],'Cream',True))
        objects.append(nature(f'flowers-{i}',[x,.61,z],'flower_yellowC' if i%2 else 'flower_purpleC',2.6))
        objects.append(nature(f'tree-{i}',[x,.6,z+.6],'tree_oak',3.8))
    # The courtyard has a small encounter, safe space and a visible machine.
    for i,(x,z) in enumerate([(5,46),(11,50)]):
        enemy=character(f'enemy-{i}',[x,.7,z],'Enemy','ParkEnemy')
        enemy[0]['components'][-1]['behaviour']='SunriseEnemy'
        objects.extend(enemy)
    objects.append(material_box('roof-machine',[7,.55,56],[2.2,1.1,1.5],'Teal',True))
    objects.append(ball('switch-roof',[7,1.5,54.9],[.12]*3,'Switch',name='ParkSwitch'))
    gear=model('gear-roof',[7,1.5,54.87],[2.7]*3,'Assets/Models/Title/Factory/cog-a.obj',[math.pi/2,0,0])
    gear['components'].append(script('SunriseMechanism',switch='switch-roof'))
    objects.append(gear)
    # A cable visually connects the machine to the shutter, and the shutter exposes stairs.
    objects.append(material_box('machine-conduit',[.5,.045,57],[13,.09,.15],'Orange'))
    objects.append(material_box('shutter-frame',[-6,3.3,61],[5.2,.3,.5],'Teal'))
    shutter=material_box('roof-shutter',[-6,1.6,61],[4.6,3.2,.3],'Orange',True)
    shutter['components'].append(script('SunriseShutter',switch='switch-roof'))
    objects.append(shutter)
    for i in range(18):
        h=.3*(i+1)
        objects.append(material_box(f'roof-step-{i}',[-6,h/2,62+i*1.05],[3.8,h,1.1],'Cream',True))
    # Real roof thickness, parapets, cafe furniture and a broad view beyond the district.
    objects.append(material_box('roof-building',[0,2.7,87],[18,5.4,14],'Cream',True))
    for x in (-6,0,6):
        objects.append(model(f'roof-paving-{x}',[x,5.402,87],[6,1,14],
            'Assets/Models/Title/Surface/Roads/sidewalk.obj'))
    for z in (82,87,92):
        objects.append(material_box(f'roof-east-rail-{z}',[9,5.85,z],[.18,.9,4.7],'Teal',True))
        objects.append(material_box(f'roof-window-{z}',[9.03,2.5,z],[.12,2.2,2.6],'Teal'))
    objects.append(material_box('roof-balcony-rail',[0,5.85,94],[18,.9,.18],'Teal',True))
    for x in (-4,3):
        objects.append(material_box(f'roof-table-{x}',[x,6.1,86],[1.6,.15,1.6],'Orange'))
        objects.append(material_box(f'roof-table-leg-{x}',[x,5.72,86],[.2,.65,.2],'Teal'))
    objects.append(material_box('roof-flowerbed',[-8,5.7,89],[1.2,.6,4],'Cream',True))
    objects.append(nature('roof-flowers',[-8,6,89],'flower_purpleC',3))
    # Low street barriers are outside all playable turns, rather than lining an empty plane.
    for key,pos,size in [('west',[-20,1,44],[.5,2,112]),('east',[25,1,44],[.5,2,112]),
            ('end',[2,1,100],[46,2,.5]),('entry',[0,1,-10],[46,2,.5])]:
        objects.append(material_box('boundary-'+key,pos,size,'Cream',True))
    for i,pos in enumerate([(-2,1,8),(8,1,22),(11,1,38),(15,1,54),(-6,3.5,71),(-7,6.3,89)]):
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
    # The next landmark is scenery. The short playable slice ends on the rooftop.
    objects.append(model('next-tower',[4,0,127],[5,6.25,5],
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
            objects.append(model(f'next-shop-{x}-{z}',[x,-.4,z],[5.2,7,8],
                'Assets/Models/Title/Surface/Commercial/building-e.obj'))
            objects.append(nature(f'next-tree-{x}-{z}',[x-3,-.4,z+5],'tree_oak',3.5))
    objects.append(obj('music',[0,0,0],components=[component('AudioSource',clip='Assets/Audio/Title/Bgm.wav',
        volume=.3,loop=True,playOnAwake=True,volumeBinding='volumeGain')]))
    for cue in ('Collect','Attack','Hit','Damage','Jump','Mechanism','Clear'):
        objects.append(obj('audio-'+cue,[0,0,0],components=[component('Script',behaviour='SunriseSound',
            parameters={'volume':.35},data={'clip':{'kind':'string','value':cue}})]))
    objects.append(obj('park-ui',[0,0,0],components=[component('Canvas',referenceSize=[1280,720],scaleWithScreen=True,
        stateDefaults={'parkAttack':0,'parkClear':0,'sunriseHurt':0})]))
    objects += [ui('hud-bg',[22,20],[510,72],color=[.035,.09,.14,.8]),
        ui('stage-title',[38,24],[490,30],text='朝の街角 ― 屋上のカフェへ'),
        ui('park-stats',[38,56],[490,30],text='ライフ 3   花 0/6'),
        ui('hint-bg',[230,652],[820,46],color=[.035,.09,.14,.78]),
        ui('park-hint',[248,659],[790,30],text='WASD：移動   Space：ジャンプ   J：攻撃'),
        ui('park-clear-bg',[240,250],[800,210],color=[.035,.09,.14,.94],visible='parkClear=1'),
        ui('park-result',[280,285],[730,160],text='街区クリア！',visible='parkClear=1')]
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
                for key,value in dict(scene=SCENE,statsTemplate='ライフ {hp}   花 {coins}/6   歯車 {switches}/1',
                    resultTemplate='街区クリア！\n花のかけら {coins} / 6\nR：もう一度    Esc：タイトル',
                    hint1='WASD：移動   Space：ジャンプ   J：攻撃',hint2='オレンジの看板を目印に、店先を右へ',
                    hint3='敵がためたら横によけよう。J で攻撃！',hint4='歯車を J で動かして、屋上のカフェへ').items():
                    values[key]={'kind':'string','value':value}
    return batch_static(scene,BATCHES)[0] if batched else scene


if __name__=='__main__':
    write_assets()
    scene,assets=batch_static(assemble(False),BATCHES)
    for relative,content in assets.items():write(GAME/relative,content)
    write(GAME/SCENE,scene)
    print(f'Authored {SCENE}: {len(scene["objects"])} objects')
