"""Author a physical sign from the downloaded Openclipart CC0 source; explicit only."""
from pathlib import Path
import shutil
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "Content/Assets/Models/Title/EmergencyExit"
DEST.mkdir(parents=True, exist_ok=True)
for extension in ("svg", "png"):
    shutil.copy2(ROOT / f"generated/title-assets/exit-sign.{extension}", DEST / f"Source.{extension}")
texture = Image.new("RGB", (1024, 384), (0, 132, 65))
icon = Image.open(DEST / "Source.png").convert("RGBA").convert("RGB").resize((320, 320))
texture.paste(icon, (32, 32))
draw = ImageDraw.Draw(texture)
draw.text((416, 102), "EXIT", font=ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", 174), fill="white")
texture.save(DEST / "Sign.png")

lines = ["mtllib Sign.mtl"]
count = 0
def quad(points, material, textured=False):
    global count
    lines.append(f"usemtl {material}")
    for x, y, z in points:
        lines.append(f"v {x} {y} {-z}")  # OBJ to engine left-handed conversion.
    indices = range(count+1, count+5)
    lines.append("f " + " ".join(f"{i}/{uv}" if textured else str(i) for i, uv in zip(indices,(1,2,3,4))))
    lines.append("f " + " ".join(f"{i}/{uv}" if textured else str(i) for i, uv in reversed(list(zip(indices,(1,2,3,4))))))
    count += 4

lines += ["vt 0 0", "vt 1 0", "vt 1 1", "vt 0 1"]
def box(x0,x1,y0,y1,z0,z1,material):
    quad([(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0)],material)
    quad([(x1,y0,z1),(x0,y0,z1),(x0,y1,z1),(x1,y1,z1)],material)
    quad([(x0,y0,z1),(x0,y0,z0),(x0,y1,z0),(x0,y1,z1)],material)
    quad([(x1,y0,z0),(x1,y0,z1),(x1,y1,z1),(x1,y1,z0)],material)
    quad([(x0,y1,z0),(x1,y1,z0),(x1,y1,z1),(x0,y1,z1)],material)
    quad([(x0,y0,z1),(x1,y0,z1),(x1,y0,z0),(x0,y0,z0)],material)

box(-1.65,1.65,-.65,.65,0,.22,"frame")
for x in (-1.1,1.1):
    box(x-.065,x+.065,-2.0,-.6,.08,.19,"frame")
quad([(-1.55,-.55,-.012),(1.55,-.55,-.012),(1.55,.55,-.012),(-1.55,.55,-.012)],"sign",True)
(DEST / "Sign.obj").write_text("\n".join(lines)+"\n",encoding="utf-8")
(DEST / "Sign.mtl").write_text("newmtl frame\nKd .8 .83 .8\n\nnewmtl sign\nKd 1 1 1\nmap_Kd Sign.png\n",encoding="utf-8")
(DEST / "License.txt").write_text("Security by yves_guillou, Openclipart, 2010-10-23.\nSource: https://openclipart.org/detail/92155/security-by-yves_guillou-92155\nSVG: https://openclipart.org/download/92155/security.svg\nPNG: https://openclipart.org/image/800px/92155\nCC0 1.0 Universal: https://creativecommons.org/publicdomain/zero/1.0/\nOpenclipart policy: https://openclipart.org/share\nDerivative: green background and EXIT text; physical frame and mounting posts authored for WP1.\nFrame geometry also dedicated to CC0 1.0.\n",encoding="utf-8")
