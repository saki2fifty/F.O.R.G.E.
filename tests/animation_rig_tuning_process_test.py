"""Actual isolated-runtime rig tuning, rollback, pause, recovery and Stop."""
import json
from pathlib import Path
import subprocess
import sys
import time
from physics_velocity_example_test import Worker


class CheckedWorker(Worker):
    def request(self, command, expect_ok=True, **fields):
        self.sequence += 1
        self.process.stdin.write(json.dumps(dict(protocol=2, id=self.sequence,
            command=command, session=self.session, **fields)) + '\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        assert line, self.process.stderr.read()
        result = json.loads(line)
        assert result['ok'] == expect_ok, result
        self.session = result['session']
        return result


runtime, fixture, converter, source = (Path(sys.argv[i]).resolve() for i in (1, 2, 4, 5))
worker = sys.argv[3] if sys.argv[3] == '--direct' else str(Path(sys.argv[3]).resolve())
prepared = subprocess.run([fixture, runtime.parent/'rig-tuning-data', worker, converter,
    source, '--prepare'], capture_output=True, text=True, encoding='utf-8', timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
root = Path(prepared.stdout.strip().splitlines()[-1])
(root/'forge.project.json').write_text(json.dumps(dict(version=2, name='Rig tuning',
    startup_scene=None, input=dict(version=1, actions=[]), modules=[])), encoding='utf-8')
authored = (root/'main.scene.json').read_bytes()
scene = json.loads(authored)
actor, = [r for r in scene['entities'] if 'forge.animator' in r['components']]
actor_id = actor['id']
reach = actor['components']['forge.two_bone_ik']
aim = dict(joint=reach['end'], enabled=True, weight=1, twist_degrees=0,
    target_x=1, target_y=2, target_z=1, pole_x=0, pole_y=1, pole_z=0,
    forward_x=0, forward_y=1, forward_z=0, up_x=1, up_y=0, up_z=0)
actor['components']['forge.aim_ik'] = aim


def row(state):
    return next(r for r in state['effective_scene']['entities'] if r['id'] == actor_id)


def endpoint(state):
    p = row(state)['animation_pose']
    return p['model'][p['joint_assets'].index(reach['end'])][12:15]


a, b = CheckedWorker(runtime, root), None
try:
    s = a.request('replace', scene=scene)
    deadline = time.monotonic() + 15
    while s['recovery'] is None or not row(s).get('animation_pose'):
        assert time.monotonic() < deadline, s
        time.sleep(.002)
        s = a.request('snapshot')
    s = a.request('step')
    s = a.request('animation_playback', generation=s['world_generation'],
        source=dict(scene=scene['asset_id'], entity=actor_id), operation='pause')
    phase, tick = row(s)['animation_pose']['time'], s['timing']['tick']

    def tune(field, value, type_key='forge.two_bone_ik', expect_ok=True, **extra):
        fields = dict(generation=s['world_generation'], scene_asset=scene['asset_id'],
            entity=actor_id, type=type_key, property=field, value=value)
        fields.update(extra)
        return a.request('property_tune', expect_ok=expect_ok, **fields)

    s = tune('target_x', .8)
    assert abs(endpoint(s)[0] - .8) < .003, endpoint(s)
    assert abs(s['property_tune']['value'] - .8) < 1e-6
    assert row(s)['animation_pose']['time'] == phase and s['timing']['tick'] == tick
    s = tune('twist_degrees', 30, 'forge.aim_ik')
    preserved = a.request('snapshot')
    for field, value, type_key, extra in (
        ('middle', reach['end'], 'forge.two_bone_ik', {}),
        ('axis_z', 0, 'forge.two_bone_ik', {}),
        ('end', scene['asset_id'], 'forge.two_bone_ik', {}),
        ('weight', 2, 'forge.two_bone_ik', {}),
        ('enabled', 'true', 'forge.two_bone_ik', {}),
        ('up_x', 0, 'forge.aim_ik', {}),
        ('unknown', 1, 'forge.aim_ik', {}),
        ('x', 2, 'forge.local_translation', {}),
        ('speed', 2, 'project.unadmitted', {}),
        ('weight', .5, 'forge.two_bone_ik', dict(generation=s['world_generation']+1)),
        ('weight', .5, 'forge.two_bone_ik', dict(scene_asset=actor_id)),
        ('weight', .5, 'forge.two_bone_ik', dict(entity=scene['asset_id'])),
    ):
        tune(field, value, type_key, False, **extra)
        after = a.request('snapshot')
        assert after['scene'] == preserved['scene']
        assert after['effective_scene'] == preserved['effective_scene']
        assert after['timing']['tick'] == tick
    cp = s['recovery']
    b = CheckedWorker(runtime, root)
    t = b.request('replace', scene=cp['scene'], recovery=cp,
        recovery_session=cp['session'], recovery_tick=cp['tick'])
    assert row(t)['animation_pose'] == row(s)['animation_pose']
    assert row(t)['components'] == row(s)['components']
    for _ in range(3):
        left, right = a.request('step'), b.request('step')
        assert row(left)['animation_pose'] == row(right)['animation_pose']
        assert row(left)['animation_pose']['time'] == phase
    s = tune('enabled', False)
    assert abs(endpoint(s)[0]) < .003, endpoint(s)
    s = tune('enabled', True)
    assert abs(endpoint(s)[0] - .8) < .003, endpoint(s)
    a.request('replace', scene=scene)
    assert row(a.request('snapshot'))['components']['forge.two_bone_ik'] == reach
    assert (root/'main.scene.json').read_bytes() == authored
finally:
    a.close()
    if b:
        b.close()
print('Isolated Live reach/aim tuning, complete rejection, pause, fresh recovery and Stop passed')
