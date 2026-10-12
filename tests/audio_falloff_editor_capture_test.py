"""Actual native editor images of the shipped spatial-audio Inspector."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture, gallery, evidence = (Path(p).resolve() for p in sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='audio-falloff-capture-') as scratch:
    project = Path(scratch)/'Gallery'
    shutil.copytree(gallery, project)
    result = subprocess.run([fixture, evidence, '--audio-falloff', project],
                            capture_output=True, text=True, timeout=100)
    (evidence/'capture.log').write_text(result.stdout+result.stderr)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout+result.stderr
    for label in ('audio-falloff', 'audio-falloff-150', 'audio-falloff-200'):
        image = evidence/f'editor-{label}.ppm'
        assert image.is_file() and image.stat().st_size > 1920*1080, image
print('Native spatial-audio Inspector captured at100/150/200%')
