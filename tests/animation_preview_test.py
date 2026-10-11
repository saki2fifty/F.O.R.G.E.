"""Run real preview interactions against a normally cooked, isolated model project."""
from pathlib import Path
import subprocess
import sys

fixture, editor, scratch, worker, converter, source = sys.argv[1:7]
mode = sys.argv[7:]
root = Path(scratch)
root.mkdir(parents=True, exist_ok=True)
prepared = subprocess.run([fixture, str(root/'prepared'), worker, converter, source, '--prepare'],
                          text=True, capture_output=True, timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
project = prepared.stdout.strip().splitlines()[-1]
subprocess.run([editor, project, str(root/'ui'), *mode], check=True, timeout=120)
