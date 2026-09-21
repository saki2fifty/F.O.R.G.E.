"""Cooked-model animation loading, paused clocks and recovery through real runtime IPC."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

runtime, tools, scratch = map(lambda x: Path(x).resolve(), sys.argv[1:])
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as temporary:
    project = Path(temporary) / 'Model animation runtime'
    assets = project / 'Assets'
    assets.mkdir(parents=True)
    binary = assets / 'animation.bin'
    binary.write_bytes(struct.pack('<8f', 0, 1, 0, 0, 0, 1, 2, 3))
    source = assets / 'animation.gltf'
    source.write_text(json.dumps({
        'asset': {'version': '2.0'}, 'nodes': [{}], 'scenes': [{'nodes': [0]}], 'scene': 0,
        'buffers': [{'uri': 'animation.bin', 'byteLength': 32}],
        'bufferViews': [{'buffer': 0, 'byteLength': 8}, {'buffer': 0, 'byteOffset': 8, 'byteLength': 24}],
        'accessors': [{'bufferView': 0, 'componentType': 5126, 'type': 'SCALAR', 'count': 2, 'min': [0], 'max': [1]},
                      {'bufferView': 1, 'componentType': 5126, 'type': 'VEC3', 'count': 2}],
        'animations': [{'name': 'Move', 'samplers': [{'input': 0, 'output': 1}],
                        'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'translation'}}]}]
    }))

    def import_model():
        result = subprocess.run([str(tools), '--assets', 'import', str(project), 'Assets/animation.gltf'],
                                capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, (result.stdout, result.stderr)
        return json.loads(result.stdout)

    import_model()
    records = json.loads((project / 'forge.assets.json').read_text())['assets']
    skeleton = next(r['id'] for r in records if r['type'] == 'skeleton')
    clip = next(r['id'] for r in records if r['type'] == 'animation_clip')
    scene = {'version': 1, 'entities': [{'id': 'actor', 'name': 'Actor', 'components': {
        'forge.position': {'x': 0, 'y': 0, 'z': 0},
        'forge.animator': {'skeleton': skeleton, 'clip': clip, 'enabled': True,
                           'play_on_start': True, 'loop': False, 'playback_speed': 1}}}]}

    class Worker:
        def __init__(self):
            self.process = subprocess.Popen([str(runtime), '--project', str(project)], stdin=subprocess.PIPE,
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.session = ''
            self.sequence = 0

        def request(self, command, ok=True, **extra):
            self.sequence += 1
            self.process.stdin.write(json.dumps(dict(protocol=2, id=self.sequence, session=self.session,
                                                     command=command, **extra)) + '\n')
            self.process.stdin.flush()
            line = self.process.stdout.readline()
            assert line, self.process.stderr.read()
            response = json.loads(line)
            self.session = response['session']
            assert response['ok'] == ok, response
            return response

        def close(self):
            if self.process.poll() is None:
                try:
                    self.request('quit')
                    self.process.wait(timeout=5)
                finally:
                    if self.process.poll() is None:
                        self.process.kill()
                        self.process.wait()

    def pose(response):
        return response['effective_scene']['entities'][0].get('animation_pose')

    def restore(worker, checkpoint, ok=True):
        return worker.request('replace', ok=ok, scene=checkpoint['scene'], recovery=checkpoint,
                              recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])

    first = Worker()
    second = None
    try:
        first.request('hello', simulation_hz=60)
        pending = first.request('replace', scene=scene)
        assert pending['recovery'] is None, pending
        ready = pending
        deadline = time.monotonic() + 15
        while (pose(ready) is None or ready['recovery'] is None) and time.monotonic() < deadline:
            time.sleep(.002)
            ready = first.request('snapshot')
        assert ready['recovery'] is not None and pose(ready)['time'] == 0, ready
        assert ready['timing']['paused'] and ready['timing']['tick'] == 0
        stepped = first.request('step')
        assert abs(pose(stepped)['time'] - 1 / 60) < 1e-6
        assert abs(pose(stepped)['local'][0]['translation'][1] - 2 / 60) < .002
        checkpoint = stepped['recovery']
        first.process.kill()
        first.process.wait()
        second = Worker()
        second.request('hello', simulation_hz=60)
        recovered = restore(second, checkpoint)
        assert pose(recovered) == pose(stepped), (recovered, stepped)
        # Reimport while paused: a genuine old checkpoint must reject the newly
        # selected family, without replacing the running world's held good data.
        binary.write_bytes(struct.pack('<8f', 0, 1, 0, 0, 0, 2, 4, 6))
        import_model()
        restore(second, checkpoint, ok=False)
        retained = second.request('snapshot')
        assert pose(retained) == pose(stepped), retained
        assert retained['recovery'] == recovered['recovery']
        notified = second.request('refresh_model_assets')
        assert pose(notified) == pose(retained)
        # Paused preparation retains both time and the old published sample.
        for _ in range(20):
            time.sleep(.002)
            frozen = second.request('snapshot')
            assert pose(frozen) == pose(retained), frozen
        deadline = time.monotonic() + 15
        updated = second.request('step')
        while (pose(updated)['model_revision'] == pose(retained)['model_revision']
               and time.monotonic() < deadline):
            time.sleep(.002)
            updated = second.request('step')
        assert pose(updated)['model_revision'] != pose(retained)['model_revision'], updated
        assert pose(updated)['time'] > pose(retained)['time'], updated
        assert abs(pose(updated)['local'][0]['translation'][1] -
                   4 * pose(updated)['time']) < .003, updated
        # A corrupt catalog notification fails asynchronously without silencing
        # the active sampler or replacing the runtime world.
        catalog_path = project / 'forge.assets.json'
        saved_catalog = catalog_path.read_bytes()
        catalog_path.write_text('{invalid')
        second.request('refresh_model_assets')
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            frozen = second.request('snapshot')
            if any(d.get('category') == 'animation.catalog' for d in frozen['diagnostics']):
                break
            time.sleep(.002)
        else:
            raise AssertionError('Missing structured catalog failure')
        assert pose(frozen) == pose(updated), frozen
        catalog_path.write_bytes(saved_catalog)
        second.request('refresh_model_assets')
        resumed = second.request('step')
        assert pose(resumed)['model_revision'] == pose(updated)['model_revision']
    finally:
        first.close()
        if second:
            second.close()
print('Cooked model IPC loading, fixed-boundary refresh, failure retention and recovery passed')
