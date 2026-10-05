"""Generate only the farewell sign; gate geometry is Kenney Castle Kit CC0."""
from pathlib import Path
root = Path(__file__).resolve().parents[1] / 'App/Assets/Models/Title/Exit'
root.mkdir(parents=True, exist_ok=True)
# Assimp flips Z and UVs. The sign faces the same direction as the CC0 gate.
lines = ['mtllib ExitFacade.mtl', 'usemtl farewell']
for x,y,z in [(2.3,5.20,-.55),(-2.3,5.20,-.55),(-2.3,5.98,-.55),(2.3,5.98,-.55)]:
    lines.append(f'v {x} {y} {z}')
lines.extend(['vt 0 0','vt 1 0','vt 1 1','vt 0 1','f 1/1 2/2 3/3 4/4','f 4/4 3/3 2/2 1/1'])
(root / 'ExitFacade.obj').write_text('\n'.join(lines) + '\n', encoding='utf-8')
(root / 'ExitFacade.mtl').write_text('newmtl farewell\nKd 1 1 1\nmap_Kd Farewell.png\n', encoding='utf-8')
