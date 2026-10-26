"""Copied multi-hit rays and guards through ordinary and production SDK Play hosts."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid
from runtime_client import Runtime, activate

runtime, probe, denied = (Path(value).resolve() for value in sys.argv[1:4])
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']


def body(name, y, planar, sensor=False):
    suffix = '_2d' if planar else ''
    return dict(id=str(uuid.uuid4()), name=name, spatial=dict(mode='world'), components={
        'forge.local_translation': dict(x=0, y=y, z=7 if planar else 0),
        'forge.physics_body'+suffix: dict(motion=0, sensor=sensor, mask=0, density=1000, mass=0,
            friction=.5, restitution=0, gravity_factor=0),
        'forge.box_collider'+suffix: dict(x=1, y=1, **({} if planar else dict(z=1))),
    })


scene = dict(version=5, asset_id=str(uuid.uuid4()), entities=[
    body(f'{label}{plane}', y, plane, label == 'sensor')
    for plane in (False, True) for label, y in (('far', 0), ('near', 2), ('sensor', 3))])
with tempfile.TemporaryDirectory(dir=runtime.parent) as temporary:
    root = Path(temporary)
    for module in (probe, denied):
        shutil.copyfile(module, root/module.name)
    for rejected in (False, True):
        selected = denied if rejected else probe
        module_id = 'project.physics_rays_denied' if rejected else 'project.physics_rays_probe'
        settings = dict(version=2, name='Original ray SDK acceptance', startup_scene=None,
            game=dict(version=1, application_id='org.forge.ray-test', title='Ray SDK test',
                profile='development', save_schema=1,
                display=dict(mode='windowed', width=1280, height=720, display=0, vsync=False),
                audio=dict(master_volume=1), input=dict(mouse_sensitivity=1)),
            input=dict(version=1, actions=[]), modules=[dict(id=module_id, implementation='1',
                sdk='experimental-1', fingerprint=fingerprint, library=selected.name,
                dependencies=['forge.physics'])])
        (root/'forge.project.json').write_text(json.dumps(settings))
        original = (root/'forge.project.json').read_bytes()
        for sdk_play in (False, True):
            args = ['--sdk-project', root]
            if sdk_play:
                args += ['--sdk-play', 'on', '--user-data', root/'users']
            worker = Runtime(runtime, args)
            fresh = None
            try:
                activate(worker, scene, sdk_play)
                for tick in range(1, 5):
                    state = worker.request('step')
                    rows = [row for row in state['diagnostics']
                            if row['text'] == 'Multi-hit ray SDK proof passed'
                            and row['context'].get('tick') == state['timing']['tick']]
                    assert len(rows) == 1 and rows[0]['context']['module'] == module_id, rows
                    if tick == 2 and not sdk_play:
                        checkpoint = state['recovery']
                        fresh = Runtime(runtime, args)
                        restored = fresh.request('replace', scene=state['scene'], recovery=checkpoint,
                            recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                        assert restored['timing']['tick'] == tick
                    elif fresh:
                        other = fresh.request('step')
                        assert other['scene'] == state['scene']
                        assert any(row['text'] == 'Multi-hit ray SDK proof passed'
                                   for row in other['diagnostics'])
                assert (root/'forge.project.json').read_bytes() == original
            finally:
                worker.close()
                if fresh:
                    fresh.close()
print('Multi-hit SDK rays: both domains, both hosts, copied geometry, filtering, '
      'alias/size/capacity/clearing/thread/fixed/permission guards and native recovery passed')
