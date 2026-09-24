"""Exact-pin staging, refusal, idempotence and shader identity regression."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]
engine = Path(sys.argv.pop(1)).resolve()
spec = importlib.util.spec_from_file_location('stage_fx', root/'tools/stage_diligentfx_patch.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
manifest = root/'cmake/patches/diligentfx-aaa41d47-shader-warnings.json'


class PatchTests(unittest.TestCase):
    def test_real_pin_and_idempotence(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)/'staged'
            record = module.stage(engine, target, manifest)
            times = {p: p.stat().st_mtime_ns for p in target.rglob('*') if p.is_file()}
            self.assertEqual(record, module.stage(engine, target, manifest))
            self.assertEqual(times, {p: p.stat().st_mtime_ns for p in times})
            for name, expected in record['files'].items():
                self.assertEqual(module.sha((engine/'DiligentFX'/name).read_bytes().replace(b'\r\n', b'\n')), expected)
                self.assertNotEqual(expected, record['outputs'][name])

    def test_refusal_and_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            metadata = json.loads(manifest.read_text())
            source = directory/'engine'
            local_manifest = directory/manifest.name
            patch_path = directory/metadata['patch']
            shutil.copy2(manifest.parent/metadata['patch'], patch_path)
            for name in metadata['files']:
                p = source/'DiligentFX'/name
                p.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(engine/'DiligentFX'/name, p)
            local_manifest.write_text(json.dumps(metadata))
            target = directory/'staged'
            revisions = [metadata['engine_commit'], metadata['fx_commit']]
            # Fake only git revision discovery; real patch application/hashes run.
            with patch.object(module.subprocess, 'check_output', side_effect=revisions):
                first = module.stage(source, target, local_manifest)
            metadata['review_note'] = 'identity-only change must invalidate included shader bytes'
            local_manifest.write_text(json.dumps(metadata))
            with patch.object(module.subprocess, 'check_output', side_effect=revisions):
                second = module.stage(source, target, local_manifest)
            self.assertNotEqual(first['identity'], second['identity'])
            self.assertNotEqual(first['outputs'], second['outputs'])
            with patch.object(module.subprocess, 'check_output', return_value='wrong revision'):
                with self.assertRaisesRegex(RuntimeError, 'revision'):
                    module.stage(source, target, local_manifest)
            name = next(iter(metadata['files']))
            p = source/'DiligentFX'/name
            p.write_bytes(p.read_bytes()+b'\n')
            with patch.object(module.subprocess, 'check_output', side_effect=revisions):
                with self.assertRaisesRegex(RuntimeError, 'shader hash'):
                    module.stage(source, target, local_manifest)
            patch_path.write_bytes(patch_path.read_bytes()+b'\n')
            with patch.object(module.subprocess, 'check_output', side_effect=revisions):
                with self.assertRaisesRegex(RuntimeError, 'patch hash'):
                    module.stage(source, target, local_manifest)
            with self.assertRaisesRegex(RuntimeError, 'outside'):
                module.stage(source, source/'generated', local_manifest)


if __name__ == '__main__':
    unittest.main()
