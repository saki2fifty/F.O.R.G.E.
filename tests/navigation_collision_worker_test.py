"""Native worker admits bounded convex masks and rejects malformed staging geometry."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

worker, gallery, scratch = map(lambda p: Path(p).resolve(), sys.argv[1:4])
scratch.mkdir(parents=True, exist_ok=True)
scene = json.loads((gallery / 'Scenes/136-navigation-polygons.scene.json').read_text())
metadata = next(row['metadata'] for row in json.loads((gallery / 'forge.assets.json').read_text())['assets']
                if row['type'] == 'navmesh' and row['metadata']['source_scene'] == scene['asset_id'])
vertices = [-10, 0, -10, 10, 0, 10, 10, 0, -10, -10, 0, -10, -10, 0, 10, 10, 0, 10]
def ring(count, step=1):
    return [coordinate for i in range(count)
            for coordinate in (2 * math.cos(2 * math.pi * i * step / count), 0,
                               2 * math.sin(2 * math.pi * i * step / count))]
with tempfile.TemporaryDirectory(prefix='convex-mask-', dir=scratch) as temporary:
    root = Path(temporary)
    def run(mask, valid):
        for name in ('navigation.fnav', 'error.txt'):
            (root / name).unlink(missing_ok=True)
        # This fixture tests native input admission, not the builder's source digest.
        (root / 'source.json').write_text(json.dumps(dict(metadata=copy.deepcopy(metadata),
            vertices=vertices, obstacles=[mask])), encoding='utf-8')
        result = subprocess.run([worker, '--build-navigation'], cwd=root, capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == valid, (len(mask), result.returncode, result.stderr)
        assert (root / 'navigation.fnav').exists() == valid
        if valid:
            assert (root / 'navigation.fnav').read_bytes().startswith(b'FORGENAV')
        else:
            assert (root / 'error.txt').read_text().strip()
    for count in (3, 4, 8, 62, 64):
        run(ring(count), True)
    for bad in ([], ring(65), ring(5, 2), [v for i in reversed(range(8)) for v in ring(8)[i*3:i*3+3]], [0, 0, 0] * 3,
                [0, 0, 0, 1, 0, 0, 2, 0, 0], [0, 1, 0, 1, 0, 0, 0, 0, 1],
                [0, 0, 0, 1e100, 0, 0, 0, 0, 1]):
        run(bad, False)
print('Native worker: 3..64 convex vertices; limits/star/repetition/collapse/plane/overflow rejected')
