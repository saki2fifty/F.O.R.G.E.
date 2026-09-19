"""Pinned Script worker: language, managed ownership, diagnostics and containment."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
base = '''using flecs.meta
struct Position {
  x { member: {f32} }
  y { member: {f32} }
}
Old { Position: {10, 20} }
'''
with tempfile.TemporaryDirectory(prefix='forge-script-', dir=Path.cwd()) as temp:
    root = Path(temp)
    project = root / 'project'
    (project / 'Assets').mkdir(parents=True)
    staging = root / 'worker'
    staging.mkdir()
    def run(code, previous=''):
        request = {'project': str(project), 'source': 'Assets/main.flecs', 'code': code, 'previous': previous}
        (staging / 'request.json').write_text(json.dumps(request))
        result_path = staging / 'result.json'
        result_path.unlink(missing_ok=True)
        child = subprocess.run([str(exe), '--script-worker'], cwd=staging, capture_output=True, text=True, timeout=35)
        assert child.returncode == 0, child.stdout + child.stderr
        return json.loads(result_path.read_text())
    result = run(base)
    assert result['ok'], result
    assert result['world'] == 'isolated Preview'
    old = json.dumps(result['entities'])
    assert 'Old' in old and 'Position' in old, result
    # Grammar from the pinned Eval and Template suites, through FORGE's worker.
    procedural = base + '\nusing flecs.script.math\n' + '''
Tag {}
template Marker {
  prop height = flecs.meta.f32: 0
  Position: {$height, 2}
  Tag
}
for i in 0..3 {
  "marker_{$i}" { Marker: {$i + 1} }
}
Math { Position: {sqrt(16), sin(0)} }
if 2 > 1 { Conditional {} }
Parent { Child {} }
'''
    result = run(procedural)
    assert result['ok'], result
    generated = json.dumps(result['entities'])
    for name in ('marker_0', 'marker_1', 'marker_2', 'Math', 'Conditional', 'Child'):
        assert name in generated, (name, result)
    replacement = base.replace('Old', 'New').replace('10, 20', '30, 40')
    result = run(replacement, base)
    assert result['ok'] and result['managed_update'], result
    text = json.dumps(result['entities'])
    assert 'New' in text and 'Old' not in text, result
    invalid = run('Broken {', base)
    assert not invalid['ok'] and invalid['line'] > 0, invalid
    invalid = run('MissingType: {1}', base)
    assert not invalid['ok'], invalid
    (project / 'Assets' / 'included.flecs').write_text('Included {}\n')
    included = run('include included.flecs\nMain {}\n')
    assert included['ok'] and 'Assets/included.flecs' in included['includes'] and 'Included' in json.dumps(included['entities']), included
    # Failed managed file includes have a dedicated, visible pinned-upstream
    # regression. All cases in this ordinary suite must remain sanitizer-clean.
    outside = root / 'outside.flecs'
    outside.write_text('Outside {}\n')
    invalid = run('include ../outside.flecs\n')
    assert not invalid['ok'], invalid
    invalid = run('include ' + outside.as_posix() + '\n')
    assert not invalid['ok'], invalid
    try:
        (project / 'Assets' / 'link.flecs').symlink_to(outside)
    except OSError:
        pass  # Windows symlink privilege is checked separately by path tests.
    else:
        invalid = run('include link.flecs\n')
        assert not invalid['ok'], invalid
    invalid = run('Value {}\0Hidden {}')
    assert not invalid['ok'], invalid
    assert not run(base, 'Previous {}\0Hidden {}')['ok']
    assert not run(' ' * (1024 * 1024 + 1))['ok']
    (project / 'Assets' / 'huge.flecs').write_text(' ' * (1024 * 1024 + 1))
    assert not run('include huge.flecs\n')['ok']
    many = []
    for n in range(128):
        (project / 'Assets' / f'count{n}.flecs').write_text(f'Count{n} {{}}\n')
        many.append(f'include count{n}.flecs\n')
    assert not run(''.join(many))['ok'], 'Root plus128 included files exceeded the128-file budget'
    total = []
    for n in range(9):
        (project / 'Assets' / f'bytes{n}.flecs').write_text('// ' + 'a' * (960 * 1024) + f'\nBytes{n} {{}}\n')
        total.append(f'include bytes{n}.flecs\n')
    assert not run(''.join(total))['ok'], 'Total source input exceeded8MiB'
    assert run(base)['ok'], 'A failed independent candidate poisoned a subsequent worker'
print('Script language, managed update, errors, includes and containment passed')
