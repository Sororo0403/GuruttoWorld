"""Six playable spatial-rhetoric studies, each with a mirrored layout.

Only this explicit authoring command writes the lab scenes and title entry.
The original city, its START target, and title artwork are preserved.
"""
import copy
import json
import math
from collections import defaultdict
from AuthorSunriseStreet import GAME, script
from AuthorEntranceCafe import material, TEXTURES as CAFE_TEXTURES
from SunriseDistrictBake import DistrictBake, TEXTURES as DISTRICT_TEXTURES
from GenerateParkAdventure import component, obj, write, ui
from SunriseBatching import rotate

CASES=[('擬人法','鉢植えが枝で手招きして、通りへ誘う'),('反復法','同じ庇を、同じ強さで繰り返す'),
       ('換喩','カップから、その先のカフェを連想する'),('漸層法','植栽を小・中・大と段階的に強める'),
       ('Anadiplosis','前の終わりの目印を、次の始まりへ渡す'),
       ('Anapodoton','門とカップの対応を示し、最後の帰結を空ける')]
GROUPS=['定番','定番','珍しい','珍しい','激レア','激レア']
FOLDER='Assets/Models/GuidanceLab'
GALLERY='Assets/Scenes/GuidanceGallery.json'


def baker(name):
    return DistrictBake(FOLDER+'/'+name,'Assets/Materials/GuidanceLab/'+name,
                        'Assets/Textures/GuidanceLab/'+name,'lab-'+name)


def mirrored(source,name):
    b=baker(name)
    for key,polygons in source.groups.items():
        b.groups[key]=[[((-p[0],p[1],p[2]),uv,(-n[0],n[1],n[2]))
                         for p,uv,n in reversed(poly)] for poly in polygons]
    b.sources=[dict(item,position=[-item['position'][0],*item['position'][1:]],
                    angle=-item['angle'],reflectedX=True) for item in source.sources]
    return b


def sign(b,x,y,z,scale=1):
    w,h=2.2*scale,1.45*scale
    b.cube((x,y,z+.10),(w+.12,h+.12,.12),material(color=(.19,.34,.36,1)))
    surface=material(CAFE_TEXTURES+'/CafeSign.png',roughness=.82)
    vertices=[(x-w/2,y-h/2,-z),(x+w/2,y-h/2,-z),(x+w/2,y+h/2,-z),(x-w/2,y+h/2,-z)]
    b.groups[(b.zone,json.dumps(surface,sort_keys=True))].append(
        [(p,uv,(0,0,1)) for p,uv in zip(vertices,((0,0),(1,0),(1,1),(0,1)))])


def awning(b,x,z,width=1.8,angle=0,scale=1):
    cream=material(color=(.94,.86,.66,1));orange=material(color=(.80,.39,.16,1))
    # Fabric, valance and attached supports all turn together at the corner.
    for local,size,surface in [((0,3.2*scale,-.65),(width,.13,1.4),orange),
                               ((0,3.04*scale,-1.3),(width,.28,.10),cream),
                               ((0,3.1*scale,0),(width,.12,.14),cream)]:
        p=rotate(local,(0,angle,0))
        sx,sy,sz=size
        if abs(angle)>1:sx,sz=sz,sx
        b.cube((x+p[0],p[1],z+p[2]),(sx,sy,sz),surface)


def common():
    b=baker('CommonA')
    plaster=material(CAFE_TEXTURES+'/Plaster.png');teal=material(color=(.19,.34,.36,1))
    paving=material(DISTRICT_TEXTURES+'/paving.png',roughness=.94)
    b.cube((0,-.045,23),(44,.08,62),paving)
    b.cube((0,3,26),(9.4,6,19.4),plaster)
    b.cube((0,6.1,26),(10.15,.18,20.15),teal)

    def facade(x,z,count,angle,floors=2):
        for floor in range(floors):
            for i in range(count):
                name='Trim_Window' if i%2==0 else 'Trim_Plain_3'
                b.place('q',name,(x+math.cos(angle)*i*2,floor*3,z-math.sin(angle)*i*2),1,angle)
        for i in range(count):b.place('q','Cornice_Trim_Center',(x+math.cos(angle)*i*2,floors*3,z-math.sin(angle)*i*2),1,angle)
    facade(-4,16,5,0);facade(5,17,10,-math.pi/2);facade(-5,35,10,math.pi/2);facade(4,36,5,math.pi)
    for side in (-1,1):
        for z in (5,29,49):
            b.cube((side*18,4.5,z),(7.4,9,13.4),plaster)
            b.cube((side*18,9.15,z),(8.1,.18,14.1),teal)
            facade(side*14,z+(-6 if side<0 else 6),7,-math.pi/2 if side<0 else math.pi/2,3)
        for z in (15,39):
            b.place('q','Prop_Planter_Single',(side*13,0,z),.6)
            b.place('k','pottedPlant',(side*13,.2,z),2.2)
    # Destination cafe: the same cup pictogram, furniture and gear in all cases.
    b.cube((8.5,3,51),(7.4,6,5.4),plaster)
    facade(5.5,48,4,0)
    sign(b,8.5,4.3,47.9,1.2)
    for x in (5.5,7.5,9.5,11.5):awning(b,x,48)
    for x,z in ((-9,43),(11,43)):
        b.place('k','tableRound',(x,0,z),2.1)
        for dz,angle in ((-.9,0),(.9,math.pi)):b.place('k','chairRounded',(x,0,z+dz),2.2,angle)
    b.place('k','kitchenCoffeeMachine',(11,.8,43),1.3)
    return b


