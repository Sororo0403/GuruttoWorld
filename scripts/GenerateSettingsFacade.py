"""Generate original, texture-free geometry for the title's settings facility."""
from pathlib import Path
import math

root = Path(__file__).resolve().parents[1] / 'App/Assets/Models/Title/Settings'
root.mkdir(parents=True, exist_ok=True)
lines = ['# Original WP1 settings facade', 'mtllib ControlFacade.mtl']
count = 0

def face(vertices, material):
    global count
    lines.append('usemtl ' + material)
    for x, y, z in vertices:
        lines.append(f'v {x:.6f} {y:.6f} {z:.6f}')
    ids = [count + i + 1 for i in range(len(vertices))]
    # Both sides are explicit so the raised symbols are readable from the street.
    lines.append('f ' + ' '.join(map(str, ids)))
    lines.append('f ' + ' '.join(map(str, reversed(ids))))
    count += len(vertices)

def box(x, y, z, dx, dy, dz, material):
    v = [(x + a*dx/2, y + b*dy/2, z + c*dz/2)
         for a,b,c in [(-1,-1,-1),(-1,-1,1),(-1,1,1),(-1,1,-1),
                       (1,-1,-1),(1,-1,1),(1,1,1),(1,1,-1)]]
    for indices in [(0,1,2,3),(4,7,6,5),(0,4,5,1),(3,2,6,7),(0,3,7,4),(1,5,6,2)]:
        face([v[i] for i in indices], material)

# Building front faces +X after its -90-degree rotation. All details sit outside it.
box(1.85, 3.25, 0, .20, 1.45, 3.6, 'housing')
box(1.97, 3.25, 0, .06, 1.26, 3.4, 'screen')
# Large hollow cog on the sign, with eight teeth.
for i in range(64):
    a, b = i*math.tau/64, (i+1)*math.tau/64
    ra = .48 if i % 8 in (1,2,3,4) else .39
    rb = .48 if (i+1) % 8 in (1,2,3,4) else .39
    face([(2.02, 3.25 + r*math.sin(t), -.95 + r*math.cos(t))
          for r,t in [(ra,a),(rb,b),(.22,b),(.22,a)]], 'accent')
# Three oversized adjustment sliders, readable without text.
for y, knob in [(2.87,.35),(3.25,1.15),(3.63,.72)]:
    box(2.02,y,.8,.06,.055,1.55,'rail')
    box(2.08,y,knob,.10,.23,.14,'accent')
# Entrance canopy, two illuminated uprights and a freestanding control kiosk.
box(2.04,2.28,0,.85,.12,2.3,'housing')
for z in [-1.02,1.02]:
    box(1.95,1.12,z,.10,2.0,.10,'accent')
box(2.18,.58,-1.65,.48,1.16,.58,'housing')
box(2.44,.92,-1.65,.045,.38,.43,'accent')
box(2.45,.61,-1.65,.06,.05,.32,'rail')

(root / 'ControlFacade.obj').write_text('\n'.join(lines) + '\n', encoding='utf-8')
(root / 'ControlFacade.mtl').write_text(
    'newmtl housing\nKd 0.12 0.20 0.26\n\n'
    'newmtl screen\nKd 0.04 0.10 0.14\n\n'
    'newmtl accent\nKd 0.22 0.88 0.82\n\n'
    'newmtl rail\nKd 0.78 0.88 0.89\n', encoding='utf-8')
