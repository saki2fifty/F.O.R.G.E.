"""Run real source-owner lifecycle checks in an isolated Gallery copy."""
import subprocess
import sys
import tempfile

host, worker, gallery = sys.argv[1:4]
mode = sys.argv[4] if len(sys.argv) > 4 else 'publication'
with tempfile.TemporaryDirectory(prefix='forge-source-publication-') as root:
    subprocess.run([host, root, worker, mode, gallery], check=True)