def cues(mode):
    b=baker('Cue'+str(mode)+'A')
    if mode==0:b.place('k','pottedPlant',(3.8,0,15.1),3)
    if mode==1:
        for x in (-4,-2,0,2,4):awning(b,x,15.7)
        for z in (18,22,26):awning(b,5.05,z,angle=-math.pi/2)
    if mode==2:
        sign(b,3.8,2.3,15.58,.82)
    if mode==3:
        for x,z,scale in ((-3,15.65,1.1),(0,15.65,1.75),(3.5,15.65,2.6)):
            b.place('q','Prop_Planter_Single',(x,0,z),scale*.23)
            b.place('k','pottedPlant',(x,.13,z),scale)
    if mode==4:
        # A->B / B->C / C->A: the end of each group is repeated at the next
        # beginning. This differs from repeating one uniform trail of A's.
        awning(b,-3.8,15.7)
        sign(b,-1.6,2.3,15.58,.62)
        sign(b,1.4,2.3,15.58,.62)
        b.place('k','pottedPlant',(4,.05,15.6),2.6)
        b.place('k','pottedPlant',(5.8,.05,20.3),2.6)
        awning(b,5.05,23,angle=-math.pi/2)
    if mode==5:
        wood=material(CAFE_TEXTURES+'/Wood.png')
        # Establish "frame -> cup" twice; the third frame surrounds open
        # space, deliberately withholding the expected consequent.
        for x in (-3,0):
            for dx in (-1.25,1.25):b.cube((x+dx,2.4,15.45),(.12,1.85,.14),wood)
            for y in (1.48,3.32):b.cube((x,y,15.45),(2.6,.12,.14),wood)
            sign(b,x,2.4,15.28,.82)
        for x in (5.5,11.5):
            b.cube((x,1.8,18),(.18,3.6,.22),wood)
            # Short returns suggest the missing lintel, without placing an
            # opaque bar across the camera's view of the player.
            b.cube((x+(.45 if x<8 else -.45),3.55,18),(1.1,.16,.22),wood)
    return b


def controller(mode,mirror=False):
    return obj('lab-controller',[0,0,0],components=[component('Script',behaviour='GuidanceLab',
               parameters={'mode':mode,'mirror':int(mirror)})])


