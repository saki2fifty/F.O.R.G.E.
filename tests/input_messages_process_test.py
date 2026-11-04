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
    verify_triggers(runtime, module, gallery)
    print('Typed-message Gallery: both hosts, fixed edges, analog changes, snapshots, restart, cold ordinary recovery and graceful exits passed')


def verify_triggers(runtime, module, gallery):
    for sdk in (False, True):
        with touch_example(runtime, module, gallery, '159-input-message-triggers.scene.json',
                           'project.input_messages', sdk) as (worker, original):
            routes = {row['components']['forge.input_message']['action']: row['id']
                      for row in original['entities'] if 'forge.input_message' in row['components']}
            release = routes['8c68fc99-bdb7-4bf4-9b91-dae060349ff0']
            one = routes['15900000-1590-4159-8159-000000000010']
            two = routes['15900000-1590-4159-8159-000000000011']

            def positions(state):
                return {row['id']: row['components']['forge.local_translation']
                        for row in state['effective_scene']['entities'] if row['id'] in routes.values()}

            def messages(state):
                return state['input']['messages']

            initial = activate(worker, original, sdk)
            baseline = positions(initial)
            assert messages(initial)['emitted_total'] == 0
            if sdk:
                step = worker.request('step', input_events=[dict(control='key.e', value=1)])
                assert positions(step) == baseline and messages(step)['emitted_total'] == 0
                worker.request('resume')

            def send(events, count):
                command = 'snapshot' if sdk else 'step'
                state = worker.request(command, **(dict(input_events=events) if events else {}))
                if sdk:
                    state = observe_touch(worker, lambda row: messages(row)['emitted_total'] == count
                                          and row['timing']['tick'] >= state['timing']['tick'] + 2)
                assert messages(state)['emitted_total'] == count, (sdk, messages(state))
                return state

            pressed = send([dict(control='key.space', value=1)], 0)
            assert positions(pressed) == baseline
            released = send([dict(control='key.space', value=0)], 1)
            assert positions(released)[release]['y'] == 2
            first = send([dict(control='key.e', value=1), dict(control='key.d', value=1)], 3)
            assert positions(first)[one]['y'] == 3.5 and positions(first)[two]['x'] == 3.5, (sdk, first)
            held = send([], 3)
            assert positions(held) == positions(first)
            changed = send([dict(control='key.d', value=0), dict(control='key.w', value=1)], 4)
            assert positions(changed)[two]['x'] == 1.5 and positions(changed)[two]['y'] == 3.5
            neutral = send([dict(control='key.e', value=0), dict(control='key.w', value=0)], 6)
            assert positions(neutral)[one]['y'] == 1.5 and positions(neutral)[two]['y'] == 1.5
            quick = send([dict(control='key.space', value=1), dict(control='key.space', value=0)], 7)
            assert positions(quick)[release]['y'] == .5
            nonneutral = send([dict(control='key.e', value=1)], 8)
            assert positions(nonneutral)[one]['y'] == 3.5
            paused = worker.request('pause')
            idle = worker.request('snapshot')
            assert idle['timing']['tick'] == paused['timing']['tick']
            assert messages(idle) == messages(paused)
            stepped = worker.request('step')
            # SDK clock release neutralizes capture; the ordinary protocol host
            # owns no editor capture and retains its submitted physical value.
            assert messages(stepped)['emitted_total'] == (9 if sdk else 8)
            assert positions(stepped)[one]['y'] == (1.5 if sdk else 3.5)
            if sdk:
                worker.request('resume')
            neutral = send([dict(control='key.e', value=0)], 9)
            assert positions(neutral)[one]['y'] == 1.5
            restarted = activate(worker, original, sdk)
            assert positions(restarted) == baseline and messages(restarted)['emitted_total'] == 0
            worker.request('quit'); worker.process.wait(timeout=5)
            assert worker.process.returncode == 0


if __name__ == '__main__':
    verify(*(Path(value).resolve() for value in sys.argv[1:4]))
