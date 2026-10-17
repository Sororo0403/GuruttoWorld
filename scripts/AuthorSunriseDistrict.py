"""CC0 の通り・中庭・屋上を作成します。形状は経路を優先して構成します。"""
import math
import shutil

from SunriseDistrictBake import DistrictBake, GAME, TEXTURES, FOLDER, MATERIALS
from AuthorEntranceCafe import TEXTURES as CAFE_TEXTURES, material, quaternius
from GenerateParkAdventure import write
from SunriseBlockPlan import BUILDINGS, PLAZA_GARDEN


def bake():
    folder=GAME/TEXTURES;folder.mkdir(parents=True,exist_ok=True)
    for name in ('paving','asphalt'):
        shutil.copyfile(GAME/f'Assets/Models/Title/Surface/Textures/{name}.png',folder/(name+'.png'))
    from GenerateTitleMaterials import texture
    (folder/'grass.png').write_bytes(texture((104,137,90),.12,70))
    b=DistrictBake()
    plaster=material(CAFE_TEXTURES+'/Plaster.png')
    wood=material(CAFE_TEXTURES+'/Wood.png')
    paving=material(TEXTURES+'/paving.png',roughness=.94)
    asphalt=material(TEXTURES+'/asphalt.png',roughness=.96)
    grass=material(TEXTURES+'/grass.png',roughness=.98)
    teal=material(color=(.19,.34,.36,1))
    orange=material(color=(.80,.39,.16,1))
    cream=material(color=(.94,.86,.66,1))
    active_bounds=None

    def facade(position,count,floors,angle,scale=1,door=False,cap=True):
        # エンジンの座標系変換後、元データの正面は -Z を向きます。
        for floor in range(floors):
            for i in range(count):
                x=position[0]+math.cos(angle)*i*2*scale
                z=position[2]-math.sin(angle)*i*2*scale
                # 建物と逆向きの外壁列を事前に検出し、
                # 何もない衝突形状の上に壁だけが浮く状態を防ぎます。
                if active_bounds:
                    x0,x1,z0,z1=active_bounds
                    assert x0-.15<=x<=x1+.15 and z0-.15<=z<=z1+.15,(x,z,active_bounds)
                if door and floor==0 and i==count//2:name='DoorFrame_Wooden'
                else:name=('Trim_FirstFloor_Window_001' if floor==0 else 'Trim_Window') if i%2==0 else 'Trim_Plain_3'
                b.place('q',name,(x,position[1]+floor*3*scale,z),scale,angle)
        for i in range(count if cap else 0):
            b.place('q','Cornice_Trim_Center',(position[0]+math.cos(angle)*i*2*scale,
                    position[1]+floors*3*scale,position[2]-math.sin(angle)*i*2*scale),scale,angle)

    def building(name):
        nonlocal active_bounds
        x0,x1,z0,z1,height,zone=BUILDINGS[name]
        assert b.zone==zone
        active_bounds=(x0,x1,z0,z1)
        b.cube(((x0+x1)/2,height/2,(z0+z1)/2),(x1-x0-.7,height,z1-z0-.7),plaster)
        b.cube(((x0+x1)/2,height+.65,(z0+z1)/2),(x1-x0+.12,.18,z1-z0+.12),teal)

    def planter(x,y,z,scale=.5):
        b.place('q','Prop_Planter_Single',(x,y,z),scale)
        for dx,dz in ((-.22,0),(.22,.12)):
            b.place('k','pottedPlant',(x+dx,y+.2,z+dz),1.7)

    def seating(x,y,z):
        b.place('k','tableRound',(x,y,z),2.1)
        for dz,angle in ((-.95,0),(.95,math.pi)):
            b.place('k','chairRounded',(x,y,z+dz),2.2,angle)
        b.place('k','plantSmall1',(x,y+.78,z),.7)

    # 重なった直線道路のタイルを、連続した路面へ置き換えます。
    for zone,start,end in ((0,-10,22),(1,22,44),(2,44,64),(3,64,100)):
        b.zone=zone
        b.cube((2.5,-.045,(start+end)/2),(45,.08,end-start),paving)
    b.zone=0
    # カフェの前庭は、両側の縁石に挟まれた幅 6 メートルの帯ではなく、
    # 連続した舗装と空いた中央部で構成します。次の通りは斜めに開きます。
    b.cube((8,.002,25),(8,.025,6),asphalt)
    # 外壁全体を、対応する建物の物理的な平面形状とともに移動します。
    building('entry-right')
    facade((12.5,0,9),7,3,math.pi/2,door=True)
    facade((21.5,0,10),5,3,math.pi,door=False)
    x0,x1,z0,z1,h=PLAZA_GARDEN
    b.cube(((x0+x1)/2,h/2,(z0+z1)/2),(x1-x0,h,z1-z0),cream)
    b.cube(((x0+x1)/2,h,(z0+z1)/2),(x1-x0-.22,.025,z1-z0-.22),grass)
    for x in (8.6,9.3,10.0):
        b.place('k','pottedPlant',(x,h,4.2),1.8)
    seating(10,.02,11.8)

    b.zone=1
    b.cube((8,.002,36),(8,.025,16),asphalt)
    # 縁石は通りの内側から始め、前庭を横切る線を作りません。
    for x in (3.85,12.15):b.cube((x,.07,36),(.18,.14,16),cream)
    building('corner')
    facade((-10,0,28),6,3,0,door=True)
    facade((1,0,29),7,3,-math.pi/2)
    building('arcade')
    facade((14,0,41),9,3,math.pi/2,door=True)
    facade((15,0,24),5,3,0,door=False)
    # 実際の柱と梁でアーケードを構成し、装飾は縁石の奥に置きます。
    for z in (27,33,39):
        b.place('q','Metal_Column_Center',(12.7,0,z),1)
        b.cube((13.3,3.05,z), (1.9,.18,4.4),wood)
        b.cube((13.3,3.18,z), (2.0,.12,4.4),cream)
    for z in (28,38):planter(12.6,.02,z,.42)
    b.place('q','Prop_ManholeCover',(8,.028,32),1)

    b.zone=2
    building('cafe')
    facade((-10,0,45),9,3,-math.pi/2,door=True)
    building('workshop')
    facade((19,0,61),9,2,math.pi/2,door=True)
    # 中庭を囲みながら、戦闘用の空間と戻り道を空けます。
    for x,z in ((-7.5,46),(-1.5,60),(17.8,48),(17.8,58)):
        planter(x,.02,z,.55)
    for x,z in ((-6,48),(-3,48),(-6,51),(-3,51)):
        planter(x,.02,z,.9)
    for x in (-9,18):
        b.place('q','Metal_Column_Center',(x,0,52),1)
        b.place('k','lampWall',(x,2.7,52),1.5)
    b.cube((.5,.035,57),(13,.04,1.8),cream)
    # 機械をシャッターと階段の入口へ物理的につなげます。
    b.place('q','Metal_Column_Center',(-8.45,0,61),1.1)
    b.place('q','Metal_Column_Center',(-3.55,0,61),1.1)
    b.cube((-6,3.45,61),(5.4,.28,.6),teal)
    for y in (.35,.8,1.25,1.7,2.15,2.6):
        b.cube((-8.2,y,61.18),(.22,.06,.12),orange)
    b.place('q','Prop_ACUnit',(8.2,.05,56),2.5,math.pi)
    # モデルの比率を保ち、階段の横の手すりを各段に合わせて上げます。
    step_length=math.hypot(1.05,.3)
    for i in range(18):
        b.place('q','Stairs_Rails_Metal_Straight_1',(-6,.3*(i+1),61.5+i*1.05),
                step_length,math.pi,math.atan2(.3,1.05),origin=(0,.97,0),spread=.8)

    b.zone=3
    active_bounds=BUILDINGS['roof'][:4]
    # 階段脇の空いた場所を、植栽のある前庭にします。
    b.cube((-14.7,-.025,72),(9,.06,17),grass)
    # 一様に拡縮した 2 階分を、既存の高さ 5.4 m のプレイ可能な屋上に合わせます。
    b.cube((0,2.675,87),(17.3,5.35,13.3),plaster)
    facade((-8.1,0,80),10,2,0,.9,cap=False)
    facade((9,0,81),7,2,-math.pi/2,.9,cap=False)
    facade((-9,0,93),7,2,math.pi/2,.9,cap=False)
    b.cube((0,5.36,87),(18,.08,14),paving)
    # 胸壁の笠木と見通しのよい CC0 の手すりで、屋上の先の眺めを保ちます。
    for x in (-8.9,8.9):
        b.cube((x,5.50,87),(.22,.20,14),cream)
        for z in range(80,94):b.place('q','Stairs_Rails_Metal_Straight_1',(x,5.59,z),1,math.pi,origin=(.97,.97,0),side=1)
    b.cube((0,5.5,93.9),(18,.2,.22),cream)
    for x in range(-9,9):b.place('q','Stairs_Rails_Metal_Straight_1',(x,5.59,93.9),1,-math.pi/2,origin=(.97,.97,0),side=1)
    # カフェの屋上を、座席・パーゴラ・植栽・設備のあるテラスにします。
    for x,z in ((-2.5,84.2),(4.8,86),(3.5,90.6)):seating(x,5.42,z)
    for x in (2.7,7):
        for z in (83.5,90.5):b.place('q','Metal_Column_Center',(x,5.4,z),.9)
    for z in (83.5,85,86.5,88,89.5,90.5):b.cube((4.85,8.15,z),(5,.15,.18),wood)
    for x,z in ((-8,83),(-8,91.5),(7.8,92),(7.8,81.3)):planter(x,5.42,z,.55)
    b.place('q','Prop_ACUnit',(-7.3,5.42,81.5),2,math.pi)
    b.cube((-7.3,5.9,81.7),(2.1,1.0,.1),teal)
    b.cube((0,5.43,91),(3,.025,2.7),cream)
    objects=b.finish()
    write(GAME/FOLDER/'Plan.json',BUILDINGS)
    # 動くドアは元の比率を保ち、3 個の固定した複製で開口部を埋めます。
    polygons=quaternius('Door_1')
    assert all(surface==polygons[0][0] for surface,poly in polygons)
    lines=['# Quaternius CC0 Door_1, native material kept separately','mtllib District.mtl','usemtl white']
    index=1
    for surface,polygon in polygons:
        face=[]
        for p,uv,n in polygon:
            lines+=['v '+' '.join(map(str,p)),'vt '+' '.join(map(str,uv)),'vn '+' '.join(map(str,n))]
            face.append(f'{index}/{index}/{index}');index+=1
        lines.append('f '+' '.join(face))
    write(GAME/FOLDER/'Baked/SlidingDoor.obj','\n'.join(lines)+'\n')
    write(GAME/MATERIALS/'SlidingDoor.mat',polygons[0][0])
    return objects


if __name__=='__main__':bake()