def course(mode,mirror,common_objects,cue_objects):
    old=json.loads((GAME/'Assets/Scenes/SunriseBlock.json').read_text(encoding='utf-8'))
    signX=-1 if mirror else 1
    keep={'block-sky','block-directionallight','music','park-player','hero-rig','hero-body','hero-backpack','hero-backpack-buckle','park-puff','park-ui'}
    objects=[copy.deepcopy(o) for o in old['objects'] if o['id'] in keep or o['id'].startswith(('park-player-','audio-'))]
    objects+=copy.deepcopy(common_objects)+copy.deepcopy(cue_objects)
    player=next(o for o in objects if o['id']=='park-player');player['position']=[0,1.05,1]
    for c in player['components']:
        if c['type']=='Script':
            c['behaviour']='SunrisePlayer';c['parameters'].update(requiredSwitches=0,leftLimit=-15,rightLimit=15,coinTotal=0)
            data=c['data']['value'];data['scene']={'kind':'string','value':f'Assets/Scenes/GuidanceLab/Course-{mode}{"B" if mirror else "A"}.json'}
            data['returnScene']={'kind':'string','value':GALLERY}
            for key in ('goal','gate','stats','hint','result'):data[key]={'kind':'reference','value':''}
            for key in ('coins','switches','enemies','checkpoints'):data[key]={'kind':'array','value':[]}
    objects.append(obj('park-camera',[0,4.9,-6.8],rotation=[.30,0,0],components=[
        component('Camera',verticalFov=49,nearClip=.1,farClip=160,referenceAspect=16/9,preserveHorizontal=True),
        script('GuidanceCamera',target='park-player')]))
    for key,pos,size in [('floor',[0,-.6,23],[28,1.2,62]),('island',[0,3,26],[10,6,20]),
            ('cafe',[8.5,3,51],[8,6,6]),('left',[-14.1,.7,23],[.2,1.4,62]),
            ('right',[14.1,.7,23],[.2,1.4,62]),('end',[0,.7,54],[28,1.4,.2]),('back',[0,.7,-8],[28,1.4,.2])]:
        pos[0]*=signX
        objects.append(obj('lab-collision-'+key,pos,size,[component('BoxCollider',center=[0,0,0],size=[1,1,1])]))
    # A visible shared destination; no collectibles, enemies or wayfinding HUD.
    from AuthorSunriseStreet import model
    objects.append(model('lab-goal',[8.5*signX,1.35,47.5],[2.4]*3,'Assets/Models/Title/Factory/cog-a.obj',[math.pi/2,0,0]))
    if mode==0:
        from GenerateParkAdventure import ball, box
        for i,x in enumerate((3.65,3.95)):
            objects.append(ball(f'guide-eye-{i}',[x*signX,1.25,14.70],[.18,.22,.10],'White'))
            objects.append(ball(f'guide-pupil-{i}',[x*signX,1.25,14.64],[.075,.10,.045],'Eye'))
        objects.append(obj('guide-hand-pivot',[4.0*signX,1.05,15.0],components=[
            component('Script',behaviour='GuidanceGesture',parameters={'mirror':int(mirror)})]))
        arm=box('guide-arm',[.48*signX,0,0],[.96,.12,.14],'Wood');arm['parent']='guide-hand-pivot';objects.append(arm)
        objects.append(ball('guide-hand',[1.0*signX,0,0],[.28,.24,.20],'White',parent='guide-hand-pivot'))
    objects.append(controller(mode,mirror))
    return dict(version=4,settings=copy.deepcopy(old['settings']),objects=objects)


def button(identity,position,size,text,event,parent='park-ui',font=24):
    o=ui(identity,position,size,text,color=[.11,.20,.23,.95],parent=parent)
    next(c for c in o['components'] if c['type']=='Text')['fontSize']=font
    o['components'].append(component('Button',action='click',event=event,target='',sound='',
        hoverColor=[.30,.45,.44,1],pressedColor=[.18,.32,.33,1]))
    return o


