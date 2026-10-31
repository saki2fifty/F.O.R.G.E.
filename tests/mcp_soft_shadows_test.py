"""Current Light metadata, authoring/history and disk owners through actual MCP."""
from contextlib import contextmanager
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from mcp_test_client import Client, failed, finish_host, write_capture

TOOLS, HOST = sys.argv[1:3]


@contextmanager
def editor(mode='files'):
    with tempfile.TemporaryDirectory(prefix='forge-mcp-soft-shadows-') as temporary:
        root = Path(temporary)
        process = subprocess.Popen([HOST, str(root), mode], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding='utf-8')
        try:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            yield root, json.loads(line)
        finally:
            finish_host(process, root)


for legacy in (False, True):
    with editor() as (root, config), Client(config, legacy) as client:
        def discover():
            value = client.tool('forge_discover')
            assert value['ok'], value
            return value

        def args(**extra):
            value = discover()
            return dict(target=value['result']['target'], expected_revision=value['revision'],
                        sequence=value['next_sequence'], **extra)

        def ok(name, **extra):
            value = client.tool(name, **args(**extra))
            assert value['ok'], value
            return value['result']

        def read():
            value = client.tool('forge_scene_read', target=discover()['result']['target'])
            assert value['ok'], value
            return value['result']

        schema = next(c for c in discover()['result']['schema']['components'] if c['id'] == 'forge.light')
        fields = {f['id']: f for f in schema['fields']}
        radius, angle = fields['shadow_source_radius'], fields['shadow_angular_radius']
        assert radius['default'] == angle['default'] == 0
        assert radius['minimum'] == angle['minimum'] == 0
        assert radius['maximum'] == 10000 and .785398 < angle['maximum'] < .785399
        assert radius['unit'] == 'm' and angle['unit'] == 'rad'
        assert [choice['label'] for choice in fields['kind']['choices']] == [
            'Directional', 'Point', 'Spot', 'Rectangle', 'Sphere']
        for field, default in [('emitter_radius', .5), ('emitter_width', 1), ('emitter_height', 1)]:
            assert fields[field]['default'] == default and fields[field]['unit'] == 'm'
            assert .00099 < fields[field]['minimum'] < .00101
            assert fields[field]['maximum'] == 10000
        assert fields['texture']['default'] is None and fields['texture']['nullable']
        assert fields['texture']['asset_type'] == 'texture'
        assert fields['image_tiled']['default'] is False
        for name in ('image_width', 'image_height'):
            assert fields[name]['default'] == 1 and fields[name]['unit'] == 'm'
            assert .00099 < fields[name]['minimum'] < .00101
            assert fields[name]['maximum'] == 10000
        light = ok('forge_entity_create', arguments=dict(recipe='render.light'))['results'][0]['selected']
        before = read()
        request = args(arguments=dict(entity=light, component='forge.light', values=dict(
            cast_shadows=True, shadow_source_radius=.25, shadow_angular_radius=.01)))
        result = client.tool('forge_property_set_many', **request)
        assert result['ok'], result
        assert client.tool('forge_property_set_many', **request) == result
        authored = read()
        value = next(e for e in authored['entities'] if e['id'] == light)['components']['forge.light']
        assert value['cast_shadows'] and value['shadow_source_radius'] == .25
        assert abs(value['shadow_angular_radius']-.01) < 1e-8
        failed(client.tool('forge_property_set_many', **dict(args(arguments=request['arguments']), expected_revision=0)),
               'stale_revision')
        for field, bad in [('shadow_source_radius', -1), ('shadow_source_radius', 10001),
                           ('shadow_source_radius', True), ('shadow_source_radius', '1'),
                           ('shadow_angular_radius', -1), ('shadow_angular_radius', 1),
                           ('shadow_angular_radius', None)]:
            previous = discover()
            result = client.tool('forge_property_set', **args(arguments=dict(
                entity=light, component='forge.light', field=field, value=bad)))
            assert not result['ok'] and discover()['revision'] == previous['revision'], result
            assert read() == authored
        ok('forge_history_undo')
        assert read() == before
        ok('forge_history_redo')
        assert read() == authored
        ok('forge_document_save')
        disk = root/'Game/Scenes/main.scene.json'
        assert json.loads(disk.read_text()) == authored
        ok('forge_property_set', arguments=dict(entity=light, component='forge.light',
                                              field='shadow_source_radius', value=0))
        ok('forge_document_open', path='Scenes/main.scene.json', discard=True)
        assert read() == authored
        assert config['token'] not in json.dumps(authored)
        for kind in (0, 1, 2):
            previous = read()
            texture = ('d4c60053-60c8-4456-8771-a7cf8182a902' if kind == 1
                       else 'af69e469-dd94-4545-a61f-682cafb574ee')
            values = dict(kind=kind, texture=texture, image_width=4, image_height=2,
                          image_tiled=True)
            request = args(arguments=dict(entity=light, component='forge.light', values=values))
            result = client.tool('forge_property_set_many', **request)
            assert result['ok'] and client.tool('forge_property_set_many', **request) == result
            textured = read()
            value = next(e for e in textured['entities'] if e['id'] == light)['components']['forge.light']
            assert all(value[key] == expected for key, expected in values.items())
            for patch in (dict(image_width=0), dict(image_height=10001),
                          dict(image_width=True), dict(image_height='1'),
                          dict(texture='not-a-uuid'), dict(texture=7),
                          dict(image_tiled=1), dict(kind=3), dict(kind=4),
                          dict(image_width=8, image_height=0)):
                revision = discover()['revision']
                rejected = client.tool('forge_property_set_many', **args(arguments=dict(
                    entity=light, component='forge.light', values=patch)))
                assert not rejected['ok'] and discover()['revision'] == revision, rejected
                assert read() == textured
            ok('forge_history_undo')
            assert read() == previous
            ok('forge_history_redo')
            assert read() == textured
            ok('forge_document_save')
            assert json.loads(disk.read_text()) == textured
            ok('forge_document_open', path='Scenes/main.scene.json', discard=True)
            assert read() == textured
        ok('forge_property_set', arguments=dict(entity=light, component='forge.light',
                                              field='texture', value=None))
        for kind in (3, 4):
            previous = read()
            values = dict(kind=kind, cast_shadows=False, emitter_radius=.75,
                          emitter_width=2, emitter_height=.5)
            request = args(arguments=dict(entity=light, component='forge.light', values=values))
            result = client.tool('forge_property_set_many', **request)
            assert result['ok'] and client.tool('forge_property_set_many', **request) == result
            area = read()
            value = next(e for e in area['entities'] if e['id'] == light)['components']['forge.light']
            assert all(value[key] == expected for key, expected in values.items())
            for field, bad in [('emitter_radius', 0), ('emitter_radius', True),
                               ('emitter_width', .0001), ('emitter_height', 10001),
                               ('emitter_height', None), ('emitter_width', '1'), ('kind', 5)]:
                revision = discover()['revision']
                rejected = client.tool('forge_property_set', **args(arguments=dict(
                    entity=light, component='forge.light', field=field, value=bad)))
                assert not rejected['ok'] and discover()['revision'] == revision, rejected
                assert read() == area
            ok('forge_history_undo')
            assert read() == previous
            ok('forge_history_redo')
            assert read() == area
            ok('forge_document_save')
            assert json.loads(disk.read_text()) == area
            ok('forge_document_open', path='Scenes/main.scene.json', discard=True)
            assert read() == area
    with editor('read') as (root, config), Client(config, legacy) as client:
        advertised = {tool['name'] for tool in client.request('tools/list')['result']['tools']}
        assert 'forge_property_set' not in advertised and 'forge_property_set_many' not in advertised
        assert 'forge_scene_read' in advertised

write_capture(sys.argv[3])
print('Light kinds/images, shadow/emitter schema/units/defaults, revisions/replay, validation, history/save/reopen and grants PASS in both protocols')
