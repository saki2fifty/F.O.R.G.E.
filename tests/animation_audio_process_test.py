"""Original animated audio Gallery through ordinary or SDK Play and recovery."""
import copy
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

runtime, gallery = (Path(value).resolve() for value in sys.argv[1:3])
sdk = len(sys.argv) > 3 and sys.argv[3] == "--sdk"
with tempfile.TemporaryDirectory(dir=runtime.parent, prefix="animated-audio-") as scratch:
    root = Path(scratch) / "project"
    shutil.copytree(gallery, root)
    path = root / "Scenes/53-animated-audio.scene.json"
    source = json.loads(path.read_text())
    emitter = next(row["id"] for row in source["entities"]
                   if "forge.audio_source" in row["components"])
    project = dict(version=2, name="Animated audio process", startup_scene=None,
                   input=dict(version=1, actions=[]), modules=[])
    (root / "forge.project.json").write_text(json.dumps(project))
    process = subprocess.Popen([runtime, "--sdk-project" if sdk else "--project", root,
                                "--audio", "offline"], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    session = ""
    sequence = 0

    def request(command, **fields):
        global session, sequence
        sequence += 1
        process.stdin.write(json.dumps(dict(protocol=2, id=sequence, session=session,
                                           command=command, **fields)) + "\n")
        process.stdin.flush()
        line = process.stdout.readline()
        assert line, process.stderr.read()
        response = json.loads(line)
        assert response["ok"], response.get("error", response.get("diagnostics"))
        session = response["session"]
        return response

    def values(response):
        row = next(row for row in response["effective_scene"]["entities"]
                   if row["id"] == emitter)
        return row["components"]["forge.audio_source"]

    try:
        request("hello", simulation_hz=60)
        initial = request("replace", scene=source)
        # Both hosts publish the admitted time-zero sample before any fixed tick.
        # Derive the oracle from the original editable clip, not runtime output.
        authored = next(row for row in source["entities"] if row["id"] == emitter)["components"]
        assets = json.loads((root / "forge.assets.json").read_text())["assets"]
        clip = next(row for row in assets if row["id"] == authored["forge.transform_animator"]["clip"])
        keys = json.loads((root / clip["source"]).read_text())["keys"]
        initial_values = next(key["values"] for key in keys if key["time_us"] == 0)
        for field in ("gain", "pitch"):
            assert abs(values(initial)[field] - initial_values["forge.audio_source." + field]) < 1e-6
        initial_entries = initial["recovery"]["animation"]["transform_entries"]
        assert next(row["time_us"] for row in initial_entries if row["entity"]["entity"] == emitter) == 0
        assert initial["timing"]["tick"] == 0
        assert values(request("snapshot")) == values(initial)
        for _ in range(30):
            stepped = request("step")
        assert stepped["recovery"] is not None, "Animation did not become ready for recovery"
        entries = stepped["recovery"]["animation"]["transform_entries"]
        clock = next(row["time_us"] for row in entries if row["entity"]["entity"] == emitter)
        assert 0 < clock <= 500001, clock
        fraction = clock / 3000000
        assert abs(values(stepped)["gain"] - (.1 + .7 * fraction)) < 1e-5
        assert abs(values(stepped)["pitch"] - (.5 + fraction)) < 1e-5
        snapshot = request("snapshot")
        assert values(snapshot) == values(stepped)
        assert snapshot["timing"]["tick"] == stepped["timing"]["tick"]
        checkpoint = copy.deepcopy(snapshot["recovery"])
        restored = request("replace", scene=checkpoint["scene"], recovery=checkpoint,
                           recovery_session=checkpoint["session"], recovery_tick=checkpoint["tick"])
        assert values(restored) == values(snapshot)
        advanced = request("step")
        assert advanced["timing"]["tick"] > snapshot["timing"]["tick"]
        assert values(advanced)["gain"] > values(snapshot)["gain"]
        assert json.loads(path.read_text()) == source
        request("quit")
        assert process.wait(timeout=5) == 0
        print("Animated audio values, readonly snapshot and recovery passed:",
              "SDK host" if sdk else "ordinary host")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
