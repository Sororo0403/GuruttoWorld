"""開始スキャンのため・放出・余韻を、一つの同期した音声として生成します。"""

import math
import random
import struct
import wave
from pathlib import Path


def generate():
    rate = 32000
    duration = 1.25
    release = 0.38
    rng = random.Random(217)
    samples = []
    filtered_noise = 0.0
    for index in range(round(rate * duration)):
        time = index / rate
        noise = rng.uniform(-1, 1)
        filtered_noise += (noise - filtered_noise) * 0.22
        sample = 0.0
        if time < release:
            progress = time / release
            envelope = math.sin(math.pi * progress / 2) ** 2
            phase = 2 * math.pi * (180 * time + 650 * time * time)
            sample = envelope * (0.13 * math.sin(phase) + 0.045 * filtered_noise)
        else:
            elapsed = time - release
            attack = min(1, elapsed / 0.006)
            sweep = math.sin(2 * math.pi * (90 * elapsed + 13 * (1 - math.exp(-12 * elapsed))))
            body = 0.42 * sweep * math.exp(-11 * elapsed)
            air = 0.65 * filtered_noise * math.exp(-6 * elapsed)
            chime = 0.09 * math.sin(2 * math.pi * 880 * elapsed) * math.exp(-9 * elapsed)
            sample = attack * (body + air + chime)
        # 両端を滑らかに閉じ、ループしない短い余韻にします。
        sample *= min(1, time / 0.012, (duration - time) / 0.08)
        samples.append(sample)
    peak = max(abs(value) for value in samples)
    gain = min(1, 0.78 / max(peak, 1e-6))
    with wave.open(str(Path(__file__).with_name("Arrival.wav")), "wb") as output:
        output.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", round(value * gain * 32767)) for value in samples))


if __name__ == "__main__":
    generate()
