"""Prepare the original foreign-target HDR and verify native source-free content."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from gallery_project_fixture import copy_gallery_project


def verify(fixture, gallery, worker, tools, evidence):
    fixture, gallery, worker, tools, evidence = (
        Path(path).resolve() for path in (fixture, gallery, worker, tools, evidence))
    evidence.mkdir(parents=True, exist_ok=True)
    scene_path = 'Scenes/158-imported-lightmaps.scene.json'
    image_path = 'Assets/Lightmaps/Example.hdr'
    material_path = 'Assets/Lightmaps/Shared.material.json'
    shader_path = 'Assets/Lightmaps/Irradiance.shader.json'
    target = dict(platform='windows', backend='d3d12')

    def run(label, arguments, timeout=180):
        result = subprocess.run([str(value) for value in arguments], text=True,
                                capture_output=True, timeout=timeout)
        (evidence / (label + '.log')).write_text(result.stdout + '\n' + result.stderr,
                                               encoding='utf-8')
        return result

    with tempfile.TemporaryDirectory(prefix='lightmap-gallery-', dir=evidence) as temporary:
        root = Path(temporary)
        source = copy_gallery_project(
            gallery, root / 'source', {scene_path, image_path, material_path, shader_path},
            startup=scene_path)
        catalog = json.loads((source / 'forge.assets.json').read_text(encoding='utf-8'))
        texture = next(row for row in catalog['assets'] if row['source'] == image_path)
        original = texture['metadata']['forge.import']
        assert original['platform'] == 'linux' and original['backend'] == 'none'
        # The runtime guard must reject the actual foreign artifact, even though
        # its decoded image is usable by source inspection and Material cooking.
        rejected = run('foreign-target', [tools, '--assets', 'package', source,
                       root / 'rejected', json.dumps(target), json.dumps([texture['id']])])
        assert rejected.returncode != 0, rejected.stdout
        failure = json.loads(rejected.stdout)
        assert failure['error']['code'] == 'asset_tool_failed', failure
        assert 'Cooked artifact target differs' in failure['error']['message'], failure

        prepared = run('prepare', [fixture, '--prepare', source, worker], timeout=600)
        assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
        catalog = json.loads((source / 'forge.assets.json').read_text(encoding='utf-8'))
        selected = [row for row in catalog['assets'] if row['source'] == image_path]
        assert len(selected) == 1 and selected[0]['id'] == texture['id']
        recipe = selected[0]['metadata']['forge.import']
        assert {key: recipe[key] for key in target} == target and recipe['profile'] == 'desktop'
        scene = json.loads((source / scene_path).read_text(encoding='utf-8'))
        package = root / 'content'
        exported = run('export', [tools, '--assets', 'package', source, package,
                       json.dumps(target), json.dumps([scene['asset_id']])])
        assert exported.returncode == 0 and json.loads(exported.stdout)['ok'], (
            exported.stdout, exported.stderr)
        source.rename(root / 'source-offline')
        relocated = root / 'relocated-content'
        package.rename(relocated)
        assert not any((relocated / path).exists() for path in
                       (scene_path, image_path, material_path, shader_path))
        verified = run('relocated', [tools, '--assets', 'verify-package', relocated,
                       json.dumps(target)])
        assert verified.returncode == 0 and json.loads(verified.stdout)['ok'], (
            verified.stdout, verified.stderr)
        receipt = dict(scene=scene['asset_id'], texture=texture['id'],
                       original_platform=original['platform'], target=target,
                       identity_preserved=True, foreign_target_rejected=True,
                       prepared=True, source_hidden=True, relocated=True,
                       scope='Native Windows HDR preparation and runtime content export; '
                             'production EXE startup remains a final combined-package gate.')
        (evidence / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n',
                                             encoding='utf-8')
        return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('fixture', 'gallery', 'worker', 'tools', 'evidence'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    verify(args.fixture, args.gallery, args.worker, args.tools, args.evidence)
    print('Lightmap foreign-target rejection, native preparation and source-free relocation passed')
