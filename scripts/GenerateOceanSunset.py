"""独立したエディター用シーンと独自の波の環境音を作成します。既存のシーンは変更しません。"""
import argparse
import datetime as dt
import json
import math
from pathlib import Path
import random
import struct
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "Games/GuruttoWorld"


def solar_position(date, latitude, longitude):
    """NOAA の端数年による近似を日本標準時で計算します。方位角は北から時計回りです。"""
    day = date.timetuple().tm_yday
    hour = date.hour + date.minute / 60
    gamma = 2 * math.pi / 365 * (day - 1 + (hour - 12) / 24)
    equation = 229.18 * (0.000075 + 0.001868 * math.cos(gamma) - 0.032077 * math.sin(gamma)
                        - 0.014615 * math.cos(2 * gamma) - 0.040849 * math.sin(2 * gamma))
    declination = (0.006918 - 0.399912 * math.cos(gamma) + 0.070257 * math.sin(gamma)
                   - 0.006758 * math.cos(2 * gamma) + 0.000907 * math.sin(2 * gamma)
                   - 0.002697 * math.cos(3 * gamma) + 0.00148 * math.sin(3 * gamma))
    angle = math.radians((hour * 60 + equation + 4 * longitude - 60 * 9) / 4 - 180)
    latitude = math.radians(latitude)
    elevation = math.asin(math.sin(latitude) * math.sin(declination)
                          + math.cos(latitude) * math.cos(declination) * math.cos(angle))
    azimuth = math.atan2(math.sin(angle), math.cos(angle) * math.sin(latitude)
                        - math.tan(declination) * math.cos(latitude)) + math.pi
    return round(math.degrees(elevation), 4), round(math.degrees(azimuth) % 360, 4)


def object_(id_, name, components, position=(0, 0, 0), rotation=(0, 0, 0)):
    return dict(id=id_, name=name, parent="", position=position, rotation=rotation,
                scale=[1, 1, 1], components=components)


