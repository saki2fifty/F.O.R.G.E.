"""Original compiled C++ 2D character fixture through both isolated hosts."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

from runtime_client import Runtime

runtime, allowed, denied = (Path(value).resolve() for value in sys.argv[1:4])
fingerprint = json.loads(subprocess.check_output([runtime, "--sdk-info"], text=True))["fingerprint"]


def row(name, x, y, components):
    return dict(id=str(uuid.uuid4()), name=name, spatial=dict(mode="world"), components={
        "forge.local_translation": dict(x=x, y=y, z=7),
        "forge.local_scale": dict(x=-1, y=1, z=0), **components})


scene = dict(version=5, asset_id=str(uuid.uuid4()), entities=[
    row("Floor", 0, -.5, {"forge.physics_body_2d": {},
                          "forge.box_collider_2d": dict(x=20, y=1)}),
    row("Wall", 2.5, 2, {"forge.physics_body_2d": {},
                          "forge.box_collider_2d": dict(x=1, y=6)}),
    row("Actor", 0, 0, {"forge.character_controller_2d": {}})])
watched = dict(scene=scene["asset_id"], entity=scene["entities"][2]["id"])


def replace(worker, sdk_play):
    response = worker.request("replace", scene=scene)
    if not sdk_play:
        return response
    deadline = time.monotonic() + 10
    while not response.get("candidate"):
        assert time.monotonic() < deadline, response
        time.sleep(.005)
        response = worker.request("snapshot")
    ticket = response["candidate"]["ticket"]
    candidate = worker.request("candidate", ticket=ticket)["candidate"]
    assert candidate["session"] == worker.session and len(candidate["scene"]["entities"]) == 3
    response = worker.request("snapshot", candidate_ack=dict(
        session=worker.session, ticket=ticket, accepted=True, diagnostic=""))
    while response["activation"]["generation"] != ticket:
        assert time.monotonic() < deadline, response
        time.sleep(.005)
        response = worker.request("snapshot")
    # Headless protocol observation only; this does not assert physical editor/GPU acceptance.
    return worker.request("snapshot", editor_epoch=dict(
        epoch=ticket, captured=False, input_device="EditorHost"))


with tempfile.TemporaryDirectory(dir=runtime.parent) as temporary:
    project = Path(temporary)
    for module in (allowed, denied):
        shutil.copyfile(module, project / module.name)
    for deny in (False, True):
        module = denied if deny else allowed
        module_id = "project.character_2d_denied" if deny else "project.character_2d_probe"
        (project / "forge.project.json").write_text(json.dumps(dict(version=2,
            name="2D Character SDK acceptance", startup_scene=None,
            game=dict(version=1, application_id="org.forge.character-2d-test", title="2D character",
                profile="development", save_schema=1,
                display=dict(mode="windowed", width=1280, height=720, display=0, vsync=False),
                audio=dict(master_volume=1), input=dict(mouse_sensitivity=1)),
            input=dict(version=1, actions=[]), modules=[dict(id=module_id,
            implementation="1", sdk="experimental-1", fingerprint=fingerprint,
            library=module.name, dependencies=["forge.physics"])])))
        for sdk_play in (False, True):
            arguments = ["--sdk-project", project]
            if sdk_play:
                arguments += ["--sdk-play", "on", "--user-data", project / "users"]
            worker = Runtime(runtime, arguments)
            try:
                initial = replace(worker, sdk_play)
                assert initial["physics"]["bodies_2d"] == 3, initial
                for tick in range(1, 91):
                    state = worker.request("step", physics_debug=watched)
                    debug = state["physics"]["character_debug"]
                    assert debug["domain"] == "2d" and debug["entity"] == watched["entity"], debug
                    assert debug["epoch"] > 0 and len(debug["velocity"]) == 2, debug
                    assert debug["shape_blocked"] == (not deny and tick == 41), debug
                    if deny or tick <= 41 or tick >= 65:
                        assert debug["ground"] == 0 and debug["supporting_epoch"] > 0, debug
                        assert debug["support"] == dict(scene=scene["asset_id"],
                                                       entity=scene["entities"][0]["id"]), debug
                    entries = [entry for entry in state["diagnostics"]
                        if entry["context"].get("tick") == state["timing"]["tick"]
                        and entry["text"].startswith("Character2D SDK ")]
                    expected = "Character2D SDK denied" if deny else f"Character2D SDK passed {tick} "
                    assert any(entry["text"].startswith(expected) and
                               entry["context"]["module"] == module_id for entry in entries), state
                    actor = next(row for row in state["scene"]["entities"] if row["name"] == "Actor")
                    components = actor["components"]
                    assert "forge.physics_body_2d" not in components
                    assert components["forge.local_translation"]["z"] == 7
                    assert components["forge.local_scale"] == dict(x=-1, y=1, z=0)
                    if deny:
                        assert components["forge.local_translation"]["x"] == 0
                    if sdk_play:
                        assert state["recovery"] is None  # No arbitrary C++ recovery promise.
                before_bad_watch = state["scene"]
                bad = worker.request("snapshot", physics_debug=dict(
                    scene=scene["asset_id"], entity=str(uuid.uuid4())))
                assert "debug_diagnostic" in bad["physics"] and "character_debug" not in bad["physics"]
                assert bad["scene"] == before_bad_watch
                reset = replace(worker, sdk_play)
                assert reset["timing"]["tick"] == 0, reset
                first = worker.request("step")
                assert any(entry["text"].startswith("Character2D SDK denied" if deny else
                           "Character2D SDK passed 1 ") for entry in first["diagnostics"]), first
            finally:
                worker.close()

print("C++ Character2D movement/support/FIFO/placement, contexts/permissions and both hosts passed")
