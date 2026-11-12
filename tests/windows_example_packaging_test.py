"""Exercise the real ZIP writer with the complete curated Gallery and fixture tools."""
import errno
import hashlib
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SOURCE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SOURCE/'tools'))
import package_windows


class GalleryPackageTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        # Native preparer arguments use canonical paths, including Windows TEMP aliases.
        self.root = Path(self.scratch.name).resolve()
        self.source, self.build, self.dependencies = (self.root/name for name in ('source', 'build', 'dependencies'))
        self.build.mkdir()
        self.archive = self.root/'editor.zip'
        self.gallery = self.source/'samples/projects/FeatureGallery'
        shutil.copytree(SOURCE/'samples/projects/FeatureGallery', self.gallery)
        # Synthetic package-writer facts only; no fixture here proves native
        # Shader compilation or publication. Real preparers own that validation.
        catalog_path = self.gallery/'forge.assets.json'
        catalog = json.loads(catalog_path.read_text())
        for record in catalog['assets']:
            if record['type'] in ('shader', 'material') and not record.get('metadata', {}).get('forge.import'):
                record.setdefault('metadata', {})['forge.import'] = dict(package_writer_fixture=True)
        catalog_path.write_text(json.dumps(catalog), encoding='utf-8')
        # An unrecognized extension must survive without another allowlist change.
        self.write(self.gallery/'Assets/next-format.resource', b'\x00\x01\r\n\xff')
        for name in ('flecs', 'json', 'sdl', 'imgui_source', 'cpp_text_editor', 'diligent',
                     'jolt', 'box2d', 'miniaudio', 'ozz', 'recast', 'rmlui', 'freetype', 'harfbuzz', 'utf8proc', 'ktx',
                     'webp', 'meshoptimizer', 'draco', 'ufbx'):
            self.write(self.dependencies/(name+'-src')/'LICENSE.txt', b'Packaging test notice\n')
        metadata = dict(build_id='260101-000001', source_commit='1'*40)
        self.write(self.build/'build.json', json.dumps(metadata).encode())
        self.write(self.build/'CMakeCache.txt', b'CMAKE_BUILD_TYPE:STRING=Release\n')
        # Header-only PE fixtures validate packaging inputs; they are never executed.
        pe = b'MZ'+b'\0'*58+struct.pack('<I', 64)+b'PE\0\0\x64\x86'
        for name in ('forge_editor.exe', 'forge_runtime.exe', 'forge_tools.exe',
                     'forge_nav_build.exe', 'forge_asset_build.exe', 'forge_shader_build.exe',
                     'forge_ui_inspect.exe', 'GraphicsEngineD3D12_64r.dll', 'Archiver_64r.dll',
                     'tools/gltf2ozz.exe'):
            self.write(self.build/name, pe)
        self.write(self.build/'runtime-kit/forge.runtime-kit.json',
                   json.dumps(dict(engine=metadata, files={})).encode())
        self.write(self.source/'docs/licenses/ozz-converter.txt', b'Converter test notice\n')
        self.write(self.source/'docs/licenses/diligentfx-shader-patch.txt', b'Shader test notice\n')
        self.write(self.source/'resources/ui/font.txt', b'UI resource fixture\n')
        self.write(self.source/'manual/README.md',
                   b'# Package test\n\n![Editor](images/scene-editor.png)\n\n![FBX models](images/fbx-import-gallery.png)\n')
        self.manual_image = (SOURCE/'manual/images/scene-editor.png').read_bytes()
        self.write(self.source/'manual/images/scene-editor.png', self.manual_image)
        self.fbx_image = (SOURCE/'manual/images/fbx-import-gallery.png').read_bytes()
        self.write(self.source/'manual/images/fbx-import-gallery.png', self.fbx_image)
        self.manual_links = re.findall(r'\]\((\.\./\.\./\.\./manual/[^)]+\.md(?:#[^)]*)?)\)',
                                       (self.gallery/'README.md').read_text(encoding='utf-8'))
        self.assertTrue(self.manual_links)
        for link in self.manual_links:
            relative = link.removeprefix('../../../manual/').split('#', 1)[0]
            self.assertTrue((SOURCE/'manual'/relative).is_file(), link)
            self.write(self.source/'manual'/relative, ('# '+Path(relative).stem+'\n').encode())
        self.write(self.source/'samples/animation/two-joints.gltf', b'{}\n')
        self.write(self.source/'samples/projects/Other/main.scene.json', b'{}\n')
        self.write(self.source/'samples/projects/Other/.forge/cache/audio/audio.fpcm',
                   b'Cooked resident audio fixture\x00\x01')
        self.write(self.source/'samples/projects/Other/build/local.obj', b'Not example content')
        self.shaping_example = SOURCE/'samples/projects/UiTextShaping'
        shutil.copytree(self.shaping_example,
                        self.source/'samples/projects/UiTextShaping')
        self.collection_example = SOURCE/'samples/projects/UiFontCollections'
        shutil.copytree(self.collection_example,
                        self.source/'samples/projects/UiFontCollections')
        self.write(self.source/'samples/projects/Other/Uppercase.OTC',
                   (self.collection_example/'Assets/Fonts/Menu.ttc').read_bytes())
        self.impulse_example = SOURCE/'samples/projects/AudioImpulseResponses'
        shutil.copytree(self.impulse_example,
                        self.source/'samples/projects/AudioImpulseResponses')

    @staticmethod
    def write(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def package(self, gallery_preparer=None):
        with patch.object(package_windows, '__file__', str(self.source/'tools/package_windows.py')):
            package_windows.package(self.build, self.dependencies, self.archive, gallery_preparer)

    def test_native_preparation_uses_private_gallery_and_hashes_selected_bytes(self):
        preparer = self.build/'forge_navigation_collision_gallery_tests.exe'
        shutil.copyfile(self.build/'forge_nav_build.exe', preparer)
        before = {p.relative_to(self.gallery): p.read_bytes()
                  for p in self.gallery.rglob('*') if p.is_file()}
        prepared = b'Matching native Tile selection and navigation\n'
        prepared_catalog = json.dumps(json.loads((self.gallery/'forge.assets.json').read_text()) |
                                     dict(package_writer_fixture='prepared')).encode()
        def run(arguments, **options):
            self.assertEqual(arguments[0:2], [str(preparer), '--prepare'])
            staged = Path(arguments[2])
            self.assertNotEqual(staged, self.gallery)
            self.assertEqual(staged.parent.parent, self.build)
            self.assertEqual(arguments[3], str(self.build/'forge_nav_build.exe'))
            self.assertEqual(options, dict(check=True, timeout=180))
            self.write(staged/'forge.assets.json', prepared_catalog)
            self.write(staged/'Assets/native-only.fnav', prepared)
            self.write(staged/'.forge/writer.lock', b'Released native fixture lease')
            self.write(staged/'.forge/cache/derived/cache.lock', b'Released cache lock')
        with patch.object(package_windows.subprocess, 'run', side_effect=run) as native:
            self.package(preparer)
        native.assert_called_once()
        self.assertEqual(before, {p.relative_to(self.gallery): p.read_bytes()
                                 for p in self.gallery.rglob('*') if p.is_file()})
        self.assertFalse(list(self.build.glob('gallery-package-*')))
        with zipfile.ZipFile(self.archive) as archive:
            manifest = json.loads(archive.read('manifest.json'))['files']
            for relative in ('forge.assets.json', 'Assets/native-only.fnav'):
                name = 'Examples/FeatureGallery/'+relative
                expected = prepared_catalog if relative == 'forge.assets.json' else prepared
                self.assertEqual(archive.read(name), expected)
                self.assertEqual(manifest[name], hashlib.sha256(expected).hexdigest())
            self.assertNotIn('Examples/FeatureGallery/.forge/writer.lock', manifest)
            self.assertNotIn('Examples/FeatureGallery/.forge/cache/derived/cache.lock', manifest)
            self.assertFalse(any(preparer.name in name for name in archive.namelist()))
            self.assertIn('Examples/Other/main.scene.json', manifest)
            audio = 'Examples/Other/.forge/cache/audio/audio.fpcm'
            expected = b'Cooked resident audio fixture\x00\x01'
            self.assertEqual(archive.read(audio), expected)
            self.assertEqual(manifest[audio], hashlib.sha256(expected).hexdigest())
            self.assertNotIn('Examples/Other/build/local.obj', manifest)
            for source in self.impulse_example.rglob('*'):
                if source.is_file():
                    name = 'Examples/AudioImpulseResponses/'+source.relative_to(self.impulse_example).as_posix()
                    self.assertEqual(archive.read(name), source.read_bytes())
                    self.assertEqual(manifest[name], hashlib.sha256(source.read_bytes()).hexdigest())

    def test_native_preparation_failure_retains_previous_archive(self):
        self.package()
        before = self.archive.read_bytes()
        preparer = self.build/'forge_navigation_collision_gallery_tests.exe'
        shutil.copyfile(self.build/'forge_nav_build.exe', preparer)
        with patch.object(package_windows.subprocess, 'run',
                          side_effect=subprocess.CalledProcessError(1, 'native-preparer')):
            with self.assertRaises(subprocess.CalledProcessError):
                self.package(preparer)
        self.assertEqual(self.archive.read_bytes(), before)
        self.assertFalse(list(self.build.glob('gallery-package-*')))

    def test_unpublished_surface_source_retains_previous_archive(self):
        self.package()
        before = self.archive.read_bytes()
        path = self.gallery/'forge.assets.json'
        catalog = json.loads(path.read_text())
        for record in catalog['assets']:
            if record['type'] == 'shader':
                record['metadata'].pop('forge.import', None)
        path.write_text(json.dumps(catalog), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'matching native preparation'):
            self.package()
        self.assertEqual(self.archive.read_bytes(), before)

    def test_multiple_preparers_share_one_private_gallery(self):
        preparers = [self.build/name for name in ('tile-preparer.exe', 'physical-preparer.exe')]
        for preparer in preparers:
            shutil.copyfile(self.build/'forge_nav_build.exe', preparer)
        stages = []
        def run(arguments, **options):
            index = len(stages)
            self.assertEqual(arguments[0], str(preparers[index]))
            staged = Path(arguments[2])
            stages.append(staged)
            if index:
                self.assertEqual((staged/'Assets/first.fnav').read_bytes(), b'First preparation')
            self.write(staged/('Assets/first.fnav' if not index else 'Assets/second.fnav'),
                       b'First preparation' if not index else b'Second preparation')
        with patch.object(package_windows.subprocess, 'run', side_effect=run) as native:
            self.package(preparers)
        self.assertEqual(native.call_count, 2)
        self.assertEqual(stages[0], stages[1])
        self.assertFalse(stages[0].exists())
        self.assertFalse((self.gallery/'Assets/first.fnav').exists())
        self.assertFalse((self.gallery/'Assets/second.fnav').exists())
        with zipfile.ZipFile(self.archive) as archive:
            manifest = json.loads(archive.read('manifest.json'))['files']
            for name, content in (('first', b'First preparation'), ('second', b'Second preparation')):
                path = 'Examples/FeatureGallery/Assets/'+name+'.fnav'
                self.assertEqual(archive.read(path), content)
                self.assertEqual(manifest[path], hashlib.sha256(content).hexdigest())

    def test_later_preparation_failure_retains_previous_archive(self):
        self.package()
        before = self.archive.read_bytes()
        preparers = [self.build/name for name in ('first-preparer.exe', 'failed-preparer.exe')]
        for preparer in preparers:
            shutil.copyfile(self.build/'forge_nav_build.exe', preparer)
        with patch.object(package_windows.subprocess, 'run',
                          side_effect=[None, subprocess.CalledProcessError(1, 'second-preparer')]) as native:
            with self.assertRaises(subprocess.CalledProcessError):
                self.package(preparers)
        self.assertEqual(native.call_count, 2)
        self.assertEqual(self.archive.read_bytes(), before)
        self.assertFalse(list(self.build.glob('gallery-package-*')))

    def test_complete_gallery_bytes_and_manifest(self):
        self.package()
        expected = {p.relative_to(self.gallery).as_posix(): p.read_bytes()
                    for p in self.gallery.rglob('*') if p.is_file()
                    and p.relative_to(self.gallery).as_posix() not in package_windows.GALLERY_RUNTIME_LOCKS}
        with zipfile.ZipFile(self.archive) as archive:
            self.assertIsNone(archive.testzip())
            manifest = json.loads(archive.read('manifest.json'))['files']
            actual = {name.removeprefix('Examples/FeatureGallery/')
                      for name in archive.namelist() if name.startswith('Examples/FeatureGallery/')}
            self.assertEqual(actual, set(expected))
            for relative, content in expected.items():
                name = 'Examples/FeatureGallery/'+relative
                if relative == 'README.md':
                    for link in self.manual_links:
                        page, separator, anchor = link.partition('#')
                        target = '../../manual/'+page.removeprefix('../../../manual/')[:-3]+'.html'+separator+anchor
                        content = content.replace((']('+link+')').encode(), (']('+target+')').encode())
                self.assertEqual(archive.read(name), content, name)
                self.assertEqual(manifest[name], hashlib.sha256(content).hexdigest(), name)
            for link in self.manual_links:
                page, separator, anchor = link.partition('#')
                target = 'manual/'+page.removeprefix('../../../manual/')[:-3]+'.html'
                self.assertIn(('](../../'+target+separator+anchor+')').encode(),
                              archive.read('Examples/FeatureGallery/README.md'))
                self.assertIn(target, manifest)
            self.assertIn('Examples/Other/main.scene.json', manifest)
            self.assertNotIn('Examples/Other/build/local.obj', manifest)
            notice = 'licenses/box2d-src/LICENSE.txt'
            self.assertEqual(archive.read(notice), b'Packaging test notice\n')
            self.assertEqual(archive.read('licenses/utf8proc-src/LICENSE.txt'),
                             b'Packaging test notice\n')
            self.assertEqual(manifest[notice], hashlib.sha256(archive.read(notice)).hexdigest())
            image = 'manual/images/scene-editor.png'
            self.assertEqual(archive.read(image), self.manual_image)
            self.assertEqual(manifest[image], hashlib.sha256(self.manual_image).hexdigest())
            self.assertIn(b'src="images/scene-editor.png"', archive.read('manual/index.html'))
            notice = 'licenses/ufbx-src/LICENSE.txt'
            self.assertEqual(archive.read(notice), b'Packaging test notice\n')
            self.assertEqual(archive.read('licenses/utf8proc-src/LICENSE.txt'),
                             b'Packaging test notice\n')
            self.assertEqual(manifest[notice], hashlib.sha256(archive.read(notice)).hexdigest())
            image = 'manual/images/fbx-import-gallery.png'
            self.assertEqual(archive.read(image), self.fbx_image)
            self.assertEqual(manifest[image], hashlib.sha256(self.fbx_image).hexdigest())
            self.assertIn(b'src="images/fbx-import-gallery.png"', archive.read('manual/index.html'))

    def test_default_packaging_excludes_runtime_locks_and_keeps_source_files(self):
        locks = ('.forge/writer.lock', '.forge/cache/derived/cache.lock')
        for relative in locks:
            self.write(self.gallery/relative, b'Released local runtime lock')
        self.write(self.gallery/'Assets/cache.lock', b'Authored resource with a similar name')
        self.package()
        with zipfile.ZipFile(self.archive) as archive:
            manifest = json.loads(archive.read('manifest.json'))['files']
            for relative in locks:
                self.assertTrue((self.gallery/relative).is_file())
                self.assertNotIn('Examples/FeatureGallery/'+relative, archive.namelist())
                self.assertNotIn('Examples/FeatureGallery/'+relative, manifest)
            name = 'Examples/FeatureGallery/Assets/cache.lock'
            self.assertEqual(archive.read(name), b'Authored resource with a similar name')
            self.assertIn(name, manifest)

    def test_collection_ui_example_fonts_and_manual_link(self):
        self.package()
        with zipfile.ZipFile(self.archive) as archive:
            manifest = json.loads(archive.read('manifest.json'))['files']
            for path in self.collection_example.rglob('*'):
                if not path.is_file() or '.forge' in path.relative_to(self.collection_example).parts:
                    continue
                name = 'Examples/UiFontCollections/'+path.relative_to(self.collection_example).as_posix()
                content = path.read_bytes()
                if path.name == 'README.md':
                    content = content.replace(b'](../../../manual/editor/ui-text-shaping.md)',
                                              b'](../../manual/editor/ui-text-shaping.html)')
                self.assertEqual(archive.read(name), content, name)
                self.assertEqual(manifest[name], hashlib.sha256(content).hexdigest(), name)
            self.assertEqual(archive.read('Examples/Other/Uppercase.OTC'),
                             (self.collection_example/'Assets/Fonts/Menu.ttc').read_bytes())
            self.assertNotIn('Examples/UiFontCollections/.forge/writer.lock', archive.namelist())

    def test_shaped_ui_example_fonts_styles_and_manual_link(self):
        self.package()
        with zipfile.ZipFile(self.archive) as archive:
            manifest = json.loads(archive.read('manifest.json'))['files']
            for path in self.shaping_example.rglob('*'):
                if not path.is_file():
                    continue
                name = 'Examples/UiTextShaping/'+path.relative_to(self.shaping_example).as_posix()
                content = path.read_bytes()
                if path.name == 'README.md':
                    content = content.replace(b'](../../../manual/editor/ui-text-shaping.md)',
                                              b'](../../manual/editor/ui-text-shaping.html)')
                    content = content.replace(b'](../../../manual/editor/ui-text-input.md)',
                                              b'](../../manual/editor/ui-text-input.html)')
                self.assertEqual(archive.read(name), content, name)
                self.assertEqual(manifest[name], hashlib.sha256(content).hexdigest(), name)
            notice = 'licenses/harfbuzz-src/LICENSE.txt'
            self.assertEqual(archive.read(notice), b'Packaging test notice\n')
            self.assertEqual(archive.read('licenses/utf8proc-src/LICENSE.txt'),
                             b'Packaging test notice\n')

    def test_sibling_manual_anchors_survive_offline_rewrite(self):
        self.write(self.source/'samples/projects/Other/README.md',
                   b'[Fonts](../../../manual/editor/runtime-ui.md#styles-fonts-and-images)\n')
        self.package()
        with zipfile.ZipFile(self.archive) as archive:
            name = 'Examples/Other/README.md'
            expected = b'[Fonts](../../manual/editor/runtime-ui.html#styles-fonts-and-images)\n'
            self.assertEqual(archive.read(name), expected)
            manifest = json.loads(archive.read('manifest.json'))['files']
            self.assertEqual(manifest[name], hashlib.sha256(expected).hexdigest())

    def test_missing_backend_notice_retains_previous_archive(self):
        self.package()
        before = self.archive.read_bytes()
        (self.dependencies/'box2d-src/LICENSE.txt').unlink()
        with self.assertRaisesRegex(ValueError, 'No license notice found'):
            self.package()
        self.assertEqual(self.archive.read_bytes(), before)

    def test_gallery_link_rejection_retains_previous_archive(self):
        link = self.gallery/'Assets/linked.resource'
        try:
            link.symlink_to(self.gallery/'README.md')
        except OSError as error:
            if getattr(error, 'winerror', None) == 1314 or error.errno in (errno.EPERM, errno.ENOSYS, errno.EOPNOTSUPP):
                self.skipTest('This host cannot create the link fixture: '+str(error))
            raise
        link.unlink()
        self.package()
        before = self.archive.read_bytes()
        link.symlink_to(self.gallery/'README.md')
        with patch.object(package_windows.subprocess, 'run') as native:
            with self.assertRaisesRegex(ValueError, 'Feature Gallery contains a link'):
                self.package(self.build/'unused-preparer.exe')
            native.assert_not_called()
        self.assertEqual(self.archive.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
