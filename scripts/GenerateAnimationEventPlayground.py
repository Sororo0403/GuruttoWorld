"""Generate an owned animation-event scene with visible per-character light pulses."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "Content"


def main():
    scene = json.loads((ROOT / "Assets/Scenes/BlendTreePlayground.json").read_text(encoding="utf-8"))
    model = json.loads((ROOT / "Assets/Models/AnimatedBox.gltf").read_text(encoding="utf-8"))
    durations = {clip["name"]: max(model["accessors"][sampler["input"]]["max"][0] for sampler in clip["samplers"]) for clip in model["animations"]}
    scene["settings"]["postEffects"] = dict(enabled=True, exposure=-1, toneMapping="filmic", bloomEnabled=True, bloomIntensity=.3, bloomThreshold=1, bloomRadius=1)
    colors = iter(([1, .15, .1], [.1, 1, .2], [.1, .4, 1]))
    for actor in scene["objects"]:
        animator = next((component for component in actor["components"] if component["type"] == "Animator"), None)
        if animator is None:
            continue
        actor["name"] += " Events"
        animator["events"] = []
        for clip, duration in durations.items():
            for number, fraction in enumerate((.1, .6)):
                animator["events"].append(dict(clip=clip, name="footstep", time=round(duration * fraction, 6), value=1, minimumWeight=.05, stringValue="left" if number == 0 else "right", intValue=number))
        actor["components"].append(dict(id="pulseLight", type="PointLight", enabled=True, color=next(colors), intensity=0, range=8))
        actor["components"].append(dict(id="pulse", type="Script", enabled=True, behaviour="AnimationEventPulse", parameters=dict(duration=.2, peak=15)))
    target = ROOT / "Assets/Scenes/AnimationEventPlayground.json"
    target.write_bytes((json.dumps(scene, ensure_ascii=False, indent=2) + "\n").replace("\n", "\r\n").encode("utf-8"))
    metadata = target.with_name(target.name + ".meta")
    if not metadata.exists():
        identity = hashlib.sha256(b"WP1/Assets/Scenes/AnimationEventPlayground.json").hexdigest()[:32]
        metadata.write_bytes((json.dumps(dict(version=1, id=identity, scale=1, flipV=False, previousPaths=[]), indent=2) + "\n").replace("\n", "\r\n").encode("utf-8"))


if __name__ == "__main__":
    main()
