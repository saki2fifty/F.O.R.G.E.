"""Original parallel component system through both real exact-SDK runtime hosts."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from runtime_client import Runtime, activate


def verify(runtime, module, gallery):
    runtime, module, gallery = map(lambda value: Path(value).resolve(), (runtime, module, gallery))
    fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
    defaults = json.loads((gallery / 'forge.project.json').read_text())['game']
    scene_id = str(uuid.uuid4())
    actors = [str(uuid.uuid4()) for _ in range(128)]
    reference = None

    def positions(state):
        rows = {row['id']: row for row in state['effective_scene']['entities']}
        values = [rows[key]['components']['forge.local_translation']['x'] for key in actors]
        assert all(rows[key]['world_affine'][3] == value for key, value in zip(actors, values))
        return values

    for sdk in (False, True):
        for count in (1, 4):
            with tempfile.TemporaryDirectory(prefix='fixed-workers-', dir=runtime.parent) as temporary:
                root = Path(temporary)
                shutil.copy2(module, root / module.name)
                game = copy.deepcopy(defaults)
                game['application_id'] = 'org.forge.fixed-workers-test'
                project = dict(version=2, name='Fixed worker test', ecs_workers=count,
                    simulation_hz=60, startup_scene=None, input=dict(version=1, actions=[]), game=game,
                    modules=[dict(id='project.parallel_motion', implementation='1', sdk='experimental-1',
                        fingerprint=fingerprint, library=module.name, dependencies=['forge.transforms'])])
                settings = root / 'forge.project.json'
                settings.write_text(json.dumps(project))
                declarations = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))
                component, = declarations['components']
                stamp = dict(format='forge.authored-component', version=1, module=component['module'],
                             schema_version=component['schema_version'], digest=component['digest'])
                source = dict(version=3, asset_id=scene_id, entities=[])
                for index, entity in enumerate(actors):
                    source['entities'].append(dict(id=entity, name=f'Actor {index}', components={
                        'forge.local_translation': dict(x=index / 4, y=0, z=0),
                        'forge.local_rotation': dict(x=0, y=0, z=0, w=1),
                        'forge.local_scale': dict(x=1, y=1, z=1), 'forge.primitive': dict(kind=4),
                        'project.parallelmotion': dict(speed=.5 if index % 2 else -.25,
                                                       **{'$forge': stamp})}))
                saved = root / 'Scene.scene.json'
                saved.write_text(json.dumps(source))
                original_hash = hashlib.sha256(saved.read_bytes()).hexdigest()
                args = ['--sdk-project', root, '--sdk-play', 'on' if sdk else 'off',
                        '--user-data', root / 'Users']
                worker = Runtime(runtime, args)
                try:
                    initial = activate(worker, source, sdk)
                    generation = initial['activation']['generation'] if sdk else initial['world_generation']
                    start = positions(initial)
                    worker.request('profiling', generation=generation, enabled=True, clear=True,
                                   native_systems=True)
                    worker.request('snapshot', profile_watch=True)
                    result = initial
                    for tick in range(1, 6):
                        result = worker.request('step', profile_watch=True)
                        deadline = time.monotonic() + 5
                        while result['timing']['tick'] != tick:
                            assert time.monotonic() < deadline, result['timing']
                            result = worker.request('snapshot', profile_watch=True)
                        assert result['timing']['paused']
                    moved = positions(result)
                    assert all((value > old) if index % 2 else (value < old)
                               for index, (old, value) in enumerate(zip(start, moved)))
                    if reference is None:
                        reference = moved
                    assert moved == reference, (sdk, count, moved[:3], reference[:3])
                    for _ in range(3):
                        held = worker.request('snapshot', profile_watch=True)
                        assert held['timing']['tick'] == 5 and positions(held) == moved
                    if result['cpu_profile']['compiled']:
                        plan = result['cpu_profile']['ecs']
                        assert plan['available'] and plan.get('ecs_workers', 1) == count
                        assert any(row.get('name', '').endswith('ParallelMotionUpdate') and
                                   row['multi_threaded'] for row in plan['entries'])
                        if count > 1:
                            assert not plan['native_system_timing'] and plan['system_trace_scope'] == 'owner_thread'
                            assert all(row['cpu_seconds'] is None for row in plan['entries']
                                       if row['kind'] == 'system')
                    if not sdk:
                        recovery = result['recovery']
                        assert recovery and recovery.get('ecs_workers', 1) == count
                        restored = worker.request('replace', scene=recovery['scene'], recovery=recovery,
                            recovery_session=recovery['session'], recovery_tick=recovery['tick'])
                        assert restored['timing']['tick'] == 5 and positions(restored) == moved
                    else:
                        replacement = activate(worker, source, True)
                        assert replacement['activation']['generation'] > generation
                        assert positions(replacement) == start
                        worker.request('profiling', generation=replacement['activation']['generation'],
                                       enabled=True, clear=True)
                        replacement = worker.request('step', profile_watch=True)
                        deadline = time.monotonic() + 5
                        while replacement['timing']['tick'] != 1:
                            assert time.monotonic() < deadline
                            replacement = worker.request('snapshot', profile_watch=True)
                        assert replacement['cpu_profile']['ecs'].get('ecs_workers', 1) == count
                    worker.request('quit')
                    assert worker.process.wait(timeout=10) == 0
                    assert hashlib.sha256(saved.read_bytes()).hexdigest() == original_hash
                finally:
                    worker.close()
                if not sdk:
                    project['ecs_workers'] = 4 if count == 1 else 1
                    settings.write_text(json.dumps(project))
                    mismatch = Runtime(runtime, args)
                    try:
                        try:
                            mismatch.request('replace', scene=recovery['scene'], recovery=recovery,
                                recovery_session=recovery['session'], recovery_tick=recovery['tick'])
                        except RuntimeError as error:
                            assert 'boundary mismatch' in str(error), error
                        else:
                            raise AssertionError('Changed worker count accepted the old checkpoint')
                        fresh = activate(mismatch, source, False)
                        assert positions(fresh) == start
                        mismatch.request('quit')
                        assert mismatch.process.wait(timeout=10) == 0
                    finally:
                        mismatch.close()
    print('Real ordinary/SDK hosts:1/4 stages, original parallel transforms, exact values, Pause/Step, '
          'native timing limits, SDK world replacement, ordinary recovery/count rejection, '
          'unchanged sources and graceful exits passed')


if __name__ == '__main__':
    verify(*sys.argv[1:4])
