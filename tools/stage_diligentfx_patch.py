"""Stage reviewed exact-pin Diligent repairs; never edit upstream sources."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def stage(engine, destination, manifest):
    engine, destination, manifest = map(lambda p: Path(p).resolve(), (engine, destination, manifest))
    if destination == engine or engine in destination.parents:
        raise RuntimeError('Patch output must be outside the upstream checkout')
    record = json.loads(manifest.read_text())
    name = record.get('module', 'DiligentFX')
    revision = {'DiligentFX': 'fx_commit', 'DiligentCore': 'core_commit'}.get(name)
    if revision is None:
        raise RuntimeError('Unreviewed upstream module')
    source = engine / name
    for checkout, expected in ((engine, record['engine_commit']), (source, record[revision])):
        actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != expected:
            raise RuntimeError(f'Unreviewed upstream revision: {checkout}: {actual}')
    patch = manifest.parent / record['patch']
    patch_bytes = patch.read_bytes().replace(b'\r\n', b'\n')
    if sha(patch_bytes) != record['patch_sha256']:
        raise RuntimeError('Reviewed patch hash mismatch')
    inputs = {}
    support = record.get('support_files', {})
    if set(support) & set(record['files']):
        raise RuntimeError('Patched and unchanged support files overlap')
    for name, expected in (record['files'] | support).items():
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise RuntimeError('Patch input must be module-relative')
        data = (source / name).read_bytes().replace(b'\r\n', b'\n')
        if sha(data) != expected:
            raise RuntimeError(f'Upstream {"shader" if revision == "fx_commit" else "source"} hash mismatch: {name}')
        inputs[name] = data
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='forge-fx-patch-', dir=destination.parent) as scratch:
        scratch = Path(scratch)
        for name, data in inputs.items():
            p = scratch / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        normalized_patch = scratch / 'reviewed.patch'
        normalized_patch.write_bytes(patch_bytes)
        subprocess.run(['git', '-c', 'core.autocrlf=false', 'apply', '--check', str(normalized_patch)], cwd=scratch, check=True)
        subprocess.run(['git', '-c', 'core.autocrlf=false', 'apply', str(normalized_patch)], cwd=scratch, check=True)
        # Put patch identity into the embedded shader bytes (Diligent hashes
        # included source by content), even for a metadata-only patch revision.
        identity = sha(manifest.read_bytes().replace(b'\r\n', b'\n') + patch_bytes)
        kind = 'shader exception' if revision == 'fx_commit' else 'D3D12 descriptor repair'
        notice = f'// FORGE maintained {kind}; patch identity SHA256 {identity}\n'.encode()
        outputs = {}
        for name in inputs:
            data = (scratch / name).read_bytes()
            if name in support:
                if data != inputs[name]:
                    raise RuntimeError(f'Patch modified unchanged support input: {name}')
            elif data == inputs[name]:
                raise RuntimeError(f'Patch did not modify expected input: {name}')
            else:
                data = notice + data
            outputs[name] = data
        for name, data in outputs.items():
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists() or target.read_bytes() != data:
                target.write_bytes(data)
        stamp = dict(record, identity=identity, outputs={name: sha(data) for name, data in outputs.items()})
        data = (json.dumps(stamp, indent=2) + '\n').encode()
        target = destination / 'patch-identity.json'
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
        return stamp


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).resolve().parents[1] /
                        'cmake/patches/diligentfx-aaa41d47-shader-warnings.json')
    args = parser.parse_args()
    print(json.dumps(stage(args.engine, args.destination, args.manifest), indent=2))
