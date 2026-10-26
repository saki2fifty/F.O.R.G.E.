"""Race the actual SceneDocument creator at its last absence check on Linux."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

owner, fixture, scratch = map(lambda p: Path(p).resolve(), sys.argv[1:])
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as temporary:
    root = Path(temporary)
    ordinary = root / 'Ordinary project'
    command = [str(owner), '--create-project']
    created = subprocess.run([*command, str(ordinary)], capture_output=True, timeout=10)
    assert created.returncode == 0, created.stderr
    original = (ordinary / 'forge.project.json').read_bytes()
    repeated = subprocess.run([*command, str(ordinary)], capture_output=True, timeout=10)
    assert repeated.returncode != 0
    assert (ordinary / 'forge.project.json').read_bytes() == original
    destination = root / 'Concurrent project'
    signal = root / 'race-observed'
    env = dict(os.environ, LD_PRELOAD=str(fixture), FORGE_PROJECT_RACE_TARGET=str(destination),
               FORGE_PROJECT_RACE_SIGNAL=str(signal))
    raced = subprocess.run([*command, str(destination)], env=env, capture_output=True, timeout=10)
    assert signal.read_bytes() == b'injected', (raced.returncode, raced.stderr)
    assert raced.returncode != 0, 'Project creator replaced a destination that appeared after preflight'
    assert destination.is_dir() and not any(destination.iterdir()), 'Competing directory was changed'
    assert not destination.with_name(destination.name + '.forge-creating').exists(), 'Private stage leaked'
print('Actual project creation preserves preexisting and concurrently created destinations')
