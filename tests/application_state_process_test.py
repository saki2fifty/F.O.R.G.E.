"""Ordinary Play admits cooked states and restores semantic pending requests."""
import copy
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import uuid

from runtime_client import Runtime


def integrity(value):
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":"),
                         ensure_ascii=False).encode("utf-8")
    result = 14695981039346656037
    for byte in encoded:
        result = ((result ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return str(result)


def app(response):
    return response["recovery"]["application_states"]["domains"][0]


def main():
    tools, runtime, scratch = (pathlib.Path(item).resolve() for item in sys.argv[1:4])
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="state-process-", dir=scratch) as folder:
        root = pathlib.Path(folder)
        (root / "Assets").mkdir()
        (root / "forge.assets.json").write_text(
            json.dumps({"version": 1, "assets": []}), encoding="utf-8")
        asset = str(uuid.uuid4())
        source = {
            "format": "forge.application-states", "version": 1, "asset_id": asset,
            "domains": [{"key": "app", "initial": "menu",
                         "states": [{"key": "menu"}, {"key": "playing"}]}],
        }
        locator = "Assets/Modes.appstates.json"
        (root / locator).write_text(json.dumps(source), encoding="utf-8")
        imported = subprocess.run(
            [str(tools), "--assets", "import", str(root), locator],
            capture_output=True, text=True, timeout=30)
        assert imported.returncode == 0, (imported.stdout, imported.stderr)
        # The real runtime loader must use its selected cache, without editable data.
        (root / "Assets").rename(root / "Source offline")
        lifetime_module = pathlib.Path(sys.argv[4]).resolve() if len(sys.argv) > 4 else None
        if lifetime_module:
            settings = json.loads((pathlib.Path(__file__).resolve().parents[1] /
                                   'samples/projects/FeatureGallery/forge.project.json').read_text(encoding='utf-8'))
            settings['startup_scene'] = None
            fingerprint = json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True))['fingerprint']
            settings['modules'] = [dict(id='project.runtime_lifetime_probe', implementation='1',
                                       sdk='experimental-1', fingerprint=fingerprint,
                                       library=lifetime_module.name, dependencies=[])]
            (root/'forge.project.json').write_text(json.dumps(settings), encoding='utf-8')
            shutil.copy2(lifetime_module, root/lifetime_module.name)
        worker = Runtime(runtime, ["--sdk-project" if lifetime_module else "--project", root])
        try:
            scene = {"version": 1, "entities": [
                {"id": "modes", "name": "Modes", "components": {
                    "forge.application_states": {"definition": asset}}}]}
            initial = worker.request("replace", scene=scene)
            assert app(initial)["active"] == "menu" and app(initial)["revision"] == 1
            generation = initial["world_generation"]
            if lifetime_module:
                worker.request('step')
                initial = worker.request('step')
                snapshot = initial['recovery']['scene']
                actor = next(row for row in snapshot['entities'] if row['name'] == 'MCP scoped runtime root')
                reference = dict(scene=snapshot['asset_id'], entity=actor['id'])
                scoped = worker.request('application_state', generation=generation, entity=reference,
                                        domain='app', state='menu', expected_revision=1)
                roots = scoped['recovery']['runtime_entity_lifetimes']['roots']
                assert len(roots) == 1 and roots[0]['scope'] == dict(domain='app', state='menu')
                checkpoint = scoped['recovery']
                bad = copy.deepcopy(checkpoint)
                bad.pop('integrity')
                bad['runtime_entity_lifetimes']['roots'].append(roots[0])
                bad['integrity'] = integrity(bad)
                try:
                    worker.request('replace', scene=bad['scene'], recovery=bad,
                                   recovery_session=bad['session'], recovery_tick=bad['tick'])
                except RuntimeError as error:
                    assert 'duplicate' in str(error), error
                else:
                    raise AssertionError('Duplicate lifetime recovery was accepted')
                initial = worker.request('replace', scene=checkpoint['scene'], recovery=checkpoint,
                                         recovery_session=checkpoint['session'], recovery_tick=checkpoint['tick'])
                generation = initial['world_generation']
                assert initial['recovery']['runtime_entity_lifetimes'] == checkpoint['runtime_entity_lifetimes']
            moved = worker.request("application_state", generation=generation, domain="app",
                                   state="playing", expected_revision=1)
            if lifetime_module:
                assert moved['recovery'] is None, 'Pending 2D retirement must not become a partial checkpoint'
                assert moved['application_states']['domains'][0]['active'] == 'playing'
                assert moved['physics']['bodies_2d'] == 0
                assert moved['timing']['tick'] == initial['timing']['tick']
                moved = worker.request('step')
            assert app(moved)["active"] == "playing" and app(moved)["revision"] == 2
            assert moved["timing"]["tick"] == initial["timing"]["tick"] + bool(lifetime_module)
            assert moved["application_states"]["domains"][0]["states"][0]["key"] == "menu"
            if lifetime_module:
                assert all(row['id'] != reference['entity'] for row in moved['recovery']['scene']['entities'])
            for gen, revision in ((generation, 1), (generation + 1, 2)):
                try:
                    worker.request("application_state", generation=gen, domain="app",
                                   state="menu", expected_revision=revision)
                except RuntimeError as error:
                    assert "stale" in str(error).lower(), error
                else:
                    raise AssertionError("Stale state/world control was accepted")
                assert app(worker.request("snapshot")) == app(moved)
            initial = worker.request("application_state", generation=generation, domain="app",
                                     state="menu", expected_revision=2)
            base_revision = app(initial)["revision"]
            checkpoint = copy.deepcopy(initial["recovery"])
            checksum = checkpoint.pop("integrity")
            assert integrity(checkpoint) == checksum, "Test does not match the recovery checksum"
            # A pending request is part of private recovery state; it must wait for Step.
            checkpoint["application_states"]["domains"][0]["requested"] = "playing"
            checkpoint["integrity"] = integrity(checkpoint)
            restored = worker.request(
                "replace", scene=checkpoint["scene"], recovery=checkpoint,
                recovery_session=checkpoint["session"], recovery_tick=checkpoint["tick"])
            assert app(restored)["active"] == "menu" and app(restored)["requested"] == "playing"
            assert restored["timing"]["tick"] == initial["timing"]["tick"]
            stepped = worker.request("step")
            assert app(stepped)["active"] == "playing" and app(stepped)["revision"] == base_revision + 1
            assert app(stepped)["requested"] == ""
            bad = copy.deepcopy(stepped["recovery"])
            bad.pop("integrity")
            bad["application_states"]["definition_digest"] = "0" * 64
            bad["integrity"] = integrity(bad)
            try:
                worker.request("replace", scene=bad["scene"], recovery=bad,
                               recovery_session=bad["session"], recovery_tick=bad["tick"])
            except RuntimeError as error:
                assert "Application state recovery definition" in str(error), error
            else:
                raise AssertionError("Mismatched state definition was recovered")
            unchanged = worker.request("snapshot")
            assert app(unchanged) == app(stepped), "Rejected recovery disturbed the active World"
            worker.request("quit")
            assert worker.process.wait(timeout=10) == 0, "Ordinary Play did not shut down cleanly"
        finally:
            worker.close()
    print("Ordinary Play source-free state initialization, pending recovery, Step and rejected definition preservation passed")


if __name__ == "__main__":
    main()
