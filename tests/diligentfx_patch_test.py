"""Exact-pin staging, refusal, idempotence and shader identity regression."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
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
    def test_native_target_source_and_header_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            project = directory/'project'
            project.mkdir()
            # Configure the real integration against a composition-only native
            # target. Native compilation/execution remains a separate gate.
            (project/'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.24)
project(DescriptorSelection CXX)
set(PROJECT_SOURCE_DIR "''' + root.as_posix() + '''")
set(diligent_SOURCE_DIR "''' + engine.as_posix() + '''")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_library(Diligent-GraphicsEngineD3D12-static STATIC)
add_library(Diligent-Archiver-static STATIC)
set(native "${diligent_SOURCE_DIR}/DiligentCore/Graphics/GraphicsEngineD3D12")
target_sources(Diligent-GraphicsEngineD3D12-static PRIVATE
 "${native}/src/CommandContext.cpp" "${native}/src/DeviceContextD3D12Impl.cpp"
 "${native}/src/PipelineResourceSignatureD3D12Impl.cpp" "${native}/src/ShaderResourceCacheD3D12.cpp"
 "${native}/src/RootSignature.cpp")
target_sources(Diligent-Archiver-static PRIVATE "${native}/src/RootSignature.cpp")
set_target_properties(Diligent-GraphicsEngineD3D12-static Diligent-Archiver-static PROPERTIES LINKER_LANGUAGE CXX)
include("${PROJECT_SOURCE_DIR}/cmake/diligent_d3d12_patch.cmake")
get_target_property(sources Diligent-GraphicsEngineD3D12-static SOURCES)
get_target_property(headers Diligent-GraphicsEngineD3D12-static INCLUDE_DIRECTORIES)
get_target_property(archiver Diligent-Archiver-static INCLUDE_DIRECTORIES)
file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${sources}\\n${headers}\\n${archiver}\\n")
''')
            build = directory/'build'
            result = subprocess.run(['cmake', '-S', str(project), '-B', str(build)],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            sources, headers, archiver = (build/'selected.txt').read_text().splitlines()
            for name in ('CommandContext', 'DeviceContextD3D12Impl',
                         'PipelineResourceSignatureD3D12Impl', 'ShaderResourceCacheD3D12'):
                self.assertIn(f'forge-patched-core/Graphics/GraphicsEngineD3D12/src/{name}.cpp', sources)
                self.assertNotIn(f'DiligentCore/Graphics/GraphicsEngineD3D12/src/{name}.cpp', sources)
            self.assertIn(str(engine/'DiligentCore/Graphics/GraphicsEngineD3D12/src/RootSignature.cpp').replace('\\', '/'), sources)
            self.assertEqual(headers, archiver)
            self.assertTrue(headers.endswith('forge-patched-core/Graphics/GraphicsEngineD3D12/include'))

    def test_core_pin_and_private_header_overlay(self):
        core_manifest = root/'cmake/patches/diligentcore-744f079f-descriptor-tables.json'
        metadata = json.loads(core_manifest.read_text())
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)/'core'
            record = module.stage(engine, target, core_manifest)
            times = {p: p.stat().st_mtime_ns for p in target.rglob('*') if p.is_file()}
            self.assertEqual(record, module.stage(engine, target, core_manifest))
            self.assertEqual(times, {p: p.stat().st_mtime_ns for p in times})
            for name in metadata['files']:
                data = (target/name).read_bytes()
                self.assertTrue(data.startswith(b'// FORGE maintained D3D12 descriptor repair;'))
                self.assertIn(b'Copyright', data)
                self.assertNotEqual(record['files'][name], record['outputs'][name])
            headers = engine/'DiligentCore/Graphics/GraphicsEngineD3D12/include'
            expected = {str(p.relative_to(engine/'DiligentCore')) for p in headers.iterdir() if p.is_file()}
            actual = {str(p.relative_to(target)) for p in (target/'Graphics/GraphicsEngineD3D12/include').iterdir()}
            self.assertEqual(expected, actual)
            for name, digest in metadata['support_files'].items():
                self.assertEqual(module.sha((target/name).read_bytes()), digest)
            # A sibling's quoted include must resolve to the same staged layout.
            self.assertIn(b'#include "CommandContext.hpp"',
                          (target/'Graphics/GraphicsEngineD3D12/include/RenderDeviceD3D12Impl.hpp').read_bytes())

    def test_core_support_source_refusal(self):
        metadata = json.loads((root/'cmake/patches/diligentcore-744f079f-descriptor-tables.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            local = directory/'manifest.json'
            # Check real upstream data while deliberately invalidating one
            # unmodified sibling header. Revision and patch checks stay active.
            name = next(iter(metadata['support_files']))
            metadata['support_files'][name] = '0'*64
            local.write_text(json.dumps(metadata))
            shutil.copy2(root/'cmake/patches'/metadata['patch'], directory/metadata['patch'])
            with self.assertRaisesRegex(RuntimeError, 'source hash'):
                module.stage(engine, directory/'staged', local)

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
            patch_path.write_bytes((manifest.parent/metadata['patch']).read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
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
