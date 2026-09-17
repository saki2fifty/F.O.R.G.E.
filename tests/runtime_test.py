import json
import subprocess
import sys
from pathlib import Path
runtime, module = map(str, map(Path, sys.argv[1:]))
p = subprocess.Popen([runtime], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
session=''
sequence=0
def request(command, **fields):
    global session,sequence
    sequence+=1
    data=dict(protocol=2,id=sequence,session=session,command=command)
    data.update(fields)
    p.stdin.write(json.dumps(data)+'\n')
    p.stdin.flush()
    result=json.loads(p.stdout.readline())
    if command=='hello': session=result['session']
    return result
try:
    assert request('hello')['ok']
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
    stepped = request('step')
    assert stepped['ok'] and stepped['effective_scene']['version'] == 3
    assert stepped['timing']['tick']==1 and stepped['timing']['paused']
    assert stepped['activation']['state']=='active'
    assert not request('step', seconds=0)['ok']
    assert not request('step', protocol=1, seconds=.1)['ok']
    assert not request('resume', session='old-session')['ok']
    assert not request('resume', id=1)['ok']
    paused=request('snapshot')
    import time
    time.sleep(.08)
    assert request('snapshot')['timing']['tick']==paused['timing']['tick']
    assert request('resume')['ok']
    time.sleep(.15) # No periodic commands: runtime must progress by itself.
    running=request('snapshot')
    assert running['timing']['tick']>=paused['timing']['tick']+3
    assert not request('step')['ok']
    boundary=request('pause')
    for i in range(3):
        r=request('step'); assert r['timing']['paused'] and r['timing']['tick']==boundary['timing']['tick']+i+1
    # Force response larger than OS pipe capacity; stop reading temporarily.
    large=r['scene'];large['opaque_padding']='x'*(512*1024)
    assert request('replace',scene=large)['ok']
    assert request('resume')['ok']
    sequence+=1
    p.stdin.write(json.dumps(dict(protocol=2,id=sequence,session=session,command='snapshot'))+'\n');p.stdin.flush()
    time.sleep(.25)
    blocked=json.loads(p.stdout.readline())
    later=request('pause')
    assert later['timing']['tick']>=blocked['timing']['tick']+4, (blocked['timing'],later['timing'])
    assert later['timing']['alpha']==1
    assert request('snapshot')['scene']==later['scene']
    assert request('ping')['ok']
    assert request('quit')['ok']
    assert p.wait(timeout=5)==0
finally:
    if p.poll() is None:
        p.kill()
        p.wait()
# New process has its own transient session; old messages are rejected.
p = subprocess.Popen([runtime], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
old_session=session
sequence=0
try:
    assert request('hello')['ok'] and session!=old_session
    assert not request('resume',session=old_session)['ok']
    assert request('quit')['ok']
    assert p.wait(timeout=5)==0
finally:
    if p.poll() is None: p.kill();p.wait()
for hz in ('0','-1','241','nan'):
    result=subprocess.run([runtime,'--simulation-hz',hz],capture_output=True,text=True,timeout=5)
    assert result.returncode!=0
print('Protocol v2, autonomous timing, pause/step, blocked output, stale session tests passed')

# Both ends must be nonblocking, like SDL (a blocking Python reader can hide
# writes larger than the default Windows anonymous-pipe quota).
import os
p=subprocess.Popen([runtime],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
try:
    os.set_blocking(p.stdin.fileno(),False)
    os.set_blocking(p.stdout.fileno(),False)
    generation=''
    for identifier,command in enumerate(('hello','replace','step','quit'),1):
        fields=dict(protocol=2,id=identifier,command=command,session=generation)
        if command=='replace':fields['scene']=dict(migrated,nonblocking_padding='y'*20000)
        outgoing=(json.dumps(fields)+'\n').encode();incoming=b'';deadline=time.monotonic()+5
        while b'\n' not in incoming and time.monotonic()<deadline:
            if outgoing:
                try:
                    count=os.write(p.stdin.fileno(),outgoing[:1024]);outgoing=outgoing[count:]
                except BlockingIOError:pass
            try:
                incoming+=os.read(p.stdout.fileno(),8192)
            except BlockingIOError:pass
            time.sleep(.001)
        assert b'\n' in incoming,(command,len(outgoing),len(incoming))
        reply=json.loads(incoming);assert reply['ok'],reply
        generation=reply['session']
    assert p.wait(timeout=5)==0
finally:
    if p.poll() is None:p.kill();p.wait()
print('Nonblocking parent/runtime pipe framing and large-message progress passed')
