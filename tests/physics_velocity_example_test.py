"""Independent equations and true worker recovery for the ordinary C++ example."""
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys


class Worker:
    def __init__(self, runtime, project, env=None):
        self.process = subprocess.Popen([str(runtime), '--sdk-project', str(project)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, env=env, cwd=project)
        self.sequence, self.session = 0, ''
        self.request('hello')

    def request(self, command, **fields):
        self.sequence += 1
        self.process.stdin.write(json.dumps(dict(protocol=2, id=self.sequence,
            command=command, session=self.session, **fields)) + '\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        assert line, self.process.stderr.read()
        result = json.loads(line)
        assert result['ok'], result
        self.session = result['session']
        return result

    def close(self):
        if self.process.poll() is None:
            self.request('quit')
            self.process.wait(timeout=10)
        assert self.process.returncode == 0, self.process.stderr.read()


def verify(runtime, project, env=None):
    settings = json.loads((project / 'forge.project.json').read_text())
    path = project / settings['startup_scene']['source']
    authored_bytes = path.read_bytes()
    original = json.loads(authored_bytes)
    dt = struct.unpack('f', struct.pack('f', 1/60))[0]

    def values(state):
        return {e['components']['project.velocity_control']['stop_channel']: e['components']
                for e in state['scene']['entities']
                if 'project.velocity_control' in e['components']}

    def compare(left, right):
        lv, rv = values(left), values(right)
        for channel in lv:
            for component in ('forge.local_translation', 'forge.local_rotation',
                              'project.velocity_control'):
                for field, value in lv[channel][component].items():
                    other = rv[channel][component][field]
                    if isinstance(value, (int, float)) and not isinstance(value, bool):
                        assert abs(value-other) < 3e-6, (channel, component, field, value, other)
                    else:
                        assert value == other, (channel, component, field, value, other)

    def diagnostics(state, speeds):
        # Protocol diagnostics are a retained bounded log, not a per-step delta.
        rows = [d for d in state['diagnostics'] if d['text'].startswith('Velocity control:')
                and d['context'].get('tick') == state['timing']['tick']]
        observed = {}
        for row in rows:
            match = re.fullmatch(r'Velocity control: channel (\d) \| linear ([\d.]+) m/s \| angular ([\d.]+) rad/s', row['text'])
            assert match and row['context']['tick'] == state['timing']['tick'], row
            channel = int(match[1])
            assert channel not in observed, rows
            observed[channel] = tuple(map(float, match.group(2, 3)))
        assert observed.keys() == speeds.keys(), (observed, speeds)
        for channel, expected in speeds.items():
            assert all(abs(a-b) < 2e-6 for a, b in zip(observed[channel], expected)), (observed, speeds)

    for tuned in (False, True):
        scene = json.loads(authored_bytes)
        linear_cap, angular_cap = (.5, math.pi/6) if tuned else (1., 1.)
        ld, ad = (1-2*dt, 1-3*dt) if tuned else (1., 1.)
        if tuned:
            for entity in scene['entities']:
                if 'project.velocity_control' in entity['components']:
                    entity['components']['forge.physics_body'].update(linear_damping=2.,
                        angular_damping=3., max_linear_velocity=.5,
                        max_angular_velocity=30., allow_sleeping=False)
        a, b = Worker(runtime, project, env), None
        try:
            a.request('replace', scene=scene)
            position, angle, linear, angular = 0., 0., linear_cap, angular_cap
            stopped_position, stopped_angle = None, None
            for tick in range(1, 151):
                left = a.request('step')
                if b:
                    compare(left, b.request('step'))
                linear *= ld
                angular *= ad
                if tick == 120:
                    stopped_position, stopped_angle = position, angle
                position += linear*dt
                angle += angular*dt
                state = values(left)
                for channel, components in state.items():
                    p, q = components['forge.local_translation'], components['forge.local_rotation']
                    expected_p = stopped_position if tick >= 120 and channel & 1 else position
                    expected_angle = stopped_angle if tick >= 120 and channel & 2 else angle
                    assert abs(p['y']-expected_p) < 2e-5, (tick, channel, p, expected_p)
                    expected_q = dict(x=0., y=0., z=math.sin(expected_angle/2),
                                      w=math.cos(expected_angle/2))
                    # q and -q describe the same orientation. Check the complete
                    # quaternion so a reversed rotation or extra axis cannot pass.
                    error = min(sum((q[k]-sign*value)**2 for k, value in expected_q.items())
                                for sign in (-1, 1))
                    assert error < (2e-5)**2, (tick, channel, q, expected_q)
                    assert components['project.velocity_control']['stopped'] == bool(tick >= 120 and channel)
                if tick == 1:
                    diagnostics(left, {channel: (linear, angular) for channel in range(4)})
                elif tick == 120:
                    diagnostics(left, {channel: (0 if channel & 1 else linear,
                        0 if channel & 2 else angular) for channel in (1, 2, 3)})
                else:
                    diagnostics(left, {})
                if tick == 90:
                    # A genuinely new process starts with the captured runtime state,
                    # before the one-shot stop commands, rather than replacing in place.
                    b = Worker(runtime, project, env)
                    checkpoint = left['recovery']
                    restored = b.request('replace', scene=left['scene'], recovery=checkpoint,
                        recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                    assert restored['timing']['paused'] and restored['timing']['tick'] == 90
            assert path.read_bytes() == authored_bytes, 'Velocity example changed authored source'
        finally:
            a.close()
            if b:
                b.close()
    print('Compiled C++ velocity example: launch, zero/preserved channels, independent motion, '
          'caps/damping and fresh-process timed-stop recovery passed')


if __name__ == '__main__':
    verify(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
