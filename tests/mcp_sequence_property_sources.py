"""Create compatible clip sources through the existing animation document owner."""
import copy
import json
import time


def check_sequence_property_sources(read, mutate, edit, status, content, root, templates=None):
    owner = 'animation_timeline'
    declarations = (
        ('Blue', {'forge.light.intensity': 2., 'forge.light.color_r': .1,
                  'forge.light.color_g': .2, 'forge.light.color_b': 1.}),
        ('Warm', {'forge.light.intensity': 4., 'forge.light.color_r': 1.,
                  'forge.light.color_g': .35, 'forge.light.color_b': .05}),
        ('FrameFirst', {'forge.sprite_renderer.frame': 0}),
        ('FrameNext', {'forge.sprite_renderer.frame': 3}),
    )
    if templates is not None:
        declarations = templates
    assets = []
    for name, values in declarations:
        path = 'Assets/Pair' + name + '.animation.json'
        created = mutate('forge_asset_document_create', editor=owner, path=path)
        assert created['ok'], created
        baseline = content(owner)
        if templates is None:
            source = copy.deepcopy(baseline)
            # The default transform-only source predates built-in property tracks.
            source['version'] = max(3, source['version'])
            source['channels'] = []
            source['properties'] = list(values)
            for key in source['keys']:
                key['values'] = dict(values)
            invalid_path = '/keys/0/values/' + next(iter(values))
        else:
            source = copy.deepcopy(values)
            source['asset_id'] = baseline['asset_id']
            invalid_path = ('/keys/0/material_values/0/value/0' if source.get('material_bindings')
                            else '/keys/0/component_values/0/value')
        changed = edit('forge_asset_document_patch', owner,
                       patch=[dict(op='replace', path='', value=source)], label='Property pair')
        assert changed['ok'], changed
        assert content(owner) == source
        assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == baseline
        assert edit('forge_asset_document_redo', owner)['ok'] and content(owner) == source
        before = status(owner)
        invalid = edit('forge_asset_document_patch', owner,
                       patch=[dict(op='replace', path=invalid_path, value='invalid')])
        assert not invalid['ok'] and content(owner) == source
        assert status(owner)['revision'] == before['revision']
        saved = edit('forge_asset_document_save', owner)
        assert saved['ok'], saved
        deadline = time.monotonic() + 35
        while True:
            settled = status(owner)
            if not settled['save_requested'] and not settled['publishing']:
                break
            assert time.monotonic() < deadline, settled
            time.sleep(.02)
        assert not settled['dirty'] and not settled['unpublished'] and not settled['diagnostic'], settled
        assert json.loads((root/'Game'/path).read_text()) == source
        assets.append(settled['asset'])
        assert edit('forge_asset_document_close', owner)['ok']
    assert len(set(assets)) == 4
    records = {row['id']: row for row in json.loads((root/'Game/forge.assets.json').read_text())['assets']}
    for asset in assets:
        record = records[asset]
        assert record['type'] == 'transform_animation' and record['metadata']['forge.import']['output_version'] == 9
