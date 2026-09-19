"""Explicit pinned upstream defect; never a general sanitizer exception/suppression.

Every instrumented failed worker must emit exactly the identified native buffer
leak, reject its result, and exit. A clean result is XPASS requiring removal of
this exception. Ordinary Script tests remain strict and clean.
"""
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import time

exe, publication = (Path(p).resolve() for p in sys.argv[1:3])
sanitized = sys.argv[3] == 'ON'
pin = 'fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8'
assert pin in (Path(__file__).parents[1] / 'cmake/dependencies.cmake').read_text(), \
    'Re-evaluate/remove the known-include exception when changing the Flecs pin'
evidence = Path.cwd() / 'Testing/flecs-script-known-include'
evidence.mkdir(parents=True, exist_ok=True)
ansi = re.compile(r'\x1b\[[0-9;]*m')
findings = re.compile(r'LeakSanitizer|AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:|DEADLYSIGNAL')

def launch(arguments, cwd, label):
    out, err = evidence / (label + '.stdout'), evidence / (label + '.stderr')
    with out.open('wb') as stdout, err.open('wb') as stderr:
        process = subprocess.Popen([str(a) for a in arguments], cwd=cwd, stdout=stdout, stderr=stderr)
        deadline = time.monotonic() + 30
        try:
            while process.poll() is None:
                assert time.monotonic() < deadline, 'Worker exceeded bounded execution time'
                # ASan shadow mappings cannot fit the production 512MiB virtual
                # address-space limit. Bound resident memory in this instrumented
                # test host; normal supervisor/resource limits are tested below.
                status = Path('/proc') / str(process.pid) / 'status'
                if status.exists():
                    rss = re.search(r'^VmRSS:\s+(\d+) kB', status.read_text(), re.M)
                    assert not rss or int(rss[1]) <= 512 * 1024, 'Worker exceeded 512MiB resident bound'
                assert out.stat().st_size + err.stat().st_size <= 16 * 1024 * 1024, 'Worker output exceeded bound'
                time.sleep(.01)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
    return process.returncode, out.read_text(errors='replace'), err.read_text(errors='replace')

def require_known(report, expected_bytes):
    report = ansi.sub('', report)
    assert report.count('ERROR: LeakSanitizer: detected memory leaks') == 1, report
    assert report.count('Direct leak of ') == 1 and 'Indirect leak' not in report, report
    assert re.search(rf'Direct leak of {expected_bytes} byte\(s\) in 1 object\(s\)', report), report
    assert re.search(rf'SUMMARY: AddressSanitizer: {expected_bytes} byte\(s\) leaked in 1 allocation\(s\)\.', report), report
    # The exact allocation call chain is the exception. No stack wildcard for
    # arbitrary Flecs or FORGE allocations; changed signatures fail closed.
    frames = re.findall(r'^\s*#\d+\s+.*$', report, re.M)
    assert len(frames) >= 7, report
    for frame, name in zip(frames[1:5], ('ecs_os_api_malloc', 'flecs_load_from_file', 'ecs_script_init', 'flecs_script_eval_include')):
        assert name in frame, report
    assert 'src/addons/script/script.c:290' in frames[3], report
    assert 'src/addons/script/visit_eval.c:1822' in frames[4], report
    assert any('forge::evaluate_flecs_script_worker' in f for f in frames), report
    assert not re.search(r'runtime error:|UndefinedBehaviorSanitizer|DEADLYSIGNAL|ERROR: AddressSanitizer|heap-use|buffer-overflow|double-free|alloc-dealloc', report), report
    # Reject extra sanitizer reports even if they use an unfamiliar wording.
    assert len(findings.findall(report)) == 2, report

results = []
with tempfile.TemporaryDirectory(prefix='forge-known-include-', dir=Path.cwd()) as temporary:
    root = Path(temporary)
    project = root / 'project'
    (project / 'Assets').mkdir(parents=True)
    source = 'include included.flecs\nMain {}\n'
    (project / 'Assets/main.flecs').write_text(source)
    def evaluate(included, label, expect_leak=False):
        (project / 'Assets/included.flecs').write_text(included)
        with tempfile.TemporaryDirectory(prefix='candidate-', dir=root) as candidate:
            staging = Path(candidate)
            (staging / 'request.json').write_text(json.dumps({
                'project': str(project), 'source': 'Assets/main.flecs', 'code': source, 'previous': ''}))
            rc, out, err = launch([exe, '--script-worker'], staging, label)
            result_file = staging / 'result.json'
            assert result_file.is_file(), (rc, out, err)
            result = json.loads(result_file.read_text())
            if expect_leak:
                assert result['ok'] is False and result['source'] == 'Assets/included.flecs', result
                assert 'entities' not in result, 'Failed candidate exported generated state'
                pub_rc, pub_out, pub_err = launch([publication, '--script-publication', result_file], root, label + '-publication')
                assert pub_rc == 0 and not findings.search(pub_err), pub_out + pub_err
                if sanitized:
                    assert rc == 1, 'XPASS or changed worker termination: re-evaluate the exception\n' + out + err
                    # Pinned flecs_load_from_file allocates exact file bytes + NUL.
                    require_known(err, len(included.encode()) + 1)
                    print('EXPECTED pinned upstream finding (not clean LSan):\n' + err, flush=True)
                else:
                    assert rc == 0 and not findings.search(err), out + err
            else:
                assert rc == 0 and result['ok'] is True and not findings.search(err), out + err
        assert not staging.exists(), 'Terminated candidate staging was not cleaned'
        results.append({'case': label, 'worker_exited': True, 'staging_cleaned': True,
                        'accepted': result['ok'], 'exit_code': rc,
                        'classification': 'EXPECTED_PINNED_UPSTREAM_LEAK' if sanitized and expect_leak else 'CLEAN'})
    evaluate('Included {}\n', 'initial-clean')
    # Same exact failure path at two file sizes. 18 bytes is not a maximum.
    for label, body in [('minimal', 'MissingType: {1}\n'),
                        ('larger', '// ' + 'a' * 4096 + '\nMissingType: {1}\n')]:
        evaluate(body, label, True)
        evaluate('Included {}\n', label + '-recovery')
    if not sanitized:
        # Exercise the actual production supervisor/RAII cleanup, rather than
        # mistaking the instrumented direct-worker harness for process limits.
        for label, body, expected in [('supervisor-clean', 'Included {}\n', 0),
                                      ('supervisor-reject', 'MissingType: {1}\n', 1),
                                      ('supervisor-recover', 'Included {}\n', 0)]:
            (project / 'Assets/included.flecs').write_text(body)
            rc, out, err = launch([exe, '--script', project, 'Assets/main.flecs'], root, label)
            assert rc == expected and not findings.search(err), out + err
            assert json.loads(out)['ok'] is (expected == 0), out
            assert not any((project / '.forge/cache/script').iterdir()), 'Production staging leaked'
        results.append({'production_supervisor': 'success / reject / recover; staging cleaned'})
(evidence / 'result.json').write_text(json.dumps({'pin': pin, 'sanitized': sanitized, 'cases': results}, indent=2) + '\n')
print('FORGE sanitizer failures: 0; unexpected upstream findings: 0; ' +
      ('known pinned Flecs issue: EXPECTED managed-include buffer leak (no suppression)' if sanitized else
       'normal include rejection/publication/worker exit/recovery checks passed'))
