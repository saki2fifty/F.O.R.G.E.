"""Drive the actual Windows sheet chooser, pending selection, Undo and Cancel."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture, gallery, evidence = (Path(p).resolve() for p in sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=True)


def authored_files(project):
    paths = [project / 'forge.project.json', project / 'forge.assets.json']
    paths += list((project / 'Scenes').rglob('*.json'))
    paths += [p for p in (project / 'Assets').rglob('*') if p.is_file()]
    return {str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in paths if p.is_file()}


def raw_sources(files):
    return {name: digest for name, digest in files.items()
            if name != 'forge.assets.json' and not name.endswith('.forge-import.json')}


def identities(project):
    rows = json.loads((project / 'forge.assets.json').read_text(encoding='utf-8'))['assets']
    result = {row['id']: (row['type'], row['source'].replace('\\', '/')) for row in rows}
    assert len(result) == len(rows), 'Duplicate asset identities in native fixture'
    return result


for pattern in ('editor-*-sprite-sheet-fit-*.ppm', 'workflow.json', 'sprite-frame-workflow.json',
                'source-baseline.json'):
    for path in evidence.glob(pattern):
        assert path.is_file() and not path.is_symlink(), path
        path.unlink()

baseline_folder = evidence / 'source-baseline'
if baseline_folder.exists():
    assert baseline_folder.is_dir() and not baseline_folder.is_symlink(), baseline_folder
    shutil.rmtree(baseline_folder)

with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='sprite-frame-editor-') as scratch:
    project = Path(scratch) / 'Gallery'
    shutil.copytree(gallery, project)
    original = authored_files(project)
    original_ids = identities(project)
    result = subprocess.run([fixture, evidence, '--sprite-frames', project],
                            capture_output=True, text=True, timeout=190)
    (evidence / 'capture.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, 'Native sheet workflow failed; see capture.log and workflow.json'
    assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
    workflow = json.loads((evidence / 'workflow.json').read_text(encoding='utf-8'))
    assert workflow['ok'] and workflow['completed_steps'] == workflow['total_steps'], workflow
    assert not workflow['state']['sprite_sheet_ready']
    assert not workflow['state']['sprite_chooser_open']
    assert not workflow['state']['playing'] and workflow['state']['scene_redo']
    preparation = json.loads((evidence / 'source-baseline.json').read_text(encoding='utf-8'))
    assert preparation['source_import_ready'] and preparation['preparation_ms'] <= 90000
    baseline = authored_files(evidence / 'source-baseline')
    final = authored_files(project)
    assert raw_sources(original) == raw_sources(final), 'Native fixture changed authored sources'
    final_ids = identities(project)
    assert all(final_ids.get(key) == value for key, value in original_ids.items()), 'Startup import changed asset identities'
    assert baseline == final, ('Chooser changed admitted project content',
                               sorted(name for name in baseline.keys() | final.keys()
                                      if baseline.get(name) != final.get(name)))
    assertions = [step['value'] for step in workflow['trace'] if step.get('operation') == 'assert']
    for name, count in (('sprite-sheet-baseline', 3), ('sprite-sheet-chosen', 6),
                        ('sprite-sheet-restored', 6), ('sprite-sheet-cancelled', 3),
                        ('sprite-sheet-ready', 9)):
        assert assertions.count(name) == count, (name, assertions.count(name))
    captures = [step for step in workflow['trace'] if 'image' in step]
    assert len(captures) == 3, captures
    images = {}
    for scale, capture in zip((100, 150, 200), captures):
        assert capture['image'].endswith(f'-sprite-sheet-fit-{scale}.ppm')
        assert abs(capture['ui_scale'] - scale / 100) < .001
        path = evidence / capture['image']
        with path.open('rb') as stream:
            assert stream.readline() == b'P6\n'
            width, height = map(int, stream.readline().split())
            assert (width, height) == (1920, 1080)
            assert stream.readline() == b'255\n'
            pixels = stream.read()
            assert len(pixels) == width * height * 3
            # The real published four-arrow image must reach the native UI, with
            # its saturated green/blue/orange/pink regions visible at each scale.
            colors = [0, 0, 0, 0]
            for i in range(0, len(pixels), 3):
                r, g, b = pixels[i:i + 3]
                colors[0] += g > 150 and r < 80 and b < 150
                colors[1] += b > 150 and r < 80 and g > 80
                colors[2] += r > 180 and g > 120 and b < 80
                colors[3] += r > 150 and b > 120 and g < 100
            assert min(colors) > 100, (scale, colors)
        images[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    receipt = {'complete': True, 'source_commit': workflow['source_commit'],
               'build': workflow['build'], 'authored_unchanged': True,
               'admitted_state_unchanged': True, 'asset_ids_preserved': True,
               'preparation': preparation,
               'startup_import_updates': sorted(name for name in original.keys() | baseline.keys()
                                                if original.get(name) != baseline.get(name)),
               'captures': captures, 'images': images,
               'scope': 'Actual editor host and published sheet at three scales; pending '
                        'keyboard choice, one Scene Undo/Redo edit, cancellation preserves redo. '
                        'Separate owner tests cover prefab/live/Timeline. Software D3D12 WARP; '
                        'physical desktop and GPU acceptance remain separate.'}
    (evidence / 'sprite-frame-workflow.json').write_text(json.dumps(receipt, indent=2) + '\n')
print('Native Sprite sheet chooser workflow passed at three scales')
