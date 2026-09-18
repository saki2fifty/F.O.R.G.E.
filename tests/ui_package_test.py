"""Verify runtime UI font/notices and render from a relocated Windows package."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

archive, fixture = map(lambda value: Path(value).resolve(), sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='ui-relocation-', dir=archive.parent) as temporary:
    root = Path(temporary) / 'Relocated FORGE'
    root.mkdir()
    with zipfile.ZipFile(archive) as package:
        package.extractall(root)
    manifest = json.loads((root / 'manifest.json').read_text())
    font = root / 'resources/ui/LatoLatin-Regular.ttf'
    assert hashlib.sha256(font.read_bytes()).hexdigest() == manifest['files']['resources/ui/LatoLatin-Regular.ttf']
    assert (root / 'resources/ui/LICENSE-Lato.txt').is_file()
    assert (root / 'licenses/rmlui-src/LICENSE.txt').is_file()
    assert (root / 'licenses/freetype-src/docs/FTL.TXT').is_file()
    assert (root / 'manual/editor/runtime-ui.html').is_file()
    relocated = root / fixture.name
    shutil.copy2(fixture, relocated)
    env = os.environ.copy()
    env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
    subprocess.run([relocated, root / 'ui-checks', font], cwd=root, env=env, check=True, timeout=90)
print('Packaged runtime UI font, notices, rendering and relocation passed')
