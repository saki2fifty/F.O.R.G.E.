"""Actual texture importer CLI, PATH launch, reimport, relocation and failure retention."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
scratch = Path(sys.argv[2]).resolve()
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as temporary:
    root = Path(temporary)
    project = root / 'Texture project with spaces'
    (project / 'Assets').mkdir(parents=True)
    pixels = bytearray(18)
    pixels[2], pixels[12], pixels[14], pixels[16], pixels[17] = 2, 2, 2, 24, 32
    pixels += bytes([0, 0, 255] * 4)
    source = project / 'Assets' / 'color.tga'
    source.write_bytes(pixels)
    env = os.environ.copy()
    env['PATH'] = str(exe.parent) + os.pathsep + env.get('PATH', '')

    def call(project_path, overrides=None, success=True, locator='Assets/color.tga'):
        args = [exe.name, '--assets', 'import', str(project_path), locator]
        if overrides is not None:
            args.append(json.dumps(overrides))
        result = subprocess.run(args, cwd=root, env=env, capture_output=True, text=True, timeout=150)
        try:
            value = json.loads(result.stdout)
        except ValueError:
            raise AssertionError((result.returncode, result.stdout, result.stderr))
        assert value['ok'] == success and (result.returncode == 0) == success, (value, result.stderr)
        return value

    first = call(project, {'additional_usages': ['data']})
    assert not first['cache_hit']
    repeat = call(project)
    assert repeat['cache_hit'] and repeat['asset'] == first['asset']
    assert repeat['build_key'] == first['build_key']
    sidecar = source.with_name(source.name + '.forge-import.json')
    before = ((project / 'forge.assets.json').read_bytes(), sidecar.read_bytes())
    call(project, {'unknown-setting': 3}, success=False)
    call(project, {'anisotropy': 0}, success=False)
    source.write_bytes(bytes(pixels[:18]))
    call(project, success=False)
    assert before == ((project / 'forge.assets.json').read_bytes(), sidecar.read_bytes())
    source.write_bytes(pixels)
    call(project, success=False, locator='../outside.tga')
    moved = root / 'Relocated project'
    shutil.copytree(project, moved, ignore=shutil.ignore_patterns('.forge'))
    rebuilt = call(moved)
    assert rebuilt['asset'] == first['asset'] and rebuilt['build_key'] == first['build_key']
    assert not rebuilt['cache_hit'], 'Disposable cache was unexpectedly required or retained'
    assert not list((moved / '.forge' / 'jobs').iterdir()), 'Completed worker staging leaked'
print('Texture CLI/PATH launch, publication/reimport, stale retention and relocation passed')
