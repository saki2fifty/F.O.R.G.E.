import importlib.util
import json
from pathlib import Path
import sys
import math
sys.dont_write_bytecode = True
import tempfile

sdk, runtime, cmake, ninja = map(Path, sys.argv[1:])
spec=importlib.util.spec_from_file_location('forge_native',sdk/'tools/forge_native.py')
native=importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)
with tempfile.TemporaryDirectory(dir=Path.cwd()) as scratch:
    root=Path(scratch)
    project=root/'project'
    native.create_project(project,sdk,'cpp')
    session=native.Session(project,root/'work',sdk,runtime,str(cmake),str(ninja))
    def advance_half():
        result=None
        for _ in range(30): result=session.step()
        return result
    def expect_x(value):
        actual=advance_half()['scene']['entities'][0]['components']['forge.local_translation']['x']
        assert math.isclose(actual,value,rel_tol=1e-6,abs_tol=1e-6),(actual,value)
    try:
        session.configure()
        ok,log=session.build()
        assert ok,log
        state=advance_half()['scene']
        assert math.isclose(state['entities'][0]['components']['forge.local_translation']['x'],0.5,rel_tol=1e-6)
        original=(project/'gameplay.cpp').read_text()
        (project/'gameplay.cpp').write_text('invalid c++ source')
        old=session.active
        ok,log=session.build()
        assert not ok and session.active==old
        expect_x(1)
        (project/'gameplay.cpp').write_text(original.replace('seconds, 0.0f','2 * seconds, 0.0f'))
        ok,log=session.build()
        assert ok,log
        assert session.active==old and session.pending is not None
        expect_x(2)
        assert session.active!=old
        # Candidate crashes during probe; active process remains usable.
        (project/'gameplay.cpp').write_text(original.replace('static void tick(const ForgeHostV1* host, float seconds) {','static void tick(const ForgeHostV1* host, float seconds) { *(volatile int*)0=1;'))
        ok,log=session.build()
        assert not ok,log
        expect_x(3)
        # A changed schema requires an explicit play restart while retaining host data.
        (project/'gameplay.cpp').write_text(original.replace('forge.position.v1','forge.position.v2'))
        ok,log=session.build()
        assert ok and 'restart' in log.lower(),log
        expect_x(3.5)
        session.runtime.process.kill()
        session.runtime.process.wait(timeout=5)
        try:
            session.step()
            raise AssertionError('Expected crash report')
        except RuntimeError:
            pass
        expect_x(4)
        # Paused pending, supersession and Resume use the same transaction as the editor.
        import time
        stable=session.active
        (project/'gameplay.cpp').write_text(original)
        assert session.build()[0] and session.pending and session.active==stable
        tick=session.poll()['timing']['tick']
        time.sleep(.03)
        assert session.poll()['timing']['tick']==tick
        old_session=session.runtime.session
        (project/'gameplay.cpp').write_text(original.replace('seconds, 0.0f','3 * seconds, 0.0f'))
        assert session.build()[0] and session.pending and session.runtime.session!=old_session
        session.resume();time.sleep(.06);session.poll()
        assert session.pending is None and session.active!=stable and not session.paused
        # Running reload completes through autonomous ticks, without caller dt.
        stable=session.active
        (project/'gameplay.cpp').write_text(original)
        assert session.build()[0]
        time.sleep(.06);session.poll()
        assert session.active!=stable and session.pending is None
        session.pause()
        stable=session.active
        checkpoint=session.poll()['scene']
        marker=json.dumps(str(root/'live-first-tick'))
        source='#include <fstream>\n#include <cstdlib>\nstatic bool live=false;\n'+original
        source=source.replace('FORGE_EXPORT const ForgeModuleV1* forge_module_v1(void) {',
            'FORGE_EXPORT const ForgeModuleV1* forge_module_v1(void) { live=std::ifstream('+marker+').good(); std::ofstream('+marker+') << 1;')
        source=source.replace('static void tick(const ForgeHostV1* host, float seconds) {',
            'static void tick(const ForgeHostV1* host, float seconds) { if(live) std::abort();')
        (project/'gameplay.cpp').write_text(source)
        assert session.build()[0] and session.pending
        try:
            session.step()
            raise AssertionError('Expected first live tick failure')
        except RuntimeError:
            pass
        recovered=session.poll()
        assert session.active==stable and session.pending is None and session.paused
        assert recovered['scene']==checkpoint and recovered['timing']['tick']==0 and recovered['timing']['alpha']==1
        (project/'gameplay.cpp').write_text(original)
        assert session.build()[0] and session.pending
        session.close()
        assert session.active==stable and session.pending is None and session.state=='Stopped'

    finally:
        session.close()
print('Native build, failed build, compatible reload, crash isolation and recovery passed')
