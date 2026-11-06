"""Publish real graph inputs, then exercise the production native source panel."""
from pathlib import Path
import subprocess
import sys


fixture, editor, scratch, worker, converter, source = map(Path, sys.argv[1:])
prepared = subprocess.check_output(
    [str(fixture), str(scratch / 'prepared'), str(worker), str(converter), str(source), '--prepare'],
    text=True, encoding='utf-8', timeout=90)
project = Path(next(line for line in prepared.splitlines() if (Path(line) / 'forge.assets.json').is_file()))
subprocess.run([str(editor), str(project), str(scratch / 'controls')], check=True, timeout=90)
