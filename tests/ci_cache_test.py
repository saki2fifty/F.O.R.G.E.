"""Cache identities must isolate incompatible environments while allowing source iteration."""
import importlib.util
from pathlib import Path
import sys
import os
import json
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
sys.dont_write_bytecode = True
source = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ci_cache_key', source/'tools/ci_cache_key.py')
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)
spec = importlib.util.spec_from_file_location('ci_source_stamps', source/'tools/ci_source_stamps.py')
stamps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stamps)


class CacheIdentity(unittest.TestCase):
    def test_hash_addressed_project_checkout(self):
        work = source.parent/'AgentFiles'
        work.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work, prefix='project checkout ') as directory:
            root = Path(directory)/'repository'
            root.mkdir()
            git = ['git', '-c', 'core.safecrlf=false']
            subprocess.run([*git, 'init', '-q'], cwd=root, check=True)
            (root/'.gitattributes').write_bytes((source/'.gitattributes').read_bytes())
            paths = {
                'samples/projects/FeatureGallery/Assets/EasedPlatform.animation.json':
                    (source/'samples/projects/FeatureGallery/Assets/EasedPlatform.animation.json').read_bytes(),
                'samples/projects/FeatureGallery/.forge/cache/derived/fixture/animation.clip':
                    b'{"cooked":true}\n',
                'samples/projects/PhysicsAcceptance/Assets/fixture.json':
                    b'{"original_crlf":true}\r\n',
            }
            for name, data in paths.items():
                path = root/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            (root/'ordinary.txt').write_bytes(b'ordinary text\n')
            subprocess.run([*git, '-c', 'core.autocrlf=false', 'add', '.'], cwd=root,
                           check=True, capture_output=True)
            checkout = Path(directory)/'Windows checkout'
            subprocess.run([*git, '-c', 'core.autocrlf=true', 'checkout-index', '--all',
                            '--prefix='+checkout.as_posix()+'/'], cwd=root,
                           check=True, capture_output=True)
            for name, data in paths.items():
                self.assertEqual((checkout/name).read_bytes(), data, name)
            # Prove the simulated Windows conversion is active outside projects.
            self.assertEqual((checkout/'ordinary.txt').read_bytes(), b'ordinary text\r\n')

    def test_invalidation(self):
        work = source.parent/'AgentFiles'
        work.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as directory, patch.object(
                cache.subprocess, 'check_output', return_value=b'pinned build tools'):
            root = Path(directory)
            (root/'CMakeLists.txt').write_text('project(FORGE)')
            (root/'CMakePresets.json').write_text('{}')
            (root/'cmake').mkdir()
            (root/'tools').mkdir()
            (root/'tools/stage_diligentfx_patch.py').write_text('patch staging implementation')
            (root/'cmake/patches').mkdir()
            shader_patch = root/'cmake/patches/diligentfx.patch'
            shader_patch.write_text('reviewed patch A')
            dependency = root/'cmake/dependencies.cmake'
            dependency.write_text('pinned dependency A')
            consumer = root/'cmake/editor.cmake'
            consumer.write_text('target_link_libraries(app PRIVATE first)')
            environment = dict(ImageVersion='image1', VCToolsVersion='msvc1',
                               WindowsSDKVersion='sdk1', RUNNER_ARCH='X64')
            first = cache.cache_key(root, environment)
            for key, value in [('FORGE_LINKAGE_PROFILE', 'shared-native-sdk'), ('FORGE_CRT_PROFILE', 'MultiThreadedDLL')]:
                self.assertNotEqual(first, cache.cache_key(root, dict(environment, **{key: value})))
            (root/'main.cpp').write_text('new product code')
            self.assertEqual(first, cache.cache_key(root, environment))
            consumer.write_text('target_link_libraries(app PRIVATE second)')
            self.assertEqual(first, cache.cache_key(root, environment))
            (root/'CMakeLists.txt').write_text('changed global compiler options')
            self.assertNotEqual(first, cache.cache_key(root, environment))
            (root/'CMakeLists.txt').write_text('project(FORGE)')
            for key in environment:
                changed = dict(environment, **{key: 'different'})
                self.assertNotEqual(first, cache.cache_key(root, changed))
            shader_patch.write_text('reviewed patch B')
            self.assertNotEqual(first, cache.cache_key(root, environment))
            shader_patch.write_text('reviewed patch A')
            dependency.write_text('pinned dependency B')
            self.assertNotEqual(first, cache.cache_key(root, environment))
            with self.assertRaises(RuntimeError):
                cache.cache_key(root, {})
            with patch.object(cache.subprocess, 'check_output', return_value=b'changed tools'):
                changed_tools = cache.cache_key(root, environment)
            self.assertNotEqual(changed_tools, cache.cache_key(root, environment))

    def test_reconfigured_consumer_relinks_cached_objects(self):
        cmake = shutil.which('cmake')
        ninja = shutil.which('ninja')
        self.assertIsNotNone(cmake, 'CMake is required for cached consumer checks')
        self.assertIsNotNone(ninja, 'Ninja is required for cached consumer checks')
        work = source.parent/'AgentFiles'
        work.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work, prefix='cached target ') as directory:
            root = Path(directory)
            (root/'cmake').mkdir()
            (root/'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.25)\nproject(CacheConsumer LANGUAGES CXX)\n'
                'add_library(first STATIC first.cpp)\nadd_library(second STATIC second.cpp)\n'
                'add_executable(consumer main.cpp)\ninclude(cmake/editor.cmake)\n', encoding='utf-8')
            (root/'first.cpp').write_text('int value() { return 1; }', encoding='utf-8')
            (root/'second.cpp').write_text('int value() { return 2; }', encoding='utf-8')
            (root/'main.cpp').write_text(
                '#include <cstdio>\nint value(); int main() { std::printf("%d", value()); }',
                encoding='utf-8')
            consumer = root/'cmake/editor.cmake'
            consumer.write_text('target_link_libraries(consumer PRIVATE first)', encoding='utf-8')
            subprocess.run(['git', 'init', '-q'], cwd=root, check=True)
            subprocess.run(['git', 'add', '.'], cwd=root, check=True)
            build = root/'build'
            subprocess.run([cmake, '-S', str(root), '-B', str(build), '-G', 'Ninja',
                            '-DCMAKE_MAKE_PROGRAM=' + ninja], check=True, capture_output=True)
            subprocess.run([cmake, '--build', str(build)], check=True, capture_output=True)
            executable = build/('consumer.exe' if os.name == 'nt' else 'consumer')
            self.assertEqual(subprocess.check_output([executable], text=True), '1')
            objects = list((build/'CMakeFiles/consumer.dir').glob('main.cpp.*'))
            self.assertEqual(len(objects), 1, objects)
            original_time = objects[0].stat().st_mtime_ns
            manifest = root/'stamps.json'
            stamps.record(root, manifest)
            recorded = json.loads(manifest.read_text())['files']['cmake/editor.cmake']['mtime_ns']
            consumer.write_text('target_link_libraries(consumer PRIVATE second)', encoding='utf-8')
            os.utime(consumer, ns=(recorded, recorded))
            stamps.restore(root, manifest)
            subprocess.run([cmake, '--build', str(build)], check=True, capture_output=True)
            self.assertEqual(subprocess.check_output([executable], text=True), '2')
            self.assertEqual(objects[0].stat().st_mtime_ns, original_time,
                             'Consumer source was unnecessarily recompiled')

    def test_content_verified_incremental_inputs(self):
        ninja = shutil.which('ninja')
        if not ninja:
            candidate = source.parent/'AgentFiles/tools/bin/ninja'
            ninja = str(candidate) if candidate.is_file() else None
        self.assertIsNotNone(ninja, 'Ninja is required for the real cache reuse regression')
        work = source.parent/'AgentFiles'
        work.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work, prefix='cache inputs ') as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q'], cwd=root, check=True)
            (root/'main.txt').write_text('main', encoding='utf-8')
            header = root/'header.txt'
            header.write_text('old header', encoding='utf-8')
            (root/'builder.py').write_text(
                'from pathlib import Path\n'
                'Path("product.txt").write_text(Path("main.txt").read_text()+Path("header.txt").read_text())\n', encoding='utf-8')
            (root/'build.ninja').write_text(
                f'rule generate\n  command = "{sys.executable}" builder.py\n'
                'build product.txt: generate main.txt header.txt | builder.py\n', encoding='utf-8')
            subprocess.run(['git', 'add', '.'], cwd=root, check=True)
            def build():
                return subprocess.check_output([ninja], cwd=root, text=True)
            build()
            manifest = root/'source-stamps.json'
            self.assertEqual(stamps.record(root, manifest), 4)
            recorded = json.loads(manifest.read_text())
            for name in stamps.tracked(root):
                (root/name).touch()
            self.assertEqual(stamps.restore(root, manifest), 4)
            self.assertIn('no work to do', build())
            self.assertEqual((root/'product.txt').read_text(), 'mainold header')
            # Reproduce modified content with a preserved mtime. Hash mismatch
            # must still make Ninja rebuild the dependent output.
            header.write_text('new header', encoding='utf-8')
            old_time = recorded['files']['header.txt']['mtime_ns']
            os.utime(header, ns=(old_time, old_time))
            self.assertEqual(stamps.restore(root, manifest), 3)
            self.assertNotIn('no work to do', build())
            self.assertEqual((root/'product.txt').read_text(), 'mainnew header')
            # An arbitrary cached path cannot touch a non-source file.
            outside = root/'outside.txt'
            outside.write_text('untouched', encoding='utf-8')
            outside_time = outside.stat().st_mtime_ns
            recorded['files']['../outside.txt'] = recorded['files']['main.txt']
            manifest.write_text(json.dumps(recorded), encoding='utf-8')
            stamps.restore(root, manifest)
            self.assertEqual(outside.stat().st_mtime_ns, outside_time)
            manifest.write_text('{bad json', encoding='utf-8')
            with self.assertRaises(ValueError):
                stamps.restore(root, manifest)


if __name__ == '__main__':
    unittest.main()
