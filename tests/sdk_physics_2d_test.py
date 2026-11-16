"""Compiled exact-SDK 2D controls/observations through both real Play hosts."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

from runtime_client import Runtime

runtime, module, denied_module = (Path(value).resolve() for value in sys.argv[1:4])
fingerprint = json.loads(subprocess.check_output([runtime, "--sdk-info"], text=True))["fingerprint"]


def row(name, x, dynamic=False):
    return dict(id=str(uuid.uuid4()), name=name, spatial=dict(mode="world"), components={
        "forge.local_translation": dict(x=x, y=0, z=7),
        "forge.local_scale": dict(x=-1, y=1, z=0),
        "forge.physics_body_2d": dict(motion=2 if dynamic else 0, mass=2,
            sensor=dynamic, gravity_factor=0, linear_damping=0, angular_damping=0,
            allow_sleeping=False),
        "forge.box_collider_2d": dict(x=1, y=1),
    })


scene = dict(version=5, asset_id=str(uuid.uuid4()),
             entities=[row("Barrier", 0), row("Other", 8), row("Actor", -3, True)])


def actor(state):
    return next(value for value in state["scene"]["entities"] if value["name"] == "Actor")


def messages(state, prefix="Planar SDK "):
    return [entry for entry in state["diagnostics"]
            if entry["context"].get("tick") == state["timing"]["tick"]
            and entry["text"].startswith(prefix)]


def replace_scene(worker, sdk_play, document=scene):
    response = worker.request("replace", scene=document)
    if not sdk_play:
        return response
    # Exercise the real editor admission handshake before activating the candidate.
    deadline = time.monotonic() + 10
    while not response.get("candidate"):
        assert time.monotonic() < deadline, response
        time.sleep(.005)
        response = worker.request("snapshot")
    ticket = response["candidate"]["ticket"]
    candidate = worker.request("candidate", ticket=ticket)["candidate"]
    assert candidate["session"] == worker.session and candidate["ticket"] == ticket
    assert len(candidate["scene"]["entities"]) == len(document["entities"]), candidate
    response = worker.request("snapshot", candidate_ack=dict(
        session=worker.session, ticket=ticket, accepted=True, diagnostic=""))
    while response["activation"]["generation"] != ticket:
        assert time.monotonic() < deadline, response
        time.sleep(.005)
        response = worker.request("snapshot")
    # Simulated editor protocol observation for this headless fixture; no OS/GPU acceptance.
    return worker.request("snapshot", editor_epoch=dict(
        epoch=ticket, captured=False, input_device="EditorHost"))


def write_project(project, module_id, selected):
    (project / "forge.project.json").write_text(json.dumps(dict(version=2,
        name="Original planar SDK acceptance", startup_scene=None,
        game=dict(version=1, application_id="org.forge.physics-2d-test", title="2D SDK test",
            profile="development", save_schema=1,
            display=dict(mode="windowed", width=1280, height=720, display=0, vsync=False),
            audio=dict(master_volume=1), input=dict(mouse_sensitivity=1)),
        input=dict(version=1, actions=[]), modules=[dict(id=module_id,
        implementation="1", sdk="experimental-1", fingerprint=fingerprint,
        library=selected.name, dependencies=["forge.physics"])])))


with tempfile.TemporaryDirectory(dir=runtime.parent) as temporary:
    project = Path(temporary)
    shutil.copyfile(module, project / module.name)
    shutil.copyfile(denied_module, project / denied_module.name)
    for denied, sdk_play in ((False, False), (False, True), (True, False), (True, True)):
        selected = denied_module if denied else module
        module_id = "project.physics_2d_denied" if denied else "project.physics_2d_probe"
        write_project(project, module_id, selected)
        arguments = ["--sdk-project", project]
        if sdk_play:
            arguments += ["--sdk-play", "on", "--user-data", project / "users"]
        a, b = Runtime(runtime, arguments), None
        retired, begun, ended = False, False, False
        try:
            initial = replace_scene(a, sdk_play)
            assert initial["physics"]["bodies_2d"] == 3
            for tick in range(1, 121):
                left = a.request("step")
                entries = messages(left)
                marker = "Planar SDK permission denial passed" if denied else "Planar SDK controls and observations passed"
                assert any(entry["text"] == marker
                           for entry in entries), left
                assert all(entry["context"]["module"] == module_id
                           for entry in entries), entries
                retired |= any("retired 1" in entry["text"] for entry in entries)
                begun |= any("SDK Begin" in entry["text"] for entry in entries)
                ended |= any("SDK End" in entry["text"] for entry in entries)
                components = actor(left)["components"]
                assert components["forge.local_translation"]["z"] == 7
                assert components["forge.local_scale"] == dict(x=-1, y=1, z=0)
                if denied:
                    assert components["forge.local_translation"]["x"] == -3
                    continue
                if b:
                    right = b.request("step")
                    assert actor(right)["components"] == components
                    assert [entry["text"] for entry in messages(right)] == [
                        entry["text"] for entry in entries]
                if tick == 45 and not sdk_play:
                    checkpoint = left["recovery"]
                    b = Runtime(runtime, arguments)
                    restored = b.request("replace", scene=left["scene"], recovery=checkpoint,
                        recovery_session=checkpoint["session"], recovery_tick=checkpoint["tick"])
                    assert restored["timing"]["paused"] and restored["timing"]["tick"] == tick
                    assert actor(restored)["components"] == components
                if sdk_play:
                    assert left["recovery"] is None  # Arbitrary exact-SDK Play recovery isn't added.
            if not denied:
                assert begun and ended and retired, (sdk_play, begun, ended, retired)
                assert actor(left)["components"]["forge.physics_body_2d"]["mass"] == 3
            clean = replace_scene(a, sdk_play)
            assert clean["timing"]["tick"] == 0
            assert actor(clean)["components"]["forge.local_translation"]["x"] == -3
        finally:
            a.close()
            if b:
                b.close()


def directional_events(selected):
    source = Path(__file__).resolve().parents[1] / 'samples/projects/FeatureGallery/Scenes/217-directional-sensor-events.scene.json'
    authored = json.loads(source.read_text())
    components = {'forge.physics_body_2d', 'forge.box_collider_2d', 'forge.circle_collider_2d',
                  'forge.local_translation', 'forge.local_rotation', 'forge.local_scale'}
    document = dict(version=5, asset_id=authored['asset_id'], entities=[
        dict(id=e['id'], name=e['name'], spatial=e.get('spatial', dict(mode='world')),
             components={k: v for k, v in e['components'].items() if k in components})
        for e in authored['entities'] if 'forge.physics_body_2d' in e['components']])
    names = {e['name']: e['id'] for e in document['entities']}
    expected = {
        names['Ordinary sensor - falling from above']: names['Front passage - ordinary sensor'],
        names['Directional sensor - falling from above']: names['Front passage - directional sensor'],
        names['Ordinary sensor - rising from below']: names['Back passage - ordinary sensor'],
        names['Directional sensor - rising from below']: names['Back passage - directional sensor'],
    }
    for sdk_play in (False, True):
        with tempfile.TemporaryDirectory(dir=runtime.parent) as temporary:
            project = Path(temporary)
            shutil.copyfile(selected, project / selected.name)
            module_id = 'project.physics_2d_directional'
            write_project(project, module_id, selected)
            arguments = ['--sdk-project', project]
            if sdk_play:
                arguments += ['--sdk-play', 'on', '--user-data', project / 'users']
            worker = Runtime(runtime, arguments)
            counts = {key: [0, 0] for key in expected}
            epochs = {}
            try:
                initial = replace_scene(worker, sdk_play, document)
                assert initial['physics']['bodies_2d'] == 10, initial
                for tick in range(1, 241):
                    state = worker.request('step')
                    for entry in messages(state, 'Directional sensor '):
                        fields = entry['text'].split()
                        assert len(fields) == 8 and entry['context']['module'] == module_id, entry
                        first, second = fields[2:4]
                        begin, first_epoch, second_epoch, event_tick = map(int, fields[4:])
                        assert first in expected and expected[first] == second, entry
                        assert event_tick == tick and begin in (0, 1), entry
                        pair = (first, second)
                        assert epochs.setdefault(pair, (first_epoch, second_epoch)) == (first_epoch, second_epoch), entry
                        counts[first][0 if begin else 1] += 1
                for sensor, count in counts.items():
                    target = [0, 0] if sensor == names['Directional sensor - rising from below'] else [1, 1]
                    assert count == target, (sdk_play, sensor, counts)
                worker.request('quit')
                assert worker.process.wait(timeout=10) == 0, 'Directional SDK host did not exit cleanly'
            finally:
                worker.close()
    print('Directional sensor copied events, unchanged epochs and graceful exits passed through both exact-SDK hosts')


if len(sys.argv) == 5:
    directional_events(Path(sys.argv[4]).resolve())

print("Exact-SDK 2D controls/observations, native units, contexts, retired epochs, both Play hosts and supported recovery passed")
