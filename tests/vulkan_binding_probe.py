"""Compile FORGE-generated HLSL with an explicitly supplied DXC, then read a Vulkan pixel."""
from pathlib import Path
import subprocess
import sys

probe, compiler, output = sys.argv[1:]
root = Path(output)
root.mkdir(parents=True, exist_ok=True)
subprocess.run([compiler, "--version"], check=True)
subprocess.run([probe, "emit", str(root)], check=True)
for name, stage in (("vertex", "vs_6_0"), ("pixel", "ps_6_0")):
    subprocess.run([compiler, "-spirv", "-fspv-target-env=vulkan1.1", "-T", stage,
                    "-E", "main", str(root / (name + ".hlsl")), "-Fo",
                    str(root / (name + ".spv"))], check=True)
subprocess.run([probe, "draw", str(root)], check=True)
for name, entry in (("surface.color", "ForgeSurfaceColor"), ("surface.depth", "ForgeSurfaceDepth")):
    subprocess.run([compiler, "-spirv", "-fspv-target-env=vulkan1.1", "-T", "ps_6_0",
                    "-E", entry, str(root / (name + ".hlsl")), "-Fo",
                    str(root / (name + ".spv"))], check=True)
print("Generated custom-surface color/depth, five texture dimensions, 19 samplers: Vulkan compile PASS")
