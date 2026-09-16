"""Compile the actual embedded preview shaders with the active Windows SDK."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/editor/viewport.cpp').read_text()
vertex = re.search(r'const char\* vs = R"\((.*?)\)";', source, re.S)
pixel = re.search(r'const char\* ps =\s*("(?:[^"\\]|\\.)*");', source, re.S)
if not vertex or not pixel:
    raise RuntimeError('Cannot locate preview shader sources')
compiler = Path(os.environ['WindowsSdkDir']) / 'bin' / os.environ['WindowsSDKVersion'].rstrip('\\/') / 'x64/fxc.exe'
work = root.parent / 'AgentFiles' / 'shader-tests'
work.mkdir(parents=True, exist_ok=True)
grid = (root / 'src/editor/grid.hpp').read_text()
grid_vertex = re.search(r'grid_vertex_shader\[\] = R"\((.*?)\)";', grid, re.S)
grid_pixel = re.search(r'grid_pixel_shader\[\] = R"\((.*?)\)";', grid, re.S)
if not grid_vertex or not grid_pixel:
    raise RuntimeError('Cannot locate grid shader sources')
with tempfile.TemporaryDirectory(dir=work) as temporary:
    for name, text, profile in [('vertex', vertex.group(1), 'vs_5_1'), ('pixel', json.loads(pixel.group(1)), 'ps_5_1'), ('grid_vertex', grid_vertex.group(1), 'vs_5_1'), ('grid_pixel', grid_pixel.group(1), 'ps_5_1')]:
        path = Path(temporary) / (name + '.hlsl')
        path.write_text(text)
        subprocess.run([str(compiler), '/nologo', '/WX', '/T', profile, '/E', 'main',
                        '/Fo', str(path.with_suffix('.cso')), str(path)], check=True)
print('Preview and infinite-grid vertex/pixel shaders compiled successfully')
