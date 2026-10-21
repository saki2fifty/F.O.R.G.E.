"""Exact SDK typed 2D joint helpers through both real hosts and supported recovery."""
import copy
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

from runtime_client import Runtime

runtime, module = (Path(value).resolve() for value in sys.argv[1:3])
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
scene_id = str(uuid.uuid4())


def body(name, x, y, dynamic):
    return dict(id=str(uuid.uuid4()), name=name, spatial=dict(mode='world'), components={
        'forge.local_translation': dict(x=x, y=y, z=7),
        'forge.local_scale': dict(x=-1, y=1, z=0),
        'forge.physics_body_2d': dict(motion=2 if dynamic else 0, mass=2, mask=0,
            gravity_factor=0, linear_damping=0, angular_damping=0),
        'forge.box_collider_2d': dict(x=1, y=1),
    })


entities = []
for kind, x in enumerate((-5, 0, 5)):
    anchor = body(f'Anchor{kind}', x, 0, False)
    actor = body(f'Actor{kind}', x, -3 if kind == 0 else 0, True)
    value = dict(enabled=True, kind=kind,
        body_a=dict(scene=scene_id, entity=anchor['id']),
        body_b=dict(scene=scene_id, entity=actor['id']),
        anchor_a_x=0, anchor_a_y=0, anchor_b_x=0, anchor_b_y=0,
        frame_a_angle=0, frame_b_angle=0, distance=3,
        spring_frequency=0, spring_damping=1, limits_enabled=True,
        min_angle=-30, max_angle=30, min_travel=-.5, max_travel=.5,
        motor_enabled=False, hinge_motor_speed=90, slider_motor_speed=1,
        max_motor_torque=10, max_motor_force=100, collide_connected=False)
    joint = dict(id=str(uuid.uuid4()), name=f'Joint{kind}',
                 components={'forge.physics_joint_2d': value})
    entities.extend((anchor, actor, joint))
scene = dict(version=5, asset_id=scene_id, entities=entities)
original_source = copy.deepcopy(scene)


def components(state):
    return {row['name']: row['components'] for row in state['scene']['entities']}


def replace_scene(worker, sdk_play):
    response = worker.request('replace', scene=scene)
    if not sdk_play:
        return response
    deadline = time.monotonic() + 10
    while not response.get('candidate'):
        assert time.monotonic() < deadline, response
        time.sleep(.005)
        response = worker.request('snapshot')
    ticket = response['candidate']['ticket']
    candidate = worker.request('candidate', ticket=ticket)['candidate']
    assert candidate['session'] == worker.session and len(candidate['scene']['entities']) == 9
    response = worker.request('snapshot', candidate_ack=dict(
        session=worker.session, ticket=ticket, accepted=True, diagnostic=''))
    while response['activation']['generation'] != ticket:
        assert time.monotonic() < deadline, response
        time.sleep(.005)
        response = worker.request('snapshot')
    return worker.request('snapshot', editor_epoch=dict(
        epoch=ticket, captured=False, input_device='EditorHost'))


with tempfile.TemporaryDirectory(dir=runtime.parent) as directory:
    project = Path(directory)
    shutil.copyfile(module, project / module.name)
    (project / 'forge.project.json').write_text(json.dumps(dict(version=2,
        name='Original connected2D SDK acceptance', startup_scene=None,
        game=dict(version=1, application_id='org.forge.joint-2d-test', title='2D joint SDK',
            profile='development', save_schema=1,
            display=dict(mode='windowed', width=1280, height=720, display=0, vsync=False),
            audio=dict(master_volume=1), input=dict(mouse_sensitivity=1)),
        input=dict(version=1, actions=[]), modules=[dict(id='project.physics_2d_joint_probe',
            implementation='1', sdk='experimental-1', fingerprint=fingerprint,
            library=module.name, dependencies=['forge.physics'])])))
    for sdk_play in (False, True):
        arguments = ['--sdk-project', project]
        if sdk_play:
            arguments += ['--sdk-play', 'on', '--user-data', project / 'users']
        a, b = Runtime(runtime, arguments), None
        try:
            initial = replace_scene(a, sdk_play)
            assert initial['physics']['joints_2d'] == 3 and initial['physics']['joints'] == 0, initial
            for tick in range(1, 121):
                left = a.request('step')
                messages = [entry for entry in left['diagnostics']
                    if entry['context'].get('tick') == tick and entry['text'].startswith('Joint SDK ')]
                assert {entry['text'] for entry in messages} == {
                    f'Joint SDK kind {kind} controls passed' for kind in range(3)}, left
                assert all(entry['context']['module'] == 'project.physics_2d_joint_probe'
                           for entry in messages)
                values = components(left)
                for kind in range(3):
                    actor = values[f'Actor{kind}']
                    assert actor['forge.local_translation']['z'] == 7
                    assert actor['forge.local_scale'] == dict(x=-1, y=1, z=0)
                if b:
                    assert components(b.request('step')) == values
                if tick == 35 and not sdk_play:
                    checkpoint = left['recovery']
                    b = Runtime(runtime, arguments)
                    restored = b.request('replace', scene=left['scene'], recovery=checkpoint,
                        recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                    assert restored['timing']['paused'] and restored['timing']['tick'] == tick
                    assert components(restored) == values
                if sdk_play:
                    assert left['recovery'] is None  # Arbitrary native Play recovery is not added.
            values = components(left)
            assert abs(values['Actor0']['forge.local_translation']['y'] + 2) < .02, values['Actor0']
            q = values['Actor1']['forge.local_rotation']
            assert abs(2 * math.atan2(q['z'], q['w']) + math.pi / 6) < .02, q
            assert abs(values['Actor2']['forge.local_translation']['x'] - 4.5) < .02, values['Actor2']
            assert values['Joint0']['forge.physics_joint_2d']['spring_frequency'] == 2
            assert values['Joint1']['forge.physics_joint_2d']['hinge_motor_speed'] == -90
            assert values['Joint2']['forge.physics_joint_2d']['slider_motor_speed'] == -1
            clean = replace_scene(a, sdk_play)
            assert clean['timing']['tick'] == 0
            assert not components(clean)['Joint1']['forge.physics_joint_2d']['motor_enabled']
            assert scene == original_source
        finally:
            a.close()
            if b:
                b.close()

print('Exact-SDK typed2D joint aliases/helpers, copied units, real both-host controls, '
      'independent motion, supported fresh-worker recovery and clean replacement passed')
