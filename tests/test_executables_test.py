"""Catch absent native CTest helpers before running a long acceptance selection."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
source = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('check_test_executables', source/'tools/check_test_executables.py')
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)


class TestExecutables(unittest.TestCase):
    def test_native_commands_and_python_helpers(self):
        with tempfile.TemporaryDirectory(prefix='native test inventory ') as directory:
            build = Path(directory)
            main, helper = build/'forge_host', build/'forge_font_fixture.exe'
            main.touch()
            helper.touch()
            document = dict(tests=[
                dict(name='native', command=[str(main)]),
                dict(name='python', command=[sys.executable, 'supervisor.py', str(main),
                                             str(helper), str(build/'capture.json.gz')])])
            self.assertEqual(inventory.check_executables(document, build),
                             dict(tests=2, native_executables=2))
            helper.unlink()
            with self.assertRaisesRegex(ValueError, 'python: missing forge_font_fixture.exe'):
                inventory.check_executables(document, build)

    def test_unbuilt_native_command(self):
        with self.assertRaisesRegex(ValueError, 'native: native test command is unavailable'):
            inventory.check_executables(dict(tests=[dict(name='native')]), Path('.'))

    def test_empty_selection(self):
        with self.assertRaisesRegex(ValueError, 'No tests selected'):
            inventory.check_executables(dict(tests=[]), Path('.'))

    def test_scope_boundary(self):
        with self.assertRaisesRegex(ValueError, 'outside the required mcp_ scope'):
            inventory.check_executables(dict(tests=[dict(name='renderer', command=['renderer'])]),
                                        Path('.'), 'mcp_')


if __name__ == '__main__':
    unittest.main()
