"""主人公の溜め・放出と、街から返る木と葉の音を生成します。"""

import math
import random
import struct
import wave
from pathlib import Path


RATE = 32000
RELEASE = 0.92


def write_clip(name, samples):
    peak = max(abs(value) for value in samples)
    gain = min(1.0, 0.82 / max(peak, 1e-6))
    path = Path(__file__).with_name(name)
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", round(value * gain * 32767)) for value in samples))


def arrival():
    rng = random.Random(217)
    duration = 1.94
    filtered_noise = 0.0
    samples = []
    for index in range(round(RATE * duration)):
        time = index / RATE
        noise = rng.uniform(-1, 1)
        filtered_noise += (noise - filtered_noise) * 0.20
        sample = 0.0
        if time < RELEASE:
            # 歯がかみ合う三拍を、放出直前の静けさへつなげます。
            for start in (0.08, 0.36, 0.74):
                elapsed = time - start
                if elapsed >= 0:
                    sample += 0.12 * math.exp(-elapsed * 125) * (
                        math.sin(2 * math.pi * 720 * elapsed) + 0.4 * noise
                    )
            if time < 0.82:
                progress = time / 0.82
                breath = math.sin(math.pi * progress) ** 2
                sample += breath * (0.075 * math.sin(2 * math.pi * (140 * time + 520 * time * time))
                                    + 0.055 * filtered_noise)
        else:
            elapsed = time - RELEASE
            attack = min(1, elapsed / 0.004)
            # 低い着地音と短い空気の抜けを重ね、長い電子音を残しません。
            impact = 0.48 * math.sin(2 * math.pi * (62 * elapsed + 7 * (1 - math.exp(-24 * elapsed))))
            impact *= math.exp(-elapsed * 19)
            air = 0.70 * filtered_noise * math.exp(-elapsed * 9)
            latch = 0.10 * math.sin(2 * math.pi * 1150 * elapsed) * math.exp(-elapsed * 44)
            sample = attack * (impact + air + latch)
        sample *= max(0, min(1, time / 0.008, (duration - time) / 0.08))
        samples.append(sample)
    return samples


def reply(distant):
    rng = random.Random(590 if distant else 391)
    duration = 0.95 if distant else 0.55
    filtered_noise = 0.0
    samples = []
    for index in range(round(RATE * duration)):
        time = index / RATE
        noise = rng.uniform(-1, 1)
        filtered_noise += (noise - filtered_noise) * 0.24
        if distant:
            # 葉の擦れに、小さな金物の余韻を混ぜます。
            rustle = (noise - filtered_noise) * 0.14 * math.exp(-time * 5)
            ring = sum(math.sin(2 * math.pi * frequency * time) * math.exp(-time * decay)
                       for frequency, decay in ((830, 8), (1379, 12), (2213, 18))) * 0.035
            sample = rustle + ring
        else:
            # ひさしの軽い木の返事を二拍にします。
            sample = 0.0
            for start, gain in ((0, 0.25), (0.085, 0.10)):
                elapsed = time - start
                if elapsed >= 0:
                    sample += gain * (math.sin(2 * math.pi * 410 * elapsed) + 0.6 * filtered_noise) * math.exp(-elapsed * 48)
            sample += 0.10 * (noise - filtered_noise) * math.exp(-time * 12)
        sample *= max(0, min(1, time / 0.004, (duration - time) / 0.05))
        samples.append(sample)
    return samples


def generate():
    write_clip("Arrival.wav", arrival())
    write_clip("NearReply.wav", reply(False))
    write_clip("FarReply.wav", reply(True))


if __name__ == "__main__":
    generate()
