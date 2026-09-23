"""Exercise the real read-only asset CLI with a portable temporary project."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid

executable = Path(sys.argv[1]).resolve()
scratch = Path(sys.argv[2]).resolve()
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as temporary:
    root = Path(temporary)
    project = root / 'Project with spaces'
    (project / 'Assets').mkdir(parents=True)
    (project / 'Assets' / 'picture.PNG').write_bytes(b'bytes, not yet decoded')
    (project / 'Assets' / '.hidden').write_bytes(b'hidden')
    a, b = str(uuid.uuid4()), str(uuid.uuid4())
    index = {
        'version': 2,
        'assets': [
            dict(id=a, type='material', source='Assets/example.material.json', schema_version=1,
                 dependencies=[b], metadata={}, dependency_edges=[
                     dict(target=b, type='texture', kind='build', role='base_color', revision='')]),
            dict(id=b, type='texture', source='Assets/picture.PNG', schema_version=1,
                 dependencies=[], metadata={}, dependency_edges=[]),
        ],
    }
    index_path = project / 'forge.assets.json'
    index_path.write_text(json.dumps(index), encoding='utf-8')
    before = {p.relative_to(project).as_posix(): p.read_bytes()
              for p in project.rglob('*') if p.is_file()}

    def request(*arguments, success=True):
        result = subprocess.run([str(executable), '--assets', *map(str, arguments)],
                                cwd=root, capture_output=True, text=True, timeout=10)
        document = json.loads(result.stdout)
        assert document['api'] == 1
        assert document['ok'] == success, (result.stdout, result.stderr)
        assert (result.returncode == 0) == success, (result.stdout, result.stderr)
        return document

    scan = request('scan', project)
    assert scan['complete'] and len(scan['files']) == 1 and scan['filtered'] == 1
    assert scan['files'][0]['source'] == 'Assets/picture.PNG'
    assert scan['files'][0]['digest'] == hashlib.sha256(b'bytes, not yet decoded').hexdigest()
    assert scan['files'][0]['source_kind'] == 'image'
    query = request('query', project)
    assert {x['id'] for x in query['assets']} == {a, b}
    dependents = request('dependents', project, b)
    assert dependents['registered'] and dependents['direct'] == [a]
    assert dependents['transitive'] == [a]
    assert request('dependents', project, a)['direct'] == []
    assert not request('dependents', project, str(uuid.uuid4()))['registered']
    target = json.dumps(dict(platform='linux', backend='none'))
    roots = json.dumps(['80c55df2-1abb-42b0-9591-502e783cdc97'])
    package = root / 'Cooked content with spaces'
    request('package', project, package, target, roots)
    assert request('verify-package', package, target)['assets'] == 0  # engine-owned root
    request('package', project, package, target, roots, success=False)
    request('verify-package', package, json.dumps(dict(platform='linux', backend='vulkan')),
            success=False)
    request('package', project, root / 'unsupported', target, json.dumps([a]), success=False)
    request('package', project, root / 'bad', 'invalid-json', roots, success=False)
    source_refs = request('source-dependents', project, 'Assets/picture.PNG')
    assert source_refs['direct'] == [b] and set(source_refs['affected']) == {a, b}
    request('source-dependents', project, '../escape', success=False)
    request('dependents', project, 'not-a-uuid', success=False)
    request('scan', project, '../escape', success=False)
    request('scan', root / 'missing', success=False)
    request('unknown', project, success=False)
    request('query', project, 'extra', success=False)
    request(success=False)
    after = {p.relative_to(project).as_posix(): p.read_bytes()
             for p in project.rglob('*') if p.is_file()}
    assert before == after, 'Read-only asset commands wrote project files'
    for operation in ('cache-stats', 'cache-verify', 'cache-prune', 'cache-cleanup',
                      'cache-clear-all'):
        extra = ('0',) if operation == 'cache-prune' else ()
        assert request(operation, project, *extra)['statistics']['entries'] == 0
    request('cache-prune', project, '-1', success=False)
    request('cache-prune', project, '1.5', success=False)
    request('cache-clear-asset', project, a, success=False)
    request('cache-clear-asset', project, 'bad-id', success=False)
    request('cache-unknown', project, success=False)
    assert index_path.read_bytes() == before['forge.assets.json']
    assert (project / 'Assets/picture.PNG').read_bytes() == before['Assets/picture.PNG']
    # A nonempty module map must survive option parsing and reach project
    # validation. Iterating items() on a temporary JSON owner used to dangle.
    export_options = dict(destination=str(root/'Export'), runtime_kit=str(root/'Kit'),
                          module_kits={'example.game': str(root/'Module kit')})
    rejected = request('export-game', project, json.dumps(export_options), success=False)
    assert 'Set Game defaults and Startup Scene' in rejected['error']['message'], rejected
    export_options['module_kits'] = []
    rejected = request('export-game', project, json.dumps(export_options), success=False)
    assert 'module_kits must be an object' in rejected['error']['message'], rejected
    index_path.write_text('{broken', encoding='utf-8')
    request('query', project, success=False)
    assert request('scan', project)['complete'], 'File scan unnecessarily depends on valid catalog'
print('Asset scan/query/dependents CLI, framing, paths and no-write behavior passed')
