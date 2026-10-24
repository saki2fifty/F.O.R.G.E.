"""Create, compile, capture and export a C++ game through the packaged editor's MCP.

Only the explicit test fixture enables the temporary connection. All game edits,
builds, Play controls and output use the real editor owners through shipped tools.
The fixture EXE is separate from the package; shipped DLLs/SDK/resources are reused.
"""
import argparse
import base64
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import zipfile

import mcp_test_client as mcp


def wait_for(function, predicate, seconds=60):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = function()
        if predicate(value):
            return value
        time.sleep(.1)
    raise AssertionError(('Native MCP acceptance timed out', value))


def terminate_tree(process):
    if process.poll() is None:
        subprocess.run(['taskkill', '/F', '/T', '/PID', str(process.pid)],
                       capture_output=True, timeout=20)
        process.wait(timeout=15)


def exercise(client, evidence, manifest):
    def discover():
        response = client.tool('forge_discover')
        assert response['ok'], response
        return response

    def read(tool, **values):
        response = client.tool(tool, target=discover()['result']['target'], **values)
        assert response['ok'], response
        return response['result']

    def edit(tool, **values):
        # Wait for known preflight availability; never retry an uncertain edit.
        state = wait_for(discover, lambda r: any(
            w['method'].replace('.', '_') == tool.removeprefix('forge_') and w['available']
            for w in r['result'].get('workflow_methods', []))
            if tool.startswith(('forge_code_', 'forge_project_', 'forge_asset_document_',
                                'forge_capture_', 'forge_export_', 'forge_output_'))
            else r['result']['mutation_available'])
        response = client.tool(tool, target=state['result']['target'],
                               expected_revision=state['revision'],
                               sequence=state['next_sequence'], **values)
        assert response['ok'], response
        return response['result']

    def control(tool, **values):
        state = wait_for(lambda: read('forge_play_status'), lambda r: r['control_ready'])
        routing = discover()
        response = client.tool(tool, target=routing['result']['target'],
                               expected_revision=routing['revision'],
                               sequence=routing['next_sequence'], launch=state['launch'],
                               session=state['session'], generation=state['generation'], **values)
        assert response['ok'], response
        return response['result']

    def cpp(tool, path, **values):
        state = read('forge_code_read', path=path)
        return edit(tool, path=path, session=state['session'], state=state['state'], **values)

    def entity(recipe, name, position):
        return edit('forge_entity_create', arguments=dict(recipe=recipe, name=name,
                                                         position=position))['results'][0]['selected']

    def property_value(entity_id, component, field, value):
        return edit('forge_property_set', arguments=dict(entity=entity_id, component=component,
                                                        field=field, value=value))

    def document_edit(tool, **values):
        status = read('forge_asset_document_status', editor='material')
        return edit(tool, editor='material', document_session=status['session'],
                    document_revision=status['revision'], **values)

    tools = {t['name']: t for t in client.request('tools/list')['result']['tools']}
    assert {'forge_project_create', 'forge_code_build', 'forge_play_start',
            'forge_capture_image', 'forge_export_run'} <= tools.keys()
    assert tools['forge_code_build']['annotations']['openWorldHint']
    edit('forge_project_create', path='McpSpinCube', name='MCP Spin Cube')
    project = Path(read('forge_project_workspace_read')['workspace']) / 'McpSpinCube'
    wait_for(discover, lambda r: r['result']['mutation_available'])
    cube = entity('primitive.0', 'MCP C++ spinning cube', dict(x=0, y=1, z=0))
    entity('render.camera', 'Game camera', dict(x=0, y=1, z=-5))
    entity('render.light', 'Key light', dict(x=0, y=3, z=-3))

    edit('forge_asset_document_create', editor='material', path='Assets/Cube.material.json')
    material = read('forge_asset_document_status', editor='material')['asset']
    document_edit('forge_asset_document_save')
    saved = wait_for(lambda: read('forge_asset_document_status', editor='material'),
                     lambda r: not r['save_requested'] and not r['publishing'])
    assert not saved['dirty'] and not saved['diagnostic'], saved
    document_edit('forge_asset_document_close')
    property_value(cube, 'forge.mesh_renderer', 'materials',
                   [dict(slot='surface', material=material)])

    state = read('forge_project_settings_read')
    edit('forge_project_settings_game_defaults', state=state['state'])
    state = read('forge_project_settings_read')
    edit('forge_project_settings_save', state=state['state'])
    edit('forge_project_settings_close', state=read('forge_project_settings_read')['state'])
    edit('forge_code_setup')
    edit('forge_code_create', kind='component', name='Rotator')
    edit('forge_code_create', kind='system', name='Spin', component='Rotator')
    system_path = 'Native/Systems/Spin.cpp'
    source = read('forge_code_read', path=system_path)['text']
    anchor = 'entity.set<forge::LocalRotation>(next);'
    assert source.count(anchor) == 1, 'Generated system contract changed'
    # Bounded stderr proof is gameplay authored through MCP, not a runtime probe.
    # stdout remains exclusively the native Play protocol.
    marker = ('static unsigned observed = 0;\n'
              '                if (++observed % 15 == 0) {\n'
              '                    std::fprintf(stderr, "FORGE_MCP_SPIN %.3f %.6f\\n", '
              'double(data.speed), double(next.y));\n'
              '                    std::fflush(stderr);\n'
              '                }')
    source = '#include <cstdio>\n' + source.replace(anchor, anchor + '\n                ' + marker)
    cpp('forge_code_replace', system_path, text=source)
    cpp('forge_code_save', system_path)
    edit('forge_code_build')
    built = wait_for(lambda: read('forge_code_status'), lambda r: not r['busy'], 240)
    (evidence / 'compiler.log').write_text(read('forge_code_build_log')['text'], encoding='utf-8')
    assert not built['error'], built
    edit('forge_code_inspect')
    inspected = wait_for(lambda: read('forge_code_schemas'), lambda r: not r['busy'])
    assert inspected['ready'] and not inspected['error'], inspected
    assert 'project.rotator' in json.dumps(inspected['schemas'])
    for path in ('Native/Components/Rotator.hpp', system_path):
        cpp('forge_code_close', path)
    edit('forge_component_add', arguments=dict(entity=cube, component='project.rotator'))
    property_value(cube, 'project.rotator', 'speed', 180.0)
    edit('forge_document_save')

    edit('forge_play_start')
    wait_for(lambda: read('forge_play_status'), lambda r: r['ready'] and r['control_ready'], 120)
    before = wait_for(lambda: read('forge_play_log'), lambda r: 'FORGE_MCP_SPIN 180.000' in r['text'])
    control('forge_play_pause')
    paused = wait_for(lambda: read('forge_play_status'), lambda r: r['paused'] and r['control_ready'])
    tick = paused['timing']['tick']
    control('forge_play_step')
    wait_for(lambda: read('forge_play_status'),
             lambda r: r['paused'] and r['control_ready'] and r['timing']['tick'] == tick + 1)
    control('forge_play_tune', entity=cube, component='project.rotator', property='speed', value=-180.0)
    tune = wait_for(lambda: read('forge_play_status'),
                    lambda r: not r['tune_pending'] and r['tune_ack'] is not None)
    assert not tune['tune_ack'].get('error'), tune
    wait_for(lambda: read('forge_play_snapshot'), lambda r: any(
        e['id'] == cube and e['components'].get('project.rotator', {}).get('speed') == -180
        for e in r['entities']))
    control('forge_play_tuning_capture', entity=cube)
    control('forge_play_resume')
    after = wait_for(lambda: read('forge_play_log'), lambda r: 'FORGE_MCP_SPIN -180.000' in r['text'])
    control('forge_play_pause')
    wait_for(lambda: read('forge_play_status'), lambda r: r['paused'] and r['control_ready'])

    # This is real native renderer/readback via the normal ViewImageTask.
    capture = edit('forge_capture_start', view='game', path='Play.png')
    image_state = wait_for(lambda: read('forge_capture_status'), lambda r: not r['busy'], 120)
    assert image_state['preview_ready'] and not image_state['error'], image_state
    response = client.request('tools/call', dict(name='forge_capture_image', arguments=dict(
        target=discover()['result']['target'], operation=capture['operation'])))['result']
    assert not response['isError'], response
    block = next(c for c in response['content'] if c['type'] == 'image')
    pixels = base64.b64decode(block['data'], validate=True)
    assert pixels[:8] == b'\x89PNG\r\n\x1a\n' and block['mimeType'] == 'image/png'
    assert block['data'] not in response['content'][0]['text']
    (evidence / 'play-preview.png').write_bytes(pixels)
    shutil.copy2(image_state['file'], evidence / 'play-full.png')
    control('forge_play_stop')
    review = read('forge_play_tuning_read')
    assert any(f['component'] == 'project.rotator' and f['property'] == 'speed' and
               f['after'] == -180 for f in review['fields']), review
    edit('forge_play_tuning_apply', state=review['state'])
    edit('forge_history_undo')
    assert next(e for e in read('forge_scene_read')['entities'] if e['id'] == cube)[
        'components']['project.rotator']['speed'] == 180
    edit('forge_history_redo')
    edit('forge_document_save')
    (evidence / 'play.log').write_text(after['text'], encoding='utf-8')

    exported = edit('forge_export_start', path='SpinCubeGame')
    finished = wait_for(lambda: read('forge_export_status'), lambda r: not r['busy'], 180)
    assert finished['output'] and not finished['error'], finished
    output = Path(finished['output'])
    package = json.loads((output / 'forge.standalone.json').read_text())
    assert package['engine']['source_commit'] == manifest['source_commit']
    edit('forge_export_run', operation=exported['operation'])
    runtime = wait_for(lambda: read('forge_export_run_log'),
                       lambda r: 'FORGE_MCP_SPIN -180.000' in r['text'], 120)
    assert runtime['running'] and not runtime['error'], runtime
    runtime = wait_for(lambda: read('forge_export_run_log'), lambda r: len(re.findall(
        r'FORGE_MCP_SPIN -180\.000 (-?\d+\.\d+)', r['text'])) >= 4)
    samples = [float(q) for q in re.findall(r'FORGE_MCP_SPIN -180\.000 (-?\d+\.\d+)', runtime['text'])]
    assert all(math.isfinite(q) for q in samples) and max(samples) - min(samples) > .1
    (evidence / 'exported-game.log').write_text(runtime['text'], encoding='utf-8')
    edit('forge_export_cancel', operation=runtime['operation'])
    assert not read('forge_export_status')['running']
    for relative in ('forge.project.json', read('forge_project_read')['scene_path'],
                     'Native/Components/Rotator.hpp', system_path, 'Assets/Cube.material.json'):
        target = evidence / 'authored' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(project / relative, target)
    (evidence / 'export-manifest.json').write_text(json.dumps(package, indent=2), encoding='utf-8')
    return dict(complete=True, source_commit=manifest['source_commit'], build_id=manifest['build_id'],
                project_created=True, reusable_asset=True, cpp_compiled=True, isolated_inspection=True,
                actual_editor_play=True, fixed_step=True, live_cpp_tuning=True, keep_undo_redo=True,
                gpu_game_capture=True, mcp_image_block=True, native_export=True,
                shipped_game_executed=True, exported_cpp_observed=True,
                visual_review='Pending image inspection', physical_acceptance='User deferred')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zip', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    assert os.name == 'nt', 'This gate requires native Windows/D3D12'
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    token = ''
    report = dict(complete=False)
    with tempfile.TemporaryDirectory(prefix='FORGE MCP native ') as temporary:
        scratch = Path(temporary)
        root = scratch / 'package'
        with zipfile.ZipFile(args.zip) as archive:
            archive.extractall(root)
        manifest = json.loads((root / 'manifest.json').read_text())
        for relative, expected in manifest['files'].items():
            assert hashlib.sha256((root / relative).read_bytes()).hexdigest() == expected, relative
        fixture = root / 'bin/forge_editor_mcp_fixture.exe'
        shutil.copy2(args.fixture, fixture)
        tools = root / 'bin/forge_tools.exe'
        sdk = root / manifest['native_sdk']['path']
        connection = scratch / 'private-connection.json'
        output = scratch / 'fixture'
        process = None
        try:
            with (evidence / 'editor.stdout.log').open('w', encoding='utf-8') as stdout, \
                    (evidence / 'editor.stderr.log').open('w', encoding='utf-8') as stderr:
                process = subprocess.Popen([fixture, output, '--mcp-authoring', sdk, connection],
                                           stdout=stdout, stderr=stderr)
                wait_for(lambda: (connection.exists(), process.poll()),
                         lambda r: r[0] or r[1] is not None, 120)
                assert connection.exists(), 'Editor did not create its private MCP handoff'
                config = json.loads(connection.read_text())
                token = config['token']
                connection.unlink()
                with mcp.Client(config, tools=tools) as client:
                    report = exercise(client, evidence, manifest)
                (output / 'mcp-complete').touch()
                assert process.wait(timeout=30) == 0
        finally:
            if process is not None:
                terminate_tree(process)
            connection.unlink(missing_ok=True)
            # Requests/results contain no credentials. Never retain the connection JSON.
            mcp.write_capture(evidence / 'mcp-protocol-capture.json.gz')
            if token:
                with gzip.open(evidence / 'mcp-protocol-capture.json.gz', 'rb') as protocol:
                    tail = b''
                    for chunk in iter(lambda: protocol.read(1024 * 1024), b''):
                        assert token.encode() not in tail + chunk, 'Credential leak in MCP capture'
                        tail = chunk[-len(token):]
            for path in evidence.rglob('*'):
                if path.is_file() and path.suffix in ('.log', '.json', '.cpp', '.hpp'):
                    assert not token or token.encode() not in path.read_bytes(), 'Credential leak in evidence'
            (evidence / 'acceptance.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Packaged native editor MCP/C++/GPU/exported-game acceptance passed')


if __name__ == '__main__':
    main()
