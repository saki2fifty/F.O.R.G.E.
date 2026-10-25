"""Run actual navigation publication and overlay checks in disposable projects."""
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='forge-navigation-lifecycle-') as root:
    subprocess.run([sys.argv[1], root, sys.argv[2]], check=True)
