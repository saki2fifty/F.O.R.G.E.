"""Run graph project-boundary checks with disposable real projects."""
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable')
parser.add_argument('--worker')
arguments = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='forge-graph-lifecycle-') as root:
    command = [arguments.executable, root]
    if arguments.worker:
        command.append(arguments.worker)
    subprocess.run(command, check=True)
