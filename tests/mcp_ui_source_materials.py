"""Published UI Material selection through the native source owner."""
import json
from mcp_test_client import failed


def check_material_source(read, ok, mutate, root, advertised, client, arguments, guard, edit, before_scene):
    fixture = json.loads((root / 'ui-material-fixture.json').read_text())
    initial = read('forge_ui_source_read')
    catalog = (root / 'Game/forge.assets.json').read_bytes()
    disk = (root / 'Game/Assets/Hud.rml').read_bytes()
    procedural = read('forge_ui_source_material_reference', **guard(), asset=fixture['ui'])
    assert procedural['shader'] == fixture['shader'] and procedural['asset'] == fixture['ui']
    reference = read('forge_ui_source_material_reference', **guard(), asset=fixture['ui_texture'])
    assert reference['asset'] == fixture['ui_texture'] and reference['shader'] == fixture['shader']
    assert reference['value'] == 'shader("material:' + fixture['ui_texture'] + '")'
    assert len(reference['shader_revision']) == 64
    assert advertised['forge_ui_source_material_reference']['annotations']['readOnlyHint']
    assert read('forge_ui_source_read') == initial
    assert (root / 'Game/forge.assets.json').read_bytes() == catalog
    assert (root / 'Game/Assets/Hud.rml').read_bytes() == disk
    for asset in [fixture['mesh'], fixture['shader'], fixture['unpublished'],
                  '00000000-0000-4000-8000-000000000001']:
        failed(client.tool('forge_ui_source_material_reference',
                           target=arguments()['target'], **guard(), asset=asset), 'ui_material_reference')
        assert read('forge_ui_source_read') == initial
    path = root / 'Game/Assets/TexturedEffect.material.json'
    hidden = path.with_suffix('.hidden')
    path.rename(hidden)
    try:
        assert read('forge_ui_source_material_reference', **guard(), asset=fixture['ui_texture']) == reference
    finally:
        hidden.rename(path)
    paragraph = next(row['index'] for row in initial['elements'] if row['tag'] == 'p')
    old = guard()
    args = arguments(**old, element=paragraph, name='decorator', value=reference['value'])
    first = client.tool('forge_ui_source_style', **args)
    replay = client.tool('forge_ui_source_style', **args)
    assert first['ok'] and replay['ok'] and first['result'] == replay['result']
    assigned = read('forge_ui_source_read')
    failed(client.tool('forge_ui_source_material_reference', target=args['target'],
                       **old, asset=fixture['ui_texture']), 'stale_ui_source')
    assert read('forge_ui_source_style_read', **guard(), element=paragraph,
                name='decorator')['value'] == reference['value']
    assert edit('undo')['text'] == initial['text']
    assert edit('redo')['text'] == assigned['text']
    edit('style', element=paragraph, name='decorator', value=None)
    assert read('forge_ui_source_style_read', **guard(), element=paragraph,
                name='decorator')['value'] is None
    pair = edit('replace', text='<rml><body><p id="a"/><p id="b"/></body></rml>')
    members = [row['index'] for row in pair['elements'] if row['tag'] == 'p']
    edit('style_group', elements=members, name='decorator', value=reference['value'])
    for member in members:
        assert read('forge_ui_source_style_read', **guard(), element=member,
                    name='decorator')['value'] == reference['value']
    assert edit('undo')['text'] == pair['text']
    edit('save')
    css = '/*keep*/ .effect{color:red;}\r\n'
    ok('forge_source_text_write', path='Assets/Effects.rcss', state='missing', text=css)
    opened = ok('forge_ui_source_open', path='Assets/Effects.rcss')
    assert (root / 'Game/Assets/Effects.rcss').read_bytes() == css.encode()
    assert read('forge_ui_source_material_reference', **guard(), asset=fixture['ui_texture']) == reference
    edit('rule_property', rule=0, name='decorator', value=reference['value'])
    changed = read('forge_ui_source_read')
    assert changed['dirty'] and '/*keep*/' in changed['text']
    assert edit('undo')['text'] == opened['text']
    assert edit('redo')['text'] == changed['text']
    edit('save')
    assert (root / 'Game/Assets/Effects.rcss').read_bytes() == changed['text'].replace('\n', '\r\n').encode()
    assert read('forge_scene_read') == before_scene
    assert (root / 'Game/forge.assets.json').read_bytes() == catalog
    edit('close')
    print('UI Material typed publication/retained Shader, source-hidden resolution, '
          'atomic refusal, guards, read-only query, replay, inline/group/rule Undo/Save and isolation passed')
