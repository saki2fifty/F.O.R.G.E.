"""Render FORGE's deliberately small Markdown subset as an offline user manual.

Supported: headings, paragraphs, flat numbered/bullet lists, fenced code, inline
code, bold text, relative links and local PNG screenshots. No raw HTML, scripts,
network resources, or third-party renderer. Unknown block forms fail rather than
silently disappearing.
"""
import argparse
import html
import os
import re
import shutil
from pathlib import Path
from urllib.parse import quote


CSS = '''*{box-sizing:border-box}body{margin:0;background:#0c1118;color:#e2e8f0;font:17px/1.7 system-ui,sans-serif}a{color:#82bcff}nav{position:fixed;inset:0 auto 0 0;width:270px;padding:28px 22px;overflow:auto;background:#111923;border-right:1px solid #293646}nav a{display:block;padding:5px 0;text-decoration:none}nav strong{display:block;margin:20px 0 8px;color:#a6b4c6;font-size:14px;text-transform:uppercase}main{max-width:1160px;margin-left:270px;padding:40px 56px 80px}h1{font-size:36px;line-height:1.2}h1,h2{border-bottom:1px solid #293646;padding-bottom:14px}h2{margin-top:42px;font-size:25px}h3{margin-top:30px;font-size:20px}p,li{max-width:850px}li{margin:9px 0}code{background:#1c2938;padding:2px 5px;border-radius:4px}pre{overflow:auto;padding:18px;background:#141e2a}pre code{padding:0;background:none}.edition{color:#a6b4c6;font-size:14px}a:focus-visible{outline:2px solid #82bcff;outline-offset:3px}@media(max-width:850px){nav{position:static;width:auto;max-height:35vh;border-bottom:1px solid #293646}main{margin:0;padding:24px}}@media print{nav{display:none}main{margin:0;padding:0}body{background:white;color:black}a{color:inherit}}'''
CSS += '\nimg{display:block;max-width:100%;height:auto;margin:16px 0;border:1px solid #293646}nav a[aria-current="page"]{font-weight:bold;text-decoration:underline}'


def local_url(url, suffix):
    if ':' in url or '\\' in url or url.startswith('/') or not url.endswith(suffix):
        raise ValueError(f'Manual references must target local {suffix} files: {url}')
    return html.escape(quote(url, safe='/.-'))


def inline(text):
    # Escape first; only our own markup may enter the output.
    result, end = [], 0
    pattern = r'`([^`]+)`|\*\*([^*]+)\*\*|(!?)\[([^\]]+)\]\(([^)]+)\)'
    for match in re.finditer(pattern, text):
        result.append(html.escape(text[end:match.start()]))
        if match[1] is not None:
            result.append('<code>' + html.escape(match[1]) + '</code>')
        elif match[2] is not None:
            result.append('<strong>' + html.escape(match[2]) + '</strong>')
        else:
            url, label = match[5], html.escape(match[4])
            if match[3]:
                target = local_url(url, '.png')
                result.append(f'<a href="{target}"><img src="{target}" alt="{label}" loading="lazy"></a>')
            else:
                local_url(url, '.md')
                target = url[:-3] + '.html'
                if target.endswith('README.html'):
                    target = target[:-11] + 'index.html'
                result.append(f'<a href="{html.escape(quote(target, safe="/.-"))}">{label}</a>')
        end = match.end()
    result.append(html.escape(text[end:]))
    return ''.join(result)


def render(text):
    output, paragraph, listing, code = [], [], None, None
    def flush():
        if paragraph:
            output.append('<p>' + inline(' '.join(paragraph)) + '</p>')
            paragraph.clear()
    for line in text.splitlines() + ['']:
        if line.startswith('```'):
            flush()
            if listing:
                output.append(f'</{listing}>')
                listing = None
            if code is None:
                code = []
            else:
                output.append('<pre><code>' + html.escape('\n'.join(code)) + '</code></pre>')
                code = None
            continue
        if code is not None:
            code.append(line)
            continue
        item = re.match(r'^(?:- |\d+\. )(.*)', line)
        kind = ('ul' if line.startswith('- ') else 'ol') if item else None
        if listing and listing != kind:
            output.append(f'</{listing}>')
            listing = None
        if item:
            flush()
            if not listing:
                listing = kind
                output.append(f'<{listing}>')
            output.append('<li>' + inline(item[1]) + '</li>')
        elif not line.strip():
            flush()
        elif line.startswith('#'):
            flush()
            heading = re.fullmatch(r'(#{1,3}) (.+)', line)
            if not heading:
                raise ValueError(f'Invalid heading: {line}')
            level = len(heading[1])
            output.append(f'<h{level}>' + inline(heading[2]) + f'</h{level}>')
        else:
            if line.startswith((' ', '|', '>', '<')):
                raise ValueError(f'Unsupported manual block: {line}')
            paragraph.append(line)
    if code is not None:
        raise ValueError('Unclosed code fence')
    return '\n'.join(output)


