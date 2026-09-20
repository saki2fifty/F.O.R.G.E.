"""Exercise canonical input with the exact official converter and admitted sampling."""
import base64
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys

converter, test, root = map(Path, sys.argv[1:])
root.mkdir(parents=True, exist_ok=True)


def run(name, nodes, channels, expected, skins=None, roots=None):
    work = root / name
    work.mkdir(exist_ok=True)
    data = bytearray()
    doc = dict(asset=dict(version="2.0"), nodes=nodes, scenes=[dict(nodes=[0] if roots is None else roots)], scene=0,
               bufferViews=[], accessors=[])

    def accessor(values, width, times=False):
        offset = len(data)
        data.extend(struct.pack("<" + "f" * len(values), *values))
        doc["bufferViews"].append(dict(buffer=0, byteOffset=offset, byteLength=len(data)-offset))
        a = dict(bufferView=len(doc["bufferViews"])-1, componentType=5126,
                 count=len(values)//width, type={1: "SCALAR", 3: "VEC3", 4: "VEC4"}[width])
        if times:
            a.update(min=[values[0]], max=[values[-1]])
        doc["accessors"].append(a)
        return len(doc["accessors"])-1

    samplers, targets = [], []
    for node, path, mode, times, values in channels:
        width = 4 if path == "rotation" else 1 if path == "weights" else 3
        samplers.append(dict(input=accessor(times, 1, True), output=accessor(values, width), interpolation=mode))
        targets.append(dict(sampler=len(samplers)-1, target=dict(node=node, path=path)))
    if targets:
        doc["animations"] = [dict(samplers=samplers, channels=targets)]
    if skins:
        doc["skins"] = skins
    if any(path == "weights" for _, path, *_ in channels):
        pos = accessor([0, 0, 0, 1, 0, 0, 0, 1, 0], 3)
        doc["accessors"][pos].update(min=[0, 0, 0], max=[1, 1, 0])
        doc["meshes"] = [dict(primitives=[dict(attributes=dict(POSITION=pos), targets=[dict(POSITION=pos)])])]
    if data:
        doc["buffers"] = [dict(byteLength=len(data), uri="data:application/octet-stream;base64," + base64.b64encode(data).decode())]
    (work / "input.gltf").write_text(json.dumps(doc))
    (work / "expected.json").write_text(json.dumps(expected))
    subprocess.run([str(test), "prepare", str(work)], check=True, timeout=20)
    subprocess.run([str(test), "convert", str(work), str(converter)], check=True, timeout=30, capture_output=True)
    subprocess.run([str(test), "verify", str(work)], check=True, timeout=20)
    return json.loads((work / "metadata.json").read_text())


# Matrix ancestor not animated: native unadapted fallback loses its X translation.
nodes = [dict(matrix=[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 0, 0, 1], children=[2, 1]), {}, dict(translation=[0, 1, 0])]
base = dict(rest=[[0, 12, 5], [1, 12, 5], [1, 13, 1]], parents=[-1, 0],
            samples=[dict(ratio=.5, values=[[0, 12, 5], [1, 12, 5.5], [1, 13, 1.5]])])
meta = run("matrix", nodes, [(2, "translation", "LINEAR", [0, 1], [0, 1, 0, 1, 2, 0])], base)
assert meta["joint_nodes"] == [0, 2]  # unrelated source node omitted, identity still source index
reordered_nodes = [copy.deepcopy(nodes[2]), copy.deepcopy(nodes[0]), copy.deepcopy(nodes[1])]
reordered_nodes[1]["children"] = [0, 2]
reordered = run("node-reorder", reordered_nodes, [(0, "translation", "LINEAR", [0, 1], [0, 1, 0, 1, 2, 0])], base, roots=[1])
assert reordered["joint_nodes"] == [1, 0]
assert reordered["content_evidence"] == meta["content_evidence"]
assert reordered["semantic_evidence"] == meta["semantic_evidence"]
for evidence in ("content_evidence", "semantic_evidence"):
    assert reordered["clips"][0][evidence] == meta["clips"][0][evidence]

# Explicit signed/zero TRS remains explicit, including animation through zero.
run("signed", [dict(scale=[-1, 2, 0], children=[1]), dict(translation=[0, 1, 0])],
    [(0, "scale", "LINEAR", [0, 1], [-1, 2, 0, 1, 2, 0]), (1, "translation", "LINEAR", [0, 1], [0, 1, 0, 0, 1, 0])],
    dict(rest=[[0, 0, -1], [0, 10, 0], [1, 13, 2]], parents=[-1, 0], samples=[
        dict(ratio=0, values=[[0, 0, -1]]), dict(ratio=.5, values=[[0, 0, 0], [1, 13, 2]]),
        dict(ratio=1, values=[[0, 0, 1]])]))
# Every TRS channel is occupied; longer morph animation must still retain duration.
for mode in ("STEP", "LINEAR", "CUBICSPLINE"):
    values = [0, 0, 0, 1, 0, 0]
    if mode == "CUBICSPLINE":
        values = [0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 9, 0, 0]
    meta = run("morph-tail-" + mode, [dict(mesh=0)], [
        (0, "translation", mode, [0, 1], values),
        (0, "rotation", "LINEAR", [0, 1], [0, 0, 0, 1] * 2),
        (0, "scale", "LINEAR", [0, 1], [1, 1, 1] * 2),
        (0, "weights", "LINEAR", [0, 2], [0, 1])],
        dict(rest=[[0, 12, 0]], parents=[-1], samples=[dict(ratio=.75, values=[[0, 12, 1]])]))
    assert meta["clips"][0]["duration"] == 2
    assert meta["clips"][0]["morph_tracks"][0]["times"] == [0, 2]
# Source rest values outside ECS LocalScale but within Ozz profile are data, not ECS writes.
for matrix in (False, True):
    node = dict(scale=[20000, 1, 1])
    if matrix:
        node = dict(matrix=[20000, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
    run("rest-range-" + str(matrix), [node], [], dict(rest=[[0, 0, 20000]], parents=[-1], samples=[]), skins=[dict(joints=[0])])
run("constant", [dict()], [(0, "translation", "LINEAR", [0], [2, 3, 4])],
    dict(rest=[[0, 12, 0]], parents=[-1], samples=[dict(ratio=.5, values=[[0, 12, 2], [0, 13, 3]])]))
run("morph-only", [dict(mesh=0)], [(0, "weights", "LINEAR", [0, 2], [0, 1])],
    dict(rest=[[0, 12, 0]], parents=[-1], samples=[dict(ratio=.5, values=[[0, 12, 0]])]))
print("Canonical converter, matrix fallback, joint order, signed/zero scale, morph timing and constant clips passed")
