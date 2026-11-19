"""Cook the editable particle example and execute its relocated Windows content."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('fixture', 'gallery', 'worker', 'renderer', 'scratch'):
    parser.add_argument(name, type=Path)
arguments = parser.parse_args()
fixture, gallery, worker, renderer, scratch = (getattr(arguments, name).resolve()
    for name in ('fixture', 'gallery', 'worker', 'renderer', 'scratch'))
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='particle-graph-', dir=scratch) as temporary:
    source = Path(temporary) / 'source'
    shutil.copytree(gallery, source)
    subprocess.run([str(fixture), '--verify-export', str(source), str(worker)],
                   check=True, timeout=900)
    content = Path(temporary) / 'relocated-content'
    subprocess.run([str(renderer), str(scratch / 'native'), 'particle-graphs', str(content)],
                   check=True, timeout=90)
