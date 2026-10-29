"""Exercise the existing gallery exporter and renderer after source-free relocation."""
from pathlib import Path
import json
import subprocess
import sys
import uuid

exporter, renderer, project, scratch = (Path(value).resolve() for value in sys.argv[1:5])
root = scratch / str(uuid.uuid4())
root.mkdir(parents=True)
subprocess.run([exporter, project, root / 'export'], check=True, timeout=60)
moved, = (root / 'export').glob('*-export-relocated')
assert not (moved / 'Assets/OcclusionNormals/RippleNormals.png').exists()
assert not (moved / 'Assets/OcclusionNormals/RippleWall.material.json').exists()
scene = '03d7f430-77ba-4c78-b75a-ff3a638ea3d9'
for content, images in ((project, root / 'authored'), (moved, root / 'relocated')):
    subprocess.run([renderer, content, images, scene, '--rough-gallery'], check=True, timeout=180)
for authored in sorted((root / 'authored').glob('*.ppm')):
    assert authored.read_bytes() == (root / 'relocated' / authored.name).read_bytes(), authored
assert len(list((root / 'authored').glob('*.ppm'))) == 8
(root / 'receipt.json').write_text(json.dumps(dict(
    scene=scene, relocated=str(moved), exact_readbacks=8,
    source_images_present=False,
    limit='Software Vulkan texture/content/render proof; no Windows/D3D12 execution.'), indent=2) + '\n')
print('Rough reflection Gallery source-free relocation: all eight wide/tall filtered/point/on/off images exact PASS')
