"""Actual CLI import/discovery and retained selection for an authored behavior."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid


tools = str(Path(sys.argv[1]).resolve())
scratch = Path(sys.argv[2]).resolve()
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="behavior-cli-", dir=scratch) as temporary:
    root = Path(temporary)
    (root / "Assets").mkdir()
    (root / "forge.assets.json").write_text(json.dumps(dict(version=1, assets=[])), encoding="utf-8")
    asset = str(uuid.uuid4())
    source = dict(format="forge.navigation-behavior", version=1, asset_id=asset,
                  entry=1, next_id=2,
                  states=[dict(id=1, name="Wait", action=dict(kind="wait"), transitions=[])])
    path = root / "Assets/Original.behavior.json"
    path.write_text(json.dumps(source), encoding="utf-8")

    def call(*arguments, ok=True):
        result = subprocess.run([tools, "--assets", *arguments], text=True,
                                encoding="utf-8", capture_output=True, timeout=30)
        assert (result.returncode == 0) == ok, (result.returncode, result.stdout, result.stderr)
        value = json.loads(result.stdout)
        assert value["ok"] == ok, value
        return value

    result = call("import", str(root), "Assets/Original.behavior.json")
    assert result["asset"] == asset, result
    catalog = json.loads((root / "forge.assets.json").read_text(encoding="utf-8"))
    record = next(row for row in catalog["assets"] if row["id"] == asset)
    assert record["type"] == "navigation_behavior"
    assert record["metadata"]["forge.import"]["importer"] == "forge.navigation.behavior"
    assert record["metadata"]["forge.navigation_behavior"]["file"] == "behavior.json"
    scanned = call("scan", str(root))
    assert "navigation_behavior" in json.dumps(scanned)
    source["entry"] = 999
    path.write_text(json.dumps(source), encoding="utf-8")
    call("import", str(root), "Assets/Original.behavior.json", ok=False)
    assert json.loads((root / "forge.assets.json").read_text(encoding="utf-8")) == catalog

print("CLI behavior identity/import/discovery and failed-source last-good selection passed")
