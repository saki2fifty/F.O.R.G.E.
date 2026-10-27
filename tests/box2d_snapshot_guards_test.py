"""Exercise the selected exact-source build repair without changing its checkout."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SOURCE, HELPER, CMAKE = (Path(arg).resolve() for arg in sys.argv[1:4])
del sys.argv[1:4]


class SnapshotGuardsTests(unittest.TestCase):
    def configure(self, root):
        return subprocess.run(
            [str(CMAKE), "-S", str(root), "-B", str(root / "build")],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )

    def fixture(self, root, source):
        (root / "src").mkdir()
        (root / "src/snapshot.c").write_bytes(source)
        (root / "src/other.c").write_text("void unrelated(void) {}\n")
        (root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            'project(SnapshotGuardTest C)\n'
            'set(box2d_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")\n'
            'add_library(box2d STATIC src/snapshot.c src/other.c)\n'
            f'include("{HELPER.as_posix()}")\n'
            'get_target_property(sources box2d SOURCES)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/sources.txt" "${sources}")\n'
        )

    def check_source(self, source):
        with tempfile.TemporaryDirectory(prefix="forge-box2d-guards-") as temp:
            root = Path(temp)
            self.fixture(root, source)
            result = self.configure(root)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual((root / "src/snapshot.c").read_bytes(), source)
            output = root / "build/forge-box2d/snapshot.c"
            text = output.read_text()
            for count in ("chainCount", "sensorCount", "islandCount"):
                self.assertEqual(text.count(f"if ( {count} > 0 )"), 1)
            expected = [output.as_posix(), (root / "src/other.c").as_posix()]
            self.assertEqual((root / "build/sources.txt").read_text().split(";"), expected)
            record = json.loads((output.parent / "source.json").read_text())
            digest = lambda value: hashlib.sha256(value).hexdigest()
            self.assertEqual(record["compiled_sha256"], digest(output.read_bytes()))
            self.assertEqual(record["guard_sha256"], digest(HELPER.read_bytes()))
            stamp = output.stat().st_mtime_ns
            result = self.configure(root)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual(output.stat().st_mtime_ns, stamp)
            return output.read_bytes()

    def test_lf_and_crlf_have_identical_generated_source(self):
        source = SOURCE.read_bytes().replace(b"\r\n", b"\n")
        self.assertEqual(self.check_source(source), self.check_source(source.replace(b"\n", b"\r\n")))

    def test_unreviewed_source_fails_closed(self):
        with tempfile.TemporaryDirectory(prefix="forge-box2d-guards-") as temp:
            root = Path(temp)
            self.fixture(root, SOURCE.read_bytes() + b"\n/* different revision */\n")
            result = self.configure(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("snapshot source changed", result.stdout)
            self.assertFalse((root / "build/forge-box2d/snapshot.c").exists())


if __name__ == "__main__":
    unittest.main()
