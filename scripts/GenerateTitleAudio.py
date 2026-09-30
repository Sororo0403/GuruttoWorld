"""Generate original title cues without external samples (Python standard library)."""
import math
import random
import struct
import wave
from pathlib import Path

RATE = 32000
ROOT = Path(__file__).resolve().parents[1] / "App/Assets/Audio/Title"
ROOT.mkdir(parents=True, exist_ok=True)

def save(name, samples):
    peak = max(abs(x) for x in samples) or 1
    gain = min(1, 0.8 / peak)
    with wave.open(str(ROOT / name), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", round(x * gain * 32767)) for x in samples))

def note(samples, start, duration, midi, level, bright=False):
    frequency = 440 * 2 ** ((midi - 69) / 12)
    for i in range(int(duration * RATE)):
        t = i / RATE
        envelope = min(1, t / 0.008) * min(1, (duration - t) / 0.035) * math.exp(-2 * t / duration)
        tone = math.sin(2 * math.pi * frequency * t)
        if bright:
            tone += 0.22 * math.sin(4 * math.pi * frequency * t)
        samples[(round(start * RATE) + i) % len(samples)] += level * envelope * tone

# Eight bars at 120 BPM: warm electric-key chords, syncopated bass and soft drums.
music = [0.0] * (16 * RATE)
rng = random.Random(1701)
chords = [(57, 60, 64, 67), (53, 57, 60, 64), (55, 59, 62, 65), (52, 55, 59, 62)]
for bar in range(8):
    chord = chords[bar % 4]
    for beat in (0, 0.75, 1.5):
        for pitch in chord:
            note(music, bar * 2 + beat, 0.42, pitch + 12, 0.055, True)
    for beat, pitch in ((0, chord[0]-12), (0.75, chord[0]), (1.25, chord[0]-12), (1.75, chord[2]-12)):
        note(music, bar * 2 + beat, 0.24, pitch, 0.20)
    for step in range(8):
        start = round((bar * 2 + step * 0.25) * RATE)
        for i in range(int(0.07 * RATE)):
            t = i / RATE
            music[(start+i) % len(music)] += rng.uniform(-1, 1) * math.exp(-65*t) * 0.032 * min(1, t/0.002)
    for beat in range(4):
        start = round((bar * 2 + beat * 0.5) * RATE)
        for i in range(int(0.16 * RATE)):
            t = i / RATE
            sound = math.sin(2*math.pi*(52*t + 3*(1-math.exp(-30*t)))) * math.exp(-26*t) * 0.20
            if beat % 2:
                sound += rng.uniform(-1, 1) * math.exp(-35*t) * 0.085
            music[(start+i) % len(music)] += sound * min(1, t/0.003)
save("Bgm.wav", music)
for name, pitches in (("Select.wav", (81,)), ("Confirm.wav", (76, 83)), ("Back.wav", (76, 69)), ("Error.wav", (55, 54))):
    cue = [0.0] * round((len(pitches) * 0.065 + 0.08) * RATE)
    for index, pitch in enumerate(pitches):
        note(cue, index * 0.065, 0.09, pitch, 0.30, True)
    save(name, cue)
print("Generated five mono PCM16/32kHz title audio files.")
