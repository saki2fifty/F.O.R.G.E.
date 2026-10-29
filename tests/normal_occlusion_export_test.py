"""Compare the normal-aware Gallery before and after source-free relocation."""
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
for source in ('RippleNormals.png', 'RippleWall.material.json'):
    assert not (moved / 'Assets/OcclusionNormals' / source).exists()
for content, images in ((project, root / 'authored'), (moved, root / 'relocated')):
    subprocess.run([renderer, images, content, '--normal-gallery'], check=True, timeout=90)
readbacks = sorted((root / 'authored').glob('*.ppm'))
assert len(readbacks) == 4
for authored in readbacks:
    assert authored.read_bytes() == (root / 'relocated' / authored.name).read_bytes(), authored
(root / 'receipt.json').write_text(json.dumps(dict(
    scene='277f080c-b97b-4202-bd31-5cded7e4dd18', relocated=str(moved), exact_readbacks=4,
    editable_normal_and_material_sources_present=False,
    limit='Software Vulkan content/render proof; no Windows/D3D12 execution.'), indent=2) + '\n')
print('Normal AO Gallery source-free relocation: four wide/tall normal/geometric images exact PASS')
