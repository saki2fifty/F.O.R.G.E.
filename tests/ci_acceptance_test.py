"""Keep the full native editor gate inclusive of its focused shader gate."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(add_help=False)
parser.add_argument('--ctest', default=shutil.which('ctest'))
options, arguments = parser.parse_known_args()


class AcceptanceSelection(unittest.TestCase):
    def test_shader_checks_are_required_by_full_editor_gate(self):
        self.assertTrue(options.ctest, 'CTest is required to validate its actual regex syntax')
        workflow = (SOURCE / '.github/workflows/editor-audit.yml').read_text(encoding='utf-8')
        selected = re.search(r"FORGE_SHADER_ACCEPTANCE_REGEX: '([^']+)'", workflow)
        self.assertIsNotNone(selected, 'Focused shader selection is missing')
        full = (SOURCE / 'tools/editor_acceptance_regex.txt').read_text(encoding='utf-8').strip()
        focused = re.fullmatch(r'\^\(([\w|-]+)\)\$', selected.group(1))
        self.assertIsNotNone(focused, 'Expected the focused gate to list exact test names')
        # Read names from the authoritative selection, including names expanded
        # from CMake loops. Parsing only literal add_test declarations misses them.
        names = set(focused.group(1).split('|'))
        with tempfile.TemporaryDirectory(prefix='forge acceptance selection ') as temporary:
            listing = Path(temporary) / 'CTestTestfile.cmake'
            listing.write_text(''.join(
                f'add_test("{name}" [==[{sys.executable}]==] "--version")\n'
                for name in sorted(names)), encoding='utf-8')

            def discover(expression):
                result = subprocess.run([options.ctest, '--test-dir', temporary,
                                         '--show-only=json-v1', '-R', expression],
                                        capture_output=True, text=True, encoding='utf-8', timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return {test['name'] for test in json.loads(result.stdout)['tests']}

            shader_names = discover(selected.group(1))
            full_names = discover(full)
            self.assertEqual(shader_names, names)
            self.assertEqual(sorted(shader_names - full_names), [],
                             'The full editor gate omits focused shader checks')


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *arguments])
