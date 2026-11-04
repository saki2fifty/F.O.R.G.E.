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
        process=subprocess.Popen([HOST,str(root),mode,str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'),sys.argv[4]],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                 text=True,encoding='utf-8')
        try:
            line=process.stdout.readline()
            assert line,process.stderr.read()
            yield root,json.loads(line)
        finally:
            finish_host(process, root)


def until(fn,predicate,timeout=15):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=fn()
        if predicate(value): return value
        time.sleep(.01)
    raise AssertionError(value)


def png_has_chunk(data, wanted):
    at=8
    while at+12<=len(data):
        size=struct.unpack('>I',data[at:at+4])[0]
        assert at+12+size<=len(data)
        if data[at+4:at+8]==wanted:return True
        at+=size+12
    assert at==len(data)
    return False


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

        assert 'forge_capture_texture' in catalog
        assets=json.loads((root/'Game/forge.assets.json').read_text())['assets']
        badge=next(row for row in assets if row['source']=='Assets/Sprites/Badge.png')
        normal=next(row for row in assets if row['source']=='Assets/OcclusionNormals/RippleNormals.png')
        hdr_asset=next(row for row in assets if row['source']=='Assets/ReflectionCapture/Reflection.dds')
        opaque=next(row for row in assets if row['source']=='Assets/ReflectionProbes/WarmRoom.png')
        assert not (root/'Game'/opaque['source']).exists()
        precision=next(row for row in assets if row['source']=='Assets/TexturePrecision/Normalized16.png')
        assert not (root/'Game'/precision['source']).exists()
        assert not (root/'Game'/hdr_asset['source']).exists()
        assert not (root/'Game'/badge['source']).exists()
        assert not (root/'Game'/normal['source']).exists()
        authored=read('forge_scene_read')
        stale_texture=arguments(asset=badge['id'],path='StaleTexture.png')
        stale_texture['expected_revision']+=1
        failed(client.tool('forge_capture_texture',**stale_texture),'stale_revision')
        foreign_texture=arguments(asset=badge['id'],path='ForeignTexture.png')
        foreign_texture['target']=dict(foreign_texture['target'],session='foreign')
        failed(client.tool('forge_capture_texture',**foreign_texture),'wrong_target')
        assert not (root/'Output/StaleTexture.png').exists()
        assert not (root/'Output/ForeignTexture.png').exists()
        assert not read('forge_capture_status')['busy']
        texture_request=arguments(asset=badge['id'],path='Texture.png')
        admitted=client.tool('forge_capture_texture',**texture_request)
        assert admitted['ok'],admitted
        assert client.tool('forge_capture_texture',**texture_request)==admitted
        complete=settled()
        assert complete['view']=='texture' and complete['texture']['asset']==badge['id'],complete
        assert complete['texture']['profile']=='stored_rgba8' and complete['preview_ready'] and not complete['error'],complete
        metadata,texture_png=image(admitted['result']['operation'])
        assert (metadata['source_width'],metadata['source_height'])==(32,32),metadata
        assert png_has_chunk(texture_png,b'sRGB')
        assert texture_png==(root/'Output/Texture.png').read_bytes()
        failed(mutate('forge_capture_texture',asset=badge['id'],path='Texture.png'),'output_exists')
        assert not mutate('forge_capture_texture',asset=badge['id'],path='../Escape.png')['ok']
        assert not mutate('forge_capture_texture',asset=badge['id'],path='bad.tif')['ok']
        assert not mutate('forge_capture_texture',asset=badge['id'],path='bad.png',mip=32)['ok']
        ok('forge_capture_texture',asset=badge['id'],path='Texture.png',replace=True,mip=31)
        failure=settled()
        assert failure['error'] and (root/'Output/Texture.png').read_bytes()==texture_png,failure
        small=ok('forge_capture_texture',asset=badge['id'],path='Mip.png',mip=1)
        resized=settled();assert not resized['error'],resized
        small_meta,_=image(small['operation'])
        assert (small_meta['source_width'],small_meta['source_height'])==(16,16)
        webp=ok('forge_capture_texture',asset=badge['id'],path='Texture.webp')
        complete=settled();assert complete['file_mime']=='image/webp' and not complete['error'],complete
        webp_file=(root/'Output/Texture.webp').read_bytes()
        assert webp_file[:4]==b'RIFF' and webp_file[8:12]==b'WEBP'
        ok('forge_capture_texture',asset=normal['id'],semantic='normal',path='Texture.webp',replace=True)
        failure=settled()
        assert 'require PNG' in failure['error'] and (root/'Output/Texture.webp').read_bytes()==webp_file,failure
        compressed=next(row for row in assets if row['source']=='Assets/TextureBlocks/BlockColors.dds')
        assert not (root/'Game'/compressed['source']).exists()
        compressed_request=arguments(asset=compressed['id'],path='Blocks.png')
        admitted_blocks=client.tool('forge_capture_texture',**compressed_request)
        assert admitted_blocks['ok'],admitted_blocks
        assert client.tool('forge_capture_texture',**compressed_request)==admitted_blocks
        complete=settled()
        assert not complete['error'] and complete['component_bits']==8,complete
        assert complete['texture']['profile']=='decoded_rgba8' and complete['preview_ready'],complete
        block_meta,block_png=image(admitted_blocks['result']['operation'])
        assert (block_meta['source_width'],block_meta['source_height'])==(64,32)
        assert png_has_chunk(block_png,b'sRGB') and block_png==(root/'Output/Blocks.png').read_bytes()
        failed(mutate('forge_capture_texture',asset=compressed['id'],path='Blocks.png'),'output_exists')
        assert not mutate('forge_capture_texture',asset=compressed['id'],path='../Blocks.png')['ok']
        ok('forge_capture_texture',asset=compressed['id'],path='Blocks.png',replace=True,mip=2)
        failure=settled()
        assert failure['error'] and (root/'Output/Blocks.png').read_bytes()==block_png,failure
        ok('forge_capture_texture',asset=compressed['id'],path='Blocks.webp')
        complete=settled()
        assert not complete['error'] and complete['file_mime']=='image/webp',complete
        assert complete['texture']['profile']=='decoded_rgba8',complete
        ok('forge_capture_texture',asset=compressed['id'],path='Blocks.jpg',mip=1,jpeg_quality=76)
        complete=settled()
        assert not complete['error'] and complete['texture']['profile']=='decoded_rgba8',complete
        block_jpeg=(root/'Output/Blocks.jpg').read_bytes()
        ok('forge_capture_texture',asset=compressed['id'],path='Blocks.jpg',replace=True)
        failure=settled()
        assert 'opaque' in failure['error'] and (root/'Output/Blocks.jpg').read_bytes()==block_jpeg,failure
        assert read('forge_scene_read')==authored
        jpeg_schema=catalog['forge_capture_texture']['inputSchema']['properties']['jpeg_quality']
        assert jpeg_schema['minimum']==1 and jpeg_schema['maximum']==100
        for quality in (0,101,1.5):
            assert not mutate('forge_capture_texture',asset=opaque['id'],path='Invalid.jpg',jpeg_quality=quality)['ok']
        failed(mutate('forge_capture_texture',asset=opaque['id'],path='Invalid.png',jpeg_quality=90),'invalid_arguments')
        assert not (root/'Output/Invalid.jpg').exists() and not (root/'Output/Invalid.png').exists()
        jpeg_request=arguments(asset=opaque['id'],path='Opaque.JPEG',jpeg_quality=37,mip=0)
        jpeg_job=client.tool('forge_capture_texture',**jpeg_request)
        assert jpeg_job['ok'],jpeg_job
        assert client.tool('forge_capture_texture',**jpeg_request)==jpeg_job
        jpeg_complete=settled()
        assert not jpeg_complete['error'] and jpeg_complete['preview_ready'],jpeg_complete
        assert jpeg_complete['jpeg_quality']==37 and jpeg_complete['file_mime']=='image/jpeg'
        jpeg=(root/'Output/Opaque.JPEG').read_bytes()
        assert jpeg[:2]==b'\xff\xd8' and jpeg[-2:]==b'\xff\xd9'
        _,jpeg_preview=image(jpeg_job['result']['operation'])
        assert png_has_chunk(jpeg_preview,b'sRGB')
        assert struct.unpack('>II',jpeg_preview[16:24])==(256,128)
        ok('forge_capture_texture',asset=opaque['id'],path='Opaque.JPEG',replace=True,mip=1)
        failure=settled()
        assert 'opaque' in failure['error'] and (root/'Output/Opaque.JPEG').read_bytes()==jpeg,failure
        jpeg_again=ok('forge_capture_texture',asset=opaque['id'],path='Opaque.JPEG',replace=True,jpeg_quality=37)
        jpeg_complete=settled()
        assert not jpeg_complete['error']
        preview_job=ok('forge_capture_preview',artifact_operation=jpeg_again['artifact_operation'])
        assert settled()['preview_ready']
        assert image(preview_job['operation'])[1]==jpeg_preview
        failed(mutate('forge_capture_texture',asset=opaque['id'],path='Opaque.JPEG'),'output_exists')
        for asset,semantic,diagnostic in ((badge,None,'opaque'),(normal,'normal','require PNG'),(hdr_asset,'hdr','uncompressed')):
            request=dict(asset=asset['id'],path='Opaque.JPEG',replace=True)
            if semantic:request['semantic']=semantic
            ok('forge_capture_texture',**request)
            failure=settled()
            assert diagnostic in failure['error'] and (root/'Output/Opaque.JPEG').read_bytes()==jpeg,failure
        normal_job=ok('forge_capture_texture',asset=normal['id'],semantic='normal',path='Normal.png')
        complete=settled();assert not complete['error'] and complete['preview_ready'],complete
        _,normal_image=image(normal_job['operation'])
        assert not png_has_chunk(normal_image,b'sRGB')
        precision_request=arguments(asset=precision['id'],path='Precision.PNG')
        precision_job=client.tool('forge_capture_texture',**precision_request)
        assert precision_job['ok'],precision_job
        assert client.tool('forge_capture_texture',**precision_request)==precision_job
        precision_complete=settled()
        assert not precision_complete['error'] and precision_complete['preview_ready'],precision_complete
        assert precision_complete['component_bits']==16 and precision_complete['file_mime']=='image/png'
        assert precision_complete['texture']['profile']=='stored_rgba16'
        full_precision=(root/'Output/Precision.PNG').read_bytes()
        assert full_precision[24:26]==bytes([16,6]) and not png_has_chunk(full_precision,b'sRGB')
        precision_meta,precision_preview=image(precision_job['result']['operation'])
        assert precision_meta['source_width']==256 and precision_meta['source_height']==96
        assert precision_preview[24:26]==bytes([8,6]) and not png_has_chunk(precision_preview,b'sRGB')
        later=ok('forge_capture_preview',artifact_operation=precision_complete['artifact_operation'])
        assert settled()['component_bits']==16
        assert image(later['operation'])[1]==precision_preview
        failed(mutate('forge_capture_texture',asset=precision['id'],path='Precision.PNG'),'output_exists')
        for suffix in ('.webp','.jpg','.hdr'):
            destination=root/('Output/Keep'+suffix)
            destination.write_bytes(b'last good artifact')
            ok('forge_capture_texture',asset=precision['id'],path=destination.name,replace=True)
            failure=settled()
            assert failure['error'] and destination.read_bytes()==b'last good artifact',failure
        assert (root/'Output/Precision.PNG').read_bytes()==full_precision
        failed(mutate('forge_capture_start',view='scene',path='Display.hdr'),'invalid_path')
        hdr_request=arguments(asset=hdr_asset['id'],semantic='hdr',face=5,path='Radiance.hdr')
        admitted_hdr=client.tool('forge_capture_texture',**hdr_request)
        assert admitted_hdr['ok'],admitted_hdr
        assert client.tool('forge_capture_texture',**hdr_request)==admitted_hdr
        hdr_complete=settled()
        assert not hdr_complete['error'] and hdr_complete['preview_ready'] and not hdr_complete['preview_error'],hdr_complete
        assert hdr_complete['file_mime']=='image/vnd.radiance',hdr_complete
        assert hdr_complete['texture']['profile']=='linear_rgbe' and hdr_complete['texture']['face']==5
        assert hdr_complete['preview_profile']=='hdr_reinhard_srgb',hdr_complete
        hdr_file=(root/'Output/Radiance.hdr').read_bytes()
        assert hdr_file.startswith(b'#?RADIANCE\n') and b'\n-Y 128 +X 128\n' in hdr_file
        assert b'PRIMARIES=0.6400 0.3300 0.3000 0.6000 0.1500 0.0600 0.3127 0.3290' in hdr_file
        hdr_meta,hdr_png=image(hdr_complete['operation'])
        assert hdr_meta['preview_profile']=='hdr_reinhard_srgb'
        assert (hdr_meta['source_width'],hdr_meta['source_height'])==(128,128)
        assert png_has_chunk(hdr_png,b'sRGB') and hdr_png!=hdr_file
        failed(mutate('forge_capture_texture',asset=hdr_asset['id'],path='Radiance.hdr'),'output_exists')
        ok('forge_capture_texture',asset=badge['id'],path='Radiance.hdr',replace=True)
        wrong=settled()
        assert 'HdrColor' in wrong['error'] and (root/'Output/Radiance.hdr').read_bytes()==hdr_file,wrong
        automatic_hdr=ok('forge_capture_texture',asset=hdr_asset['id'],path='Auto.hdr',face=5)
        assert not settled()['error'] and (root/'Output/Auto.hdr').read_bytes()==hdr_file
        ok('forge_capture_preview',artifact_operation=automatic_hdr['artifact_operation'])
        later=settled()
        assert not later['error'] and later['preview_ready'] and later['preview_profile']=='hdr_reinhard_srgb',later
        later_meta,later_png=image(later['operation'])
        assert later_png==hdr_png and later_meta['preview_profile']=='hdr_reinhard_srgb'
        assert (root/'Output/Auto.hdr').read_bytes()==hdr_file
        tampered=hdr_file+b'\0'
        (root/'Output/Auto.hdr').write_bytes(tampered)
        ok('forge_capture_preview',artifact_operation=later['artifact_operation'])
        rejected=settled()
        assert rejected['error'] and not rejected['preview_ready'] and 'preview_profile' not in rejected,rejected
        assert (root/'Output/Auto.hdr').read_bytes()==tampered and rejected['file'].endswith('Auto.hdr')
        (root/'Output/Auto.hdr').write_bytes(hdr_file)
        ok('forge_capture_preview',artifact_operation=rejected['artifact_operation'])
        restored=settled()
        assert not restored['error'] and restored['preview_ready'],restored
        assert image(restored['operation'])[1]==hdr_png
        assert read('forge_scene_read')==authored

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

        failed(mutate('forge_capture_start',view='scene',path='Invalid.webp',jpeg_quality=50),'invalid_arguments')
        jpeg_request=arguments(view='scene',path='Scene.jpg')
        jpeg_job=client.tool('forge_capture_start',**jpeg_request)
        assert jpeg_job['ok'],jpeg_job
        assert client.tool('forge_capture_start',**jpeg_request)==jpeg_job
        jpeg_complete=settled()
        assert jpeg_complete['jpeg_quality']==90 and jpeg_complete['preview_ready'] and not jpeg_complete['error'],jpeg_complete
        assert jpeg_complete['file_mime']=='image/jpeg'
        assert (root/'Output/Scene.jpg').read_bytes()[:2]==b'\xff\xd8'
        assert image(jpeg_job['result']['operation'])[0]['source_width']==4
        failed(mutate('forge_capture_start',view='scene',path='Scene.jpg'),'output_exists')

        # Full files use the requested codec; agent image content stays PNG.
        webp_job=ok('forge_capture_start',view='scene',path='Screenshots/Scene.WEbP')
        webp_complete=settled()
        assert webp_complete['preview_ready'] and not webp_complete['error'],webp_complete
        assert webp_complete['file_mime']=='image/webp'
        full_webp=(root/'Output/Screenshots/Scene.WEbP').read_bytes()
        assert full_webp[:4]==b'RIFF' and full_webp[8:12]==b'WEBP'
        assert image(webp_job['operation'])[1]==png
        failed(mutate('forge_capture_start',view='scene',path='Screenshots/Scene.WEbP'),'output_exists')
        assert not mutate('forge_capture_start',view='scene',path='../Escape.webp')['ok']
        failed(mutate('forge_capture_start',view='scene',path='unsupported.tif'),'invalid_path')
        assert read('forge_capture_status')['artifact_operation']==webp_complete['artifact_operation']
        webp_preview=ok('forge_capture_preview',artifact_operation=webp_complete['artifact_operation'])
        assert settled()['preview_ready']
        assert image(webp_preview['operation'])[1]==png
        assert (root/'Output/Screenshots/Scene.WEbP').read_bytes()==full_webp

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
        assert 'forge_capture_texture' not in names

with gzip.open(sys.argv[3],'wt',encoding='utf-8') as output: json.dump(CAPTURE,output,separators=(',',':'))
