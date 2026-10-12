"""Production Inspector and directional sound guides from an editable gallery copy."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture, gallery, evidence = (Path(p).resolve() for p in sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='audio-cone-capture-') as scratch:
    project = Path(scratch)/'Gallery'
    shutil.copytree(gallery, project)
    result = subprocess.run([fixture, evidence, '--audio-cones', project],
                            capture_output=True, text=True, timeout=160)
    (evidence/'capture.log').write_text(result.stdout+result.stderr)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout+result.stderr
    for label in ('audio-cones', 'audio-cones-150', 'audio-cones-200',
                  'audio-cone-guide', 'audio-cone-guide-150', 'audio-cone-guide-200'):
        image = evidence/f'editor-{label}.ppm'
        assert image.is_file() and image.stat().st_size > 1920*1080, image
print('Native directional sound controls and Scene guides captured at100/150/200%')
