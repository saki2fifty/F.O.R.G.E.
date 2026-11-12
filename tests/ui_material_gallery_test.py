"""Cook real Gallery UI Materials and inspect their relocated source-free closure."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture, gallery, worker, scratch, font = (Path(value).resolve() for value in sys.argv[1:])
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='ui-material-', dir=scratch) as temporary:
    source = Path(temporary) / 'source'
    shutil.copytree(gallery, source)
    subprocess.run([str(fixture), '--verify-export', str(source), str(worker), str(font)],
                   check=True, timeout=150)
