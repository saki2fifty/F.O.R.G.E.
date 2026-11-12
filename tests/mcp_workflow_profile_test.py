"""CTest routes ordinary and exact SDK Play consumers to their actual profile."""
import json
from pathlib import Path
import subprocess
import sys

build, cmake, sdk = sys.argv[1:]
ctest = Path(cmake).with_name('ctest.exe' if sys.platform == 'win32' else 'ctest')
tests = {row['name']: row for row in json.loads(subprocess.check_output(
    [ctest, '--test-dir', build, '--show-only=json-v1'], text=True))['tests']}
shared = sdk == '1'
for name in ('ui_forms', 'ui_fields', 'ui_font_collections', 'mesh_wind',
             'animation_graph', 'fog_volumes'):
    assert ('mcp_' + name + '_sdk_stdio' in tests) == shared, name
for name in ('audio_pause', 'audio_stream_play', 'audio_routing_play',
             'sound_cues_play', 'finite_sound_cues_play', 'sound_playlist'):
    test = tests['mcp_' + name + '_stdio']
    environment = next(row['value'] for row in test['properties'] if row['name'] == 'ENVIRONMENT')
    assert [value for value in environment if value.startswith('FORGE_MCP_TEST_SDK=')] == [
        'FORGE_MCP_TEST_SDK=' + sdk], (name, environment)
print('MCP Play fixture profile matches shared SDK' if shared else 'MCP Play fixture profile matches static core')
