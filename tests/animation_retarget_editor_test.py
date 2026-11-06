"""Prepare isolated real publications, then drive native retarget editor controls."""
from pathlib import Path
import subprocess
import sys


fixture, editor, scratch, source, worker, converter = map(Path, sys.argv[1:])
prepared = subprocess.check_output(
    [str(fixture), str(scratch / 'prepared'), str(source), str(worker), str(converter), '--prepare'],
    text=True, encoding='utf-8', timeout=90)
project = Path(next(line for line in prepared.splitlines() if (Path(line) / 'forge.assets.json').is_file()))
subprocess.run([str(editor), str(project), str(scratch / 'controls')], check=True, timeout=90)
