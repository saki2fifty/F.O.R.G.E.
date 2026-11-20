"""Exercise the actual Linux CI compiler setup without root or network access."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]


def compiler_script():
    workflow = (SOURCE / '.github/workflows/build.yml').read_text()
    section = workflow.split('      - name: Prepare required Linux compiler\n', 1)[1]
    assert section.startswith("        if: runner.os == 'Linux'\n        timeout-minutes: 10\n")
    body = section.split('        run: |\n', 1)[1]
    lines = []
    for line in body.splitlines():
        if not line.startswith('          '):
            break
        lines.append(line[10:])
    assert lines
    return '\n'.join(lines) + '\n'


@unittest.skipUnless(os.name == 'posix' and Path('/bin/bash').is_file(),
                     'The Linux compiler setup uses Bash and system packages')
class CompilerSetup(unittest.TestCase):
    def exercise(self, initial=None, mode='success', installed='12.4.0'):
        with tempfile.TemporaryDirectory(prefix='forge compiler setup ') as temporary:
            root = Path(temporary)
            binaries = root / 'bin'
            binaries.mkdir()
            calls = root / 'calls'
            calls.touch()
            def compiler(path, version):
                path.write_text('#!/bin/bash\nexit 127\n' if version is None else
                    '#!/bin/bash\nprintf "%s\\n" ' + shlex.quote(version) + '\n')
                path.chmod(0o755)
            for name, version in (initial or {}).items():
                compiler(binaries / name, version)
            for name in ('gcc-12', 'g++-12'):
                compiler(root / name, installed)
            sudo = binaries / 'sudo'
            sudo.write_text('''#!/bin/bash
set -eu
printf '%s\\n' "$*" >> "$FORGE_FAKE_CALLS"
case " $* " in
  *' update ')
    case "$FORGE_FAKE_MODE" in fail) exit 42;; timeout) exit 124;; esac ;;
  *' install '*)
    if [[ "$FORGE_FAKE_MODE" == install_fail ]]; then exit 43; fi
    /bin/cp "$FORGE_FAKE_ROOT/gcc-12" "$FORGE_FAKE_ROOT/bin/gcc-12"
    /bin/cp "$FORGE_FAKE_ROOT/g++-12" "$FORGE_FAKE_ROOT/bin/g++-12" ;;
  *) exit 99 ;;
esac
''')
            sudo.chmod(0o755)
            result = subprocess.run(['/bin/bash', '-c', compiler_script()],
                env=dict(os.environ, PATH=str(binaries), FORGE_FAKE_CALLS=str(calls),
                         FORGE_FAKE_MODE=mode, FORGE_FAKE_ROOT=str(root)),
                capture_output=True, text=True, timeout=10)
            return result, calls.read_text().splitlines()

    def test_installed_pair_avoids_network(self):
        result, calls = self.exercise({'gcc-12': '12', 'g++-12': '12.4.0'})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls, [])
        self.assertIn('C++ compiler:', result.stdout)

    def test_missing_pair_installs_with_bounded_network(self):
        result, calls = self.exercise()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertIn('timeout --kill-after=10s 240s', call)
            self.assertIn('DEBIAN_FRONTEND=noninteractive', call)
            for option in ('Acquire::Retries=2', 'Acquire::http::Timeout=30',
                           'Acquire::https::Timeout=30', 'DPkg::Lock::Timeout=120'):
                self.assertIn(option, call)
        self.assertTrue(calls[0].endswith(' update'))
        self.assertTrue(calls[1].endswith(' install -y g++-12'))

    def test_partial_pair_installs(self):
        result, calls = self.exercise({'g++-12': '12.4.0'})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 2)

    def test_wrong_major_requires_installation(self):
        result, calls = self.exercise({'gcc-12': '13.2.0', 'g++-12': '13.2.0'})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 2)

    def test_broken_compiler_requires_installation(self):
        result, calls = self.exercise({'gcc-12': None, 'g++-12': '12.4.0'})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 2)

    def test_failed_installation_stops_setup(self):
        result, calls = self.exercise(mode='install_fail')
        self.assertEqual(result.returncode, 43, result.stderr)
        self.assertEqual(len(calls), 2)

    def test_failed_update_stops_before_install(self):
        for mode, status in (('fail', 42), ('timeout', 124)):
            with self.subTest(mode=mode):
                result, calls = self.exercise(mode=mode)
                self.assertEqual(result.returncode, status, result.stderr)
                self.assertEqual(len(calls), 1)

    def test_setup_cannot_accept_wrong_compiler(self):
        result, calls = self.exercise(installed='13.2.0')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(len(calls), 2)
        self.assertIn('Required GCC 12 C/C++ compiler pair', result.stderr)


if __name__ == '__main__':
    unittest.main()
