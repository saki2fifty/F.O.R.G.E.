"""Run real import, native physics, relocation, and optional Windows UI capture."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
from physics_acceptance_fixture import make_project

parser = argparse.ArgumentParser()
parser.add_argument('--tools', required=True, type=Path)
parser.add_argument('--runner', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--editor', type=Path)
parser.add_argument('--sdk', type=Path)
args = parser.parse_args()
project = make_project(args.tools.resolve(), args.output.resolve())
evidence = args.output.resolve() / ('evidence-' + project.name)
command = [str(args.runner.resolve()), str(project), str(evidence)]
if args.sdk:
    command.append(str(args.sdk.resolve()))
subprocess.run(command, check=True, timeout=120)
if args.editor:
    subprocess.run([str(args.editor.resolve()), str(evidence / 'editor'), '--physics', str(project)],
                   check=True, timeout=240)
print(json.dumps(dict(project=str(project), evidence=str(evidence),
                      windows_images=bool(args.editor))))
