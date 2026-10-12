"""Capture the actual AudioClip tab; no audio device or autoplay is requested."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture, gallery, evidence = (Path(p).resolve() for p in sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='audio-preview-capture-') as scratch:
    project = Path(scratch)/'Gallery'
    shutil.copytree(gallery, project)
    result = subprocess.run([fixture, evidence, '--audio-preview', project],
                            capture_output=True, text=True, timeout=120)
    (evidence/'capture.log').write_text(result.stdout+result.stderr)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout+result.stderr
    for label in ('audio-clip-preview', 'audio-clip-preview-150', 'audio-clip-preview-200'):
        image = evidence/f'editor-{label}.ppm'
        assert image.is_file() and image.stat().st_size > 1920*1080, image
print('Native AudioClip preview controls captured at100/150/200%; no physical playback claim')
