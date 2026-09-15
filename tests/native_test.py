import importlib.util
import json
from pathlib import Path
import sys
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
    try:
        session.configure()
        ok,log=session.build()
        assert ok,log
        state=session.step(0.5)['scene']
        assert state['entities'][0]['components']['forge.position']['x']==0.5
        original=(project/'gameplay.cpp').read_text()
        (project/'gameplay.cpp').write_text('invalid c++ source')
        old=session.active
        ok,log=session.build()
        assert not ok and session.active==old
        assert session.step(0.5)['scene']['entities'][0]['components']['forge.position']['x']==1
        (project/'gameplay.cpp').write_text(original.replace('seconds, 0.0f','2 * seconds, 0.0f'))
        ok,log=session.build()
        assert ok,log
        assert session.active!=old
        assert session.step(0.5)['scene']['entities'][0]['components']['forge.position']['x']==2
        # Candidate crashes during probe; active process remains usable.
        (project/'gameplay.cpp').write_text(original.replace('static void tick(const ForgeHostV1* host, float seconds) {','static void tick(const ForgeHostV1* host, float seconds) { *(volatile int*)0=1;'))
        ok,log=session.build()
        assert not ok,log
        assert session.step(0.5)['scene']['entities'][0]['components']['forge.position']['x']==3
        # A changed schema requires an explicit play restart while retaining host data.
        (project/'gameplay.cpp').write_text(original.replace('forge.position.v1','forge.position.v2'))
        ok,log=session.build()
        assert ok and 'restart' in log.lower(),log
        assert session.step(0.5)['scene']['entities'][0]['components']['forge.position']['x']==3.5
        session.runtime.process.kill()
        session.runtime.process.wait(timeout=5)
        try:
            session.step()
            raise AssertionError('Expected crash report')
        except RuntimeError:
            pass
        assert session.step(0.5)['scene']['entities'][0]['components']['forge.position']['x']==4
    finally:
        session.close()
print('Native build, failed build, compatible reload, crash isolation and recovery passed')
