"""Compile the cell consumer using only an installed SDK, relocate, and execute."""

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


def main():
    build, fixture, worker = map(lambda p: Path(p).resolve(), sys.argv[1:4])
    cmake, ninja = sys.argv[4:6]
    cache = dict(
        line.split("=", 1)
        for line in (build / "CMakeCache.txt").read_text().splitlines()
        if "=" in line and not line.startswith(("//", "#"))
    )
    compiler = next(v for k, v in cache.items() if k.startswith("CMAKE_CXX_COMPILER:"))
    with tempfile.TemporaryDirectory(prefix="tile-sdk-", dir=build) as scratch:
        base = Path(scratch)
        stage, client, moved = base / "sdk", base / "client", base / "moved kit"
        subprocess.run(
            [cmake, "--install", str(build), "--prefix", str(stage), "--component", "NativeSdk"],
            check=True,
        )
        # Compile the Gallery helper through this installed-only public boundary.
        # The fixture still executes its existing full copied cell lifecycle.
        assert (stage / "sdk/sample/TileMapEdits.hpp").is_file()
        probe = stage / "sdk/sample/tile_map_probe.cpp"
        probe.write_text('#include "TileMapEdits.hpp"\n' + probe.read_text(encoding="utf-8"),
                         encoding="utf-8")
        subprocess.run(
            [cmake, "-S", str(stage / "sdk/sample"), "-B", str(client), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_COMPILER=" + compiler,
             "-DCMAKE_MAKE_PROGRAM=" + ninja, "-DFORGE_NATIVE_SDK=" + str(stage)],
            check=True,
        )
        subprocess.run(
            [cmake, "--build", str(client), "--target", "tile_map_gameplay", "--parallel", "2"],
            check=True,
        )
        windows = os.name == "nt"
        module_name = "tile_map_gameplay" + (".dll" if windows else ".so")
        module = client / module_name
        if not windows:
            imports = subprocess.check_output(["readelf", "-d", str(module)], text=True)
            exports = subprocess.check_output(["nm", "-D", "--defined-only", str(module)], text=True)
            assert "libflecs.so.4" in imports and " ecs_init" not in exports
        assert not (stage / "sdk/include/forge/assets.hpp").exists()
        assert (stage / "sdk/docs/tilemap-foundation.md").is_file()
        (stage / "project").mkdir()
        shutil.copy2(module, stage / "project" / module_name)
        executable = "tile-api-fixture" + (".exe" if windows else "")
        shutil.copy2(fixture, stage / "bin" / executable)
        stage.rename(moved)
        env = os.environ.copy()
        if not windows:
            env["LD_LIBRARY_PATH"] = str(moved / "bin") + ":" + env.get("LD_LIBRARY_PATH", "")
        subprocess.run(
            [str(moved / "bin" / executable), str(worker), str(base / "assets"),
             str(moved / "project" / module_name)],
            env=env, cwd=moved, check=True, timeout=120,
        )
    print("Installed-only Tile Map module, moved Scene closure and fixed cell lifecycle passed")


if __name__ == "__main__":
    main()
