"""Real process relaunch, writer exclusion/crash release and blocked Windows replacement."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

executable, scratch = sys.argv[1:]
Path(scratch).mkdir(parents=True, exist_ok=True)


def run(root, operation, ok=True):
    result = subprocess.run([executable, '--store-probe', operation, str(root)],
                            capture_output=True, text=True, timeout=20)
    assert (result.returncode == 0) == ok, (operation, result.returncode, result.stderr)
    return result


with tempfile.TemporaryDirectory(dir=scratch) as temporary:
    root = Path(temporary)
    run(root, 'write')
    save = root / 'game-org.forge.process' / 'slot-main.json'
    original = save.read_bytes()
    run(root, 'read')
    holder = subprocess.Popen([executable, '--store-probe', 'hold', str(root)],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True)
    try:
        # A file marker alone is not lock authority; another live process holds it.
        import concurrent.futures
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            ready = pool.submit(holder.stdout.readline)
            try:
                assert ready.result(timeout=20).strip() == 'ready'
            except BaseException:
                holder.kill()
                raise
        run(root, 'write', ok=False)
    finally:
        holder.kill()
        holder.communicate(timeout=10)
    assert save.read_bytes() == original
    run(root, 'read')  # OS releases ownership after forced termination.
    run(root, 'write')  # Stale lock marker and interrupted staging do not block retry.
    assert save.read_bytes() == original
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                      wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                                      wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle.restype = wintypes.BOOL
        held = kernel.CreateFileW(str(save), 0x80000000, 3, None, 3, 0x80, None)
        assert held != ctypes.c_void_p(-1).value, ctypes.get_last_error()
        try:
            run(root, 'write', ok=False)  # Reader denies FILE_SHARE_DELETE.
            assert save.read_bytes() == original
        finally:
            assert kernel.CloseHandle(held)
        run(root, 'write')
        run(root, 'read')
    for operation in ('write', 'read'):
        subprocess.run([executable, '--game-probe', operation, str(root / 'game')],
                       check=True, capture_output=True, text=True, timeout=30)
print('Separate-process save/relaunch, crash lease release and staging preservation passed')
