"""Compile the original sampling oracle and compare native BC6 half words."""
from pathlib import Path
import subprocess
import sys

probe, compiler, destination = sys.argv[1:]
root = Path(destination)
root.mkdir(parents=True, exist_ok=True)
subprocess.run([compiler, '--version'], check=True)
subprocess.run([probe, 'emit', str(root)], check=True)
for extension, flags in [('dxil', []), ('spv', ['-spirv', '-fspv-target-env=vulkan1.1'])]:
    subprocess.run([compiler, *flags, '-T', 'cs_6_0', '-E', 'main',
                    str(root/'decode.hlsl'), '-Fo', str(root/('decode.'+extension))], check=True)
subprocess.run([probe, 'draw', str(root)], check=True)
