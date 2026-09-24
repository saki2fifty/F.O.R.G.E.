"""Run the reference game's actual SDL input workflow after source-free relocation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from reference_project_fixture import create

p = argparse.ArgumentParser()
for name in ('build', 'source', 'kit', 'module-kit', 'evidence', 'retain'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
build, source, kit, module_kit, evidence, retain = (v.resolve() for v in (a.build, a.source, a.kit, a.module_kit, a.evidence, a.retain))
evidence.mkdir(parents=True, exist_ok=True)

def run(args, log, **kwargs):
    result = subprocess.run(list(map(str, args)), text=True, capture_output=True, timeout=240, **kwargs)
    (evidence / log).write_text(result.stdout + '\n' + result.stderr, encoding='utf-8')
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout + result.stderr
    return result

with tempfile.TemporaryDirectory(prefix='FORGE reference export ') as temporary:
    work = Path(temporary)
    project = create(build, source, work, module_kit)
    run([build / 'forge_reference_level_tests.exe', build / 'forge_reference_game.dll', project,
         evidence / 'headless'], 'headless.log')
    testkit = work / 'kit'
    shutil.copytree(kit, testkit)
    shutil.copyfile(build / 'forge_game_fixture.exe', testkit / 'forge_game_fixture.exe')
    metadata = json.loads((testkit / 'forge.runtime-kit.json').read_text())
    data = (testkit / 'forge_game_fixture.exe').read_bytes()
    metadata['files']['forge_game_fixture.exe'] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    (testkit / 'forge.runtime-kit.json').write_text(json.dumps(metadata))
    result = run([build / 'forge_tools.exe', '--assets', 'export-game', project, json.dumps(dict(
        destination=str(work / 'export'), runtime_kit=str(testkit), module_kits={'project.reference': str(module_kit)}))], 'export.log')
    assert json.loads(result.stdout)['ok'], result.stdout
    destination = work / 'Relocated Field Test'
    shutil.move(str(work / 'export'), destination)
    shutil.rmtree(project)
    shutil.rmtree(testkit)
    env = os.environ.copy()
    env['PATH'] = str(Path(os.environ['SystemRoot']) / 'System32')
    run([destination / 'forge_game.exe', '--verify-startup'], 'startup.log', cwd=destination, env=env)
    run([destination / 'forge_game_fixture.exe', '--reference', evidence], 'workflow.log', cwd=destination, env=env)
    proof = json.loads((evidence / 'reference-workflow.json').read_text())
    assert proof['complete'] and proof['loading_captured'], proof
    moved = work / 'Moved Field Test'
    shutil.move(str(destination), moved)
    run([moved / 'forge_game_fixture.exe', '--reference-load', evidence / 'reopened'], 'relaunch.log', cwd=moved, env=env)
    assert json.loads((evidence / 'reopened/reference-relaunch.json').read_text())['restored']
    user_root = Path(json.loads((evidence / 'reference-storage.json').read_text())['root'])
    assert user_root == Path(json.loads((evidence / 'reopened/reference-storage.json').read_text())['root'])
    assert not user_root.is_relative_to(moved)
    slot = user_root / 'slot-one.json'
    good_save = slot.read_bytes()
    def changed_save(**fields):
        document = json.loads(good_save)
        document['payload'].update(fields)
        encoded = json.dumps(document['payload'], sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
        document['sha256'] = hashlib.sha256(encoded).hexdigest()
        return json.dumps(document).encode()
    try:
        for name, data, expected in (
            ('corrupt', b'{"payload":', ''),
            ('newer', changed_save(schema=2), 'Unsupported save schema version'),
            ('unavailable-scene', changed_save(scene='df2eaf46-0139-470a-b60d-86c47e42c2bf'), 'unavailable reference level'),
        ):
            slot.write_bytes(data)
            output = evidence / ('error-' + name)
            run([moved / 'forge_game_fixture.exe', '--reference-error', output],
                'error-' + name + '.log', cwd=moved, env=env)
            result = json.loads((output / 'reference-error.json').read_text())
            assert result['rejected'] and result['menu_retained'] and result['message'], result
            assert expected in result['message'], result
            assert slot.read_bytes() == data, 'Rejected load rewrote the player save'
    finally:
        slot.write_bytes(good_save)
    missing_output = evidence / 'error-missing-file'
    run([moved / 'forge_game_fixture.exe', '--reference-missing', missing_output],
        'error-missing-file.log', cwd=moved, env=env)
    missing = json.loads((missing_output / 'reference-error.json').read_text())
    assert missing['rejected'] and missing['menu_retained'] and missing['message'], missing
    assert slot.read_bytes() == good_save
    run([moved / 'forge_game.exe', '--verify-startup'], 'restored-content.log', cwd=moved, env=env)
    preferences = user_root / 'settings.json'
    good_preferences = preferences.read_bytes()
    try:
        preferences.write_bytes(b'{"payload":')
        output = evidence / 'error-settings'
        run([moved / 'forge_game_fixture.exe', '--reference-settings', output],
            'error-settings.log', cwd=moved, env=env)
        message = json.loads((output / 'reference-error.json').read_text())['message']
        assert message.startswith('Saved settings could not be loaded'), message
        assert preferences.read_bytes() == b'{"payload":'
    finally:
        preferences.write_bytes(good_preferences)
    if retain.exists():
        raise RuntimeError('Refusing to overwrite prior reference evidence package')
    shutil.copytree(moved, retain)
