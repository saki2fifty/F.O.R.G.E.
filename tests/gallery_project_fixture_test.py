"""Private capture projects preserve authored identities, bytes and source edges."""
import hashlib
import json
from pathlib import Path
import tempfile

from gallery_project_fixture import CAPTURE_PROJECTS, copy_capture_project

gallery = Path(__file__).resolve().parents[1] / 'samples/projects/FeatureGallery'
index = gallery / 'forge.assets.json'
original = index.read_bytes()
records = {row['id']: row for row in json.loads(original)['assets']}
with tempfile.TemporaryDirectory(prefix='forge-capture-projects-') as temporary:
    for workflow, (sources, startup) in CAPTURE_PROJECTS.items():
        project = copy_capture_project(gallery, Path(temporary) / workflow, workflow)
        staged = json.loads((project / 'forge.assets.json').read_text())['assets']
        assert 0 < len(staged) < len(records), (workflow, len(staged))
        selected = {row['id'] for row in staged}
        for source in sources:
            assert (project / source).read_bytes() == (gallery / source).read_bytes()
        for row in staged:
            assert row == records[row['id']], (workflow, row['source'])
            assert set(row['dependencies']) & records.keys() <= selected
            if row.get('subasset'):
                assert row['subasset']['owner'] in selected
            paths = {row['source']} | {edge['source'] for edge in row.get('source_dependencies', [])}
            sidecar = row['source'] + '.forge-import.json'
            if (gallery / sidecar).exists():
                paths.add(sidecar)
            imported = row.get('metadata', {}).get('forge.import')
            if imported:
                cache = Path('.forge/cache/derived') / imported['key']
                paths.update(path.relative_to(gallery) for path in (gallery / cache).rglob('*')
                             if path.is_file())
            for path in paths:
                assert hashlib.sha256((project / path).read_bytes()).digest() == hashlib.sha256(
                    (gallery / path).read_bytes()).digest(), (workflow, path)
        settings = json.loads((project / 'forge.project.json').read_text())
        if startup:
            assert settings['startup_scene']['source'] == startup
            assert settings['startup_scene']['asset'] == json.loads(
                (project / startup).read_text())['asset_id']
        print(workflow, len(staged), 'original asset records preserved')
assert index.read_bytes() == original
