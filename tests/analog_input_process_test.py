"""Actual legacy and SDK Play hosts publish the same conditioned action values."""
import json
from pathlib import Path
import sys
import tempfile
import time
from runtime_client import Runtime

runtime = Path(sys.argv[1]).resolve()
action = "12345678-1234-4234-8234-123456789abc"
source = Path(__file__).resolve().parents[1]
for sdk in (False, True):
    with tempfile.TemporaryDirectory(prefix="analog-input-", dir=runtime.parent) as temporary:
        root = Path(temporary)
        game = json.loads((source / "samples/projects/FeatureGallery/forge.project.json").read_text())["game"]
        game["input"]["stick_deadzone"] = .2
        project = dict(version=2, name="Analog response", startup_scene=None, simulation_hz=120,
                       game=game, input=dict(version=1, actions=[dict(id=action, name="Aim", kind="axis2",
                       bindings=[dict(control="pad.left_x", x=1, y=0, radial=True, deadzone=.2,
                                      outer_deadzone=.1, response_exponent=2),
                                 dict(control="pad.left_y", x=0, y=1, radial=True, deadzone=.2,
                                      outer_deadzone=.1, response_exponent=2)])]))
        (root / "forge.project.json").write_text(json.dumps(project))
        args = ["--sdk-project", root, "--sdk-play", "on" if sdk else "off",
                "--user-data", root / "users"]
        worker = Runtime(runtime, args)
        def until(predicate):
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                row = worker.request("snapshot")
                if predicate(row):
                    return row
                time.sleep(.005)
            raise AssertionError((sdk, row))
        def values(row):
            return next(item for item in row["input"]["actions"] if item["id"] == action)
        try:
            worker.request("replace", scene=dict(version=1, entities=[]))
            if sdk:
                candidate = until(lambda row: row.get("candidate") is not None)
                ticket = candidate["candidate"]["ticket"]
                worker.request("candidate", ticket=ticket)
                worker.request("snapshot", editor_epoch=dict(epoch=1, captured=True, input_device="Gamepad"),
                               candidate_ack=dict(session=worker.session, ticket=ticket, accepted=True, diagnostic=""))
                until(lambda row: row["activation"]["state"] in ("ready", "active"))
            worker.request("resume")
            worker.request("snapshot", input_events=[dict(control="pad.left_x", value=.33),
                                                       dict(control="pad.left_y", value=.44)])
            observed = until(lambda row: abs(values(row)["x"] - .15) < 1e-10 and
                                         abs(values(row)["y"] - .2) < 1e-10)
            assert observed["timing"]["tick"] > 0
            worker.request("snapshot", input_events=[dict(control="pad.left_x", value=-.9),
                                                       dict(control="pad.left_y", value=0)])
            until(lambda row: abs(values(row)["x"] + 1) < 1e-10 and abs(values(row)["y"]) < 1e-10)
            worker.request("snapshot", input_events=[dict(reset=True)])
            until(lambda row: values(row)["x"] == values(row)["y"] == 0)
            worker.request("quit")
            worker.process.wait(timeout=5)
            assert worker.process.returncode == 0
        finally:
            worker.close()
print("Analog response, radial direction, saturation and reset passed in both actual Play hosts")
