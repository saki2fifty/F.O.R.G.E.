"""Preserve Ninja input times only when cached source bytes still match exactly."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import time


def tracked(root):
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root)
    return [name.decode('utf-8') for name in names.split(b'\0') if name]


def regular_file(root, name):
    relative = PurePosixPath(name)
    if relative.is_absolute() or '..' in relative.parts or '\\' in name or not relative.parts:
        raise ValueError('Invalid source stamp path')
    path = root.joinpath(*relative.parts)
    if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
        return None
    return path


def fingerprint(path):
    before = path.stat()
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise RuntimeError('Source changed while recording build cache inputs')
    return {'bytes': after.st_size, 'sha256': digest.hexdigest(), 'mtime_ns': after.st_mtime_ns}


def record(root, manifest):
    files = {}
    for name in tracked(root):
        path = regular_file(root, name)
        if path is not None:
            files[name] = fingerprint(path)
    manifest.parent.mkdir(parents=True, exist_ok=True)
    temporary = manifest.with_suffix('.new')
    temporary.write_text(json.dumps({'version': 1, 'recorded_ns': time.time_ns(), 'files': files}), encoding='utf-8')
    temporary.replace(manifest)
    return len(files)


def restore(root, manifest):
    if not manifest.is_file():
        return 0
    if manifest.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('Source stamp manifest exceeds limit')
    data = json.loads(manifest.read_text(encoding='utf-8'))
    if data.get('version') != 1 or not isinstance(data.get('files'), dict):
        raise ValueError('Unsupported source stamp manifest')
    epoch = data.get('recorded_ns')
    if type(epoch) is not int or epoch <= 0:
        raise ValueError('Invalid source stamp recording time')
    # Only current Git-tracked regular files are eligible. Cached paths cannot
    # target arbitrary filesystem locations, and new/deleted files stay new/deleted.
    restored = 0
    for name in tracked(root):
        entry = data['files'].get(name)
        path = regular_file(root, name)
        if path is None:
            continue
        if not isinstance(entry, dict):
            os.utime(path, ns=(path.stat().st_atime_ns, max(time.time_ns(), epoch + 1_000_000_000)))
            continue
        stamp = entry.get('mtime_ns')
        current = fingerprint(path)
        if (type(stamp) is not int or stamp <= 0 or stamp > current['mtime_ns'] or
                entry.get('bytes') != current['bytes'] or entry.get('sha256') != current['sha256']):
            # A changed file must be newer than the cached completed build even
            # if a copy operation happened to preserve its old timestamp.
            os.utime(path, ns=(path.stat().st_atime_ns, max(time.time_ns(), epoch + 1_000_000_000)))
            continue
        os.utime(path, ns=(path.stat().st_atime_ns, stamp))
        restored += 1
    return restored


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('record', 'restore'))
    parser.add_argument('manifest', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    count = (record if args.operation == 'record' else restore)(root, args.manifest.resolve())
    print(f'Source stamps {args.operation}: {count} content-verified files')
