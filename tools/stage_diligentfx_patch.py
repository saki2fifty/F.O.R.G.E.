"""Stage the approved exact-pin shader exception; never edit upstream sources."""
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
    fx = engine / 'DiligentFX'
    for checkout, expected in ((engine, record['engine_commit']), (fx, record['fx_commit'])):
        actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != expected:
            raise RuntimeError(f'Unreviewed upstream revision: {checkout}: {actual}')
    patch = manifest.parent / record['patch']
    patch_bytes = patch.read_bytes().replace(b'\r\n', b'\n')
    if sha(patch_bytes) != record['patch_sha256']:
        raise RuntimeError('Reviewed patch hash mismatch')
    inputs = {}
    for name, expected in record['files'].items():
        data = (fx / name).read_bytes().replace(b'\r\n', b'\n')
        if sha(data) != expected:
            raise RuntimeError(f'Upstream shader hash mismatch: {name}')
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
        notice = f'// FORGE maintained shader exception; patch identity SHA256 {identity}\n'.encode()
        outputs = {}
        for name in inputs:
            data = notice + (scratch / name).read_bytes()
            if data == notice + inputs[name]:
                raise RuntimeError(f'Patch did not modify expected input: {name}')
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
