"""Only official conversion of a small owned fixture; malformed tests run in FORGE admission."""
import json
import base64
import struct
import pathlib
import subprocess
import sys

tool, test, source, root = map(pathlib.Path, sys.argv[1:])
root.mkdir(parents=True, exist_ok=True)
config = {"skeleton": {"filename": "skeleton.ozz"}, "animations": [
    {"clip": "Lift", "filename": "clip.ozz", "iframe_interval": 0}]}
(root / "config.json").write_text(json.dumps(config), encoding="utf-8")
subprocess.run([str(tool), "--file=" + str(source), "--config_file=config.json"],
               cwd=root, check=True, timeout=20, capture_output=True)
# A second owned fixture exercises quaternion interpolation and hierarchy rotation.
rotation = json.loads(source.read_text())
raw = base64.b64decode(rotation["buffers"][0]["uri"].split(",", 1)[1])
offset = len(raw)
raw += struct.pack("<8f", 0, 0, 0, 1, 0, 0, 2**-.5, 2**-.5)
rotation["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(raw).decode()
rotation["buffers"][0]["byteLength"] = len(raw)
rotation["bufferViews"].append(dict(buffer=0, byteOffset=offset, byteLength=32))
rotation["accessors"].append(dict(bufferView=len(rotation["bufferViews"])-1, componentType=5126, count=2, type="VEC4"))
rotation["animations"] = [dict(name="Turn", samplers=[dict(input=0, output=len(rotation["accessors"])-1, interpolation="LINEAR")], channels=[dict(sampler=0, target=dict(node=0, path="rotation"))])]
(root/"rotation.gltf").write_text(json.dumps(rotation))
config["animations"][0].update(clip="Turn", filename="turn.ozz")
(root/"rotation-config.json").write_text(json.dumps(config))
subprocess.run([str(tool), "--file=rotation.gltf", "--config_file=rotation-config.json"], cwd=root, check=True, timeout=20, capture_output=True)
subprocess.run([str(test), str(root / "skeleton.ozz"), str(root / "clip.ozz"), str(root / "turn.ozz")],
               check=True, timeout=30)
