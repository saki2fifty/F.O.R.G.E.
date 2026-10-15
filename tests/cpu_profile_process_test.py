"""Observe real fixed ticks through production protocol2, without changing time."""
import json
import sys
from pathlib import Path
from runtime_client import Runtime

worker = Runtime(Path(sys.argv[1]))
try:
    source = dict(version=1, entities=[dict(id="cube", name="Cube", components={})])
    initial = worker.request("replace", scene=source)
    generation = initial["world_generation"]
    assert "cpu_profile" not in initial
    observed = worker.request("snapshot", profile_watch=True)
    assert observed["cpu_profile"]["available"] and not observed["cpu_profile"]["enabled"]
    original_scene = observed["scene"]
    original_tick = observed["timing"]["tick"]
    for command, fields in [
            ("profiling", dict(enabled=True)),
            ("profiling", dict(enabled=True, generation=generation + 1)),
            ("profiling", dict(enabled=1, generation=generation)),
            ("profiling", dict(enabled=True, generation=generation, clear=1)),
            ("resume", dict(profile_watch=1))]:
        try:
            worker.request(command, **fields)
        except RuntimeError:
            pass
        else:
            raise AssertionError("Invalid profiling request accepted")
        unchanged = worker.request("snapshot", profile_watch=True)
        assert unchanged["scene"] == original_scene
        assert unchanged["timing"]["tick"] == original_tick and unchanged["timing"]["paused"]
        assert not unchanged["cpu_profile"]["enabled"]
    enabled = worker.request("profiling", generation=generation, enabled=True, clear=True)
    assert enabled["cpu_profile"]["world_generation"] == generation
    for _ in range(140):
        stepped = worker.request("step", profile_watch=True)
        assert stepped["timing"]["paused"] and stepped["scene"] == original_scene
    samples = stepped["cpu_profile"]["samples"]
    assert len(samples) <= 512
    if enabled["cpu_profile"]["compiled"]:
        assert len(samples) == 512
        assert any(s["name"] == "FixedSimulationTick" and s["tick"] == 140 for s in samples)
        for name in ("RuntimeEntityPublication", "PresentationPoseCapture"):
            assert any(s["name"] == name and s["tick"] == 140 for s in samples), name
        assert all(0 <= s["seconds"] <= 3600 and s["count"] == 1 for s in samples)
        assert all(a["sequence"] < b["sequence"] for a, b in zip(samples, samples[1:]))
    stopped = worker.request("profiling", generation=generation, enabled=False)
    paused_samples = stopped["cpu_profile"]["samples"]
    stepped = worker.request("step", profile_watch=True)
    assert stepped["cpu_profile"]["samples"] == paused_samples
    cleared = worker.request("profiling", generation=generation, enabled=False, clear=True)
    assert not cleared["cpu_profile"]["samples"]
    worker.request("profiling", generation=generation, enabled=True)
    worker.request("step")
    replacement = worker.request("replace", scene=source, profile_watch=True)
    assert replacement["world_generation"] != generation
    assert len(replacement["cpu_profile"]["samples"]) < 10
    assert not any(s["name"] == "FixedSimulationTick" for s in replacement["cpu_profile"]["samples"])
    assert "cpu_profile" not in worker.request("snapshot")
    assert len(json.dumps(stepped["cpu_profile"]).encode("utf-8")) < 256 * 1024
    worker.request("quit")
    worker.process.wait(timeout=5)
    assert worker.process.returncode == 0
    print("CPU profiling production process/clock/generation/bounds passed")
finally:
    worker.close()
