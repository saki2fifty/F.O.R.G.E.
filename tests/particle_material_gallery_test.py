"""Publish and draw the editable Material-palette Gallery without asset sources."""
import argparse
from pathlib import Path
import subprocess
import shutil
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('fixture', 'gallery', 'renderer', 'scratch'):
    parser.add_argument(name, type=Path)
parser.add_argument('--vulkan', action='store_true')
parser.add_argument('--graph-fixture', type=Path)
parser.add_argument('--worker', type=Path)
arguments = parser.parse_args()
fixture, gallery, renderer, scratch = (
    getattr(arguments, name).resolve() for name in ('fixture', 'gallery', 'renderer', 'scratch'))
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='particle-materials-', dir=scratch) as temporary:
    if arguments.graph_fixture:
        assert not arguments.vulkan and arguments.worker, 'Graph cook needs native Windows workers'
        copied = Path(temporary) / 'graph-source'
        shutil.copytree(gallery, copied)
        subprocess.run([str(arguments.graph_fixture.resolve()), '--prepare', str(copied),
                        str(arguments.worker.resolve())], check=True, timeout=900)
        gallery = copied
    action = '--verify-graph-export' if arguments.graph_fixture else '--verify-export'
    subprocess.run([str(fixture), action, str(gallery), temporary],
                   check=True, timeout=120)
    content = list(Path(temporary).glob('*/relocated-content'))
    assert len(content) == 1, content
    mode = ('particle-material-graph-gallery' if arguments.graph_fixture else
            '--gpu-material-gallery' if arguments.vulkan else 'particle-material-gallery')
    subprocess.run([str(renderer), str(scratch / 'images'), mode, str(content[0])],
                   check=True, timeout=150)
