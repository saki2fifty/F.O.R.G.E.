"""Publish the editable alpha example and draw its relocated source-free Game."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('fixture', 'gallery', 'renderer', 'scratch'):
    parser.add_argument(name, type=Path)
parser.add_argument('--vulkan', action='store_true')
parser.add_argument('--opacity', action='store_true')
parser.add_argument('--billboards', action='store_true')
parser.add_argument('--gpu-billboards', action='store_true')
parser.add_argument('--gpu-graph-billboards', action='store_true')
parser.add_argument('--worker', type=Path)
parser.add_argument('--graph-fixture', type=Path)
arguments = parser.parse_args()
fixture, gallery, renderer, scratch = (getattr(arguments, name).resolve()
    for name in ('fixture', 'gallery', 'renderer', 'scratch'))
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='particle-alpha-', dir=scratch) as temporary:
    billboards = arguments.billboards or arguments.gpu_billboards or arguments.gpu_graph_billboards
    action = ('--verify-export-gpu-graph-billboards' if arguments.gpu_graph_billboards else
              '--verify-export-gpu-billboards' if arguments.gpu_billboards else
              '--verify-export-billboards' if arguments.billboards else
              '--verify-export-opacity' if arguments.opacity else '--verify-export')
    worker = [str(arguments.worker.resolve())] if billboards and arguments.worker else []
    assert not billboards or worker, 'Camera-plane texture requires its asset worker'
    if arguments.gpu_graph_billboards:
        assert not arguments.vulkan and arguments.graph_fixture, 'Program cook needs native Windows'
        source = Path(temporary) / 'graph-source'
        shutil.copytree(gallery, source)
        subprocess.run([str(arguments.graph_fixture.resolve()), '--prepare', str(source), *worker],
                       check=True, timeout=900)
        gallery = source
    subprocess.run([str(fixture), action, str(gallery), temporary, *worker],
                   check=True, timeout=120)
    content = list(Path(temporary).glob('*/relocated-content'))
    assert len(content) == 1, content
    mode = (('--opacity-gallery' if arguments.opacity else '--alpha-gallery') if arguments.vulkan
            else ('particle-opacity-gallery' if arguments.opacity else 'particle-alpha-gallery'))
    if arguments.gpu_graph_billboards:
        mode = 'particle-gpu-graph-billboard-gallery'
    elif arguments.gpu_billboards:
        mode = '--gpu-billboard-gallery' if arguments.vulkan else 'particle-gpu-billboard-gallery'
    elif arguments.billboards:
        mode = '--billboard-gallery' if arguments.vulkan else 'particle-billboard-gallery'
    subprocess.run([str(renderer), str(scratch / 'native'), mode, str(content[0])],
                   check=True, timeout=150)
