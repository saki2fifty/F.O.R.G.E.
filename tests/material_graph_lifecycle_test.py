"""Run graph project-boundary checks with disposable real projects."""
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='forge-graph-lifecycle-') as root:
    subprocess.run([sys.argv[1], root], check=True)
