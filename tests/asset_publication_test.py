"""Actual process interruption at durable journal/sidecar/catalog boundaries."""
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
    for stage in (1, 2, 3):
        project = root / str(stage)
        result = subprocess.run([exe, project, "--crash", str(stage)], timeout=30)
        assert result.returncode == 90 + stage, result.returncode
        subprocess.run([exe, project, "--recover", str(stage)], check=True, timeout=30)
    # An external edit after the crash must never be overwritten by recovery.
    project = root / "conflict"
    result = subprocess.run([exe, project, "--crash", "2"], timeout=30)
    assert result.returncode == 92
    sidecar = project / "Assets/source.fixture.forge-import.json"
    external = sidecar.read_bytes() + b"\n"
    sidecar.write_bytes(external)
    result = subprocess.run([exe, project, "--recover", "2"], capture_output=True, timeout=30)
    assert result.returncode == 1 and b"conflicts with external edits" in result.stderr
    assert sidecar.read_bytes() == external
    assert (project / ".forge/asset-publication.json").is_file()
print("Publication precommit rollback, postcommit retention and conflict preservation passed")
