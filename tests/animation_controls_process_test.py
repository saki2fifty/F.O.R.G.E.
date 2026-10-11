"""Actual C++ entity references and fixed-boundary controls survive recovery."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

runtime, fixture, converter, source, sdk = [Path(sys.argv[i]).resolve() for i in (1, 2, 4, 5, 6)]
worker = sys.argv[3] if sys.argv[3] == '--direct' else str(Path(sys.argv[3]).resolve())
prepared = subprocess.run([fixture, runtime.parent/'controls-sdk-data', worker, converter,
                           source, '--markers-prepare'], capture_output=True, text=True, timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
root = Path(prepared.stdout.strip().splitlines()[-1])
shutil.copy2(sdk, root/sdk.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
inputs = json.loads((Path(__file__).resolve().parents[1]/'samples/reference_game/input.json').read_text())
next(c for c in inputs['contexts'] if c['name']=='Gameplay')['active'] = True
project = dict(version=2, name='Clip controls SDK', startup_scene=None, input=inputs,
               modules=[dict(id='project.playback_controls', implementation='1', sdk='experimental-1',
                             fingerprint=fingerprint, library=sdk.name,
                             dependencies=['forge.animation', 'forge.input'])])
(root/'forge.project.json').write_text(json.dumps(project))
scene = json.loads((root/'main.scene.json').read_text())
next(e['components']['forge.animator'] for e in scene['entities']
     if 'forge.animator' in e['components'])['play_on_start'] = False

class Worker:
    def __init__(self):
        self.p = subprocess.Popen([runtime, '--sdk-project', root], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.session = ''
        self.sequence = 0
    def request(self, command, **extra):
        self.sequence += 1
        self.p.stdin.write(json.dumps(dict(protocol=2, id=self.sequence, session=self.session,
                                          command=command, **extra))+'\n')
        self.p.stdin.flush()
        line = self.p.stdout.readline()
        assert line, self.p.stderr.read()
        result = json.loads(line)
        self.session = result['session']
        assert result['ok'], result
        assert not any(d['severity'].lower() in ('error', 'fatal') for d in result.get('diagnostics', [])), result
        return result
    def close(self):
        if self.p.poll() is None:
            try:
                self.request('quit')
                self.p.wait(timeout=5)
            finally:
                if self.p.poll() is None:
                    self.p.kill()
                    self.p.wait()

def status(result):
    return next((e['animation_playback'] for e in result['effective_scene']['entities']
                 if 'animation_playback' in e), None)

def verify(result, ticks, playing):
    value = status(result)
    assert value and abs(value['time'] - ticks/60) < 1e-6 and value['playing']==playing, value
    rows=[d for d in result['diagnostics'] if d['text'].startswith('control_probe:')]
    assert rows and rows[-1]['context']['tick']==result['timing']['tick'], rows
    before=value.copy()
    return before

first=Worker()
second=None
try:
    first.request('hello', simulation_hz=60)
    initial=first.request('replace', scene=scene)
    deadline=time.monotonic()+15
    while (status(initial) is None or initial['recovery'] is None) and time.monotonic()<deadline:
        time.sleep(.002)
        initial=first.request('snapshot')
    assert status(initial) and status(initial)['time']==0 and not status(initial)['playing'], initial
    for elapsed, playing in [(1,True),(1,False),(1,False),(2,True),(1,True),(2,True),(2,False)]:
        stepped=first.request('step')
        expected=verify(stepped,elapsed,playing)
        assert status(first.request('snapshot'))==expected
    checkpoint=stepped['recovery']
    second=Worker()
    second.request('hello', simulation_hz=60)
    recovered=second.request('replace',scene=checkpoint['scene'],recovery=checkpoint,
                             recovery_session=checkpoint['session'],recovery_tick=checkpoint['tick'])
    assert status(recovered)==expected, recovered
    verify(second.request('step'),3,True)
    verify(second.request('step'),1,True)
    print('Matching C++ entity iteration/reference lookup, initial Play, Pause/Resume, Restart, late-stage rejection and second-process intent recovery passed')
finally:
    first.close()
    if second:second.close()
    shutil.rmtree(root)
