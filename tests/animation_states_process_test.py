"""Matching shared C++ SDK selects a stable state; Pause/Step and recovery stay coherent."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

runtime, fixture, converter, source, sdk = map(lambda p: Path(p).resolve(), [sys.argv[i] for i in (1, 2, 4, 5, 6)])
worker = sys.argv[3] if sys.argv[3] == "--direct" else str(Path(sys.argv[3]).resolve())
prepared = subprocess.run([fixture, runtime.parent/'states-sdk-data', worker, converter, source, '--prepare'],
                          capture_output=True, text=True, timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
root = Path(prepared.stdout.strip().splitlines()[-1])
shutil.copy2(sdk, root/sdk.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
project = dict(version=2, name='Animation States SDK', startup_scene=None,
               input=dict(version=1, actions=[]), modules=[dict(
                   id='project.animation_probe', implementation='1', sdk='experimental-1',
                   fingerprint=fingerprint, library=sdk.name,
                   dependencies=['forge.animation', 'forge.input'])])
(root/'forge.project.json').write_text(json.dumps(project))

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


def pose(result):
    return next((e['animation_pose'] for e in result['effective_scene']['entities']
                if 'animation_pose' in e), None)


def restore(worker, checkpoint):
    return worker.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                          recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])

first = Worker()
second = None
try:
    first.request('hello', simulation_hz=60)
    initial = first.request('replace', scene=json.loads((root/'main.scene.json').read_text()))
    deadline = time.monotonic() + 15
    while (pose(initial) is None or initial['recovery'] is None) and time.monotonic() < deadline:
        time.sleep(.002)
        initial = first.request('snapshot')
    assert pose(initial) is not None and initial['recovery'] is not None, initial
    assert initial['timing']['paused'] and initial['timing']['tick'] == 0
    assert pose(initial)['state'] == 1 and pose(initial)['time'] == 0
    stepped = first.request('step')
    assert pose(stepped)['state'] == 2 and pose(stepped)['state_name'] == 'Reach'
    assert abs(pose(stepped)['time'] - 2/60) < 1e-6
    assert abs(pose(stepped)['blend_weight'] - (1/60)/.4) < 1e-6
    time.sleep(.03)
    assert pose(first.request('snapshot')) == pose(stepped)
    checkpoint = stepped['recovery']
    second = Worker()
    second.request('hello', simulation_hz=60)
    recovered = restore(second, checkpoint)
    assert pose(recovered) == pose(stepped)
    advanced = second.request('step')
    assert abs(pose(advanced)['time'] - 4/60) < 1e-6
    bad = copy.deepcopy(checkpoint)
    bad['animation']['entries'][0]['states']['revision'] = 'invalid'
    try:
        restore(second, bad)
    except AssertionError:
        pass
    else:
        raise AssertionError('Invalid state-set revision accepted')
    assert pose(second.request('snapshot')) == pose(advanced)
    print('Matching C++ SDK state selection, independent transition clocks, Pause/Step and crash recovery passed')
finally:
    first.close()
    if second:
        second.close()
    shutil.rmtree(root)