def component(type_, **properties):
    return dict(type=type_, id=type_.lower(), enabled=True, **properties)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def ambience(path):
    """シードを固定し、広帯域ノイズを循環フィルターで処理して波音を作ります。音符・楽器・音楽は使いません。"""
    rate, duration = 32000, 36
    count = rate * duration
    rng = random.Random(20261012)
    channels = []
    for channel in range(2):
        noise = [rng.uniform(-1, 1) for _ in range(count)]
        low, mid = 0.0, 0.0
        # 同じ循環ノイズバッファーの末尾を使って、フィルターの初期状態を整えます。
        for sample in noise[-4096:]:
            low += .004 * (sample - low)
            mid += .075 * (sample - mid)
        output = []
        for i, sample in enumerate(noise):
            low += .004 * (sample - low)
            mid += .075 * (sample - mid)
            phase = 2 * math.pi * i / count
            wash = (.5 + .5 * math.sin(phase * 8 + channel * .18)) ** 2
            secondary = (.5 + .5 * math.sin(phase * 5 + 1.3)) ** 3
            output.append(low * .65 + mid * (.16 + .6 * wash + .15 * secondary))
        channels.append(output)
    peak = max(abs(sample) for channel in channels for sample in channel)
    gain = .65 / peak
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as stream:
        stream.setparams((2, 2, rate, 0, "NONE", "not compressed"))
        block = bytearray(count * 4)
        for i, (left, right) in enumerate(zip(*channels)):
            struct.pack_into("<hh", block, i * 4, round(left * gain * 32767), round(right * gain * 32767))
        stream.writeframes(block)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--overwrite", action="store_true", help="Regenerate this scene's own assets only")
    args = parser.parse_args()
    scene = CONTENT / "Assets/Scenes/OceanSunset.json"
    sound = CONTENT / "Assets/Audio/OceanSunset/CalmSurf.wav"
    evidence = ROOT / "generated/ocean-sunset/authoring.json"
    if not args.overwrite and any(path.exists() for path in (scene, sound)):
        parser.error("OceanSunset already exists; use --overwrite to regenerate its own assets")
    date = dt.datetime(2026, 10, 12, 16, 58)
    latitude, longitude = 35.298, 139.474
    elevation, azimuth = solar_position(date, latitude, longitude)
    ocean = dict(sunElevation=elevation, sunAzimuth=azimuth, sunDiameter=.533,
                 refractiveIndex=1.333, seaLevel=0, waveAmplitude=.075, wavePeriod=4.5,
                 cloudCoverage=.56, cloudSpeed=4, haze=1,
                 cloudBase=1000, cloudThickness=1300, cloudExtinction=.004,
                 ozoneStrength=1, cloudAnisotropy=.65,
                 waterColor=[.018, .045, .065])
    sky = component("Sky", horizon=[.82, .22, .075], zenith=[.11, .10, .24],
                    horizonHeight=.6, sunColor=[1, .47, .15], sunCenter=[.5, .5],
                    sunRadius=[.01, .01], sunStrength=1,
                    cloudLow=[.18, .08, .15], cloudHigh=[.8, .27, .18],
                    cloudOpacity=.9, cloudVelocity=[0, 0], referenceAspect=16 / 9,
                    clouds=[dict(center=[.2, .2], size=1), dict(center=[.5, .3], size=1),
                            dict(center=[.8, .2], size=1)], ocean=ocean)
    objects = [object_("sunset-camera", "夕景 / 海岸の視点 1.75m", [
        component("Camera", verticalFov=38, nearClip=.1, farClip=120000,
                  referenceAspect=16 / 9, preserveHorizontal=True),
        component("AudioListener", volume=1)], position=[0, 1.75, 0],
        rotation=[-.035, math.radians(azimuth - 3), 0]),
        object_("sunset-ocean", "夕景 / 空・雲・海面反射", [sky]),
        object_("sunset-surf", "夕景 / 波音（音楽なし）", [component("AudioSource",
                  clip="Assets/Audio/OceanSunset/CalmSurf.wav", volume=.38, loop=True,
                  playOnAwake=True, cue="", volumeBinding="")])]
    settings = dict(background=[.1, .08, .18, 1], mainCamera="sunset-camera",
                    postEffects=dict(enabled=True, exposure=.2, toneMapping="reinhard",
                                     bloomEnabled=True, bloomIntensity=.10,
                                     bloomThreshold=1.2, bloomRadius=1.4))
    write_json(scene, dict(version=4, settings=settings, objects=objects))
    ambience(sound)
    for asset in (scene, sound, CONTENT / "Shaders/OceanSunset.hlsl"):
        metadata = Path(str(asset) + ".meta")
        if not metadata.exists():
            write_json(metadata, dict(version=1, id=uuid.uuid4().hex, scale=1,
                                      flipV=False, previousPaths=[]))
    write_json(evidence, dict(location="Enoshima / Sagami Bay reference, not a measured reconstruction",
                             latitude=latitude, longitude=longitude, localTime=date.isoformat()+"+09:00",
                             solarElevationDegrees=elevation, solarAzimuthDegrees=azimuth,
                             solarModel="NOAA fractional-year approximation; no refraction correction",
                             waterRefractiveIndex=1.333, solarDiameterDegrees=.533,
                             gravity=9.80665, wavePeriodSeconds=4.5,
                             primaryWavelengthMetres=9.80665*4.5**2/(2*math.pi),
                             atmosphereRadiusKm=6460, groundRadiusKm=6360,
                             rayleighScaleHeightKm=8, aerosolScaleHeightKm=1.2,
                             rayleighScatteringPerKm=[.005802,.013558,.033100],
                             aerosolScatteringPerKm=.003996, aerosolExtinctionPerKm=.004440,
                             ozoneAbsorptionPerKm=[.000650,.001881,.000085],
                             atmosphereViewSamples=16, atmosphereLightSamples=8,
                             cloudViewSamples=24, cloudLightSamples=4,
                             conditions="Artist-selected calm sea and cloud coverage; not observed weather",
                             sources=["https://gml.noaa.gov/grad/solcalc/solareqns.PDF",
                                      "https://nssdc.gsfc.nasa.gov/planetary/factsheet/sunfact.html",
                                      "https://www.ndbc.noaa.gov/faq/algorithm.shtml",
                                      "https://hyperphysics.phy-astr.gsu.edu/hbase/geoopt/refr2.html",
                                      "https://www.nesdis.noaa.gov/about/k-12-education/atmosphere/why-the-sky-blue",
                                      "https://sebh.github.io/publications/egsr2020.pdf",
                                      "https://ebruneton.github.io/precomputed_atmospheric_scattering/",
                                      "https://pbr-book.org/4ed/Volume_Scattering/Transmittance",
                                      "https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models"]))
    print(f"Authored {scene.relative_to(ROOT)}: solar elevation {elevation} deg, azimuth {azimuth} deg")


if __name__ == "__main__":
    main()
