"""Original particle Gallery over ordinary/exact C++ runtime pipes and recovery."""
import copy
import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

runtime, gallery = (Path(value).resolve() for value in sys.argv[1:3])
module = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
with tempfile.TemporaryDirectory(dir=runtime.parent, prefix="particle-process-") as scratch:
    root = Path(scratch) / "project"
    shutil.copytree(gallery, root)
    source_path = root / "Scenes/54-cpu-mesh-particles.scene.json"
    source = json.loads(source_path.read_text())
    actors = [row["id"] for row in source["entities"]
              if "forge.particle_emitter" in row["components"]]
    assert len(actors) == 3
    project = dict(version=2, name="Particle process", startup_scene=None,
                   input=dict(version=1, actions=[]), modules=[])
    if module:
        shutil.copy2(module, root / module.name)
        fingerprint = json.loads(subprocess.check_output(
            [runtime, "--sdk-info"], text=True))["fingerprint"]
        project["modules"] = [dict(id="project.particle_probe", implementation="1",
                                   sdk="experimental-1", fingerprint=fingerprint,
                                   library=module.name,
                                   dependencies=["forge.transforms", "forge.particles"])]
    (root / "forge.project.json").write_text(json.dumps(project))
    process = subprocess.Popen([runtime, "--sdk-project" if module else "--project", root],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
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
        result = json.loads(line)
        assert result["ok"], result.get("error", result.get("diagnostics"))
        session = result["session"]
        return result

    def captures(response):
        return [row.get("particle_playback", {}) for row in response["effective_scene"]["entities"]
                if row["id"] in actors]

    try:
        request("hello", simulation_hz=60)
        initial = request("replace", scene=source)
        # Headless imports may finish asynchronously. Observation admits resources,
        # but must never drive visual emission time.
        deadline = time.monotonic() + 10
        while True:
            initial = request("snapshot")
            rows = captures(initial)
            if len(rows) == 3 and all(row for row in rows):
                break
            assert time.monotonic() < deadline, "Particle initial admission timed out"
            time.sleep(.005)
        assert all(row["time"] == 0 for row in rows)
        assert len(initial["effective_scene"]["entities"]) == len(source["entities"])
        for _ in range(12):
            stepped = request("step")
        snapshot = request("snapshot")
        assert captures(snapshot) == captures(stepped)
        assert snapshot["timing"]["tick"] == stepped["timing"]["tick"]
        for row in captures(snapshot):
            assert abs(row["time"] - (24 if module else 12) / 60) < 1e-6, row
            assert row["generation"] > 0 and len(row["revision"]) == 64
            if module:
                assert row["seed"] == 9
        assert snapshot["scene"]["asset_id"] == source["asset_id"]
        assert len(snapshot["scene"]["entities"]) == len(source["entities"])
        if module:
            for row in snapshot["scene"]["entities"]:
                if row["id"] in actors:
                    values = row["components"]["forge.particle_emitter"]
                    assert values["seed"] == 9 and values["playback_speed"] == 2
        checkpoint = copy.deepcopy(snapshot["recovery"])
        restored = request("replace", scene=checkpoint["scene"], recovery=checkpoint,
                           recovery_session=checkpoint["session"], recovery_tick=checkpoint["tick"])
        deadline = time.monotonic() + 10
        while True:
            restored = request("snapshot")
            rows = captures(restored)
            if len(rows) == 3 and all(row for row in rows):
                break
            assert time.monotonic() < deadline, "Particle reconstruction timed out"
            time.sleep(.005)
        # Particles intentionally restart visual age from saved values. No native
        # runtime handles, birth arrays or arbitrary C++ memory enter recovery JSON.
        assert all(row["time"] == 0 for row in rows)
        assert "particles" not in checkpoint
        assert json.loads(source_path.read_text()) == source
        request("quit")
        assert process.wait(timeout=5) == 0
        print("Particle compact presentation, readonly snapshots, recovery visual reset:",
              "matching C++ module" if module else "ordinary runtime")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
