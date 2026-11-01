"""Exercise the public import command for authored application-state sources."""
import json
import pathlib
import subprocess
import sys
import tempfile
import uuid


def main():
    tools = pathlib.Path(sys.argv[1]).resolve()
    scratch = pathlib.Path(sys.argv[2]).resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="state-tools-", dir=scratch) as folder:
        project = pathlib.Path(folder)
        (project / "Assets").mkdir()
        (project / "forge.assets.json").write_text(
            json.dumps({"version": 1, "assets": []}), encoding="utf-8")
        identity = str(uuid.uuid4())
        source = {
            "format": "forge.application-states", "version": 1,
            "asset_id": identity,
            "domains": [{"key": "app", "initial": "menu",
                         "states": [{"key": "menu"}, {"key": "playing"}]}],
        }
        path = project / "Assets/Modes.appstates.json"

        def write():
            path.write_text(json.dumps(source), encoding="utf-8")

        def invoke():
            result = subprocess.run(
                [str(tools), "--assets", "import", str(project),
                 "Assets/Modes.appstates.json"], capture_output=True, text=True,
                timeout=30)
            receipt = json.loads(result.stdout)
            assert receipt["ok"] == (result.returncode == 0), result.stderr
            return receipt

        write()
        first = invoke()
        assert first["ok"] and first["asset"] == identity, first
        second = invoke()
        assert second["ok"] and second["asset"] == identity
        assert second["cache_hit"] and second["build_key"] == first["build_key"]
        source["domains"][0]["initial"] = "playing"
        write()
        replacement = invoke()
        assert replacement["ok"] and replacement["asset"] == identity
        assert replacement["build_key"] != first["build_key"]
        catalog_path = project / "forge.assets.json"
        catalog = catalog_path.read_bytes()
        source["domains"][0]["initial"] = "missing"
        write()
        failed = invoke()
        assert not failed["ok"], failed
        assert failed.get("diagnostic") or failed.get("error", {}).get("message"), failed
        assert catalog_path.read_bytes() == catalog, "Failed CLI import replaced the catalog"
    print("Application state CLI identity, cache reuse, replacement and failed publication passed")


if __name__ == "__main__":
    main()
