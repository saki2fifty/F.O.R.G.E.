"""Original typed-message Gallery receiver through both actual runtime hosts."""
import copy
from pathlib import Path
import sys
from runtime_client import Runtime, activate
from touch_example import touch_example, observe_touch


def verify(runtime, module, gallery):
    runtime, module, gallery = map(Path, (runtime, module, gallery))
    for sdk in (False, True):
        with touch_example(runtime, module, gallery, '150-input-messages.scene.json',
                           'project.input_messages', sdk) as (worker, original):
            unchanged = copy.deepcopy(original)
            ids = [row['id'] for row in original['entities'] if 'forge.input_message' in row['components']]
            assert len(ids) == 2

            def positions(state):
                rows = {row['id']: row['components']['forge.local_translation']
                        for row in state['effective_scene']['entities']}
                return [rows[key]['x'] for key in ids]

            def messages(state):
                return state['input']['messages']

            initial = activate(worker, original, sdk)
            start = positions(initial)
            assert messages(initial)['emitted'] == 0
            def observe(predicate):
                try:
                    return observe_touch(worker, predicate)
                except AssertionError as error:
                    state = error.args[0]
                    raise AssertionError(dict(sdk=sdk, messages=messages(state),
                                              positions=positions(state))) from None

            if sdk:
                # SDK clock controls intentionally neutralize pending input.
                stepped = worker.request('step', input_events=[dict(control='key.space', value=1)])
                assert positions(stepped) == start and messages(stepped)['emitted_total'] == 0
                worker.request('resume')
                worker.request('snapshot', input_events=[dict(control='key.space', value=1)])
                first = observe(lambda state: messages(state)['emitted_total'] == 2)
                second = observe(lambda state: state['timing']['tick'] >= first['timing']['tick'] + 4)
                worker.request('snapshot', input_events=[dict(control='key.space', value=0),
                                                        dict(control='key.space', value=1)])
                quick = observe(lambda state: messages(state)['emitted_total'] == 4)
                assert messages(second)['emitted_total'] == 2
            else:
                first = worker.request('step', input_events=[dict(control='key.space', value=1)])
                assert messages(first)['emitted'] == 2
                assert {row['state'] for row in messages(first)['sources']} == {'Sent'}
                second = worker.request('step')
                quick = worker.request('step', input_events=[dict(control='key.space', value=0),
                                                            dict(control='key.space', value=1)])
            assert positions(first) == [value + .5 for value in start], dict(sdk=sdk, messages=messages(first))
            assert positions(second) == positions(first) and messages(second)['emitted'] == 0
            assert positions(quick) == [value + 1 for value in start]
            assert messages(quick)['emitted_total'] == 4
            worker.request('pause')
            quick = worker.request('snapshot')
            # Read-only paused observations cannot dispatch or advance the fixed tick.
            assert worker.request('snapshot')['timing']['tick'] == quick['timing']['tick']
            assert worker.request('snapshot')['input']['messages'] == messages(quick)
            try:
                worker.request('snapshot', input_events=[dict(control='key.space', value=0),
                                                        dict(control='key.not-a-key', value=1)])
            except RuntimeError:
                pass
            else:
                raise AssertionError('Invalid mixed input batch admitted')
            held = worker.request('step')
            assert positions(held) == positions(quick) and messages(held)['emitted'] == 0
            restarted = activate(worker, original, sdk)
            assert positions(restarted) == start and messages(restarted)['emitted_total'] == 0
            assert positions(worker.request('step')) == start
            # Ordinary-host recovery is an existing supported boundary. Rich SDK
            # recovery remains unavailable; never claim a project callback replay.
            if not sdk:
                saved = worker.request('step', input_events=[dict(control='key.space', value=1)])
                checkpoint = saved['recovery']
                assert checkpoint is not None
                launch = list(worker.process.args)
                worker.request('quit'); worker.process.wait(timeout=5)
                assert worker.process.returncode == 0
                recovered = Runtime(launch[0], launch[1:])
                try:
                    state = recovered.request('replace', scene=saved['scene'], recovery=checkpoint,
                                              recovery_session=checkpoint['session'],
                                              recovery_tick=checkpoint['tick'])
                    assert state['timing']['paused'] and messages(state)['emitted'] == 0
                    state = recovered.request('step')
                    assert messages(state)['emitted'] == 0
                    recovered.request('quit'); recovered.process.wait(timeout=5)
                    assert recovered.process.returncode == 0
                finally:
                    recovered.close()
            else:
                assert restarted['recovery'] is None
                worker.request('quit'); worker.process.wait(timeout=5)
                assert worker.process.returncode == 0
            assert original == unchanged
    print('Typed-message Gallery: both hosts, fixed edges, snapshots, restart, cold ordinary recovery and graceful exits passed')


if __name__ == '__main__':
    verify(*(Path(value).resolve() for value in sys.argv[1:4]))
