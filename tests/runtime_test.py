import json
import subprocess
import sys
from pathlib import Path
runtime, module = map(str, map(Path, sys.argv[1:]))
p = subprocess.Popen([runtime], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
def request(command, **fields):
    p.stdin.write(json.dumps(dict(protocol=1, command=command, **fields))+'\n')
    p.stdin.flush()
    return json.loads(p.stdout.readline())
try:
    assert request('ping')['ok']
    assert not request('unknown')['ok']
    example=json.loads((Path(__file__).resolve().parents[1]/'samples/projects/Blockout/main.scene.json').read_text())
    loaded=request('replace', scene=example)
    assert loaded['ok']
    assert 'effective_scene' in loaded
    # Reflected numbers use float32 storage; compare authored numeric values with tolerance.
    import math
    def equivalent(a, b):
        if isinstance(a, dict):
            return a.keys()==b.keys() and all(equivalent(a[k], b[k]) for k in a)
        if isinstance(a, list):
            return len(a)==len(b) and all(equivalent(x, y) for x,y in zip(a,b))
        if isinstance(a, (int,float)):
            return math.isclose(a,b,rel_tol=1e-6,abs_tol=1e-6)
        return a==b
    migrated = loaded['scene']
    assert migrated['version'] == 3 and migrated['asset_id']
    expected = dict(example, version=3, asset_id=migrated['asset_id'], legacy_ids=migrated['legacy_ids'])
    for e in expected['entities']:
        e['id'] = migrated['legacy_ids'][e['id']]
        for relation in ('parent', 'base'):
            if relation in e: e[relation] = migrated['legacy_ids'][e[relation]]
        e['spatial'] = {'mode': 'world'}
        c=e['components']
        if 'forge.position' in c: c['forge.local_translation']=c.pop('forge.position')
        if 'forge.scale' in c: c['forge.local_scale']=c.pop('forge.scale')
        if 'forge.rotation' in c:
            rotation=c.pop('forge.rotation')
            x,y,z=[math.radians(rotation[axis])/2 for axis in ('x','y','z')]
            cx,sx,cy,sy,cz,sz=math.cos(x),math.sin(x),math.cos(y),math.sin(y),math.cos(z),math.sin(z)
            c['forge.local_rotation']=dict(x=sx*cy*cz-cx*sy*sz,y=cx*sy*cz+sx*cy*sz,z=cx*cy*sz-sx*sy*cz,w=cx*cy*cz+sx*sy*sz)
    assert equivalent(expected, migrated)
    assert request('snapshot')['scene']==loaded['scene']
    assert request('load_module', path=module)['ok']
    stepped = request('step', seconds=0.1)
    assert stepped['ok'] and stepped['effective_scene']['version'] == 3
    assert not request('step', seconds=-1)['ok']
    assert request('ping')['ok']
    assert request('quit')['ok']
    assert p.wait(timeout=5)==0
finally:
    if p.poll() is None:
        p.kill()
        p.wait()
# A terminated runtime is recoverable by launching a fresh process.
result=subprocess.run([runtime],input='{"protocol":1,"command":"quit"}\n',text=True,capture_output=True,timeout=5)
assert result.returncode==0 and json.loads(result.stdout)['ok']
