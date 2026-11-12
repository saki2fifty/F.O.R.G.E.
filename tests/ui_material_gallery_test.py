"""Cook real Gallery UI Materials and inspect their relocated source-free closure."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('fixture', 'gallery', 'worker', 'scratch', 'font'):
    parser.add_argument(name, type=Path)
parser.add_argument('--graph', action='store_true')
arguments = parser.parse_args()
fixture, gallery, worker, scratch, font = (getattr(arguments, name).resolve()
                                          for name in ('fixture', 'gallery', 'worker', 'scratch', 'font'))
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ui-material-', dir=scratch) as temporary:
    source = Path(temporary) / 'source'
    shutil.copytree(gallery, source)
    action = '--verify-graph-export' if arguments.graph else '--verify-export'
    subprocess.run([str(fixture), action, str(source), str(worker), str(font)],
                   check=True, timeout=150)
