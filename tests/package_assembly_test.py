"""Fail closed on mismatched or modified inputs to the one Windows package."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from assemble_windows_package import assemble


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Assembly(unittest.TestCase):
    def test_matching_and_rejected_candidates(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            identity = dict(build_id='260919-000063', source_commit='a'*40)
            editor = {'README.txt': b'Editor', 'bin/forge_editor.exe': b'fixture'}
            manifest = dict(**identity, files={k:digest(v) for k,v in editor.items()})
            with zipfile.ZipFile(root/'editor.zip', 'w') as archive:
                for name, value in editor.items():
                    archive.writestr(name, value)
                archive.writestr('manifest.json', json.dumps(manifest))

            def sdk(change=False, corrupt=False, escape=False):
                build = dict(identity)
                if change:
                    build['source_commit'] = 'b'*40
                files = {'build.json':json.dumps(build).encode(),
                         'bin/forge_runtime.exe':b'runtime', 'bin/flecs.dll':b'flecs'}
                record = dict(build=build, symlinks={},
                              files={name:digest(value) for name,value in files.items()})
                if corrupt:
                    files['bin/flecs.dll'] = b'changed'
                if escape:
                    files['../outside'] = b'escape'
                files['sdk-manifest.json'] = json.dumps(record).encode()
                with tarfile.open(root/'sdk.tar.gz', 'w:gz') as archive:
                    for name, value in files.items():
                        info = tarfile.TarInfo(name)
                        info.size = len(value)
                        archive.addfile(info, io.BytesIO(value))

            kit = root/'kit'
            kit.mkdir()
            (kit/'flecs.dll').write_bytes(b'flecs')
            (kit/'forge_game.exe').write_bytes(b'game')
            def runtime(change=False, corrupt=False):
                records = {p.name:dict(bytes=p.stat().st_size, sha256=digest(p.read_bytes()))
                           for p in kit.iterdir() if p.name != 'forge.runtime-kit.json'}
                engine = dict(identity, profile='shared-native-sdk', sdk_fingerprint='a'*64)
                if change:
                    engine['build_id'] = '260919-000064'
                (kit/'forge.runtime-kit.json').write_text(json.dumps(dict(
                    format='forge.runtime-kit', version=1, executable='forge_game.exe',
                    target=dict(platform='windows', backend='d3d12'), engine=engine,
                    files=records)))
                if corrupt:
                    (kit/'forge_game.exe').write_bytes(b'corrupt')

            game = root/'reference'
            game.mkdir()
            def reference(change=False, corrupt=False, fixture=False):
                payload = {'forge_game.exe':b'game', 'flecs.dll':b'flecs'}
                if fixture:
                    payload['forge_game_fixture.exe'] = b'fixture'
                engine = json.loads((kit/'forge.runtime-kit.json').read_text())['engine']
                if change:
                    engine['build_id'] = '260919-000065'
                metadata = dict(format='forge.standalone', version=1, engine=engine,
                                target=dict(platform='windows', backend='d3d12'),
                                executable='forge_game.exe',
                                files={name:dict(bytes=len(value), sha256=digest(value))
                                       for name,value in payload.items()})
                for old in game.iterdir():
                    old.unlink()
                for name, value in payload.items():
                    (game/name).write_bytes(value)
                (game/'forge.standalone.json').write_text(json.dumps(metadata))
                if corrupt:
                    (game/'forge_game.exe').write_bytes(b'changed')

            sdk()
            runtime()
            reference()
            output = root/'final.zip'
            def build():
                assemble(root/'editor.zip', root/'sdk.tar.gz', output, kit, game)
            build()
            original = output.read_bytes()
            with zipfile.ZipFile(output) as archive:
                names = set(archive.namelist())
                self.assertIn('bin/forge_editor.exe', names)
                self.assertIn('NativeSdk/bin/flecs.dll', names)
                self.assertIn('runtime-kits/shared-native-sdk/forge_game.exe', names)
                self.assertIn('ReferenceGame/forge_game.exe', names)
                self.assertIn('Run-Forge-Dev.cmd', names)
                self.assertNotIn('developer-manifest.json', names)
                self.assertEqual(archive.read('NativeSdk/bin/flecs.dll'), b'flecs')
                final = json.loads(archive.read('manifest.json'))
                self.assertEqual(final['reference_game']['path'], 'ReferenceGame')
                self.assertEqual(names - {'manifest.json'}, set(final['files']))
                for name, expected in final['files'].items():
                    self.assertEqual(digest(archive.read(name)), expected, name)

            for kwargs in (dict(change=True), dict(corrupt=True), dict(fixture=True)):
                reference(**kwargs)
                with self.assertRaises(ValueError):
                    build()
                self.assertEqual(output.read_bytes(), original)
            reference()
            for kwargs in (dict(change=True), dict(corrupt=True), dict(escape=True)):
                sdk(**kwargs)
                with self.assertRaises(ValueError):
                    build()
                self.assertEqual(output.read_bytes(), original)
            sdk()
            for kwargs in (dict(change=True), dict(corrupt=True)):
                runtime(**kwargs)
                with self.assertRaises(ValueError):
                    build()
                self.assertEqual(output.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
