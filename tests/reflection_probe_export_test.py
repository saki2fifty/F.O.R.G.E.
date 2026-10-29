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
assert not (moved / 'Assets/ReflectionProbes/WarmRoom.png').exists()
assert not (moved / 'Assets/ReflectionProbes/CoolRoom.png').exists()
scene = '1f955498-5eaa-46a7-9958-9678072dc24b'
for content, images in ((project, root / 'authored'), (moved, root / 'relocated')):
    subprocess.run([renderer, content, images, scene, '--probe-gallery'], check=True, timeout=90)
for authored in sorted((root / 'authored').glob('*.ppm')):
    assert authored.read_bytes() == (root / 'relocated' / authored.name).read_bytes(), authored
assert len(list((root / 'authored').glob('*.ppm'))) == 4
(root / 'receipt.json').write_text(json.dumps(dict(
    scene=scene, relocated=str(moved), exact_readbacks=4,
    source_images_present=False,
    limit='Software Vulkan texture/content/render proof; no Windows/D3D12 execution.'), indent=2) + '\n')
print('Probe Gallery source-free relocation: all four wide/tall on/off images exact PASS')
