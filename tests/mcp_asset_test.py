"""Real owning editor controllers and installed native import workers through MCP."""
from contextlib import contextmanager
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import wave

from mcp_test_client import CAPTURE, Client, failed, write_capture, finish_host

TOOLS, HOST, WORKER, CONVERTER = map(str, map(Path, sys.argv[1:5]))


@contextmanager
def editor(mode='edit'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-assets-') as directory:
        root = Path(directory)
        process = subprocess.Popen([HOST, str(root), WORKER, CONVERTER, mode], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding='utf-8')
        try:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            yield root, process, json.loads(line)
        finally:
            finish_host(process)


def wait_for(read, predicate, seconds=30):
    end = time.monotonic() + seconds
    value = read()
    while not predicate(value):
        assert time.monotonic() < end, value
        time.sleep(.02)
        value = read()
    return value


for legacy in (False, True):
    with editor() as (root, process, config), Client(config, legacy) as client:
        def discover():
            result = client.tool('forge_discover')
            assert result['ok'], result
            return result

        def read(tool_name, **extra):
            state = discover()
            result = client.tool(tool_name, target=state['result']['target'], **extra)
            assert result['ok'], result
            return result['result']

        def arguments(**extra):
            state = discover()
            return dict(target=state['result']['target'], expected_revision=state['revision'],
                        sequence=state['next_sequence'], **extra)

        def mutate(tool_name, **extra):
            return client.tool(tool_name, **arguments(**extra))

        def jobs():
            return read('forge_asset_jobs')

        catalog = client.request('tools/list')['result']['tools']
        names = {tool['name'] for tool in catalog}
        assert {'forge_asset_search', 'forge_asset_read', 'forge_asset_import_prepare',
                'forge_asset_import_commit', 'forge_asset_place', 'forge_asset_files_prepare',
                'forge_asset_files_commit'} <= names
        workflows = discover()['result']['workflow_methods']
        for workflow in workflows:
            tool = next(item for item in catalog if item['name'] ==
                        'forge_' + workflow['method'].replace('.', '_'))
            props = dict(tool['inputSchema']['properties'])
            for key in ('target', 'expected_revision', 'sequence'):
                props.pop(key, None)
            assert props == workflow['input_schema']['properties']
        engine = read('forge_asset_search', text='Cube', type='mesh', limit=1)
        assert engine['total'] >= 1 and len(engine['assets']) == 1
        cube = engine['assets'][0]['asset']
        assert read('forge_asset_read', asset=cube)['engine_owned']
        assert read('forge_asset_search', include_engine=False)['total'] == 0

        # Placement shares actual entity selection, source validation and Scene Undo.
        placed = mutate('forge_asset_place', asset=cube, position=[1, 2, 3], name='MCP placed cube')
        assert placed['ok'], placed
        placed_job = placed['result']['job']
        outcome = wait_for(jobs, lambda value: value['placement']['state'] != 'queued')
        assert outcome['placement']['state'] == 'succeeded', outcome
        snapshot = read('forge_scene_read')
        assert snapshot['entities'][0]['name'] == 'MCP placed cube'
        assert mutate('forge_history_undo')['result']['changed']
        assert not read('forge_scene_read')['entities']
        failed(mutate('forge_asset_place_cancel', job=placed_job), 'wrong_job')
        (root / 'pause_placement').touch()
        pending = mutate('forge_asset_place', asset=cube, position=[0, 0, 0])
        assert pending['ok'], pending
        assert mutate('forge_asset_place_cancel', job=pending['result']['job'])['ok']
        (root / 'pause_placement').unlink()
        assert wait_for(jobs, lambda value: value['placement']['state'] == 'cancelled')
        assert not read('forge_scene_read')['entities']
        failed(mutate('forge_asset_place', asset=cube, position=[0, 0]), 'invalid_arguments')
        failed(mutate('forge_asset_place', asset=cube, position=[0, 0, 0], clip=cube),
               'invalid_arguments')

        # An external source is available only under the explicitly granted root.
        wav = root / 'Sources/tone.wav'
        with wave.open(str(wav), 'wb') as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(8000)
            output.writeframes(struct.pack('<64h', *([1000, -1000] * 32)))
        for path in ('../escape.wav', str(wav), 'tone.wav\x00'):
            failed(mutate('forge_asset_import_prepare', sources=[path], root='import',
                          destination='Assets/Bad'), 'invalid_path')
        prepared = mutate('forge_asset_import_prepare', sources=['tone.wav'], root='import',
                          destination='Assets/AgentAudio')
        assert prepared['ok'], prepared
        batch = prepared['result']['batch']
        reviewed = wait_for(lambda: read('forge_asset_import_review'),
                            lambda value: value['state'] in ('review', 'failed'))
        assert reviewed['state'] == 'review' and reviewed['file_count'] == 1, reviewed
        failed(mutate('forge_asset_import_commit', batch=batch, review='0' * 64), 'stale_review')
        assert not (root / 'Game/Assets/AgentAudio').exists()
        failed(mutate('forge_asset_import_commit', batch=batch + 1, review=reviewed['review']),
               'wrong_job')
        committed_args = arguments(batch=batch, review=reviewed['review'])
        committed = client.tool('forge_asset_import_commit', **committed_args)
        assert committed['ok'], committed
        assert client.tool('forge_asset_import_commit', **committed_args)['result'] == committed['result']
        imported = wait_for(jobs, lambda value: value['source_import']['state'] in ('done', 'failed'), 60)
        assert imported['source_import']['published'] == 1, imported
        assert (root / 'Game/Assets/AgentAudio/Source-1/tone.wav').exists()
        assert mutate('forge_asset_import_close', batch=batch)['ok']
        records = read('forge_asset_search', include_engine=False, type='audio_clip')['assets']
        assert len(records) == 1, records
        audio = records[0]['asset']
        assert read('forge_asset_read', asset=audio)['state'] == 'available'
        before = read('forge_asset_read', asset=audio)['metadata']['forge.import']['source_digest']
        local_wav = root / 'Game' / records[0]['source']
        with wave.open(str(local_wav), 'wb') as output:
            output.setnchannels(1); output.setsampwidth(2); output.setframerate(8000)
            output.writeframes(struct.pack('<64h', *([2000, -2000] * 32)))
        assert mutate('forge_asset_reimport', assets=[audio])['ok']
        wait_for(lambda: read('forge_asset_read', asset=audio),
                 lambda value: value['metadata']['forge.import']['source_digest'] != before, 60)

        # Actual file owner: reviewed duplicate, stale confirmation, move and delete.
        duplicate = mutate('forge_asset_files_prepare', asset=audio, action='duplicate',
                           destination='Assets/duplicate.wav')
        assert duplicate['ok'], duplicate
        job = duplicate['result']['job']
        review = wait_for(lambda: read('forge_asset_files_review'),
                          lambda value: value['state'] in ('review', 'failed'))
        assert review['state'] == 'review', review
        failed(mutate('forge_asset_files_commit', job=job, review='0' * 64), 'stale_review')
        assert mutate('forge_asset_files_commit', job=job, review=review['review'])['ok']
        complete = wait_for(lambda: read('forge_asset_files_review'),
                            lambda value: value['state'] in ('succeeded', 'failed'))
        assert complete['state'] == 'succeeded', complete
        duplicate_id = complete['result']
        assert duplicate_id != audio and local_wav.exists()
        assert mutate('forge_asset_files_close', job=job)['ok']
        moved = mutate('forge_asset_files_prepare', asset=duplicate_id, action='move',
                       destination='Assets/moved.wav')
        assert moved['ok'], moved
        job = moved['result']['job']
        review = wait_for(lambda: read('forge_asset_files_review'),
                          lambda value: value['state'] in ('review', 'failed'))
        assert review['state'] == 'review', review
        assert mutate('forge_asset_files_commit', job=job, review=review['review'])['ok']
        complete = wait_for(lambda: read('forge_asset_files_review'),
                            lambda value: value['state'] in ('succeeded', 'failed'))
        assert complete['state'] == 'succeeded', complete
        assert complete['result'] == duplicate_id
        assert mutate('forge_asset_files_close', job=job)['ok']
        deleted = mutate('forge_asset_files_prepare', asset=duplicate_id, action='delete')
        assert deleted['ok'], deleted
        job = deleted['result']['job']
        review = wait_for(lambda: read('forge_asset_files_review'),
                          lambda value: value['state'] in ('review', 'failed'))
        assert review['state'] == 'review', review
        failed(mutate('forge_asset_files_commit', job=job, review=review['review']),
               'confirmation_required')
        assert (root / 'Game/Assets/moved.wav').exists()
        assert mutate('forge_asset_files_commit', job=job, review=review['review'], acknowledge=True)['ok']
        complete = wait_for(lambda: read('forge_asset_files_review'),
                            lambda value: value['state'] in ('succeeded', 'failed'))
        assert complete['state'] == 'succeeded', complete
        assert not (root / 'Game/Assets/moved.wav').exists()
        assert (root / 'Game' / complete['receipt']['retained_files']).is_dir()
        assert mutate('forge_asset_files_close', job=job)['ok']
        assert config['token'] not in json.dumps(CAPTURE)

        # Original FORGE triangle fixture: real glTF dependency copy, model cooking
        # and the same asynchronous model-placement service as viewport drops.
        binary = root / 'Sources/triangle.bin'
        payload = struct.pack('<17f', 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0)
        binary.write_bytes(payload)
        gltf = dict(asset=dict(version='2.0'),
                    buffers=[dict(byteLength=len(payload), uri='triangle.bin')],
                    bufferViews=[dict(buffer=0, byteLength=36),
                                 dict(buffer=0, byteOffset=36, byteLength=8),
                                 dict(buffer=0, byteOffset=44, byteLength=24)],
                    accessors=[dict(bufferView=0, componentType=5126, type='VEC3', count=3,
                                    min=[0, 0, 0], max=[1, 1, 0]),
                               dict(bufferView=1, componentType=5126, type='SCALAR', count=2, min=[0], max=[1]),
                               dict(bufferView=2, componentType=5126, type='VEC3', count=2)],
                    meshes=[dict(primitives=[dict(attributes=dict(POSITION=0))])],
                    nodes=[dict(mesh=0, name='Triangle mesh')], scenes=[dict(nodes=[0])], scene=0,
                    animations=[dict(name='Lift', samplers=[dict(input=1, output=2, interpolation='LINEAR')],
                                     channels=[dict(sampler=0, target=dict(node=0, path='translation'))])])
        (root / 'Sources/triangle.gltf').write_text(json.dumps(gltf), encoding='utf-8')
        prepared = mutate('forge_asset_import_prepare', sources=['triangle.gltf'], root='import',
                          destination='Assets/AgentModel')
        assert prepared['ok'], prepared
        review = wait_for(lambda: read('forge_asset_import_review'),
                          lambda value: value['state'] in ('review', 'failed'))
        assert review['state'] == 'review' and review['file_count'] == 2, review
        # The copy owner rechecks dependencies even after a valid review token.
        binary.write_bytes(struct.pack('<f', .25) + payload[4:])
        batch = review['batch']
        assert mutate('forge_asset_import_commit', batch=batch, review=review['review'])['ok']
        stale = wait_for(jobs, lambda value: value['source_import']['state'] in ('done', 'failed'))
        assert stale['source_import']['state'] == 'failed', stale
        assert not (root / 'Game/Assets/AgentModel').exists()
        assert mutate('forge_asset_import_close', batch=batch)['ok']
        assert mutate('forge_asset_import_prepare', sources=['triangle.gltf'], root='import',
                      destination='Assets/AgentModel')['ok']
        review = wait_for(lambda: read('forge_asset_import_review'),
                          lambda value: value['state'] in ('review', 'failed'))
        assert review['state'] == 'review', review
        batch = review['batch']
        assert mutate('forge_asset_import_commit', batch=batch, review=review['review'])['ok']
        imported = wait_for(jobs, lambda value: value['source_import']['state'] in ('done', 'failed'), 60)
        assert imported['source_import']['published'] == 1, imported
        assert mutate('forge_asset_import_close', batch=batch)['ok']
        models = read('forge_asset_search', type='model', include_engine=False)['assets']
        assert len(models) == 1, models
        model = models[0]['asset']
        clips = [asset for asset in read('forge_asset_search', type='animation_clip', include_engine=False)['assets']
                 if asset.get('subasset', {}).get('owner') == model]
        assert len(clips) == 1, clips
        clip = clips[0]['asset']
        placed = mutate('forge_asset_place', asset=model, position=[0, 0, 0],
                         source_scene=0, clip=clip, name='MCP Triangle')
        assert placed['ok'], placed
        outcome = wait_for(jobs, lambda value: value['placement']['state'] in ('succeeded', 'failed'), 30)
        assert outcome['placement']['state'] == 'succeeded', outcome
        snapshot = read('forge_scene_read')
        assert any(entity['name'] == 'MCP Triangle' for entity in snapshot['entities']), snapshot
        assert clip in json.dumps(snapshot), snapshot
        assert mutate('forge_history_undo')['result']['changed']
        assert not read('forge_scene_read')['entities']
        assert mutate('forge_asset_place', asset=model, position=[0, 0, 0], source_scene=99)['ok']
        outcome = wait_for(jobs, lambda value: value['placement']['state'] in ('succeeded', 'failed'))
        assert outcome['placement']['state'] == 'failed', outcome
        assert not read('forge_scene_read')['entities']

with editor('read') as (root, process, config), Client(config) as client:
    catalog = client.request('tools/list')['result']['tools']
    assert 'forge_asset_search' in {tool['name'] for tool in catalog}
    assert not any(tool['name'] in {'forge_asset_import_prepare', 'forge_asset_files_prepare',
                                  'forge_asset_place', 'forge_asset_reimport'} for tool in catalog)
    assert client.request('tools/call', dict(name='forge_asset_place', arguments={}))['error']['code'] == -32602

if len(sys.argv) > 5:
    write_capture(sys.argv[5])
print('MCP assets: existing catalog, workers, review/commit, replay, placement/history, '
      'file impact/identity/recovery, permissions and project/source scope passed')
