"""Real Timeline/capture owners with detached fixture pixels; no GPU claim."""
import json
from mcp_test_client import failed


def check_clip_frames(read, mutate, ok, settled, image, root, catalog):
    assert catalog['forge_capture_sequence_read']['annotations']['readOnlyHint']
    assert not catalog['forge_capture_sequence_start']['annotations']['readOnlyHint']
    source = read('forge_capture_sequence_read')
    assert source['ready'] and source['source_kind'] == 'clip' and source['duration'] == 2
    assert source['controller'] == source['target'] and source['clip']
    authored = read('forge_scene_read')

    def request(path, **extra):
        observed = read('forge_capture_sequence_read')
        return dict(path=path, controller=observed['controller'],
                    source_revision=observed['source_revision'],
                    preview_generation=observed['preview_generation'], start_seconds=0,
                    frames_per_second=2, frame_count=3, fixed_output=True, width=32, height=24) | extra

    for extra in (dict(preview_generation=source['preview_generation']+1),
                  dict(source_revision=source['source_revision']+1),
                  dict(controller='00000000-0000-4000-8000-000000000099')):
        failed(mutate('forge_capture_sequence_start', **request('StaleClip', **extra)), 'stale_preview')
        assert not (root/'Output/StaleClip').exists()
    invalid = mutate('forge_capture_sequence_start', **request('Endpoint', start_seconds=2))
    assert not invalid['ok'] and not (root/'Output/Endpoint').exists()
    for output in ('png', 'webp', 'avi'):
        print('Clip capture '+output+': start', flush=True)
        folder = 'Clip-'+output
        started = ok('forge_capture_sequence_start', **request(folder, format=output))
        complete = settled()
        assert not complete['error'] and complete['sequence']['completed_frames'] == 3, complete
        assert complete['sequence']['source_kind'] == 'clip'
        assert complete['sequence']['clip'] == source['clip']
        receipt = json.loads((root/'Output'/folder/'frames.json').read_text())
        assert receipt['format'] == 'forge.clip-preview-frames' and receipt['version'] == 1
        assert receipt['state'] == 'complete' and receipt['completed_frames'] == 3
        assert receipt['clip'] == source['clip'] and receipt['target'] == source['target']
        assert 'sequence' not in receipt
        assert [f['time_seconds'] for f in receipt['frames']] == [0, .5, 1]
        for frame in receipt['frames']:
            assert (root/'Output'/folder/frame['file']).read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
        if output != 'png':
            assert (root/'Output'/folder/('animation.'+output)).is_file()
            assert receipt['animation']['frames'] == 3
        assert read('forge_capture_sequence_read')['ready']
        assert read('forge_scene_read') == authored
        result, pixels = image(complete['operation'])
        assert result['source_width'] == 32 and result['source_height'] == 24 and pixels
        print('Clip capture '+output+': complete', flush=True)
    (root/'hold_pixels').touch()
    running = ok('forge_capture_sequence_start', **request('CancelledClip'))
    failed(mutate('forge_capture_cancel', operation='00000000-0000-4000-8000-000000000099'), 'wrong_job')
    ok('forge_capture_cancel', operation=running['operation'])
    cancelled = settled()
    assert cancelled['sequence']['state'] == 'cancelled', cancelled
    assert read('forge_capture_sequence_read')['ready'] and read('forge_scene_read') == authored
    (root/'hold_pixels').unlink()
    print('Clip MCP frames: real Timeline samples, metadata/guards, PNG/WebP/AVI, image reads, '
          'cancellation and authored isolation passed; detached fixture pixels, not GPU acceptance')
