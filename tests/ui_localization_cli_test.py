"""Actual CLI import/discovery and retained selection for an authored localization catalog."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid


tools = str(Path(sys.argv[1]).resolve())
scratch = Path(sys.argv[2]).resolve()
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="localization-cli-", dir=scratch) as temporary:
    root = Path(temporary)
    (root / "Assets").mkdir()
    (root / "forge.assets.json").write_text(json.dumps(dict(version=1, assets=[])), encoding="utf-8")
    asset = str(uuid.uuid4())
    source = dict(format="forge.ui-localization", version=1, asset_id=asset,
                  default_locale="en", locales=dict(en=dict(label="English", messages=dict(menu_play="Play")),
                                                   fr=dict(label="Français", messages=dict(menu_play="Jouer"))))
    path = root / "Assets/Original.localization.json"
    path.write_text(json.dumps(source), encoding="utf-8")

    def call(*arguments, ok=True):
        result = subprocess.run([tools, "--assets", *arguments], text=True,
                                encoding="utf-8", capture_output=True, timeout=30)
        assert (result.returncode == 0) == ok, (result.returncode, result.stdout, result.stderr)
        value = json.loads(result.stdout)
        assert value["ok"] == ok, value
        return value

    result = call("import", str(root), "Assets/Original.localization.json")
    assert result["asset"] == asset, result
    catalog = json.loads((root / "forge.assets.json").read_text(encoding="utf-8"))
    record = next(row for row in catalog["assets"] if row["id"] == asset)
    assert record["type"] == "ui_localization"
    assert record["metadata"]["forge.import"]["importer"] == "forge.ui.localization"
    assert record["metadata"]["forge.ui_localization"]["file"] == "localization.json"
    scanned = call("scan", str(root))
    assert "ui_localization" in json.dumps(scanned)
    source["default_locale"] = "absent"
    path.write_text(json.dumps(source), encoding="utf-8")
    call("import", str(root), "Assets/Original.localization.json", ok=False)
    assert json.loads((root / "forge.assets.json").read_text(encoding="utf-8")) == catalog

print("CLI localization identity/import/discovery and failed-source last-good selection passed")
