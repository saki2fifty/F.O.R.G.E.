"""Exercise the real ZIP writer with the complete curated Gallery and fixture tools."""
import errno
import hashlib
import json
from pathlib import Path
import shutil
import struct
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
        self.root = Path(self.scratch.name)
        self.source, self.build, self.dependencies = (self.root/name for name in ('source', 'build', 'dependencies'))
        self.build.mkdir()
        self.archive = self.root/'editor.zip'
        self.gallery = self.source/'samples/projects/FeatureGallery'
        shutil.copytree(SOURCE/'samples/projects/FeatureGallery', self.gallery)
        # An unrecognized extension must survive without another allowlist change.
        self.write(self.gallery/'Assets/next-format.resource', b'\x00\x01\r\n\xff')
        for name in ('flecs', 'json', 'sdl', 'imgui_source', 'cpp_text_editor', 'diligent',
                     'jolt', 'miniaudio', 'ozz', 'recast', 'rmlui', 'freetype', 'ktx',
                     'webp', 'meshoptimizer', 'draco'):
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
        self.write(self.source/'manual/README.md', b'# Package test\n\nOffline manual.\n')
        self.write(self.source/'samples/animation/two-joints.gltf', b'{}\n')
        self.write(self.source/'samples/projects/Other/main.scene.json', b'{}\n')
        self.write(self.source/'samples/projects/Other/build/local.obj', b'Not example content')

    @staticmethod
    def write(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def package(self):
        with patch.object(package_windows, '__file__', str(self.source/'tools/package_windows.py')):
            package_windows.package(self.build, self.dependencies, self.archive)

    def test_complete_gallery_bytes_and_manifest(self):
        self.package()
        expected = {p.relative_to(self.gallery).as_posix(): p.read_bytes()
                    for p in self.gallery.rglob('*') if p.is_file()}
        with zipfile.ZipFile(self.archive) as archive:
            self.assertIsNone(archive.testzip())
            manifest = json.loads(archive.read('manifest.json'))['files']
            actual = {name.removeprefix('Examples/FeatureGallery/')
                      for name in archive.namelist() if name.startswith('Examples/FeatureGallery/')}
            self.assertEqual(actual, set(expected))
            for relative, content in expected.items():
                name = 'Examples/FeatureGallery/'+relative
                self.assertEqual(archive.read(name), content, name)
                self.assertEqual(manifest[name], hashlib.sha256(content).hexdigest(), name)
            self.assertIn('Examples/Other/main.scene.json', manifest)
            self.assertNotIn('Examples/Other/build/local.obj', manifest)

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
        with self.assertRaisesRegex(ValueError, 'Feature Gallery contains a link'):
            self.package()
        self.assertEqual(self.archive.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
