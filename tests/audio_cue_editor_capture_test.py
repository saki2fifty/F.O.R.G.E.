"""Actual native sequence Timeline with a cue selected through widget input."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture, gallery, evidence = (Path(p).resolve() for p in sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='audio-cue-capture-') as scratch:
    project = Path(scratch) / 'Gallery'
    shutil.copytree(gallery, project)
    scene = project / 'Scenes/70-sequence-audio-cues.scene.json'
    before = hashlib.sha256(scene.read_bytes()).hexdigest()
    result = subprocess.run([fixture, evidence, '--audio-cues', project],
                            capture_output=True, text=True, timeout=180)
    (evidence / 'capture.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
    for stage, label in enumerate(('sequence-audio-cues', 'sequence-audio-cues-150',
                                   'sequence-audio-cues-200'), 293):
        image = evidence / f'editor-{label}.ppm'
        assert image.is_file() and image.stat().st_size > 1920 * 1080, image
        chrome = json.loads((evidence / f'chrome-{stage}.json').read_text())
        assert chrome['controls_ready'] and chrome['document_in_front'], chrome
        assert chrome['stable_control_frames'] >= 3, chrome
        assert chrome['source_preparation_ready'] and chrome['source_preparation_ms'] < 90000
        assert chrome['control_stage_budget_ms'] == 45000, chrome
    assert hashlib.sha256(scene.read_bytes()).hexdigest() == before
print('Native sound-cue Timeline controls captured at 100/150/200%; physical listening deferred')
