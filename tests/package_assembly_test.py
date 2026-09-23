"""Fail closed on mismatched or modified build artifacts during final assembly."""
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


class Assembly(unittest.TestCase):
    def test_matching_and_rejected_candidates(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            identity = dict(build_id='260919-000063', source_commit='a'*40)
            editor = {'README.txt': b'Editor', 'forge_editor.exe': b'fixture'}
            manifest = dict(**identity, files={k:hashlib.sha256(v).hexdigest() for k,v in editor.items()})
            with zipfile.ZipFile(root/'editor.zip', 'w') as archive:
                for k,v in editor.items(): archive.writestr(k,v)
                archive.writestr('manifest.json', json.dumps(manifest))
            def sdk(change=False, corrupt=False, escape=False):
                build = dict(identity)
                if change: build['source_commit']='b'*40
                files={'build.json':json.dumps(build).encode(), 'bin/forge_runtime.exe':b'runtime', 'bin/flecs.dll':b'flecs'}
                m=dict(build=build, symlinks={}, files={k:hashlib.sha256(v).hexdigest() for k,v in files.items()})
                if corrupt: files['bin/flecs.dll']=b'changed'
                if escape: files['../outside']=b'escape'
                files['sdk-manifest.json']=json.dumps(m).encode()
                with tarfile.open(root/'sdk.tar.gz','w:gz') as archive:
                    for k,v in files.items():
                        t=tarfile.TarInfo(k);t.size=len(v);archive.addfile(t,io.BytesIO(v))
            kit=root/'kit'; kit.mkdir()
            (kit/'flecs.dll').write_bytes(b'flecs')
            (kit/'forge_game.exe').write_bytes(b'game')
            def runtime(change=False, corrupt=False):
                records={p.name:dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                         for p in kit.iterdir() if p.name!='forge.runtime-kit.json'}
                engine=dict(identity,profile='shared-native-sdk',sdk_fingerprint='a'*64)
                if change:engine['build_id']='260919-000064'
                (kit/'forge.runtime-kit.json').write_text(json.dumps(dict(format='forge.runtime-kit',version=1,
                    executable='forge_game.exe',target=dict(platform='windows',backend='d3d12'),engine=engine,files=records)))
                if corrupt:(kit/'forge_game.exe').write_bytes(b'corrupt')
            runtime()
            sdk(); output=root/'final.zip'
            assemble(root/'editor.zip',root/'sdk.tar.gz',output,kit)
            original=output.read_bytes()
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.read('NativeSdk/bin/flecs.dll'),b'flecs')
                self.assertEqual(archive.read('runtime-kits/shared-native-sdk/forge_game.exe'),b'game')
            for kwargs in (dict(change=True),dict(corrupt=True),dict(escape=True)):
                sdk(**kwargs)
                with self.assertRaises(ValueError): assemble(root/'editor.zip',root/'sdk.tar.gz',output,kit)
                self.assertEqual(output.read_bytes(),original)

            sdk()
            for kwargs in (dict(change=True),dict(corrupt=True)):
                runtime(**kwargs)
                with self.assertRaises(ValueError): assemble(root/'editor.zip',root/'sdk.tar.gz',output,kit)
                self.assertEqual(output.read_bytes(),original)


if __name__=='__main__': unittest.main()
