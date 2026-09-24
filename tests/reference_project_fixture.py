"""Compose reference content with the real importers, navigation bake and SDK module.
The generated project is ordinary editable content; none of this runs in a game.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from physics_acceptance_fixture import make_project


def create(build, source, output, module_kit=None):
    executable = lambda name: build / (name + ('.exe' if os.name == 'nt' else ''))
    root = make_project(executable('forge_tools'), output)
    def run(*args):
        result = subprocess.run(list(map(str, args)), text=True, capture_output=True, timeout=180)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result
    shutil.copytree(source / 'tests/fixtures/gltf-official/SimpleSkin/glTF', root / 'Actor')
    shutil.copyfile(source / 'samples/projects/Audio/test-tone.wav', root / 'ambient.wav')
    for asset in ('Actor/SimpleSkin.gltf', 'ambient.wav'):
        result = json.loads(run(executable('forge_tools'), '--assets', 'import', root, asset).stdout)
        if not result.get('ok'): raise RuntimeError(result)
    run(executable('forge_reference_project_fixture'), root, executable('forge_nav_build'), source / 'samples/reference_game')
    if module_kit:
        metadata = json.loads((module_kit / 'forge.module-kit.json').read_text())
        (root / 'Native').mkdir()
        for file in metadata['files']:
            target = root / 'Native' / file
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(module_kit / file, target)
        settings = json.loads((root / 'forge.project.json').read_text())
        settings['modules'] = [dict(id='project.reference', sdk='experimental-1', implementation='1',
            library='Native/' + metadata['library'], fingerprint=metadata['fingerprint'],
            dependencies=['forge.input', 'forge.physics', 'forge.game', 'forge.ui'])]
        (root / 'forge.project.json').write_text(json.dumps(settings, indent=2))
    return root


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--module-kit', type=Path)
    args = p.parse_args()
    print(create(args.build.resolve(), args.source.resolve(), args.output.resolve(),
                 args.module_kit.resolve() if args.module_kit else None))
