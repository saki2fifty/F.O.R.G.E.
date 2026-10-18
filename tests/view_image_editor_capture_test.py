"""Real native Save image commands; only the OS file-picker result is injected."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

fixture, gallery, evidence = (Path(p).resolve() for p in sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=True)


def authored_files(project):
    paths = [project / 'forge.project.json', project / 'forge.assets.json']
    paths += list((project / 'Scenes').rglob('*.json'))
    paths += [p for p in (project / 'Assets').rglob('*') if p.is_file()]
    return {p.relative_to(project): p.read_bytes() for p in paths if p.is_file()}


for scale in (100, 150, 200):
    for name in (f'scene-{scale}.png', f'game-ui-{scale}.png'):
        path = evidence / name
        if path.exists():
            assert path.is_file() and not path.is_symlink(), path
            path.unlink()

with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='view-image-capture-') as scratch:
    project = Path(scratch) / 'Gallery'
    shutil.copytree(gallery, project)
    before = authored_files(project)
    result = subprocess.run([fixture, evidence, '--view-images', project],
                            capture_output=True, text=True, timeout=180)
    (evidence / 'capture.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
    for group, scale in enumerate((100, 150, 200)):
        suffix = '' if scale == 100 else f'-{scale}'
        for mode, name in enumerate(('scene-menu', 'scene-saved', 'game-ui')):
            stage = 296 + group * 3 + mode
            screen = evidence / f'editor-view-image-{name}{suffix}.ppm'
            assert screen.is_file() and screen.stat().st_size > 1920 * 1080, screen
            chrome = json.loads((evidence / f'chrome-{stage}.json').read_text())
            assert chrome['controls_ready'] and chrome['document_in_front'], chrome
            assert chrome['stable_control_frames'] >= 3, chrome
            assert chrome['file_picker_injected'] and not chrome['native_file_picker_executed']
            assert chrome['file_picker_deliveries'] == group * 2 + mode, chrome
            assert abs(chrome['scale'] - scale / 100) < .001, chrome
            if mode:
                image = evidence / (f'scene-{scale}.png' if mode == 1
                                    else f'game-ui-{scale}.png')
                data = image.read_bytes()
                assert data[:8] == b'\x89PNG\r\n\x1a\n' and data[12:16] == b'IHDR', image
                width, height = struct.unpack('>II', data[16:24])
                report = chrome['saved_view']
                assert report['matches_view'] and report['width'] == width
                assert report['height'] == height and report['encoded_bytes'] == len(data)
                assert 100 < width <= 4096 and 100 < height <= 4096
                if mode == 2:
                    assert report['png_sources'] and report['jpeg_sources'], report
                    assert report['ui_changed_pixels'] >= 100, report
    after = authored_files(project)
    changed = sorted(p for p in before.keys() | after.keys() if before.get(p) != after.get(p))
    changes = []
    for path in changed:
        row = {'path': path.as_posix()}
        for label, snapshot in (('before', before), ('after', after)):
            data = snapshot.get(path)
            row[label] = None if data is None else hashlib.sha256(data).hexdigest()
            if data is not None:
                retained = evidence / 'source-changes' / label / path
                retained.parent.mkdir(parents=True, exist_ok=True)
                retained.write_bytes(data)
        changes.append(row)
    (evidence / 'source-preservation.json').write_text(json.dumps({
        'before_files': len(before), 'after_files': len(after), 'changes': changes
    }, indent=2) + '\n')
    assert not changed, 'Image capture changed Gallery source/catalog: ' + ', '.join(
        p.as_posix() for p in changed)
print('Native Scene/Game Save image controls and exact PNG pixels checked at 100/150/200%; '
      'Game includes actual PNG/JPEG runtime UI. OS picker selection injected; '
      'physical Windows picker acceptance remains deferred.')
