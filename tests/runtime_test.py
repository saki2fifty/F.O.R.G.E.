import json
import atexit
import faulthandler
import subprocess
import sys
import time
from pathlib import Path
# A blocked Windows pipe must identify the exact operation, rather than leaving
# only the process banner when CTest's outer deadline expires.
faulthandler.dump_traceback_later(20, repeat=True)
atexit.register(faulthandler.cancel_dump_traceback_later)
runtime, module = map(str, map(Path, sys.argv[1:]))
p = subprocess.Popen([runtime], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
session=''
sequence=0
def request(command, **fields):
    global session,sequence
    sequence+=1
    data=dict(protocol=2,id=sequence,session=session,command=command)
    data.update(fields)
    print(f"Protocol request {sequence}: {command}", flush=True)
    started=time.monotonic()
    p.stdin.write(json.dumps(data)+'\n')
    p.stdin.flush()
    incoming=p.stdout.readline()
    result=json.loads(incoming)
    print(f"Protocol reply {sequence}: {command}, {len(incoming)} chars, {time.monotonic()-started:.3f}s", flush=True)
    if command=='hello': session=result['session']
    return result
try:
    greeting=request('hello')
    assert greeting['ok']
    assert request('ping')['ok']
    assert not request('unknown')['ok']
    example=json.loads((Path(__file__).resolve().parents[1]/'samples/projects/Blockout/main.scene.json').read_text())
    # Native render properties cross the existing copied presentation boundary.
    schema={c['id']:c for c in greeting['schema']['components']}
    for component in ('forge.camera','forge.light'):
        values={f['id']:f['default'] for f in schema[component]['fields']}
        values['basis']=1  # glTF -Z; does not change the node's TRS.
        values['future_extension']={'retain':'opaque'}
        example['entities'][0]['components'][component]=values
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
    for component in ('forge.camera','forge.light'):
        assert equivalent(loaded['effective_scene']['entities'][0]['components'][component],
                          expected['entities'][0]['components'][component])
    import copy
    invalid_camera=copy.deepcopy(migrated)
    invalid_camera['entities'][0]['components']['forge.camera']['near_plane']=0
    assert not request('replace',scene=invalid_camera)['ok']
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

# Structured definitions travel with the isolated runtime checkpoint, without
# resolving editor paths or flattening inherited state into authored rows.
import uuid
uid=lambda:str(uuid.uuid4())
asset,scene_asset,root_member,child_member,root_entity=([uid() for _ in range(5)])
components={'forge.local_translation':dict(x=0,y=1,z=0)}
prefab=dict(format='forge.prefab',version=1,asset_id=asset,revision=1,root=root_member,
    members=[dict(id=root_member,name='Root',components=components),
             dict(id=child_member,name='Child',parent=root_member,components=components)])
structured=dict(version=4,asset_id=scene_asset,entities=[dict(id=root_entity,name='Instance',components={},
    prefab_instance=dict(asset=asset,revision=1,members={root_member:root_entity}))],_prefab_sources=[prefab])
p=subprocess.Popen([runtime],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
sequence=0
try:
    assert request('hello')['ok']
    loaded=request('replace',scene=structured);assert loaded['ok'],loaded
    checkpoint=loaded['scene'];mapping=checkpoint['entities'][0]['prefab_instance']['members']
    assert len(mapping)==2 and mapping[root_member]==root_entity
    child=next(e for e in loaded['effective_scene']['entities'] if e['id']==mapping[child_member])
    assert child['world_affine'][7]==2
    assert child['spatial_resolved']
    assert all(not e['components'] for e in checkpoint['entities'])
    assert request('replace',scene=checkpoint)['scene']==checkpoint
    assert request('load_module',path=module)['ok']
    step=request('step');assert step['ok'] and step['activation']['state']=='active'
    assert step['scene']['_prefab_sources']==[prefab]
    assert next(e for e in step['scene']['entities'] if e['id']==root_entity)['prefab_instance']['members']==mapping
    assert request('quit')['ok'];assert p.wait(timeout=5)==0
finally:
    if p.poll() is None:p.kill();p.wait()
print('Structured prefab isolated runtime realization, checkpoint and native fixed-tick preservation passed')
