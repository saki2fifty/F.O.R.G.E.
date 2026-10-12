"""Native root mode and durable-joint Inspector controls at actual UI scales."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture, gallery, evidence = (Path(p).resolve() for p in sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='root-motion-capture-') as scratch:
    project = Path(scratch)/'Gallery'
    shutil.copytree(gallery, project)
    result = subprocess.run([fixture, evidence, '--root-motion', project],
                            capture_output=True, text=True, encoding='utf-8', timeout=100)
    (evidence/'capture.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout+result.stderr
    for label in ('animation-root-motion', 'animation-root-motion-150', 'animation-root-motion-200'):
        image = evidence/f'editor-{label}.ppm'
        assert image.is_file() and image.stat().st_size > 1920*1080, image
print('Native root-motion mode and imported root-joint controls captured at100/150/200%')
