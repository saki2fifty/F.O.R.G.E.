"""ABI1 pending activation rollback with live falling-body state."""
import json,sys,tempfile,time
from pathlib import Path
sys.dont_write_bytecode=True
sdk,runtime,cmake,ninja=map(lambda x:Path(x).resolve(),sys.argv[1:])
sys.path.insert(0,str(sdk/'tools'))
import forge_native as native
with tempfile.TemporaryDirectory(dir=Path.cwd()) as scratch:
 root=Path(scratch);project=root/'project';native.create_project(project,sdk,'cpp')
 original=(project/'gameplay.cpp').read_text().replace('seconds, 0.0f','0 * seconds, 0.0f')
 (project/'gameplay.cpp').write_text(original)
 scene=dict(version=1,entities=[dict(id='cube',name='Cube',components={'forge.position':dict(x=0,y=5,z=0),'forge.physics_body':dict(motion=2,density=1000,mass=0,friction=.5,restitution=0,gravity_factor=1),'forge.box_collider':dict(x=1,y=1,z=1)})])
 (project/'main.scene.json').write_text(json.dumps(scene))
 session=native.Session(project,root/'work',sdk,runtime,str(cmake),str(ninja))
 reference=native.Runtime(runtime)
 try:
  session.configure();ok,log=session.build();assert ok,log
  for _ in range(12):boundary=session.step()
  stable=session.active;tick=boundary['timing']['tick'];checkpoint=boundary['recovery']
  reference.request('replace',scene=boundary['scene'],recovery=checkpoint,recovery_session=checkpoint['session'],recovery_tick=tick)
  marker=json.dumps(str(root/'first-live-tick'))
  bad='#include <fstream>\n#include <cstdlib>\nstatic bool live=false;\n'+original
  bad=bad.replace('FORGE_EXPORT const ForgeModuleV1* forge_module_v1(void) {','FORGE_EXPORT const ForgeModuleV1* forge_module_v1(void) { live=std::ifstream('+marker+').good(); std::ofstream('+marker+') << 1;')
  bad=bad.replace('static void tick(const ForgeHostV1* host, float seconds) {','static void tick(const ForgeHostV1* host, float seconds) { if(live) std::abort();')
  (project/'gameplay.cpp').write_text(bad);ok,log=session.build();assert ok and session.pending,log
  assert session.poll()['timing']['tick']==tick
  try:session.step()
  except RuntimeError:pass
  else:raise AssertionError('Expected first live tick crash')
  restored=session.poll();assert session.paused and session.active==stable and not session.pending
  assert restored['timing']['tick']==tick and restored['scene']==boundary['scene']
  for _ in range(5):
   actual=session.step();expected=reference.request('step')
   assert actual['scene']==expected['scene'],'Physics velocity/state was lost in ABI1 fallback'
  (project/'gameplay.cpp').write_text(original);ok,log=session.build();assert ok and session.pending,log
  session.resume();time.sleep(.06);resumed=session.poll();assert not session.pending and resumed['timing']['tick']>tick+5
  session.pause();ok,log=session.build();assert ok and session.pending,log
  session.close();assert session.state=='Stopped' and session.pending is None
 finally:session.close();reference.close()
print('Falling-body ABI1 probe isolation, paused first-live-tick rollback, Step, Resume and pending Stop passed')
