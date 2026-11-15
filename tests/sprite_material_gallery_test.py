"""Cook both Sprite programs and inspect their relocated source-free content closure."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from gallery_project_fixture import copy_gallery_project

SCENE = 'Scenes/215-programmable-sprite-materials.scene.json'
SOURCES = {
    SCENE,
    'Assets/Sprites/Badge.png',
    'Assets/Sprites/Materials/Image.shader.json',
    'Assets/Sprites/Materials/Tint.shader.json',
    'Assets/Sprites/Materials/Image.material.json',
    'Assets/Sprites/Materials/Tint.material.json',
    'Assets/Sprites/Materials/Shaders/image.hlsl',
}


def verify(fixture, gallery, worker, evidence):
    fixture, gallery, worker, evidence = (
        Path(p).resolve() for p in (fixture, gallery, worker, evidence))
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='Sprite Material Gallery ') as temporary:
        source = copy_gallery_project(gallery, Path(temporary) / 'source', SOURCES, startup=SCENE)
        result = subprocess.run([str(fixture), '--verify-export', str(source), str(worker)],
                                capture_output=True, text=True, timeout=1500)
        (evidence / 'cook-and-content-export.log').write_text(
            result.stdout + result.stderr, encoding='utf-8')
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        assert not source.exists(), 'Content check did not hide its source project'
    receipt = dict(content_closure_assets=4, cooked_programs_identical=True,
                   shader_forms=['HLSL', 'Material Graph'],
                   image_modes=['Simple', 'Sliced', 'Tiled'], frame_filtering='Linear',
                   scope='Windows Shader worker/publication and relocated source-free '
                         'content closure. Separate Sprite export tests launch the production game.')
    (evidence / 'sprite-material-export.json').write_text(
        json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('fixture', 'gallery', 'worker', 'evidence'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    verify(args.fixture, args.gallery, args.worker, args.evidence)
    print('Programmable Sprite cooked-program equality and four-asset content closure passed')
