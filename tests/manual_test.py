"""Validate the shipped manual, local links, escaping, and build identifiers."""
from pathlib import Path
import re
import sys
import tempfile
import unittest
from urllib.parse import unquote
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from build_identity import validate_build_id
from build_manual import build_manual, render


class ManualTests(unittest.TestCase):
    def test_actual_manual(self):
        work = ROOT.parent/'AgentFiles'
        work.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as temporary:
            output = Path(temporary)
            count = build_manual(ROOT/'manual', output, '260915-000006')
            self.assertGreaterEqual(count, 15)
            self.assertEqual(count, len(list(output.rglob('*.html'))))
            for page in output.rglob('*.html'):
                text = page.read_text(encoding='utf-8')
                self.assertIn('Build: 260915-000006', text)
                self.assertNotIn('<script', text)
                for url in re.findall(r'href="([^"]+)"', text):
                    target = (page.parent/unquote(url)).resolve()
                    self.assertTrue(target.is_relative_to(output.resolve()))
                    self.assertTrue(target.is_file(), f'{page}: {url}')

    def test_rendering(self):
        result = render('# A title\n\nA **bold** word and `<unsafe>` value.\n\n1. First\n2. Second\n\n```\n<script>\n```\n')
        self.assertIn('<h1>A title</h1>', result)
        self.assertIn('<strong>bold</strong>', result)
        self.assertIn('&lt;unsafe&gt;', result)
        self.assertIn('<ol>', result)
        self.assertNotIn('<script>', result)
        with self.assertRaises(ValueError):
            render('[unsafe](https://example.com)')
        with self.assertRaises(ValueError):
            render('```\nunclosed')

    def test_identity(self):
        for identity in ('260915-000006', '260916-000007', '270101-1000000'):
            self.assertEqual(identity, validate_build_id(identity))
        for identity in ('unassigned', '260915-000000', '260915-6', '260231-000007',
                         'alpha', '260915-000006-beta', '260915-000006\n'):
            with self.assertRaises(ValueError):
                validate_build_id(identity)


if __name__ == '__main__':
    unittest.main()
