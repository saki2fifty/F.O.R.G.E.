"""Single-clip Timeline presentation through the ordinary source workspace owner."""
import hashlib
from mcp_test_client import failed


def check_clip_preview(read, mutate, edit, status, content, root, tools, client, discovery):
    owner = 'animation_timeline'
    names = {tool['name']: tool for tool in tools}
    prefix = 'forge_asset_document_preview_'
    assert names[prefix+'read']['annotations']['readOnlyHint']
    assert not names[prefix+'seek']['annotations']['readOnlyHint']
    assert names[prefix+'seek']['inputSchema']['properties']['editor']['enum'] == [owner]
    source = root/'Game/Assets/Sequences/UiValues.animation.json'
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    before = read('forge_scene_read')
    target = next(row['id'] for row in before['entities']
                  if 'forge.ui_values' in row['components'])
    assert mutate('forge_asset_document_open', editor=owner,
                  path='Assets/Sequences/UiValues.animation.json')['ok']
    initial = status(owner)
    original = content(owner)

    def preview():
        return read(prefix+'read', editor=owner, document_session=status(owner)['session'])['preview']

    assert not preview()['active'] and preview()['ui'] is None
    instance = None
    for seconds in (0., .5, 1., 1.5, 2., .25):
        answer = edit(prefix+'seek', owner, entity=target, seconds=seconds)
        assert answer['ok'], answer
        result = answer['result']['preview']
        assert result['active'] and not result['playing'] and result['model_ready'], result
        assert result['seconds'] == seconds and result['duration_seconds'] == 2
        snapshot = result['ui']
        assert not snapshot['errors'] and len(snapshot['documents']) == 1
        document = snapshot['documents'][0]
        model = document['model']
        triangle = seconds if seconds <= 1 else 2-seconds
        assert abs(model['amount']-(.2+.8*triangle)) < 1e-6, model
        assert abs(model['width']-(80+280*triangle)) < 1e-6, model
        assert model['tick'] == 0 and model['paused']
        if instance is None:
            instance = document['instance']
        assert document['instance'] == instance
        observed = preview()['ui']
        assert observed['documents'] == snapshot['documents']
        assert observed['session'] == snapshot['session']
        assert observed['generation'] == snapshot['generation']
        assert content(owner) == original and status(owner)['revision'] == initial['revision']
        assert read('forge_scene_read') == before

    # Native owner duration/target validation and public schemas must both refuse safely.
    for extra in (dict(seconds=-1), dict(seconds=2.01), dict(seconds=3601),
                  dict(entity='00000000-0000-4000-8000-000000000099'),
                  dict(playing='yes')):
        arguments = dict(entity=target, seconds=.5) | extra
        assert not edit(prefix+'seek', owner, **arguments)['ok']
        assert preview()['seconds'] == .25 and content(owner) == original
    old = status(owner)
    patched = edit('forge_asset_document_patch', owner, patch=[
        dict(op='replace', path='/keys/1/component_values/0/value', value=.8)])
    assert patched['ok'], patched
    failed(mutate(prefix+'seek', editor=owner, document_session=old['session'],
                  document_revision=old['revision'], entity=target, seconds=.5), 'stale_document')
    assert edit('forge_asset_document_undo', owner)['ok'] and content(owner) == original
    # A duplicate command is handled by the established envelope sequence owner.
    state, doc = discovery(), status(owner)
    arguments = dict(target=state['result']['target'], expected_revision=state['revision'],
                     sequence=state['next_sequence'], editor=owner,
                     document_session=doc['session'], document_revision=doc['revision'],
                     entity=target, seconds=.5)
    first = client.tool(prefix+'seek', **arguments)
    assert first['ok'], first
    assert client.tool(prefix+'seek', **arguments) == first
    assert edit(prefix+'seek', owner, entity=target, seconds=.5, playing=True)['ok']
    assert preview()['playing']
    assert edit(prefix+'stop', owner)['ok']
    assert not preview()['active'] and preview()['ui'] is None
    read(prefix+'read', editor=owner, document_session=initial['session']+1,
         _expected_error='wrong_document')
    assert read('forge_scene_read') == before and content(owner) == original
    assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
    print('Single-clip MCP preview: schemas, UI samples, duration/target/revision guards, '
          'replay, playback/stop and source/Scene isolation passed')
