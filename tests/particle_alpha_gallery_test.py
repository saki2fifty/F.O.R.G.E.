"""Publish the editable alpha example and draw its relocated source-free Game."""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('fixture', 'gallery', 'renderer', 'scratch'):
    parser.add_argument(name, type=Path)
parser.add_argument('--vulkan', action='store_true')
arguments = parser.parse_args()
fixture, gallery, renderer, scratch = (getattr(arguments, name).resolve()
    for name in ('fixture', 'gallery', 'renderer', 'scratch'))
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='particle-alpha-', dir=scratch) as temporary:
    subprocess.run([str(fixture), '--verify-export', str(gallery), temporary],
                   check=True, timeout=120)
    content = list(Path(temporary).glob('*/relocated-content'))
    assert len(content) == 1, content
    mode = '--alpha-gallery' if arguments.vulkan else 'particle-alpha-gallery'
    subprocess.run([str(renderer), str(scratch / 'native'), mode, str(content[0])],
                   check=True, timeout=150)
