"""Real native-process 2D recovery: paused state, continuation and isolation."""
import copy
from pathlib import Path
import sys
import uuid

from runtime_client import Runtime


def entity(name, y, motion, sensor=False):
    return dict(id=str(uuid.uuid4()), name=name, spatial=dict(mode="world"), components={
        "forge.local_translation": dict(x=0, y=y, z=7),
        "forge.local_scale": dict(x=-1, y=1, z=0),
        "forge.physics_body_2d": dict(motion=motion, sensor=sensor),
        "forge.box_collider_2d": dict(x=1, y=1),
    })


floor = entity("Floor", -.5, 0)
floor["components"]["forge.box_collider_2d"] = dict(x=20, y=1)
sensor = entity("Sensor", 4.5, 0, True)
sensor["components"]["forge.box_collider_2d"] = dict(x=3, y=3)
scene = dict(version=5, asset_id=str(uuid.uuid4()),
             entities=[floor, sensor, entity("Actor", 5, 2)])


def replace(worker, state):
    checkpoint = state["recovery"]
    return worker.request("replace", scene=state["scene"], recovery=checkpoint,
                          recovery_session=checkpoint["session"],
                          recovery_tick=checkpoint["tick"])


def motion(state):
    mapping = state["recovery"]["physics"]["planar"]["metadata"]["mapping"]
    return sorted(mapping, key=lambda body: body["epoch"])


runtime = Path(sys.argv[1]).resolve()
a, b = Runtime(runtime), Runtime(runtime)
try:
    initial = a.request("replace", scene=scene)
    assert initial["physics"]["bodies_2d"] == 3
    assert initial["recovery"]["physics"]["version"] == 6
    assert motion(replace(b, initial)) == motion(initial)
    for _ in range(20):
        saved = a.request("step")
    restored = replace(b, saved)
    assert restored["timing"]["paused"] and restored["timing"]["tick"] == 20
    assert restored["physics"]["tick"] == 20
    assert motion(restored) == motion(saved)
    for _ in range(60):
        left, right = a.request("step"), b.request("step")
        assert motion(left) == motion(right)
    # Invalid replacement discards only the candidate; the published runtime survives.
    for field in ("metadata", "solver", "bytes"):
        bad = copy.deepcopy(right)
        bad["recovery"]["physics"]["planar"][field] = None
        try:
            replace(b, bad)
        except RuntimeError:
            pass
        else:
            raise AssertionError(field)
        current = b.request("snapshot")
        assert current["timing"]["tick"] == 80
        assert motion(current) == motion(right)
    # Actual process destruction/restart, not another world in the same process.
    b.close()
    b = Runtime(runtime)
    restarted = replace(b, right)
    assert motion(restarted) == motion(right)
    assert motion(a.request("step")) == motion(b.request("step"))
    clean = b.request("replace", scene=initial["scene"])
    assert clean["timing"]["tick"] == 0
    actor = next(row for row in clean["scene"]["entities"] if row["name"] == "Actor")
    assert actor["components"]["forge.local_translation"]["y"] == 5
finally:
    a.close()
    b.close()

print("Real-process 2D recovery: paused continuation, rejected-candidate isolation and restart passed")
