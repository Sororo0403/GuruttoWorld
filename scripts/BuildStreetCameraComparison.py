"""同じプレイ可能な街を、制作時のカメラ設定と固定カメラ設定でパッケージ化します。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

REPO = Path(__file__).resolve().parents[1]
GAME = REPO / 'Games/GuruttoWorld'


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def inventory(package):
    return {p.relative_to(package).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(package.rglob('*')) if p.is_file() and p.name != 'package.json'}


def validate(package):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    subprocess.run([str(package / 'App.exe'), '--validate-package'], cwd=package,
                   startupinfo=startup, check=True, timeout=180)
    manifest = read_json(package / 'package.json')
    manifest['startupScene'] = read_json(package / 'Assets/Project.json')['startupScene']
    manifest['files'] = [dict(path=name, bytes=(package / name).stat().st_size, sha256=digest)
                         for name, digest in inventory(package).items()]
    write_json(package / 'package.json', manifest)
    if not all(hashlib.sha256((package / f['path']).read_bytes()).hexdigest() == f['sha256']
               for f in manifest['files']):
        raise RuntimeError('Package hash verification failed: ' + str(package))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-id', default=uuid.uuid4().hex)
    parser.add_argument('--skip-player-build', action='store_true')
    args = parser.parse_args()
    if len(args.run_id) != 32 or any(c not in '0123456789abcdef' for c in args.run_id):
        parser.error('--run-id must contain 32 lowercase hexadecimal characters')
    builds = GAME / 'generated/builds'
    destination = builds / ('CameraComparison-' + args.run_id)
    source = builds / ('Release-' + args.run_id)
    if destination.exists() or source.exists():
        raise FileExistsError('Choose a new run ID; existing builds are preserved.')
    env = dict(os.environ, _LINK_='/DEBUG:NONE')
    command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
               str(REPO / 'scripts/BuildPlayer.ps1'), '-Project', str(GAME),
               '-Configuration', 'Release', '-RunId', args.run_id]
    if args.skip_player_build:
        command.append('-SkipBuild')
    subprocess.run(command, cwd=REPO, env=env, check=True)
    good, bad = destination / 'Good', destination / 'Bad'
    shutil.copytree(source, good)
    shutil.copytree(source, bad)
    scene_path = bad / 'Assets/Scenes/StreetJourney.json'
    scene = read_json(scene_path)
    camera = next(o for o in scene['objects'] if o['id'] == 'park-camera')
    behaviour = next(c for c in camera['components'] if c.get('behaviour') == 'StreetCamera')
    behaviour['parameters']['fixedView'] = 1
    write_json(scene_path, scene)
    for package in (good, bad):
        validate(package)
    # 実行ファイル・スクリプトモジュール・タイトル・形状・照明・操作を一致させてください。
    a, b = inventory(good), inventory(bad)
    differences = sorted(k for k in a.keys() | b.keys() if a.get(k) != b.get(k))
    content_differences = [k for k in differences if not k.startswith('Diagnostics/')]
    if content_differences != ['Assets/Scenes/StreetJourney.json']:
        raise RuntimeError('Unexpected comparison differences: ' + repr(differences))
    good_scene = read_json(good / 'Assets/Scenes/StreetJourney.json')
    del behaviour['parameters']['fixedView']
    if scene != good_scene:
        raise RuntimeError('Only the camera setting may differ.')
    (destination / 'README.txt').write_text(
        'Good/App.exe: 場面に合わせて構図を調整するカメラ\n'
        'Bad/App.exe: 角度・通常距離・画角を固定して追従するカメラ\n'
        '両方ともタイトルのSTARTから同じステージに入ります。\n'
        'WASD:移動 / Space:ジャンプ / J:攻撃・歯車の操作 / R:再開 / Esc:タイトル\n'
        '比較対象はカメラです。壁へのめり込み防止と地面への追従は共通です。\n', encoding='utf-8-sig')
    write_json(destination / 'comparison.json', dict(
        good='Good/App.exe', bad='Bad/App.exe', contentDifferences=content_differences,
        validation='Both packages validated; content hashes verified.'))
    print('Comparison packages: ' + str(destination), flush=True)


if __name__ == '__main__':
    main()
