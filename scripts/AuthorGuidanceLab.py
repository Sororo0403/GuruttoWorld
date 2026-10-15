"""Six playable spatial-rhetoric studies, each with a mirrored layout.

Only this explicit authoring command writes the lab scenes and title entry.
Title artwork is preserved; START enters the rebuilt landmark-relay district.
"""
import copy
import json
import math
from AuthorSunriseStreet import GAME, script
from SunriseDistrictBake import DistrictBake
from GenerateParkAdventure import component, obj, write, ui

from GuidanceDistrict import CASES, build_city
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


def controller(mode,mirror=False):
    return obj('lab-controller',[0,0,0],components=[component('Script',behaviour='GuidanceLab',
               parameters={'mode':mode,'mirror':int(mirror)})])


def course(mode,mirror,city,geometry):
    old=json.loads((GAME/'Assets/Scenes/SunriseBlock.json').read_text(encoding='utf-8'))
    signX=-1 if mirror else 1
    keep={'block-sky','block-directionallight','music','park-player','hero-rig','hero-body','hero-backpack','hero-backpack-buckle','park-puff','park-ui'}
    objects=[copy.deepcopy(o) for o in old['objects'] if o['id'] in keep or o['id'].startswith(('park-player-','audio-'))]
    objects+=copy.deepcopy(geometry)
    for original in city.objects:
        o=copy.deepcopy(original)
        if mirror:
            o['position'][0]*=-1
            o['rotation'][1]*=-1
        objects.append(o)
    player=next(o for o in objects if o['id']=='park-player');player['position']=[0,1.05,1]
    for c in player['components']:
        if c['type']=='PlayerController':c.update(moveSpeed=4.6,stepHeight=.36)
        if c['type']=='Script':
            c['behaviour']='GuidancePlayer';c['parameters'].update(requiredSwitches=0,leftLimit=-26,rightLimit=26,coinTotal=0)
            data=c['data']['value'];data['scene']={'kind':'string','value':f'Assets/Scenes/GuidanceLab/Course-{mode}{"B" if mirror else "A"}.json'}
            data['returnScene']={'kind':'string','value':GALLERY}
            for key in ('goal','gate','stats','hint','result'):data[key]={'kind':'reference','value':''}
            for key in ('coins','switches','enemies','checkpoints'):data[key]={'kind':'array','value':[]}
    camera=script('GuidanceCamera',target='park-player',**{'focus'+str(i):'lab-focus-'+str(i) for i in range(4)})
    camera['parameters']={'mode':mode,'mirror':int(mirror),'height':4.3,'distance':7.4,
        'lead':4.7,'weight':[.23,.08,.30,.08,.23,.16][mode],
        'boundary0':[26,25,26,27,23,25][mode],'boundary1':[49,47,47,49,40,51][mode],'boundary2':72}
    objects.append(obj('park-camera',[-.6*signX,4.3,-6.4],rotation=[.22,0,0],components=[
        component('Camera',verticalFov=51,nearClip=.1,farClip=180,referenceAspect=16/9,preserveHorizontal=True),camera]))
    from AuthorSunriseStreet import model
    goal=list(city.goal);goal[0]*=signX
    objects.append(obj('lab-destination',goal))
    objects.append(model('lab-goal',goal,[2.4]*3,'Assets/Models/Title/Factory/cog-a.obj',[math.pi/2,0,0]))
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
    objects.append(ui('lab-task',[52,82],[1170,45],'屋上のカフェを探してください。広場・中庭・屋上を自由に探索できます。'))
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
    records=[]
    for mode,(name,description) in enumerate(CASES):
        city=build_city(mode)
        a=city.b.finish();b=mirrored(city.b,'City'+str(mode)+'B').finish()
        for mirror,geometry in ((False,a),(True,b)):
            path=f'Assets/Scenes/GuidanceLab/Course-{mode}{"B" if mirror else "A"}.json'
            scene=course(mode,mirror,city,geometry);write(GAME/path,scene)
            side=-1 if mirror else 1
            reflect=lambda route:[[x*side,z] for x,z in route]
            records.append(dict(mode=mode,name=name,group=GROUPS[mode],description=description,mirror=mirror,scene=path,
                objects=len(scene['objects']),route=reflect(city.route),alternative=reflect(city.alternative),
                shots=reflect(city.shots),goal=[city.goal[0]*side,*city.goal[1:]],
                family=city.family,renderShots=[[x*side,y,z] for x,y,z in city.renderShots],
                batches=len(geometry),polygons=sum(len(p) for p in city.b.groups.values())))
    write(GAME/GALLERY,gallery())
    write(GAME/(FOLDER+'/Experiments.json'),dict(version=4,goal='屋上のカフェ',cases=records,
          grouping='Editorial familiarity tiers; not surveyed. Foreign names also apply to Japanese.',
          comparison='Authored spatial demonstrations, not a controlled test of rhetoric alone. Architecture, camera focus and temporal response vary.',
          sources=['https://www.nintendo.com/jp/interview/arzga/02.html',
                   'https://rhetoric.byu.edu/Figures/E/epanalepsis.htm']))
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
    start=next(o for o in title['objects'] if o['id']=='world-start')
    next(c for c in start['components'] if c['type']=='Button')['target']='Assets/Scenes/GuidanceLab/Course-4A.json'
    next(c for c in canvas['components'] if c['type']=='Canvas')['menu']['entries'][0]['target']='Assets/Scenes/GuidanceLab/Course-4A.json'
    # Match the established compact-per-object title format for reviewability.
    compact=lambda value:json.dumps(value,ensure_ascii=False,separators=(',',':'))
    text='{\n  "version": '+str(title['version'])+',\n  "settings": '+compact(title['settings'])+',\n  "objects": [\n'
    text+=',\n'.join('    '+compact(o) for o in title['objects'])+'\n  ]'
    for key,value in title.items():
        if key not in ('version','settings','objects'):text+=',\n  '+json.dumps(key)+': '+compact(value)
    text+='\n}\n'
    write(GAME/'Assets/Scenes/TitleStreet.json',text)
    print('Authored 6 spatial studies / 12 layouts. START enters the landmark-relay district.')


if __name__=='__main__':author()
