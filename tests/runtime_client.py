"""Small bounded protocol-2 client for isolated runtime tests."""
import json
import queue
import subprocess
import threading
from pathlib import Path


class Runtime:
    """Bounded requests to a disposable runtime; never load native code here."""
    def __init__(self, executable: Path):
        self.process = subprocess.Popen([str(executable)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, text=True, bufsize=1)
        self.lines: queue.Queue[str | None] = queue.Queue(maxsize=2)
        self.session = ""
        self.request_id = 0
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.request("hello")

    def _read(self):
        try:
            while True:
                line = self.process.stdout.readline(16 * 1024 * 1024 + 1)
                if not line:
                    break
                if len(line) > 16 * 1024 * 1024:
                    break
                self.lines.put_nowait(line)
        finally:
            try:
                self.lines.put_nowait(None)
            except queue.Full:
                pass

    def request(self, command: str, **data):
        if self.process.poll() is not None:
            raise RuntimeError('Runtime process has exited')
        self.request_id += 1
        request = dict(protocol=2, id=self.request_id, session=self.session, command=command, **data)
        self.process.stdin.write(json.dumps(request)+'\n')
        self.process.stdin.flush()
        try:
            line = self.lines.get(timeout=5)
        except queue.Empty as error:
            self.close()
            raise RuntimeError('Runtime request timed out') from error
        if line is None:
            raise RuntimeError('Runtime exited during request')
        response = json.loads(line)
        if response.get('protocol') != 2 or response.get('id') != self.request_id:
            raise RuntimeError('Invalid/stale runtime response')
        if command == 'hello':
            self.session = response.get('session', '')
        if not self.session or response.get('session') != self.session:
            raise RuntimeError('Stale runtime session')
        if not response.get('ok'):
            raise RuntimeError(response.get('error', 'Runtime rejected request'))
        return response

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=5)
        if self.process.stdin:
            self.process.stdin.close()
        self.reader.join(timeout=5)
        if self.process.stdout:
            self.process.stdout.close()
