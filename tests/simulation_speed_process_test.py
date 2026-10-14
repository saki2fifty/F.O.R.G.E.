"""Installed matching C++ module restores held simulation through control input."""
import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

runtime, module = (Path(value).resolve() for value in sys.argv[1:3])
with tempfile.TemporaryDirectory(dir=runtime.parent, prefix="simulation-speed-") as temporary:
    root = Path(temporary)
    shutil.copy2(module, root / module.name)
    fingerprint = json.loads(subprocess.check_output([runtime, "--sdk-info"], text=True))["fingerprint"]
    actions = [dict(id=f"12345678-1234-4234-8234-123456789a0{i+1}", name=name,
                    kind="digital", context="Speed", bindings=[dict(control=key)])
               for i, (name, key) in enumerate(zip(("Hold", "Slow", "Normal", "Fast"),
                                                   ("key.h", "key.q", "key.n", "key.f")))]
    defaults_path = Path(__file__).resolve().parents[1] / "samples/projects/FeatureGallery/forge.project.json"
    defaults = json.loads(defaults_path.read_text())["game"]
    defaults["application_id"] = "org.forge.simulation-speed-probe"
    project = dict(version=2, name="Simulation speed C++", startup_scene=None, simulation_hz=100,
                   game=defaults,
                   input=dict(version=2, contexts=[dict(name="Speed", priority=10,
                              active=True, consume=True, phase="control")], actions=actions), modules=[
                       dict(id="project.time_scale_probe", implementation="1", sdk="experimental-1",
                            fingerprint=fingerprint, library=module.name,
                            dependencies=["forge.input", "forge.game", "forge.transforms"])])
    (root / "forge.project.json").write_text(json.dumps(project))
    inspected = json.loads(subprocess.check_output([runtime, "--inspect-sdk", root], text=True))
    declaration = next(row for row in inspected["components"] if row["id"] == "project.rate_probe")
    stamp = dict(format="forge.authored-component", version=1, module=declaration["module"],
                 schema_version=declaration["schema_version"],
                 digest=declaration["digest"])
    scene = dict(version=1, entities=[dict(id="probe", name="Rate probe", components={
        "project.rate_probe": dict(frames=0, ticks=0, last_dt=0, observed_rate=1,
                                   **{"$forge": stamp})})])
    process = subprocess.Popen([runtime, "--sdk-project", root, "--sdk-play", "on",
                               "--user-data", root / "users"],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    session, sequence = "", 0

    def request(command, expected=True, **fields):
        global session, sequence
        sequence += 1
        process.stdin.write(json.dumps(dict(protocol=2, id=sequence, session=session,
                                           command=command, **fields)) + "\n")
        process.stdin.flush()
        line = process.stdout.readline()
        assert line, process.stderr.read()
        response = json.loads(line)
        assert response["ok"] == expected, {key: response.get(key) for key in
                                           ("ok", "error", "activation", "timing")}
        session = response["session"]
        return response

    def probe(response):
        return next(row["components"]["project.rate_probe"]
                    for row in response["scene"]["entities"]
                    if "project.rate_probe" in row["components"])

    def wait_for(predicate):
        deadline = time.monotonic() + 10
        while True:
            response = request("snapshot")
            if predicate(response):
                return response
            assert time.monotonic() < deadline, dict(timing=response.get("timing"),
                                                       probe=probe(response),
                                                       activation=response.get("activation"))
            time.sleep(.005)

    def change(key, rate):
        request("snapshot", input_events=[dict(control=key, value=1)])
        changed = wait_for(lambda r: r["timing"]["time_scale"] == rate and
                          probe(r)["observed_rate"] == rate)
        request("snapshot", input_events=[dict(control=key, value=0)])
        return changed

    try:
        request("hello", simulation_hz=100)
        request("replace", scene=scene)
        candidate = wait_for(lambda r: r.get("candidate") is not None)
        ticket = candidate["candidate"]["ticket"]
        envelope = request("candidate", ticket=ticket)
        assert envelope.get("candidate"), envelope
        request("snapshot", editor_epoch=dict(epoch=1, captured=False, input_device="KeyboardMouse"),
                candidate_ack=dict(session=session, ticket=ticket,
                                               accepted=True, diagnostic=""))
        active = wait_for(lambda r: r["activation"]["state"] in ("ready", "active"))
        generation = active["activation"]["generation"]
        assert request("time_scale", generation=generation, scale=.5)["timing"]["time_scale"] == .5
        for target in ({}, dict(generation=generation+1), dict(generation=-1),
                       dict(generation=True), dict(generation=str(generation))):
            request("time_scale", expected=False, **target, scale=1)
        for scale in (-1, 4.01, True, "1", None):
            request("time_scale", expected=False, generation=generation, scale=scale)
        assert request("snapshot")["timing"]["time_scale"] == .5
        request("time_scale", generation=generation, scale=1)
        request("play")
        running = wait_for(lambda r: probe(r)["ticks"] >= 3)
        assert abs(probe(running)["last_dt"] - .01) < 1e-8
        held = change("key.h", 0)
        time.sleep(.08)
        observed = request("snapshot")
        assert observed["timing"]["tick"] == held["timing"]["tick"]
        assert probe(observed)["ticks"] == probe(held)["ticks"]
        assert probe(observed)["frames"] > probe(held)["frames"]
        slow = change("key.q", .25)
        continued = wait_for(lambda r: probe(r)["ticks"] > probe(slow)["ticks"] + 1)
        assert abs(probe(continued)["last_dt"] - .01) < 1e-8
        fast = change("key.f", 4)
        continued = wait_for(lambda r: probe(r)["ticks"] > probe(fast)["ticks"] + 2)
        assert abs(probe(continued)["last_dt"] - .01) < 1e-8
        change("key.n", 1)
        paused = request("pause")
        change("key.h", 0)
        stepped = request("step")
        assert stepped["timing"]["tick"] == paused["timing"]["tick"] + 1
        assert stepped["timing"]["time_scale"] == 0
        assert probe(stepped)["ticks"] == probe(paused)["ticks"] + 1
        request("quit")
        assert process.wait(timeout=5) == 0
        print("C++ control-frame hold/restore, matching fixed delta and paused Step passed")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
