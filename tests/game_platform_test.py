"""Exercise the OS adapter without changing HOME or using project/install saves."""
import os
from pathlib import Path
import subprocess
import sys

exe, work = sys.argv[1:]
root = Path(work).resolve()
root.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
if sys.platform.startswith('linux'):
    env['XDG_DATA_HOME'] = str(root / 'user data')
subprocess.run([exe], env=env, check=True)
if sys.platform.startswith('linux'):
    assert (root / 'user data' / 'FORGE' / 'Games').is_dir()
    for value in ('', 'relative-directory'):
        env['XDG_DATA_HOME'] = value
        subprocess.run([exe, '--reject'], env=env, check=True)
print('OS path integration passed')
