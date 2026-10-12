"""Capture actual source routing and project volume controls from a gallery copy."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture, gallery, evidence = (Path(p).resolve() for p in sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='audio-mixer-capture-') as scratch:
    project = Path(scratch)/'Gallery'
    shutil.copytree(gallery, project)
    result = subprocess.run([fixture, evidence, '--audio-mixer', project],
                            capture_output=True, text=True, timeout=160)
    (evidence/'capture.log').write_text(result.stdout+result.stderr)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout+result.stderr
    for label in ('audio-mix-channel', 'audio-mix-channel-150', 'audio-mix-channel-200',
                  'audio-mix-volumes', 'audio-mix-volumes-150', 'audio-mix-volumes-200',
                  'live-audio-mix', 'live-audio-mix-150', 'live-audio-mix-200',
                  'audio-doppler', 'audio-doppler-150', 'audio-doppler-200',
                  'live-audio-source', 'live-audio-source-150', 'live-audio-source-200'):
        image = evidence/f'editor-{label}.ppm'
        assert image.is_file() and image.stat().st_size > 1920*1080, image
print('Native channel routing and project mixer controls captured at100/150/200%')
