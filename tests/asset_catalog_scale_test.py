"""Characterize real catalog load/reverse traversal at 1k and 10k logical assets."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid

executable = Path(sys.argv[1]).resolve()
scratch = Path(sys.argv[2]).resolve()
roundtrip = Path(sys.argv[3]).resolve()
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as temporary:
    root = Path(temporary)
    for count in (1000, 10000):
        records = []
        for i in range(count):
            record = dict(id=str(uuid.UUID(int=i+1, version=4)), type='mesh',
                          source=f'Assets/mesh-{i}.gltf', schema_version=1,
                          dependencies=[], metadata={}, dependency_edges=[])
            if records:
                record['dependencies'] = [records[-1]['id']]
                record['dependency_edges'] = [dict(target=records[-1]['id'], type='mesh',
                                                   kind='build', role='fixture', revision='')]
            records.append(record)
        source = json.dumps(dict(version=2, assets=records), separators=(',', ':'))
        assert len(source) < 4 * 1024 * 1024
        index = root / 'forge.assets.json'
        index.write_text(source, encoding='utf-8')
        started = time.monotonic()
        result = subprocess.run([str(executable), '--assets', 'query', str(root)],
                                capture_output=True, text=True, timeout=60)
        elapsed = time.monotonic() - started
        assert result.returncode == 0, result.stderr or result.stdout
        document = json.loads(result.stdout)
        assert document['ok'] and len(document['assets']) == count
        assert len(document['dependencies']['records']) == count
        started_reverse = time.monotonic()
        result = subprocess.run([str(executable), '--assets', 'source-dependents', str(root),
                                 'Assets/mesh-0.gltf'], capture_output=True, text=True, timeout=60)
        reverse_elapsed = time.monotonic() - started_reverse
        assert result.returncode == 0, result.stderr or result.stdout
        document = json.loads(result.stdout)
        assert document['direct'] == [records[0]['id']]
        assert len(document['affected']) == count
        assert index.read_text(encoding='utf-8') == source, 'Read operation rewrote the index'
        result = subprocess.run([str(roundtrip), '--roundtrip', str(index)],
                                capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, result.stderr or result.stdout
        saved = index.with_name(index.name + '.roundtrip')
        assert len(json.loads(saved.read_text(encoding='utf-8'))['assets']) == count
        saved.unlink()
        print(result.stdout.strip(), flush=True)
        print(json.dumps(dict(assets=count, source_bytes=len(source),
                              query_seconds=round(elapsed, 4),
                              source_dependents_seconds=round(reverse_elapsed, 4))), flush=True)
print('Catalog 1k/10k load, deep dependency traversal and no-write checks passed')
