"""Collection fonts use the ordinary UI source/catalog and native Play owners."""
from pathlib import Path
import subprocess

from mcp_test_client import failed


def check_ui_font_collections(root, read, ok, control, mutate, until, fixture):
    source = Path(__file__).resolve().parents[1]
    project = root/'Game'
    if not (project/'Assets').exists():
        ok('forge_source_folder_create', path='Assets')
    ok('forge_source_folder_create', path='Assets/Incoming')
    collection = project/'Assets/Incoming/Menu.ttc'
    subprocess.run([str(fixture), '--fixture', str(collection),
                    str(source/'resources/ui/Poppins-Regular.ttf'),
                    str(source/'resources/ui/Cairo-Regular.ttf')], check=True)
    original = collection.read_bytes()
    prepared = ok('forge_asset_import_prepare', sources=['Assets/Incoming/Menu.ttc'],
                  destination='Assets/MenuFonts')
    review = until(lambda: read('forge_asset_import_review'),
                   lambda row: row['state'] in ('review', 'failed'))
    assert review['state'] == 'review' and review['copy_only'] and review['file_count'] == 1
    failed(mutate('forge_asset_import_commit', batch=prepared['batch'], review=review['review'],
                  **{'import': True}), 'copy_only')
    assert not (project/'Assets/MenuFonts').exists()
    ok('forge_asset_import_commit', batch=prepared['batch'], review=review['review'],
       **{'import': False})
    copied = until(lambda: read('forge_asset_jobs')['source_import'],
                   lambda row: row['state'] in ('done', 'failed'))
    assert copied['state'] == 'done' and copied['copied'] and copied['published'] == 0
    font_path = 'Assets/MenuFonts/' + review['files'][0]['destination']
    assert (project/font_path).read_bytes() == original
    ok('forge_asset_import_close', batch=prepared['batch'])
    font = ok('forge_source_ui_register', path=font_path)
    assert font['type'] == 'ui_font'
    assert ok('forge_source_ui_register', path=font_path)['asset'] == font['asset']
    until(lambda: read('forge_asset_search', include_engine=False, type='ui_font'),
          lambda page: any(row['asset'] == font['asset'] for row in page['assets']))
    metadata = read('forge_asset_read', asset=font['asset'])['metadata']['forge.ui_source']
    assert metadata['faces'] == 2 and metadata['runtime_features']['font_collections'] == 1
    bad = project/'Assets/Bad.ttc'
    bad.write_bytes(original[:19])
    failed(mutate('forge_source_ui_register', path='Assets/Bad.ttc'), 'operation_failed')
    hud = ok('forge_source_ui_example')
    saved = read('forge_source_text_read', path=hud['path'])
    markup = '''<rml><head><style>
@font-face {font-family:MenuLatin;src:"Assets/Menu.ttc";-rmlui-face-index:0;}
@font-face {font-family:MenuArabic;src:"Assets/Menu.ttc";-rmlui-face-index:1;}
body {font-family:MenuLatin;font-size:32px;color:#fff;}
p {display:block;} #arabic {font-family:MenuArabic;-rmlui-direction:rtl;-rmlui-language:ar;}
input {width:300px;height:48px;font-family:MenuLatin;}
</style></head><body><p>office</p><p id="arabic">سلام</p>
<input id="entry" type="text" value="office"/></body></rml>'''.replace('Assets/Menu.ttc', font_path)
    ok('forge_source_text_write', path=hud['path'], state=saved['state'], text=markup)
    ok('forge_source_ui_register', path=hud['path'])
    actor = ok('forge_entity_create', arguments=dict(name='Collection HUD'))['results'][0]['selected']
    ok('forge_component_add', arguments=dict(entity=actor, component='forge.ui_document'))
    ok('forge_property_set', arguments=dict(entity=actor, component='forge.ui_document',
                                          field='document', value=hud['asset']))
    ok('forge_document_save')
    before_scene = read('forge_scene_read')
    ok('forge_project_settings_game_defaults', state=read('forge_project_settings_read')['state'])
    for shaped in (False, True):
        ok('forge_project_settings_edit', state=read('forge_project_settings_read')['state'], patch=[
            dict(op='add', path='/game/ui', value=dict(text_shaping=shaped))])
        ok('forge_project_settings_save', state=read('forge_project_settings_read')['state'])
        ok('forge_play_start')
        control('forge_play_pause')
        live = until(lambda: read('forge_play_ui'), lambda row: bool((row.get('value') or {}).get('documents')))
        instance = live['value']['documents'][0]['instance']
        fields = read('forge_play_ui_fields', instance=instance)['value']
        revision = fields['presentation_revision']
        control('forge_play_ui_focus', instance=instance, element='entry', presentation_revision=revision)
        control('forge_play_ui_key', instance=instance, element='entry', presentation_revision=revision,
                key='select_all')
        changed = control('forge_play_ui_text', instance=instance, element='entry',
                          presentation_revision=revision, text='Collection input')
        assert next(row for row in changed['fields'] if row['id'] == 'entry')['value'] == 'Collection input'
        control('forge_play_stop')
    assert read('forge_scene_read') == before_scene
    assert collection.read_bytes() == original
    assert (project/hud['path']).read_text(encoding='utf-8') == markup
