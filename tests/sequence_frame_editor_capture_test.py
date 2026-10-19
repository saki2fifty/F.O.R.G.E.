"""Drive the real native Timeline, Game focus, settings, Cancel and PNG batch."""
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


# Clean only this fixture's named outputs, so a rerun cannot accept old images.
for name in ('Frames100', 'Frames150', 'Frames200', 'FixedFrames100', 'FixedFrames150',
             'FixedFrames200', 'source-baseline'):
    target = evidence / name
    if target.exists():
        assert target.is_dir() and not target.is_symlink(), target
        shutil.rmtree(target)
for pattern in ('editor-sequence-frame-*.ppm', 'sequence-frame-workflow.json',
                'sequence-frames-failed.json', 'source-baseline.json'):
    for target in evidence.glob(pattern):
        assert target.is_file() and not target.is_symlink(), target
        target.unlink()

with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='sequence-frame-editor-') as scratch:
    project = Path(scratch) / 'Gallery'
    shutil.copytree(gallery, project)
    original = authored_files(project)
    result = subprocess.run([fixture, evidence, '--sequence-frames', project],
                            capture_output=True, text=True, timeout=250)
    (evidence / 'capture.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, 'Native frame workflow failed; see capture.log and failure JSON'
    assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
    workflow = json.loads((evidence / 'sequence-frame-workflow.json').read_text())
    assert workflow['authored_unchanged'] and workflow['picker_injected']
    assert not workflow['physical_picker'] and len(workflow['captures']) == 3
    for scale, capture in zip((100, 150, 200), workflow['captures']):
        assert abs(capture['scale'] - scale / 100) < .001, capture
        assert capture['controls_ready'] and capture['modal_in_front'], capture
        assert capture['game_preview_ready'] and capture['stable_frames'] >= 3, capture
        assert capture['game_tab_visible'], capture
        assert capture['fixed_output'] == [320, 180] and capture['ordinary_target_restored'], capture
        assert capture['ordinary_output'] != [320, 180] and all(capture['ordinary_output']), capture
        assert (evidence / f'editor-sequence-frame-ordinary-restored-{scale}.ppm').is_file()
        assert capture['resizes'] == [
            {'window': [1400, 900], 'target': [320, 180]},
            {'window': [1680, 1000], 'target': [320, 180]}], capture
        image = evidence / f'editor-sequence-frame-settings-{scale}.ppm'
        assert image.is_file() and image.stat().st_size > 1920 * 1080, image
        image = evidence / f'editor-sequence-frame-fixed-settings-{scale}.ppm'
        assert image.is_file() and image.stat().st_size > 1920 * 1080, image
        for label, expected in (('a', (1400, 900)), ('b', (1680, 1000))):
            image = evidence / f'editor-sequence-frame-fixed-resize-{label}-{scale}.ppm'
            with image.open('rb') as stream:
                assert stream.readline() == b'P6\n'
                assert tuple(map(int, stream.readline().split())) == expected
                assert stream.readline() == b'255\n'
                assert len(stream.read()) == expected[0] * expected[1] * 3
        folder = evidence / f'Frames{scale}'
        receipt = json.loads((folder / 'frames.json').read_text())
        assert receipt['state'] == 'complete' and receipt['completed_frames'] == 4, receipt
        assert receipt['size_mode'] == 'current_view', receipt
        assert receipt['planned_frames'] == 4 and receipt['fps'] == 1, receipt
        assert receipt['engine_source'] == workflow['source_commit'], receipt
        assert receipt['build_id'] == workflow['build_id'], receipt
        assert receipt['source_digest'] and receipt['catalog_digest'], receipt
        assert len(receipt['frames']) == 4
        for index, frame in enumerate(receipt['frames']):
            assert frame == {'file': f'frame-{index:06}.png', 'time_seconds': index}, frame
            data = (folder / frame['file']).read_bytes()
            assert data[:8] == b'\x89PNG\r\n\x1a\n' and data[12:16] == b'IHDR'
            width, height = struct.unpack('>II', data[16:24])
            assert (width, height) == (receipt['width'], receipt['height'])
            assert 100 < width <= 4096 and 100 < height <= 4096
        assert len(list(folder.glob('frame-*.png'))) == 4
        assert (folder / 'frame-000000.png').read_bytes() != (folder / 'frame-000003.png').read_bytes()
        fixed_folder = evidence / f'FixedFrames{scale}'
        fixed = json.loads((fixed_folder / 'frames.json').read_text())
        assert fixed['state'] == 'complete' and fixed['completed_frames'] == 4, fixed
        assert fixed['planned_frames'] == 4 and fixed['fps'] == 1 and fixed['start_seconds'] == 0
        assert fixed['size_mode'] == 'fixed' and (fixed['width'], fixed['height']) == (320, 180)
        for key in ('engine_source', 'build_id', 'source_digest', 'catalog_digest', 'scene',
                    'sequence', 'scene_revision'):
            assert fixed[key] == receipt[key], (key, fixed, receipt)
        assert fixed['frames'] == receipt['frames']
        assert len(list(fixed_folder.glob('frame-*.png'))) == 4
        for index, frame in enumerate(fixed['frames']):
            data = (fixed_folder / frame['file']).read_bytes()
            assert data[:8] == b'\x89PNG\r\n\x1a\n' and data[12:16] == b'IHDR'
            assert struct.unpack('>II', data[16:24]) == (320, 180)
            assert data == (evidence / 'FixedFrames100' / frame['file']).read_bytes(), index
        assert (fixed_folder / 'frame-000000.png').read_bytes() != (
            fixed_folder / 'frame-000003.png').read_bytes()

    before = authored_files(evidence / 'source-baseline')
    after = authored_files(project)
    startup = sorted(p for p in original.keys() | before.keys() if original.get(p) != before.get(p))
    changed = sorted(p for p in before.keys() | after.keys() if before.get(p) != after.get(p))
    unexpected = [p for p in startup if p != Path('forge.assets.json') and
                  not p.name.endswith('.forge-import.json')]

    def identities(files):
        return {row['id']: (row['type'], row['source'])
                for row in json.loads(files[Path('forge.assets.json')])['assets']}

    preserved_identity = identities(original) == identities(before)
    (evidence / 'source-preservation.json').write_text(json.dumps({
        'original_files': len(original), 'baseline_files': len(before), 'after_files': len(after),
        'startup_updates': [p.as_posix() for p in startup],
        'unexpected_startup_updates': [p.as_posix() for p in unexpected],
        'identity_preserved': preserved_identity, 'capture_changes': [p.as_posix() for p in changed],
        'baseline_hashes': {p.as_posix(): hashlib.sha256(data).hexdigest()
                            for p, data in before.items()}
    }, indent=2) + '\n')
    assert before and not unexpected and preserved_identity and not changed
print('Native Timeline export action, Game/modal focus, Cancel restoration, preset/custom input, '
      'current/fixed PNG frames at 100/150/200%, exact fixed pixels across scales and real window '
      'resizes, and ordinary target restoration passed. Source/catalog preserved. '
      'Only OS folder selection was injected; physical Windows acceptance remains deferred.')
