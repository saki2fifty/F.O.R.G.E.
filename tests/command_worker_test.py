"""Exercise the packaged private supervisor; no shell commands or editor reaper."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import tempfile
import time


def wait_file(path):
    deadline = time.monotonic() + 5
    while not path.exists():
        assert time.monotonic() < deadline, ('Fixture handshake timed out', path.name)
        time.sleep(.01)


def target(root, mode, extra):
    root = Path(root)
    if mode == 'echo':
        print(json.dumps({'args': extra, 'cwd': os.getcwd(),
                          'environment': os.environ.get('FORGE_COMMAND_FIXTURE'),
                          'stdin': sys.stdin.read()}, ensure_ascii=False), flush=True)
        print('merged target stderr', file=sys.stderr, flush=True)
        return 7
    if mode == 'leaf':
        (root / 'leaf.pid').write_text(str(os.getpid()))
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            if (root / 'retired').exists():
                (root / 'late-write').write_text('unexpected live writer')
                break
            time.sleep(.005)
        return 0
    if mode == 'output':
        (root / 'ready').touch()
        for _ in range(512):
            os.write(1, b'x' * 8192)
        return 0
    subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--target', str(root), 'leaf'],
                     start_new_session=(os.name != 'nt' and mode == 'escape'), close_fds=True)
    wait_file(root / 'leaf.pid')
    (root / 'ready').touch()
    if mode == 'natural':
        return 7
    time.sleep(5)  # Finite fixture fallback if supervision fails.
    return 0


def start(worker, args, root, environment=None):
    process = subprocess.Popen([str(worker), '--command-worker'], cwd=root, env=environment,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    data = json.dumps({'version': 1, 'args': args}, ensure_ascii=False).encode('utf-8')
    process.stdin.write(struct.pack('>I', len(data)) + data)
    process.stdin.flush()
    return process


def collect(process):
    # The helper writes the target directly into stdout. Wait without draining:
    # cancellation must still work with that output pipe completely full.
    try:
        assert process.wait(timeout=8) == 0, 'Helper failed without confirmed retirement'
        result = json.loads(process.stderr.read())
        assert result['version'] == 1 and result['retired'] is True, result
        return result, process.stdout.read()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        if not process.stdin.closed:
            process.stdin.close()
        process.stdout.close()
        process.stderr.close()


def run(worker, parent):
    script = str(Path(__file__).resolve())
    # Real process argv/cwd/environment and exit status, including empty values
    # and characters needing Windows C-runtime quoting. No shell is involved.
    with tempfile.TemporaryDirectory(prefix='command paths café ', dir=parent) as name:
        root = Path(name)
        values = ['', 'two words', 'café日本語', '"quote"', 'trailing\\', 'slash\\"quote',
                  '$(literal); & % PATH']
        env = dict(os.environ, FORGE_COMMAND_FIXTURE='value café 日本語', PYTHONIOENCODING='utf-8')
        process = start(worker, [sys.executable, script, '--target', name, 'echo', *values], root, env)
        result, stdout = collect(process)
        lines = stdout.decode('utf-8').splitlines()
        assert result['exit_code'] == 7 and not result['cancelled'] and not result['error'], result
        echoed = json.loads(lines[0])
        assert echoed == {'args': values, 'cwd': name, 'environment': env['FORGE_COMMAND_FIXTURE'],
                          'stdin': ''}, echoed
        assert lines[1:] == ['merged target stderr'], lines

    for mode, control in [('cancel', 'byte'), ('escape', 'eof'), ('natural', None),
                          ('output', 'byte')]:
        with tempfile.TemporaryDirectory(prefix='command-retirement-', dir=parent) as name:
            root = Path(name)
            process = start(worker, [sys.executable, script, '--target', name, mode], root)
            try:
                wait_file(root / 'ready')
                if control == 'byte':
                    process.stdin.write(b'cancel')
                    process.stdin.flush()
                elif control == 'eof':
                    process.stdin.close()
                result, stdout = collect(process)
                assert result['exit_code'] == (7 if mode == 'natural' else 130), result
                assert result['cancelled'] == (mode != 'natural') and not result['error'], result
                (root / 'retired').touch()
                time.sleep(.15)
                assert not (root / 'late-write').exists(), mode
                if sys.platform.startswith('linux') and (root / 'leaf.pid').exists():
                    assert not Path('/proc', (root / 'leaf.pid').read_text()).exists(), mode
            finally:
                if process.poll() is None:
                    # Ask the real supervisor to retire before removing fixtures.
                    process.stdin.close()
                    collect(process)

    with tempfile.TemporaryDirectory(prefix='command-failures-', dir=parent) as name:
        root = Path(name)
        result, stdout = collect(start(worker, [str(root / 'missing-tool')], root))
        assert result['exit_code'] != 0 and not stdout, result
        for record in (b'', struct.pack('>I', 1024 * 1024 + 1),
                       struct.pack('>I', 5) + b'{bad}',
                       struct.pack('>I', 100) + b'partial'):
            process = subprocess.Popen([str(worker), '--command-worker'], cwd=root,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE)
            process.stdin.write(record)
            process.stdin.close()
            result, stdout = collect(process)
            assert result['exit_code'] != 0 and result['error'] and not stdout, result
    print('Command supervisor: argv/cwd/environment, natural/cancel/EOF retirement, '
          'escaped descendant, full output and failed launch passed')


if __name__ == '__main__':
    if sys.argv[1] == '--target':
        raise SystemExit(target(sys.argv[2], sys.argv[3], sys.argv[4:]))
    parent = Path(sys.argv[2]).resolve()
    parent.mkdir(parents=True, exist_ok=True)
    # Only this standalone fixture controller adopts orphans on a failing helper
    # test. Normal success must leave none; never change the editor's reaper.
    if sys.platform.startswith('linux'):
        import ctypes
        import signal
        libc = ctypes.CDLL(None, use_errno=True)
        assert libc.prctl(36, 1, 0, 0, 0) == 0
    try:
        run(Path(sys.argv[1]).resolve(), parent)
    finally:
        if sys.platform.startswith('linux'):
            while True:
                children = Path(f'/proc/self/task/{os.getpid()}/children').read_text().split()
                for pid in children:
                    try:
                        os.kill(int(pid), signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                try:
                    if os.waitpid(-1, os.WNOHANG)[0] == 0:
                        time.sleep(.01)
                except ChildProcessError:
                    break
