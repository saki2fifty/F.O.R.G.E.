"""Matching shared C++ SDK observes same-tick markers without preview/recovery replay."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

runtime, fixture, converter, source, sdk = map(lambda p: Path(p).resolve(), [sys.argv[i] for i in (1, 2, 4, 5, 6)])
worker = sys.argv[3] if sys.argv[3] == "--direct" else str(Path(sys.argv[3]).resolve())
prepared = subprocess.run([fixture, runtime.parent/'markers-sdk-data', worker, converter, source, '--markers-prepare'],
                          capture_output=True, text=True, timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
root = Path(prepared.stdout.strip().splitlines()[-1])
shutil.copy2(sdk, root/sdk.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
project = dict(version=2, name='Animation States SDK', startup_scene=None,
               input=dict(version=1, actions=[]), modules=[dict(
                   id='project.marker_probe', implementation='1', sdk='experimental-1',
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
        try:
            result = json.loads(line)
        except json.JSONDecodeError:
            raise AssertionError("Non-protocol runtime output: " + repr(line)) from None
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


def occurrences(result):
    return [d for d in result['diagnostics']
            if d['text'].startswith('marker_probe: ') and 'removed target' not in d['text']]

def ids(result):
    return [int(d['text'].split()[1]) for d in occurrences(result)]

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
    assert not occurrences(initial), initial
    stepped = first.request('step')
    assert ids(stepped) == [1], stepped
    assert occurrences(stepped)[0]['context']['tick'] == 1
    for _ in range(29):
        stepped = first.request('step')
    assert ids(stepped) == [1, 2], stepped
    assert occurrences(stepped)[-1]['context']['tick'] == 30
    assert 'Pulse' in occurrences(stepped)[-1]['text']
    root_actor = next(e for e in stepped['effective_scene']['entities'] if 'forge.animator' in e['components'])
    assert abs(root_actor['components']['forge.local_scale']['y'] - 1.12) < 1e-5, root_actor
    time.sleep(.03)
    assert ids(first.request('snapshot')) == [1, 2]
    checkpoint = stepped['recovery']
    second = Worker()
    second.request('hello', simulation_hz=60)
    recovered = restore(second, checkpoint)
    assert not occurrences(recovered), recovered
    for _ in range(90):
        advanced = second.request('step')
    assert ids(advanced) == [3, 1, 4], advanced
    assert [d['context']['tick'] for d in occurrences(advanced)] == [90, 120, 120]
    assert any(d['text'] == 'marker_probe: removed target rejected' for d in advanced['diagnostics'])
    for _ in range(5):
        advanced = second.request('step')
    assert ids(advanced) == [3, 1, 4], advanced
    assert not any(d['severity'].lower() in ('error', 'fatal') for d in advanced['diagnostics']), advanced
    print('Matching C++ PostPhysics marker copies, current-tick gating, broadcast reads, capabilities, Pause/Step, removed-target rejection and recovery passed')
finally:
    first.close()
    if second:
        second.close()
    shutil.rmtree(root)