def build_manual(source, output, build_id):
    source, output = Path(source), Path(output)
    if output.resolve().is_relative_to(source.resolve()) or source.resolve().is_relative_to(output.resolve()):
        raise ValueError('Manual source and output must be separate folders')
    pages = sorted(source.rglob('*.md'))
    if not pages or not (source/'README.md').is_file():
        raise ValueError('Manual index missing')
    entries, images = [], set()
    for page in pages:
        if not page.resolve().is_relative_to(source.resolve()):
            raise ValueError(f'Outside manual page: {page}')
        text = page.read_text(encoding='utf-8')
        if not text.startswith('# '):
            raise ValueError(f'Missing page title: {page}')
        for target in re.findall(r'\[[^\]]+\]\(([^)]+)\)', text):
            resolved = (page.parent/target).resolve()
            if not resolved.is_relative_to(source.resolve()) or not resolved.is_file():
                raise ValueError(f'Broken/outside manual link: {page}: {target}')
        for target in re.findall(r'!\[[^\]]+\]\(([^)]+)\)', text):
            local_url(target, '.png')
            image = page.parent/target
            with image.open('rb') as stream:
                if stream.read(8) != b'\x89PNG\r\n\x1a\n':
                    raise ValueError(f'Invalid PNG screenshot: {image}')
            images.add(Path(os.path.relpath(image, source)))
        relative = page.relative_to(source)
        destination = relative.with_suffix('.html')
        if relative.name == 'README.md':
            destination = relative.with_name('index.html')
        entries.append((page, destination, text.splitlines()[0][2:], render(text)))
    # The manual home page owns task order; don't maintain a second contents list.
    by_path = {page.relative_to(source).as_posix(): (destination, title)
               for page, destination, title, _ in entries}
    navigation_entries = [('', *by_path['README.md'])]
    seen, group = {'README.md'}, ''
    for line in (source/'README.md').read_text(encoding='utf-8').splitlines():
        if line.startswith('## '):
            group = line[3:]
        item = re.fullmatch(r'- \[[^\]]+\]\(([^)]+)\)', line)
        if item and item[1] in by_path and item[1] not in seen:
            navigation_entries.append((group, *by_path[item[1]]))
            seen.add(item[1])
    # Keep pages discoverable while an author is preparing a new contents entry.
    for path, (destination, title) in by_path.items():
        if path not in seen:
            navigation_entries.append(('More guides', destination, title))
    for page, destination, title, body in entries:
        prefix = '../' * (len(destination.parts) - 1)
        navigation = '<strong>FORGE User Manual</strong>'
        group = None
        for next_group, target, name in navigation_entries:
            if next_group != group:
                if next_group:
                    navigation += '<strong>' + html.escape(next_group) + '</strong>'
                group = next_group
            current = ' aria-current="page"' if target == destination else ''
            url = html.escape(quote(prefix + target.as_posix(), safe='/.-'))
            navigation += f'<a href="{url}"{current}>{html.escape(name)}</a>'
        rendered = f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(title)} — FORGE</title><style>{CSS}</style></head>
<body><nav aria-label="Manual">{navigation}</nav><main><p class="edition">Build: {html.escape(build_id)} · FORGE User Manual</p>{body}</main></body></html>'''
        target = output/destination
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(rendered, encoding='utf-8')
        (output/page.relative_to(source)).write_bytes(page.read_bytes())
    for image in images:
        target = output/image
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source/image, target)
    return len(entries)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1]/'manual')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build-id', required=True)
    args = parser.parse_args()
    print('Manual pages:', build_manual(args.source, args.output, args.build_id))
