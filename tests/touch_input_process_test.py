"""Original Gallery touch actions and installed-only C++ through both real Play hosts."""
from pathlib import Path
import sys
from runtime_client import activate
from touch_example import touch_example, observe_touch

runtime, module, gallery = (Path(p).resolve() for p in sys.argv[1:4])
for sdk in (False, True):
    with touch_example(runtime, module, gallery, '88-touch-input.scene.json', 'project.touch_input', sdk) as (worker, original):
        def cursor(state):
            return next(row['components'] for row in state['effective_scene']['entities']
                        if row['id'] == '15500000-1550-4155-8155-000000000002')
        activate(worker, original, sdk)
        worker.request('resume')
        worker.request('snapshot', input_events=[dict(control='touch.0.' + field, value=value)
            for field, value in (('down', 1), ('x', .75), ('y', .25), ('pressure', .5))])
        seen = observe_touch(worker, lambda s: abs(cursor(s)['forge.local_translation']['x'] - 4) < 1e-8 and
                              cursor(s)['forge.sprite_renderer']['visible'])
        assert cursor(seen)['forge.local_translation']['y'] == 2.5, seen
        assert abs(cursor(seen)['forge.sprite_renderer']['width'] - .8) < 1e-6, seen
        # A second contact cannot change the sample's deliberately chosen slot0.
        worker.request('snapshot', input_events=[dict(control='touch.1.x', value=0)])
        assert cursor(worker.request('snapshot'))['forge.local_translation']['x'] == 4
        worker.request('snapshot', input_events=[dict(control='touch.0.pressure', value=1)])
        observe_touch(worker, lambda s: abs(cursor(s)['forge.sprite_renderer']['width'] - 1.2) < 1e-6)
        worker.request('snapshot', input_events=[dict(reset=True)])
        observe_touch(worker, lambda s: not cursor(s)['forge.sprite_renderer']['visible'])
        worker.request('pause')
        # Restart starts neutral. No touch identity or input state enters source.
        activate(worker, original, sdk)
        worker.request('resume')
        observe_touch(worker, lambda s: not cursor(s)['forge.sprite_renderer']['visible'])
        worker.request('quit')
        worker.process.wait(timeout=5)
        assert worker.process.returncode == 0
print('Gallery88 independent touch coordinates/pressure, public C++, both hosts and neutral restart passed')
