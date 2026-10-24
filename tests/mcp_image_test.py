"""Native image IO/recording frame source and real MCP image blocks; no GPU claim."""
from contextlib import contextmanager
import base64
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
from mcp_test_client import CAPTURE, Client, failed, test_symlink, finish_host

TOOLS, HOST = sys.argv[1:3]


@contextmanager
def editor(mode='output'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-image-') as temporary:
        root=Path(temporary)
        process=subprocess.Popen([HOST,str(root),mode],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                 text=True,encoding='utf-8')
        try:
            line=process.stdout.readline()
            assert line,process.stderr.read()
            yield root,json.loads(line)
        finally:
            finish_host(process)


def until(fn,predicate,timeout=15):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=fn()
        if predicate(value): return value
        time.sleep(.01)
    raise AssertionError(value)


for legacy in (False,True):
    with editor() as (root,config),Client(config,legacy) as client:
        catalog={tool['name']:tool for tool in client.request('tools/list')['result']['tools']}
        assert catalog['forge_capture_image']['annotations']['readOnlyHint']
        assert {'forge_capture_sequence_start','forge_output_folder_create'} <= set(catalog)
        def discovery():
            value=client.tool('forge_discover'); assert value['ok'],value; return value
        def read(tool,**extra):
            value=client.tool(tool,target=discovery()['result']['target'],**extra)
            assert value['ok'],value
            return value['result']
        def arguments(**extra):
            value=discovery()
            return dict(target=value['result']['target'],expected_revision=value['revision'],sequence=value['next_sequence'],**extra)
        def mutate(tool,**extra): return client.tool(tool,**arguments(**extra))
        def ok(tool,**extra):
            value=mutate(tool,**extra); assert value['ok'],value; return value['result']
        def settled(): return until(lambda:read('forge_capture_status'),lambda r:not r['busy'])
        def image(operation):
            result=client.request('tools/call',dict(name='forge_capture_image',arguments=dict(
                target=discovery()['result']['target'],operation=operation)))['result']
            assert not result['isError'],result
            blocks=result['content']; assert len(blocks)==2 and blocks[0]['type']=='text'
            assert blocks[1]['type']=='image' and blocks[1]['mimeType']=='image/png'
            plain=json.loads(blocks[0]['text']); assert plain==result['structuredContent']
            assert 'data' not in plain['result']['image']  # No base64 dump into model text.
            png=base64.b64decode(blocks[1]['data'],validate=True)
            assert png[:8]==b'\x89PNG\r\n\x1a\n'
            assert struct.unpack('>II',png[16:24])==(plain['result']['width'],plain['result']['height'])
            assert len(png)==plain['result']['image']['bytes']
            return plain['result'],png

        (root/'hold_pixels').touch()
        ok('forge_output_folder_create',path='Screenshots')
        failed(mutate('forge_output_folder_create',path='Screenshots'),'output_exists')
        assert not mutate('forge_output_folder_create',path='../Escape')['ok']
        request=arguments(view='scene',path='Scene.png')
        admitted=client.tool('forge_capture_start',**request); assert admitted['ok'],admitted
        assert client.tool('forge_capture_start',**request)==admitted
        operation=admitted['result']['operation']
        failed(mutate('forge_capture_cancel',operation='11111111-1111-4111-8111-111111111111'),'wrong_job')
        ok('forge_capture_cancel',operation=operation)
        assert not settled()['preview_ready'] and not (root/'Output/Scene.png').exists()
        (root/'hold_pixels').unlink()
        ready=ok('forge_capture_start',view='scene',path='Scene.png')
        complete=settled(); assert complete['preview_ready'] and not complete['error'],complete
        metadata,png=image(ready['operation'])
        assert metadata['source_width']==4 and metadata['source_height']==3
        assert hashlib.sha256(png).digest()==hashlib.sha256((root/'Output/Scene.png').read_bytes()).digest()
        failed(mutate('forge_capture_start',view='scene',path='Scene.png'),'output_exists')
        assert not mutate('forge_capture_start',view='scene',path='../Escape.png')['ok']
        with test_symlink(root/'Output/Link.png', root/'Game/forge.project.json') as linked:
            if linked:
                assert not mutate('forge_capture_start',view='scene',path='Link.png',replace=True)['ok']
        preview=ok('forge_capture_preview',artifact_operation=complete['artifact_operation'])
        selected=settled(); assert selected['preview_ready'],selected
        assert image(preview['operation'])[1]==png

        entity=ok('forge_entity_create',arguments=dict(kind=0,name='Recording controller'))['results'][0]['selected']
        source=ok('forge_capture_sequence_open',controller=entity)
        assert source['ready']
        def sequence(**extra):
            return mutate('forge_capture_sequence_start',path='Frames',controller=source['controller'],
                source_revision=source['source_revision'],preview_generation=source['preview_generation'],
                start_seconds=0,frames_per_second=2,frame_count=3,**extra)
        stale=dict(source); stale['preview_generation']+=1
        failed(mutate('forge_capture_sequence_start',path='Stale',controller=source['controller'],
            source_revision=source['source_revision'],preview_generation=stale['preview_generation'],
            start_seconds=0,frames_per_second=2,frame_count=3),'stale_preview')
        assert not (root/'Output/Stale').exists()
        started=sequence(fixed_output=True,width=2048,height=512)
        assert started['ok'],started
        batch=settled(); assert batch['sequence']['completed_frames']==3 and not batch['error'],batch
        receipt=json.loads((root/'Output/Frames/frames.json').read_text())
        assert receipt['completed_frames']==3 and receipt['width']==2048 and receipt['height']==512
        assert [frame['time_seconds'] for frame in receipt['frames']]==[0,.5,1]
        restored=read('forge_capture_sequence_read'); assert restored['ready']
        assert restored['preview_generation']!=source['preview_generation']
        last,thumb=image(batch['operation'])
        assert (last['source_width'],last['source_height'])==(2048,512)
        assert (last['width'],last['height'])==(1024,256)
        selected=ok('forge_capture_preview',artifact_operation=batch['artifact_operation'],frame=0)
        assert settled()['preview_ready']
        first,thumb0=image(selected['operation'])
        assert first['file']=='Frames/frame-000000.png' and thumb0==thumb
        (root/'fail_restore').touch()
        current=read('forge_capture_sequence_read')
        ok('forge_capture_sequence_start',path='FailedFrames',controller=current['controller'],
           source_revision=current['source_revision'],preview_generation=current['preview_generation'],
           start_seconds=0,frames_per_second=2,frame_count=1)
        broken=settled()
        assert broken['sequence']['state']=='failed' and 'restore failure' in broken['error'],broken
        assert broken['sequence']['error']==broken['error']
        failure=json.loads((root/'Output/FailedFrames/frames.json').read_text())
        assert failure['state']=='failed' and failure['completed_frames']==1
        assert failure['error']==broken['error'] and read('forge_capture_sequence_read')['ready']
        (root/'fail_restore').unlink()
        print(('legacy' if legacy else 'current')+': native PNG IO/preview, real MCP image blocks, '
              'scope/replay/cancel, recording frame sampling/receipt and preview resizing passed; no GPU claim')
    with editor('read') as (root,config),Client(config,legacy) as client:
        names={tool['name'] for tool in client.request('tools/list')['result']['tools']}
        assert 'forge_capture_status' in names and 'forge_capture_image' not in names
        assert 'forge_capture_start' not in names and 'forge_capture_sequence_start' not in names

with gzip.open(sys.argv[3],'wt',encoding='utf-8') as output: json.dump(CAPTURE,output,separators=(',',':'))
