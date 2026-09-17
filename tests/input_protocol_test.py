"""Hardware-independent action snapshots through the installed/headless protocol2 runtime."""
import json
import subprocess
import sys
import uuid
runtime = sys.argv[1]
action = str(uuid.uuid4())
config = dict(version=1, actions=[dict(id=action, name='User action', kind='digital', bindings=[dict(control='key.space')])])
p = subprocess.Popen([runtime], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
session = ''
seq = 0
def request(command, **fields):
    global seq, session
    seq += 1
    p.stdin.write(json.dumps(dict(protocol=2, id=seq, session=session, command=command, **fields))+'\n')
    p.stdin.flush()
    r = json.loads(p.stdout.readline())
    if command == 'hello' and r['ok']: session = r['session']
    return r
try:
    assert not request('hello', simulation_hz=0)['ok']
    r = request('hello', simulation_hz=120, input_map=config)
    assert r['ok'] and r['timing']['simulation_hz'] == 120
    r = request('snapshot', input_events=[dict(control='key.space', value=1)])
    assert r['ok'] and r['input']['tick'] == 0
    r = request('step'); state = r['input']['actions'][0]
    assert state['held'] and state['pressed'] and state['presses'] == 1
    for _ in range(4):
        r = request('step'); state = r['input']['actions'][0]
        assert state['held'] and not state['pressed'] and state['presses'] == 1
    r = request('step', input_events=[dict(control='key.space', value=0)])
    assert r['input']['actions'][0]['released'] and r['input']['actions'][0]['releases'] == 1
    r = request('step'); assert not r['input']['actions'][0]['released']
    r = request('snapshot', input_events=[dict(control='key.space', value=1), dict(reset=True)])
    assert r['ok']
    r = request('step'); assert not r['input']['actions'][0]['pressed']
    invalid = request('snapshot', input_events=[dict(control='key.space', value=1), dict(control='bogus', value=0)])
    assert not invalid['ok'] and invalid['diagnostic']['category'] == 'runtime'
    assert not request('step')['input']['actions'][0]['held']
    before = request('snapshot')['scene']
    assert request('resume')['ok']
    r = request('pause')
    assert r['timing']['paused'] and r['scene'] == before
    assert request('quit')['ok'] and p.wait(timeout=5) == 0
finally:
    if p.poll() is None: p.kill(); p.wait()
print('Protocol2 project Hz, input snapshots, edges, reset and validation passed')
