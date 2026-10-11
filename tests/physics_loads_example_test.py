"""Execute the compiled C++ example and recover it in a fresh isolated runtime."""
import json
import math
from pathlib import Path
import subprocess
import sys


def verify(runtime, project, env=None):
    class Worker:
        def __init__(self):
            self.process = subprocess.Popen([str(runtime), '--sdk-project', str(project)],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                env=env, cwd=project)
            self.session, self.sequence = '', 0
            self.request('hello')

        def request(self, command, **fields):
            self.sequence += 1
            self.process.stdin.write(json.dumps(dict(protocol=2, id=self.sequence,
                command=command, session=self.session, **fields)) + '\n')
            self.process.stdin.flush()
            incoming = self.process.stdout.readline()
            assert incoming, self.process.stderr.read()
            result = json.loads(incoming)
            assert result['ok'], result
            self.session = result['session']
            return result

        def close(self):
            if self.process.poll() is None:
                self.request('quit')
                assert self.process.wait(timeout=10) == 0
            assert self.process.returncode == 0, self.process.stderr.read()

    def values(state):
        return {e['name']: e['components'] for e in state['scene']['entities']
                if 'project.body_loads' in e['components']}

    settings = json.loads((project / 'forge.project.json').read_text())
    authored_path = project / settings['startup_scene']['source']
    authored_bytes = authored_path.read_bytes()
    authored = json.loads(authored_bytes)
    a, b = Worker(), Worker()
    try:
        a.request('replace', scene=authored)
        first = a.request('step')
        v = values(first)
        dt, damping = 1 / 60, 1 - .05 / 60
        for name, expected in [('Force - 1 kg', 6 * dt * dt * damping),
                               ('Force - 2 kg', 3 * dt * dt * damping),
                               ('Impulse - 1 kg', 6 * dt * damping)]:
            assert abs(v[name]['forge.local_translation']['y'] - expected) < 1e-6, v[name]
        for name, angle in [('Torque - 1 kg', 6 * dt * dt * damping),
                            ('Angular impulse - 1 kg', 6 * dt * damping),
                            ('Off-center impulse - 1 kg', 36 * dt * damping)]:
            q = v[name]['forge.local_rotation']
            assert abs(q['z'] - math.sin(angle / 2)) < 1e-6, (name, q)
        for _ in range(29):
            state = a.request('step')
        checkpoint = state['recovery']
        restored = b.request('replace', scene=state['scene'], recovery=checkpoint,
            recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
        assert restored['timing']['paused'] and restored['timing']['tick'] == 30
        velocity, position = 0, 0
        for _ in range(30):
            velocity = (velocity + 6 * dt) * damping
            position += velocity * dt
        for tick in range(30, 135):
            left, right = a.request('step'), b.request('step')
            assert left['timing']['tick'] == right['timing']['tick'] == tick + 1
            lv, rv = values(left), values(right)
            for name in lv:
                for component in ('forge.local_translation', 'forge.local_rotation', 'project.body_loads'):
                    for field, value in lv[name][component].items():
                        other = rv[name][component][field]
                        if isinstance(value, (int, float)) and not isinstance(value, bool):
                            assert abs(value - other) < 2e-6, (name, component, field, value, other)
                        else:
                            assert value == other, (name, field)
            velocity = (velocity + (6 * dt if tick < 120 else 0)) * damping
            position += velocity * dt
            assert abs(lv['Force - 1 kg']['forge.local_translation']['y'] - position) < 2e-5
            assert abs(2 * lv['Force - 2 kg']['forge.local_translation']['y'] -
                       lv['Force - 1 kg']['forge.local_translation']['y']) < 2e-5
        # The same actual compiled systems must consume authored tuning too.
        # This changes only an unpublished copied scene, not example source.
        tuned = json.loads(authored_bytes)
        for row in tuned['entities']:
            if 'project.body_loads' in row['components']:
                row['components']['forge.physics_body'].update(linear_damping=2.,
                    angular_damping=3., max_linear_velocity=.05,
                    max_angular_velocity=30., allow_sleeping=False)
        a.request('replace', scene=tuned)
        first = a.request('step')
        tv = values(first)
        ld, ad, angular_cap = 1 - 2 * dt, 1 - 3 * dt, math.pi / 6
        for name, speed in [('Force - 1 kg', min(.05, 6 * dt * ld)),
                            ('Force - 2 kg', min(.05, 3 * dt * ld)),
                            ('Impulse - 1 kg', .05 * ld)]:
            assert abs(tv[name]['forge.local_translation']['y'] - speed * dt) < 1e-6, tv[name]
        for name, speed in [('Torque - 1 kg', min(angular_cap, 6 * dt * ad)),
                            ('Angular impulse - 1 kg', angular_cap * ad),
                            ('Off-center impulse - 1 kg', angular_cap * ad)]:
            assert abs(tv[name]['forge.local_rotation']['z'] - math.sin(speed * dt / 2)) < 1e-6
        for _ in range(29):
            tuned_state = a.request('step')
        checkpoint = tuned_state['recovery']
        b.request('replace', scene=tuned_state['scene'], recovery=checkpoint,
            recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
        for _ in range(12):
            lv, rv = values(a.request('step')), values(b.request('step'))
            for name in lv:
                assert lv[name]['forge.physics_body'] == rv[name]['forge.physics_body']
                for component in ('forge.local_translation', 'forge.local_rotation'):
                    for field, value in lv[name][component].items():
                        assert abs(value - rv[name][component][field]) < 2e-6
        assert authored_path.read_bytes() == authored_bytes, 'Play/recovery wrote authored source'
    finally:
        a.close()
        b.close()
    print('Compiled C++ loads example: independent equations, mass comparison, one-shot recovery, '
          'timed force, authored tuning and unchanged authored scene passed')


if __name__ == '__main__':
    verify(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
