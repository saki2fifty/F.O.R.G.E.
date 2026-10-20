"""Shared source-free Windows export/relocation gate for builtin Gallery scenes."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid


def verify_scene(tools, gallery, kit, evidence, scene_source, retain=None):
    tools, gallery, kit, evidence = (Path(p).resolve() for p in (tools, gallery, kit, evidence))
    evidence.mkdir(parents=True, exist_ok=True)
    assert os.name == 'nt', 'This acceptance requires the production Windows shared host'
    assert (kit / 'flecs.dll').is_file(), 'Expected the matching shared runtime kit'
    env = os.environ.copy()
    env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
    scene_path = Path(scene_source)
    scene = json.loads((gallery / scene_path).read_text(encoding='utf-8'))
    with tempfile.TemporaryDirectory(prefix='FORGE Gallery export ') as temporary:
        root = Path(temporary)
        source, copied_kit = root / 'Gallery source', root / 'Matching shared kit'
        shutil.copytree(gallery, source)
        shutil.copytree(kit, copied_kit)
        settings = json.loads((source / 'forge.project.json').read_text(encoding='utf-8'))
        assert not settings['modules'], 'This Gallery scene uses builtin components only'
        settings['startup_scene'] = dict(asset=scene['asset_id'], source=scene_path.as_posix())
        settings['game']['application_id'] = 'org.forge.gallery-export-test.' + uuid.uuid4().hex
        (source / 'forge.project.json').write_text(json.dumps(settings), encoding='utf-8')
        game = root / 'Exported game'
        options = dict(destination=str(game), runtime_kit=str(copied_kit))
        result = subprocess.run([str(tools), '--assets', 'export-game', str(source),
                                 json.dumps(options)], cwd=root, env=env, text=True,
                                capture_output=True, timeout=180)
        (evidence / 'export.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        assert result.returncode == 0 and json.loads(result.stdout)['ok'], (result.stdout, result.stderr)
        shutil.rmtree(source)
        shutil.rmtree(copied_kit)
        manifest = json.loads((game / 'forge.standalone.json').read_text(encoding='utf-8'))
        assert manifest['engine']['profile'] == 'shared-native-sdk' and not manifest['settings']['modules']
        assert not (game / 'Scenes').exists() and not (game / 'Assets').exists()
        assert not list(game.rglob('*.cpp')) and not list(game.rglob('*.hpp'))
        assert not list(game.rglob('forge_editor.exe'))
        for number in (1, 2):
            moved = root / f'Relocated Gallery game {number}'
            shutil.move(str(game), moved)
            game = moved
            for name, info in manifest['files'].items():
                assert hashlib.sha256((game / name).read_bytes()).hexdigest() == info['sha256'], name
            result = subprocess.run([str(game / 'forge_game.exe'), '--verify-startup'],
                                    cwd=game, env=env, text=True, capture_output=True, timeout=180)
            (evidence / f'startup-{number}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
            assert {p.relative_to(game).as_posix() for p in game.rglob('*') if p.is_file()} == set(manifest['files']) | {'forge.standalone.json'}
        if retain:
            retain = Path(retain).resolve()
            assert not retain.exists(), retain
            retain.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(game, retain)
        receipt = dict(engine=manifest['engine'], scene=scene['asset_id'],
                       source_and_kit_removed=True, production_startups=2,
                       installation_unchanged=True,
                       manifest_sha256=hashlib.sha256((game / 'forge.standalone.json').read_bytes()).hexdigest(),
                       scope='Production shared Windows game/export/relocation; separate renderer tests verify mapped pixels. Physical GPU acceptance remains separate.')
    return receipt