def gallery():
    old=json.loads((GAME/'Assets/Scenes/SunriseBlock.json').read_text(encoding='utf-8'))
    objects=[copy.deepcopy(o) for o in old['objects'] if o['id'] in ('block-sky','block-directionallight','music')]
    objects.append(obj('park-camera',[0,4.9,-6.8],components=[component('Camera',verticalFov=49,nearClip=.1,farClip=160,referenceAspect=16/9,preserveHorizontal=True)]))
    objects.append(obj('park-ui',[0,0,0],components=[component('Canvas',referenceSize=[1280,720],scaleWithScreen=True)]))
    objects.append(ui('lab-backdrop',[0,0],[1280,720],color=[.07,.13,.16,.98]))
    objects.append(ui('lab-heading',[52,30],[1170,50],'ことばで道をつくる  /  修辞の街'))
    next(c for c in objects[-1]['components'] if c['type']=='Text')['fontSize']=34
    objects.append(ui('lab-task',[52,82],[1170,45],'奥のカフェを探してください。移動・ジャンプはいつもの操作です。'))
    objects.append(button('lab-layout',[52,134],[360,42],'配置 A  /  Mで反転','guidance:mirror',font=20))
    objects.append(button('lab-blind',[434,134],[790,42],'発表モード：技法名を表示  /  Bで切替','guidance:blind',font=20))
    for i,(name,description) in enumerate(CASES):
        x=52+(i%2)*600;y=194+(i//2)*142
        objects.append(button('lab-name-'+str(i),[x,y],[576,48],str(i+1)+'   '+GROUPS[i]+'  '+name,'guidance:course:'+str(i),font=25))
        objects.append(ui('lab-description-'+str(i),[x+14,y+53],[548,43],description))
        next(c for c in objects[-1]['components'] if c['type']=='Text')['fontSize']=18
        objects.append(ui('lab-result-'+str(i),[x+14,y+97],[548,29],'未プレイ'))
        next(c for c in objects[-1]['components'] if c['type']=='Text')['fontSize']=17
    objects.append(ui('lab-export',[52,635],[1170,35],'記録は自動保存されます。R：やり直す / Tab・Esc：一覧 / M：配置反転'))
    next(c for c in objects[-1]['components'] if c['type']=='Text')['fontSize']=18
    objects.append(ui('lab-footer',[52,678],[1170,27],'1〜6：コース選択  /  WASD：移動  /  Space：ジャンプ  /  Esc：タイトル'))
    next(c for c in objects[-1]['components'] if c['type']=='Text')['fontSize']=18
    objects.append(controller(-1))
    return dict(version=4,settings=copy.deepcopy(old['settings']),objects=objects)


def author():
    base=common();commonA=base.finish();commonB=mirrored(base,'CommonB').finish()
    records=[]
    for mode,(name,description) in enumerate(CASES):
        cue=cues(mode)
        a=cue.finish() if cue.groups else []
        b=mirrored(cue,'Cue'+str(mode)+'B').finish() if cue.groups else []
        for mirror,geometry,extra in ((False,commonA,a),(True,commonB,b)):
            path=f'Assets/Scenes/GuidanceLab/Course-{mode}{"B" if mirror else "A"}.json'
            scene=course(mode,mirror,geometry,extra);write(GAME/path,scene)
            records.append(dict(mode=mode,name=name,group=GROUPS[mode],description=description,mirror=mirror,scene=path,objects=len(scene['objects'])))
    write(GAME/GALLERY,gallery())
    write(GAME/(FOLDER+'/Experiments.json'),dict(version=2,goal='奥のカフェ',cases=records,
          grouping='Presentation familiarity tiers are editorial estimates, not measured public awareness. Greek-origin names are not language-exclusive.',
          comparison='Same collision layout, controls, lighting, camera and destination; cue geometry varies. Reflection swaps left/right.',
          sources=['https://rhetoric.byu.edu/Figures/P/personification.htm','https://rhetoric.byu.edu/Figures/Groupings/of%20Repetition.htm',
                   'https://rhetoric.byu.edu/Figures/M/metonymy.htm','https://rhetoric.byu.edu/Figures/C/climax.htm',
                   'https://rhetoric.byu.edu/Figures/A/anadiplosis.htm','https://rhetoric.byu.edu/Figures/A/anapodoton.htm']))
    project=json.loads((GAME/'Assets/Project.json').read_text(encoding='utf-8'))
    keys={'GuidanceGallery':64,'GuidanceMenu':15,'GuidanceMirror':50,'GuidanceBlind':48}
    keys.update({'GuidanceCourse'+str(i):i+1 for i in range(1,7)})
    for key,code in keys.items():project['inputActions'][key]=dict(axis=0,buttons=0,keys=[code],mouseButtons=0,threshold=.5)
    write(GAME/'Assets/Project.json',project)
    title=json.loads((GAME/'Assets/Scenes/TitleStreet.json').read_text(encoding='utf-8'))
    title['objects']=[o for o in title['objects'] if o['id'] not in ('lab-controller','guidance-entry')]
    canvas=next(o for o in title['objects'] if any(c['type']=='Canvas' and 'menu' in c for c in o['components']))
    entry=button('guidance-entry',[890,658],[350,42],'修辞の街を選ぶ','guidance:gallery',parent=canvas['id'],font=19)
    next(c for c in entry['components'] if c['type']=='RectTransform')['visibleWhen']='screen=0'
    title['objects']+=[controller(-2),entry]
    # Match the established compact-per-object title format for reviewability.
    compact=lambda value:json.dumps(value,ensure_ascii=False,separators=(',',':'))
    text='{\n  "version": '+str(title['version'])+',\n  "settings": '+compact(title['settings'])+',\n  "objects": [\n'
    text+=',\n'.join('    '+compact(o) for o in title['objects'])+'\n  ]'
    for key,value in title.items():
        if key not in ('version','settings','objects'):text+=',\n  '+json.dumps(key)+': '+compact(value)
    text+='\n}\n'
    write(GAME/'Assets/Scenes/TitleStreet.json',text)
    print('Authored 6 studies / 12 layouts and a comparison gallery; original START still enters SunriseBlock.')


if __name__=='__main__':author()
