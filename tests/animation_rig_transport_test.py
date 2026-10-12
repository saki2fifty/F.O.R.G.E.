"""Cook a normal isolated model before testing both real editor transports."""
from pathlib import Path
import subprocess
import sys
runtime, fixture, transport, worker, converter, source, scratch = sys.argv[1:8]
root = Path(scratch)
root.mkdir(parents=True, exist_ok=True)
prepared = subprocess.run([fixture, str(root/'prepared'), worker, converter, source, '--prepare'],
    capture_output=True, text=True, timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
project = prepared.stdout.strip().splitlines()[-1]
subprocess.run([transport, runtime, project], check=True, timeout=120)
