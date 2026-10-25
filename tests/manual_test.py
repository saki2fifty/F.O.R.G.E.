"""Validate the shipped manual, local links, escaping, and build identifiers."""
from pathlib import Path
import base64
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
                self.assertEqual(len(re.findall(r'<a [^>]*aria-current="page"', text)), 1)
                for url in re.findall(r'(?:href|src)="([^"]+)"', text):
                    target = (page.parent/unquote(url)).resolve()
                    self.assertTrue(target.is_relative_to(output.resolve()))
                    self.assertTrue(target.is_file(), f'{page}: {url}')
            images = list((ROOT/'manual').rglob('*.png'))
            self.assertTrue(images, 'The user manual must include actual screenshots')
            for image in images:
                self.assertEqual(image.read_bytes(), (output/image.relative_to(ROOT/'manual')).read_bytes())

    def test_screenshots(self):
        png = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aZeUAAAAASUVORK5CYII=')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, output = root/'source', root/'output'
            (source/'images').mkdir(parents=True)
            (source/'editor').mkdir()
            (source/'images/screen shot.png').write_bytes(png)
            (source/'README.md').write_text('# Manual\n\n[Editor](editor/use.md)\n')
            page = source/'editor/use.md'
            page.write_text('# Editor\n\n![Scene <view> & "controls"](../images/screen shot.png)\n')
            self.assertEqual(build_manual(source, output, '260915-000006'), 2)
            rendered = (output/'editor/use.html').read_text()
            self.assertIn('src="../images/screen%20shot.png"', rendered)
            self.assertIn('alt="Scene &lt;view&gt; &amp; &quot;controls&quot;"', rendered)
            self.assertEqual((output/'images/screen shot.png').read_bytes(), png)
            # All references remain usable when the offline folder is relocated.
            relocated = root/'relocated'
            output.rename(relocated)
            for url in re.findall(r'(?:href|src)="([^"]+)"', rendered):
                self.assertTrue((relocated/'editor'/unquote(url)).is_file())
            (source/'images/screen shot.png').write_text('<html>not a PNG</html>')
            with self.assertRaisesRegex(ValueError, 'Invalid PNG'):
                build_manual(source, output, '260915-000006')
            page.write_text('# Editor\n\n![Missing](missing.png)\n')
            with self.assertRaisesRegex(ValueError, 'Broken/outside'):
                build_manual(source, output, '260915-000006')

    def test_manual_is_self_contained(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root/'source'
            source.mkdir()
            (root/'outside.md').write_text('# Outside\n')
            (source/'README.md').write_text('# Manual\n\n[Outside](../outside.md)\n')
            with self.assertRaisesRegex(ValueError, 'Broken/outside'):
                build_manual(source, root/'output', '260915-000006')
            (root/'outside.png').write_bytes(b'\x89PNG\r\n\x1a\n')
            (source/'README.md').write_text('# Manual\n\n![Outside](../outside.png)\n')
            with self.assertRaisesRegex(ValueError, 'Broken/outside'):
                build_manual(source, root/'output', '260915-000006')
            (source/'README.md').write_text('# Manual\n')
            with self.assertRaisesRegex(ValueError, 'separate folders'):
                build_manual(source, source/'output', '260915-000006')
        for url in ('https://example.com/a.png', '//host/a.png', '/a.png',
                    'C:\\a.png', 'data:image/png;base64,xxx', 'a.svg'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                render(f'![Image]({url})')

    def test_contents_order_and_navigation(self):
        with tempfile.TemporaryDirectory() as temporary:
            source, output = Path(temporary)/'source', Path(temporary)/'output'
            source.mkdir()
            (source/'README.md').write_text(
                '# Manual\n\n## Start here\n\n- [Z first](z.md)\n- [A second](a & b.md)\n')
            for filename, title in (('z.md', 'First guide'), ('a & b.md', 'Second guide'),
                                    ('other.md', 'Unlisted guide')):
                (source/filename).write_text(f'# {title}\n')
            build_manual(source, output, '260915-000006')
            text = (output/'z.html').read_text()
            nav = text.split('<nav aria-label="Manual">', 1)[1].split('</nav>', 1)[0]
            self.assertLess(nav.index('First guide'), nav.index('Second guide'))
            self.assertLess(nav.index('Second guide'), nav.index('More guides'))
            self.assertIn('href="a%20%26%20b.html"', nav)
            self.assertIn('href="z.html" aria-current="page"', nav)
            self.assertIn('Unlisted guide', nav)

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
