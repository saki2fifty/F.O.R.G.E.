"""Crash each asset source/catalog boundary and preserve external conflict bytes."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
base = Path(sys.argv[2]).resolve()
base.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=base) as temporary:
    root = Path(temporary)
    subprocess.run([exe, root / "unit"], check=True, timeout=30)
    for stage in (1, 2, 3, 4):
        project = root / str(stage)
        result = subprocess.run([exe, project, "--crash", str(stage)], timeout=30)
        assert result.returncode == 90 + stage, result.returncode
        subprocess.run([exe, project, "--recover", str(stage)], check=True, timeout=30)
    project = root / "conflict"
    result = subprocess.run([exe, project, "--crash", "3"], timeout=30)
    assert result.returncode == 93
    external = project / "Assets/moved.fixture"
    external.write_bytes(b"external edit")
    result = subprocess.run([exe, project, "--recover", "3"], capture_output=True, timeout=30)
    assert result.returncode == 1 and b"conflicts with external edits" in result.stderr
    assert external.read_bytes() == b"external edit"
    assert (project / ".forge/asset-file-operation.json").is_file()
    project = root / "corrupt-backup"
    result = subprocess.run([exe, project, "--crash", "2"], timeout=30)
    assert result.returncode == 92
    journal = json.loads((project / ".forge/asset-file-operation.json").read_text())
    backup = (project / ".forge/asset-file-operations" / journal["transaction"] /
              journal["changes"][0]["before"])
    backup.write_bytes(b"corrupt")
    original_catalog = (project / "forge.assets.json").read_bytes()
    result = subprocess.run([exe, project, "--recover", "2"], capture_output=True, timeout=30)
    assert result.returncode == 1 and b"missing or corrupt" in result.stderr
    assert (project / "forge.assets.json").read_bytes() == original_catalog
    assert not (project / "Assets/source.fixture").exists()
    assert (project / ".forge/asset-file-operation.json").is_file()
print("Asset file process interruption/recovery/conflict tests passed")
