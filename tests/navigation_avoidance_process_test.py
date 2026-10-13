"""Original owner-thread avoidance tuning through the existing Play protocol."""
import copy
import json
import shutil
import subprocess
import sys
from pathlib import Path

runtime, prepare, worker = (Path(value).resolve() for value in sys.argv[1:4])
root = Path(subprocess.check_output(
    [prepare, runtime.parent / "navigation-avoidance-process-data", worker, "--prepare"],
    text=True, timeout=30).strip())
source = json.loads((root / "scene.json").read_text())
process = subprocess.Popen([runtime, "--project", root], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
session = ""
sequence = 0

def request(command, expected=True, **extra):
    global session, sequence
    sequence += 1
    process.stdin.write(json.dumps(dict(protocol=2, id=sequence, session=session,
                                       command=command, **extra)) + "\n")
    process.stdin.flush()
    line = process.stdout.readline()
    assert line, "Avoidance worker exited before responding"
    response = json.loads(line)
    session = response["session"]
    assert response["ok"] is expected, response
    return response

def actor(response):
    return next(row for row in response["effective_scene"]["entities"] if row["name"] == "a")

try:
    request("hello", simulation_hz=60)
    initial = request("replace", scene=source)
    stepped = request("step")
    generation = stepped["world_generation"]
    target = dict(generation=generation, scene_asset=source["asset_id"],
                  entity=actor(stepped)["id"], type="forge.navigation_avoidance",
                  property="horizon", value=1.25)
    tuned = request("property_tune", **target)
    assert tuned["property_tune"]["value"] == 1.25
    snapshot = request("snapshot")
    assert actor(snapshot)["components"]["forge.navigation_avoidance"]["horizon"] == 1.25
    assert actor(snapshot)["navigation_debug"] == actor(stepped)["navigation_debug"]
    assert snapshot["timing"]["tick"] == stepped["timing"]["tick"]
    invalid = dict(target, value=0)
    request("property_tune", expected=False, **invalid)
    request("property_tune", expected=False, **dict(target, generation=generation + 1))
    assert actor(request("snapshot")) == actor(snapshot)
    checkpoint = copy.deepcopy(snapshot["recovery"])
    assert checkpoint["navigation"]["version"] == 2
    rebuilt = request("replace", scene=checkpoint["scene"], recovery=checkpoint,
                      recovery_session=checkpoint["session"], recovery_tick=checkpoint["tick"])
    assert actor(rebuilt)["components"]["forge.navigation_avoidance"]["horizon"] == 1.25
    request("property_tune", expected=False, **target)
    next_frame = request("step")
    assert actor(next_frame)["navigation_debug"]["sampled"]
    assert actor(next_frame)["navigation_debug"]["tick"] > actor(stepped)["navigation_debug"]["tick"]
    assert json.loads((root / "scene.json").read_text()) == source
    request("quit")
    process.wait(timeout=5)
    assert process.returncode == 0
    print("Avoidance worker live tuning, generation rejection and recovery passed")
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
    shutil.rmtree(root)
