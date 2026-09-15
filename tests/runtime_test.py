import json
import subprocess
import sys
from pathlib import Path
runtime, module = map(str, map(Path, sys.argv[1:]))
p = subprocess.Popen([runtime], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
def request(command, **fields):
    p.stdin.write(json.dumps(dict(protocol=1, command=command, **fields))+'\n')
    p.stdin.flush()
    return json.loads(p.stdout.readline())
try:
    assert request('ping')['ok']
    assert not request('unknown')['ok']
    assert request('load_module', path=module)['ok']
    assert request('step', seconds=0.1)['ok']
    assert not request('step', seconds=-1)['ok']
    assert request('ping')['ok']
    assert request('quit')['ok']
    assert p.wait(timeout=5)==0
finally:
    if p.poll() is None:
        p.kill()
        p.wait()
# A terminated runtime is recoverable by launching a fresh process.
result=subprocess.run([runtime],input='{"protocol":1,"command":"quit"}\n',text=True,capture_output=True,timeout=5)
assert result.returncode==0 and json.loads(result.stdout)['ok']
