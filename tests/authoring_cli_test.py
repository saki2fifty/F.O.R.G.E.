import json
import subprocess
import sys
p=subprocess.Popen([sys.argv[1],'--stdio'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
def send(text):
    p.stdin.write(text+'\n');p.stdin.flush();return json.loads(p.stdout.readline())
def request(method,**fields):
    return send(json.dumps(dict(api=1,method=method,**fields)))
try:
    d=request('discover')['result'];target=d['target'];rev=d['revision']
    assert d['persistence']=='memory-only'
    r=request('scene.replace',target=target,expected_revision=rev,document=dict(version=1,entities=[]));assert r['ok']
    r=request('scene.apply',target=target,expected_revision=r['revision'],commands=[dict(operation='entity.create',arguments=dict(kind=1,name='CLI sphere'))]);assert r['ok']
    assert request('entity.query',target=target,text='sphere')['result']['total']==1
    assert not request('scene.apply',target=target,expected_revision=0,commands=[])['ok']
    r=request('history.undo',target=target,expected_revision=r['revision']);assert r['ok']
    assert request('scene.read',target=target)['result']['entities']==[]
    assert not send('{broken')['ok']
    assert not send(' '* (1024*1024+1))['ok']
    assert not send('['*70+'0'+']'*70)['ok']
    assert request('discover')['ok']
    assert not request('save',target=target,expected_revision=r['revision'],path='must-not-write')['ok']
    p.stdin.close();assert p.wait(timeout=10)==0
finally:
    if p.poll() is None: p.kill();p.wait()
print('Headless authoring framing, limits, history and memory-only capabilities passed')

from pathlib import Path
sample=Path(__file__).resolve().parents[1]/'samples/automation/create_blockout.py'
result=subprocess.run([sys.executable,str(sample),sys.argv[1]],text=True,capture_output=True,timeout=15)
assert result.returncode==0,result.stderr
scene=json.loads(result.stdout)
assert len(scene['entities'])==4
assert [e['components']['forge.primitive']['kind'] for e in scene['entities']]==[0,1,2,3]
