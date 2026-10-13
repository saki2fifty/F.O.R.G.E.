"""Typed bone-mask edits in real C++ shared workers with cold recovery."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from physics_velocity_example_test import Worker

runtime, fixture, converter, source, module = (Path(sys.argv[i]).resolve() for i in (1, 2, 4, 5, 6))
worker = sys.argv[3] if sys.argv[3] == '--direct' else str(Path(sys.argv[3]).resolve())
prepared = subprocess.run([fixture, runtime.parent / 'mask-sdk-data', worker, converter, source, '--prepare'],
                          capture_output=True, text=True, encoding='utf-8', timeout=120)
assert prepared.returncode == 0, (prepared.stdout, prepared.stderr)
root = Path(prepared.stdout.strip().splitlines()[-1])
shutil.copy2(module, root / module.name)
fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
(root / 'forge.project.json').write_text(json.dumps(dict(version=2, name='Mask SDK', startup_scene=None,
    input=dict(version=1, actions=[]), modules=[dict(id='project.animation_mask_probe', implementation='1',
    sdk='experimental-1', fingerprint=fingerprint, library=module.name, dependencies=['forge.animation'])])), encoding='utf-8')
declaration, = json.loads(subprocess.check_output([runtime, '--inspect-sdk', root], text=True))['components']
stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
             schema_version=declaration['schema_version'], digest=declaration['digest'])
scene = json.loads((root / 'main.scene.json').read_text(encoding='utf-8'))
actor, = [r for r in scene['entities'] if 'forge.animator' in r['components']]
mask = actor['components']['forge.animator']['blend_mask']
sidecar = json.loads((root / 'Assets/OverlayColumn.gltf.forge-import.json').read_text(encoding='utf-8'))
upper = next(r['id'] for r in sidecar['identity']['entries'] if r['type'] == 'model_node' and r['display_name'] == 'Upper joint')
actor['components']['project.mask_probe'] = dict(ticks=0, mask=mask, upper=upper, **{'$forge': stamp})
def row(snapshot):
    return next(r for r in snapshot['effective_scene']['entities'] if 'forge.animator' in r['components'])
def admitted(worker, snapshot):
    deadline = time.monotonic() + 15
    while snapshot['recovery'] is None or not row(snapshot).get('animation_pose'):
        assert time.monotonic() < deadline, snapshot
        time.sleep(.002)
        snapshot = worker.request('snapshot')
    return snapshot
dt = struct.unpack('f', struct.pack('f', 1 / 60))[0]
a = Worker(runtime, root)
restored = []
try:
    snapshot = admitted(a, a.request('replace', scene=scene))
    for tick in range(1, 25):
        snapshot = admitted(a, a.request('step')); current = row(snapshot); pose = current['animation_pose']; seconds = tick * dt
        mode = 1 if tick >= 20 else 0
        subtree = upper if 15 <= tick < 20 else None
        selected_mask = None if 5 <= tick < 10 else mask
        w = 0 if subtree else 1 if selected_mask is None else .25
        config = current['components']['forge.animator']
        assert current['components']['project.mask_probe']['ticks'] == tick
        assert config['blend_mask'] == selected_mask and config['blend_root'] == subtree and config['blend_mode'] == mode
        assert abs(pose['time'] - seconds) < 3e-6, pose
        base = (.25 + .25 * seconds, -.5 * seconds)
        second = (.25 + 1.25 * seconds, .25 * seconds)
        expected = tuple(base[i] + w * (second[i] - (.25, 0)[i]) if mode else (1-w)*base[i] + w*second[i] for i in (0, 1))
        values = pose['morphs'][0]['weights']
        assert all(abs(values[i] - expected[i]) < 3e-6 for i in (0, 1)), (tick, values, expected)
        written = next(r for r in snapshot['effective_scene']['entities'] if r['components'].get('forge.model_source', {}).get('node') == pose['joint_assets'][0])
        translation = written['components']['forge.local_translation']
        assert abs(translation['x'] - w * seconds) < .003, (tick, translation, w, seconds, pose)
        assert abs(translation['z'] - (seconds if mode else (1-w)*seconds)) < .003, (tick, translation, w, seconds, pose)
        for b in restored:
            recovered = admitted(b, b.request('step'))
            assert row(recovered)['animation_pose'] == pose, (tick, row(recovered), current)
        if tick in (4, 8, 12, 17, 22):
            checkpoint = snapshot['recovery']; assert checkpoint is not None
            b = Worker(runtime, root)
            recovered = b.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                                  recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
            assert row(recovered)['animation_pose'] == pose
            restored.append(b)
finally:
    a.close()
    for b in restored:
        b.close()
print('Actual C++ weighted mask/subtree/additive edits retain phase with five fresh shared-worker recoveries')
