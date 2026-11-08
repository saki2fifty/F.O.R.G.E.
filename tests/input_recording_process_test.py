"""Record actual fixed input consumption and replay through fresh protocol2 processes."""
import json
import sys
from pathlib import Path
from runtime_client import Runtime as ProtocolRuntime
from touch_example import touch_example

ACTION = "23000000-0000-4230-8230-000000000001"
MAP = dict(version=1, actions=[dict(id=ACTION, name="Action", kind="digital",
                                   bindings=[dict(control="key.space")])])
GALLERY = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
NATIVE = len(sys.argv) > 3
if GALLERY:
    MAP = json.loads((GALLERY / 'forge.project.json').read_text())['input']
    ACTION = '8c68fc99-bdb7-4bf4-9b91-dae060349ff0'


class Runtime(ProtocolRuntime):
    def __init__(self, changed=False):
        self.changed = changed

    def __enter__(self):
        arguments = (['--sdk-project' if NATIVE else '--project', str(GALLERY)] if GALLERY else [])
        super().__init__(Path(sys.argv[1]), arguments,
                         hello_fields=dict(simulation_hz=120, input_map=MAP))
        # Explicit persistent Scene/entity identity survives a fresh process.
        scene = dict(version=2, asset_id="23000000-0000-4230-8230-000000000004",
                     entities=[dict(id="23000000-0000-4230-8230-000000000005",
                                    name="Actor", components={})])
        if GALLERY:
            scene = json.loads((GALLERY / 'Scenes/209-input-recording.scene.json').read_text())
        if self.changed:
            scene['entities'][0]['name'] = 'Changed source'
        self.request("replace", scene=scene)
        self.generation = self.request("snapshot")["world_generation"]
        return self

    def request(self, command, ok=True, **fields):
        if ok:
            return super().request(command, **fields)
        try:
            super().request(command, **fields)
        except RuntimeError as error:
            return dict(ok=False, error=str(error))
        raise AssertionError('Invalid recording operation was accepted')

    def recording(self, operation, **fields):
        return self.request("input_recording", operation=operation,
                            generation=self.generation, **fields)

    def __exit__(self, exc_type, exc, tb):
        try:
            self.request("quit")
            assert self.process.wait(timeout=10) == 0, self.process.returncode
        finally:
            forced = self.process.poll() is None
            self.close()
            if forced:
                raise AssertionError("Runtime required forced cleanup")



def positions(state):
    return [row['components']['forge.local_translation']['x']
            for row in state['effective_scene']['entities']
            if 'forge.input_message' in row['components']]


def exercise():
    with Runtime() as runtime:
        runtime.request("input_recording", ok=False, operation="begin",
                        generation=runtime.generation + 1)
        runtime.recording("begin")
        runtime.recording("finish", ok=False)
        assert runtime.request("snapshot")["input"]["recording"]["mode"] == "recording"
        expected = []
        expected_positions = []
        batches = [[dict(control="key.space", value=1), dict(control="key.space", value=0)],
                   [dict(control="key.space", value=1)], [], [dict(reset=True)],
                   [dict(control="key.space", value=1)]]
        for events in batches:
            args = dict(input_events=events) if events else {}
            value = runtime.request("step", **args)
            expected.append(value["input"]["actions"])
            if NATIVE:
                expected_positions.append(positions(value))
        runtime.recording("finish")
        document = runtime.recording("read")["input_recording_document"]
        assert len(document["frames"]) == 5
        if NATIVE:
            assert expected_positions == [[.5], [1.], [1.], [1.], [1.5]], expected_positions
        runtime.request("replace", ok=False, scene={})
        # File roundtrip preserves the admitted document.
        document = json.loads(json.dumps(document))

    with Runtime() as runtime:
        bad = dict(document, source="changed-source")
        runtime.recording("replay", document=bad, ok=False)
        assert runtime.request("snapshot")["input"]["recording"]["mode"] == "idle"
        runtime.recording("replay", document=document)
        for index, expected_actions in enumerate(expected):
            value = runtime.request("step", input_events=[dict(control="key.space", value=0)])
            assert value["input"]["actions"] == expected_actions
            if NATIVE:
                assert positions(value) == expected_positions[index]
        assert value["input"]["recording"]["mode"] == "finished"
        value = runtime.request("step")
        assert next(row for row in value['input']['actions'] if row['id'] == ACTION)['released']
    with Runtime(changed=True) as runtime:
        runtime.recording("replay", document=document, ok=False)
        assert runtime.request("snapshot")["input"]["recording"]["mode"] == "idle"
    print("Fresh protocol2 input record/read/replay, actual changed Scene/source guards and neutral exhaustion passed")


if NATIVE:
    with touch_example(Path(sys.argv[1]), Path(sys.argv[3]), GALLERY,
                       '209-input-recording.scene.json', 'project.input_messages',
                       False, launch=False) as (project, original):
        GALLERY = project
        exercise()
else:
    exercise()
