"""Run real source-owner publication checks in an isolated Gallery copy."""
import subprocess
import sys
import tempfile

host, worker, gallery = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='forge-source-publication-') as root:
    subprocess.run([host, root, worker, 'publication', gallery], check=True)
