"""Real isolated SDK inspection and authored migration through MCP.

Fixture setup uses genuine v1/v2 modules and the project owner, modeling an
external SDK build on Linux. It does not claim managed Windows deployment.
"""
from contextlib import contextmanager
import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from mcp_test_client import CAPTURE, Client, failed, finish_host

TOOLS, HOST, SDK, CMAKE, NINJA, GOOD, V2 = sys.argv[1:8]


@contextmanager
def editor():
    with tempfile.TemporaryDirectory(prefix='forge-mcp-inspection-') as temporary:
        root=Path(temporary)/'Game'
        process=subprocess.Popen([HOST,str(root),SDK,CMAKE,NINJA,'inspect',GOOD,V2],
                                 stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
        try:
            line=process.stdout.readline(); assert line,process.stderr.read()
            yield root,json.loads(line)
        finally:
            finish_host(process)


def until(fn,predicate,timeout=30):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=fn()
        if predicate(value): return value
        time.sleep(.02)
    raise AssertionError(value)


for legacy in (False,True):
    with editor() as (root,config),Client(config,legacy) as client:
        names={tool['name'] for tool in client.request('tools/list')['result']['tools']}
        assert {'forge_code_inspect','forge_code_migration_prepare','forge_code_migration_apply'} <= names
        def discovery():
            value=client.tool('forge_discover'); assert value['ok'],value; return value
        def read(tool,**extra):
            value=client.tool(tool,target=discovery()['result']['target'],**extra)
            assert value['ok'],value; return value['result']
        def mutate(tool,**extra):
            value=discovery()
            return client.tool(tool,target=value['result']['target'],expected_revision=value['revision'],
                               sequence=value['next_sequence'],**extra)
        def ok(tool,**extra):
            value=mutate(tool,**extra); assert value['ok'],value; return value['result']
        def inspect():
            ok('forge_code_inspect')
            value=until(lambda:read('forge_code_schemas'),lambda r:not r['busy'])
            assert value['ready'] and not value['error'],value
            return value
        initial=inspect()
        assert 'project.health' in json.dumps(initial['schemas'])
        cube=ok('forge_entity_create',arguments=dict(kind=0,name='Migrated health'))['results'][0]['selected']
        ok('forge_component_add',arguments=dict(entity=cube,component='project.health'))
        ok('forge_property_set_many',arguments=dict(entity=cube,component='project.health',
                                                   values=dict(health=73,lives=18446744073709551615)))
        ok('forge_document_save')
        before_version=(root/'forge.project.json').read_bytes()
        (root/'install-v2').touch()  # Controlled fixture external-build publication, through SceneDocument.
        until(lambda:(root/'forge.project.json').read_bytes(),lambda value:value!=before_version)
        changed=inspect()
        assert changed['schemas']!=initial['schemas']
        review=ok('forge_code_migration_open',document='scene')
        assert review['open'] and review['choices'],review
        failed(mutate('forge_code_inspect'),'code_busy')
        assert not mutate('forge_code_migration_prepare',state='0'*64,choice=0,rules={})['ok']
        ok('forge_code_migration_prepare',state=review['state'],choice=0,rules={})
        prepared=until(lambda:read('forge_code_migration_read'),lambda r:not r['busy'])
        assert prepared['prepared'] and not prepared['error'],prepared
        assert prepared['values']>=1,prepared
        ok('forge_entity_rename',arguments=dict(entity=cube,name='Changed after migration review'))
        assert not mutate('forge_code_migration_apply',state=prepared['state'])['ok']
        stale=read('forge_code_migration_read')
        ok('forge_code_migration_close',state=stale['state'])
        review=ok('forge_code_migration_open',document='scene')
        ok('forge_code_migration_prepare',state=review['state'],choice=0,rules={})
        prepared=until(lambda:read('forge_code_migration_read'),lambda r:not r['busy'])
        assert prepared['prepared'] and not prepared['error'],prepared
        before_apply=read('forge_scene_read')
        result=ok('forge_code_migration_apply',state=prepared['state'])
        assert not result['open'],result
        after_apply=read('forge_scene_read')
        assert after_apply!=before_apply
        value=next(row for row in after_apply['entities'] if row['id']==cube)['components']['project.health']
        assert value['health']==73 and value['lives']==18446744073709551615,value
        ok('forge_history_undo')
        assert read('forge_scene_read')==before_apply
        ok('forge_history_redo')
        assert read('forge_scene_read')==after_apply
        ok('forge_document_save')
        print(('legacy' if legacy else 'current')+': actual SDK module inspection, copied schema admission, '
              'isolated migration, stale-review denial, exact uint64 preservation and Scene Undo/Redo verified')

with gzip.open(sys.argv[8],'wt',encoding='utf-8') as output: json.dump(CAPTURE,output,separators=(',',':'))
