"""Cache identities must isolate incompatible environments while allowing source iteration."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.dont_write_bytecode = True
source = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ci_cache_key', source/'tools/ci_cache_key.py')
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)


class CacheIdentity(unittest.TestCase):
    def test_invalidation(self):
        work = source.parent/'AgentFiles'
        work.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as directory, patch.object(
                cache.subprocess, 'check_output', return_value=b'pinned build tools'):
            root = Path(directory)
            (root/'CMakeLists.txt').write_text('project(FORGE)')
            (root/'CMakePresets.json').write_text('{}')
            (root/'cmake').mkdir()
            dependency = root/'cmake/dependencies.cmake'
            dependency.write_text('pinned dependency A')
            environment = dict(ImageVersion='image1', VCToolsVersion='msvc1',
                               WindowsSDKVersion='sdk1', RUNNER_ARCH='X64')
            first = cache.cache_key(root, environment)
            for key, value in [('FORGE_LINKAGE_PROFILE', 'shared-native-sdk'), ('FORGE_CRT_PROFILE', 'MultiThreadedDLL')]:
                self.assertNotEqual(first, cache.cache_key(root, dict(environment, **{key: value})))
            (root/'main.cpp').write_text('new product code')
            self.assertEqual(first, cache.cache_key(root, environment))
            for key in environment:
                changed = dict(environment, **{key: 'different'})
                self.assertNotEqual(first, cache.cache_key(root, changed))
            dependency.write_text('pinned dependency B')
            self.assertNotEqual(first, cache.cache_key(root, environment))
            with self.assertRaises(RuntimeError):
                cache.cache_key(root, {})
            with patch.object(cache.subprocess, 'check_output', return_value=b'changed tools'):
                changed_tools = cache.cache_key(root, environment)
            self.assertNotEqual(changed_tools, cache.cache_key(root, environment))


if __name__ == '__main__':
    unittest.main()
