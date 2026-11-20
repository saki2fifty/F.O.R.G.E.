"""Package reuse preserves native inputs and rejects changed product sources."""
import unittest
from package_fixture_policy import PACKAGE_HELPERS, fixture_rebuild_required


class FixturePolicyTests(unittest.TestCase):
    def test_unchanged_fixture_reuses_source_artifact(self):
        self.assertFalse(fixture_rebuild_required([]))

    def test_package_helpers_do_not_require_compilation(self):
        self.assertFalse(fixture_rebuild_required(sorted(PACKAGE_HELPERS)))

    def test_documentation_and_workflow_reuse_artifact(self):
        self.assertFalse(fixture_rebuild_required(
            ['docs/ci.md', '.github/workflows/build.yml', 'tests/mcp_editor_package_test.py']))

    def test_native_fixture_changes_still_require_rebuild(self):
        for path in ['tests/editor_fixture.hpp', 'tests/viewport_render_tests.cpp',
                     'tests/input.h', 'tests/fixture.inc']:
            with self.subTest(path=path):
                self.assertTrue(fixture_rebuild_required(['docs/ci.md', path]))

    def test_unfamiliar_inputs_and_generators_are_conservative(self):
        for path in ['tests/fixture.json', 'tests/fixture.png', 'tests/generate_fixture.py']:
            with self.subTest(path=path):
                self.assertTrue(fixture_rebuild_required([path]))

    def test_product_and_dependency_changes_are_rejected(self):
        for path in ['src/editor/main.cpp', 'include/forge/native_sdk.h',
                     'CMakeLists.txt', 'cmake/editor.cmake', 'manual/README.md']:
            with self.subTest(path=path):
                with self.assertRaisesRegex(ValueError, 'changed product inputs'):
                    fixture_rebuild_required([path])

    def test_noncanonical_paths_are_rejected(self):
        for path in ['', '/tests/helper.py', '../docs/ci.md',
                     'tests/../src/main.cpp', 'tests\\helper.py']:
            with self.subTest(path=path):
                with self.assertRaisesRegex(ValueError, 'noncanonical'):
                    fixture_rebuild_required([path])


if __name__ == '__main__':
    unittest.main()
