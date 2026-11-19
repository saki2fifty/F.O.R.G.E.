"""Verify SDK rebuilds with nonwritable content and separate writable user data."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import navigation_rebuild_gallery_sdk_test as gallery


def verify(runtime, module, source, output):
    if os.geteuid() == 0:
        raise RuntimeError('Root bypasses POSIX modes; it cannot prove read-only content')
    output.mkdir(parents=True, exist_ok=True)
    original = gallery.Runtime
    receipts = []
    with tempfile.TemporaryDirectory(prefix='readonly-runtime-', dir=output) as temporary:
        scratch = Path(temporary)

        class ReadOnlyRuntime(original):
            def __init__(self, executable, arguments=(), **kwargs):
                args = list(arguments)
                self.root = Path(args[args.index('--sdk-project') + 1])
                assert self.root.is_relative_to(scratch)
                args[args.index('--user-data') + 1] = scratch / 'writable-users'
                self.files = {p: hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in self.root.rglob('*') if p.is_file()}
                self.directories = sorted(
                    [self.root, *(p for p in self.root.rglob('*') if p.is_dir())],
                    key=lambda p: len(p.parts))
                for path in self.files:
                    path.chmod(0o444)
                for path in reversed(self.directories):
                    path.chmod(0o555)
                try:
                    (self.root / 'forbidden-write').write_text('must fail')
                except PermissionError:
                    pass
                else:
                    self.restore_permissions()
                    raise AssertionError('Content remains writable; refusing read-only acceptance')
                try:
                    super().__init__(executable, args, **kwargs)
                except BaseException:
                    self.restore_permissions()
                    raise

            def restore_permissions(self):
                for path in self.directories:
                    path.chmod(0o755)
                for path in self.files:
                    path.chmod(0o644)

            def quit(self, **kwargs):
                super().quit(**kwargs)
                assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest
                           for p, digest in self.files.items())
                assert set(self.files) == {p for p in self.root.rglob('*') if p.is_file()}
                receipts.append(dict(files=len(self.files), write_denied=True,
                                     native_exit=self.process.returncode,
                                     all_content_bytes_preserved=True))

            def close(self):
                try:
                    super().close()
                finally:
                    self.restore_permissions()

        gallery.Runtime = ReadOnlyRuntime
        try:
            gallery.verify(runtime, module, source, scratch / 'projects')
        finally:
            gallery.Runtime = original
    assert len(receipts) == 2 and all(row['native_exit'] == 0 for row in receipts)
    (output / 'read-only-content.json').write_text(json.dumps(dict(
        uid=os.geteuid(), hosts=receipts,
        scope='POSIX nonwritable content, separate writable user data, actual SDK '
              'Gallery rebuild/Agent arrival and two graceful hosts; no Windows '
              'filesystem or graphical acceptance.'), indent=2) + '\n')
    print('Read-only content: two SDK hosts, rebuilt navigation and Agent arrival passed')


if __name__ == '__main__':
    verify(*(Path(value).resolve() for value in sys.argv[1:5]))
