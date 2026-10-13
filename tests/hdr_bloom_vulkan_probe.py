"""Compile FORGE's original bloom stages and execute the actual shared pass."""
from pathlib import Path
import subprocess
import sys

probe, compiler, output = sys.argv[1:]
root = Path(output)
root.mkdir(parents=True, exist_ok=True)
subprocess.run([compiler, "--version"], check=True)
subprocess.run([probe, "emit", str(root)], check=True)
for name in ("vertex", "extract", "blur", "composite"):
    stage = "vs_6_0" if name == "vertex" else "ps_6_0"
    for suffix, flags in (("dxil", []), ("spv", ["-spirv", "-fspv-target-env=vulkan1.1"])):
        subprocess.run([compiler, *flags, "-T", stage, "-E", "main",
                        str(root / (name + ".hlsl")), "-Fo", str(root / (name + "." + suffix))],
                       check=True)
subprocess.run([probe, "draw", str(root)], check=True)
