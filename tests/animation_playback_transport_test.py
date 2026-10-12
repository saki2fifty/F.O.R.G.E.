"""Prepare original imported content, then exercise real editor Play pipes."""
from pathlib import Path
import subprocess
import sys
runtime, fixture, converter, source, transport = (Path(sys.argv[i]).resolve() for i in (1, 2, 4, 5, 6))
worker = sys.argv[3] if sys.argv[3] == '--direct' else str(Path(sys.argv[3]).resolve())
prepared = subprocess.run([fixture, runtime.parent/'seek-transport-data', worker, converter, source,
                           '--prepare'], capture_output=True, text=True, encoding='utf-8', timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
root = Path(prepared.stdout.strip().splitlines()[-1])
result = subprocess.run([transport, runtime, root], capture_output=True, text=True, encoding='utf-8', timeout=90)
assert result.returncode == 0, (result.stdout, result.stderr)
print(result.stdout.strip())
