"""Observe real packaged model reimports; retain evidence before asserting success."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from assemble_windows_package import safe_name, verified


SOURCE = 'Assets/QuaterniusLocomotion.glb'


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def bindings(catalog, owner):
    record = next(row for row in catalog['assets'] if row['id'] == owner)
    return {edge['role'][13:]: edge['target'] for edge in record['dependency_edges']
            if edge['role'].startswith('model.member:')}


def run(package, source, output):
    # Output must be new: stale selected/candidate evidence cannot satisfy this run.
    output.mkdir(parents=True)
    root = output / 'relocated package'
    with zipfile.ZipFile(package) as archive:
        files = {}
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            name = safe_name(entry.filename)
            if name in files or (entry.external_attr >> 16 & 0o170000) == 0o120000:
                raise ValueError('Duplicate or symbolic-link package entry: ' + name)
            files[name] = archive.read(entry)
    manifest = json.loads(files['manifest.json'])
    verified(files, manifest, 'manifest.json')
    if manifest['source_commit'] != source:
        raise ValueError('Package source does not match the requested immutable revision')
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    del files
    project = output / 'Gallery working copy'
    shutil.copytree(root / 'Examples/FeatureGallery', project)
    catalog_path = project / 'forge.assets.json'
    sidecar_path = project / (SOURCE + '.forge-import.json')
    before = {'catalog': catalog_path.read_bytes(), 'sidecar': sidecar_path.read_bytes()}
    sidecar = json.loads(before['sidecar'])
    owner = sidecar['identity']['owner']
    old_catalog = json.loads(before['catalog'])
    old_bindings = bindings(old_catalog, owner)
    old_record = next(row for row in old_catalog['assets'] if row['id'] == owner)
    old_key = old_record['metadata']['forge.import']['key']
    old_cache = project / '.forge/cache/derived' / old_key
    save(output / 'selected-index.json', read(old_cache / 'model.json'))
    save(output / 'selected-manifest.json', read(old_cache / 'manifest.json'))
    save(output / 'selected-sidecar.json', sidecar)
    save(output / 'selected-bindings.json', old_bindings)
    save(output / 'package-identity.json', {key: manifest[key]
                                          for key in ('source_commit', 'build_id')})
    env = os.environ.copy()
    env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
    command = [str(root / 'bin/forge_tools.exe'), '--assets', 'import-target',
               str(project), SOURCE,
               json.dumps(dict(platform='windows', backend='d3d12', profile='desktop'))]
    for attempt in range(2):
        result = subprocess.run(command, cwd=root, env=env, text=True,
                                capture_output=True, timeout=300)
        (output / f'import-{attempt}.stdout.txt').write_text(result.stdout, encoding='utf-8')
        (output / f'import-{attempt}.stderr.txt').write_text(result.stderr, encoding='utf-8')
        receipt = json.loads(result.stdout)
        report = dict(returncode=result.returncode, receipt=receipt,
                      catalog_preserved=catalog_path.read_bytes() == before['catalog'],
                      sidecar_preserved=sidecar_path.read_bytes() == before['sidecar'])
        key = receipt.get('build_key', '')
        if len(key) == 64 and all(c in '0123456789abcdef' for c in key):
            candidate = project / '.forge/cache/derived' / key / 'model.json'
            if candidate.is_file():
                save(output / f'candidate-manifest-{attempt}.json',
                     read(candidate.parent / 'manifest.json'))
                candidate = read(candidate)
                save(output / f'candidate-{attempt}.json', candidate)
                previous = {row['id']: row for row in sidecar['identity']['entries']}
                report['evidence_comparison'] = [
                    dict(address=row['address'], type=row['type'], name=row['name'],
                         previous_id=old_bindings.get(row['address']),
                         previous=previous.get(old_bindings.get(row['address'])),
                         candidate={k: row[k] for k in
                                    ('exporter_key', 'content_digest', 'semantic_digest')})
                    for row in candidate['members']]
        save(output / f'reimport-{attempt}.json', report)
        if result.returncode or not receipt.get('ok'):
            if not (report['catalog_preserved'] and report['sidecar_preserved']):
                raise AssertionError('Rejected reimport modified selected authored metadata')
            raise AssertionError('Packaged model reimport failed; see retained structured evidence')
        if bindings(read(catalog_path), owner) != old_bindings:
            raise AssertionError('Unchanged model reimport changed existing member identities')
    print('Packaged model reimport passed twice with stable member identities')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('source')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    run(args.package.resolve(), args.source, args.output.resolve())
