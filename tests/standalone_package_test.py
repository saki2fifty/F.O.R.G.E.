"""Exercise the real exporter and production game from a relocated package.
The second executable is an explicitly inventoried capture-only test observer.
It runs the same manifest admission and game host with scripted input/captures.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--kit', type=Path, required=True)
parser.add_argument('--evidence', type=Path, required=True)
parser.add_argument('--module-kit', type=Path)
parser.add_argument('--retain', type=Path)
parser.add_argument('--source', type=Path)
args = parser.parse_args()
build, kit, evidence = (p.resolve() for p in (args.build, args.kit, args.evidence))
evidence.mkdir(parents=True, exist_ok=True)

def run(argv, **kwargs):
    result = subprocess.run([str(v) for v in argv], text=True, capture_output=True,
                            timeout=120, **kwargs)
    if result.returncode:
        raise AssertionError((argv, result.returncode, result.stdout, result.stderr))
    return result

with tempfile.TemporaryDirectory(prefix='FORGE standalone export ') as temporary:
    work = Path(temporary)
    prepared = run([build/'forge_game_fixture.exe', '--prepare', work/'fixture'])
    project = Path(prepared.stdout.strip().splitlines()[-1])
    if args.source:
        from standalone_content_fixture import complete_content
        complete_content(project, work, build, args.source.resolve(), run)
    settings = json.loads((project/'forge.project.json').read_text())
    modules = {}
    if args.module_kit:
        module_kit = args.module_kit.resolve()
        deployment = json.loads((module_kit/'forge.module-kit.json').read_text())
        library = deployment['library']
        (project/'Native').mkdir()
        # Import dependencies accompany the source library for worker validation.
        for name in deployment['files']:
            shutil.copy2(module_kit/name, project/'Native'/name)
        settings['modules'] = [{
            'id': 'project.example', 'sdk': 'experimental-1', 'implementation': '1',
            'library': 'Native/'+library, 'fingerprint': deployment['fingerprint'],
            'dependencies': ['forge.input', 'forge.transforms']}]
        modules['project.example'] = str(module_kit)
        (project/'forge.project.json').write_text(json.dumps(settings))
    # Include the matching observation executable in a test-only runtime kit.
    testkit = work/'capture-kit'
    shutil.copytree(kit, testkit)
    shutil.copy2(build/'forge_game_fixture.exe', testkit/'forge_game_fixture.exe')
    metadata = json.loads((testkit/'forge.runtime-kit.json').read_text())
    data = (testkit/'forge_game_fixture.exe').read_bytes()
    metadata['files']['forge_game_fixture.exe'] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    (testkit/'forge.runtime-kit.json').write_text(json.dumps(metadata))
    options = {'destination': str(work/'export'), 'runtime_kit': str(testkit), 'module_kits': modules}
    exported = run([build/'forge_tools.exe', '--assets', 'export-game', project, json.dumps(options)])
    (evidence/'export-result.json').write_text(exported.stdout)
    exported = json.loads(exported.stdout)
    assert exported['ok']
    relocated = work/'unrelated'/'Relocated Game'
    relocated.parent.mkdir()
    shutil.move(str(work/'export'), relocated)
    # Remove original source and all fixture kit inputs. PATH has no SDK/repo/build.
    shutil.rmtree(project)
    shutil.rmtree(testkit)
    env = os.environ.copy()
    env['PATH'] = str(Path(os.environ['SystemRoot'])/'System32')
    checked = run([relocated/'forge_game.exe', '--verify-startup'], cwd=relocated, env=env)
    assert 'FORGE standalone startup verified' in checked.stdout
    run([relocated/'forge_game_fixture.exe', '--packaged', evidence], cwd=relocated, env=env)
    storage_result = json.loads((evidence/'storage-result.json').read_text())
    assert not storage_result['reopened'], 'Fixture application ID unexpectedly reused'
    user_root = Path(storage_result['root'])
    assert user_root.is_absolute() and not user_root.is_relative_to(relocated)
    assert (user_root/'runtime.log').is_file()
    # Move the same installation again, restart, and load its existing OS-scoped data.
    moved_again = work/'Moved Again'
    shutil.move(str(relocated), moved_again)
    relocated = moved_again
    restarted = evidence/'restarted'
    run([relocated/'forge_game_fixture.exe', '--packaged', restarted], cwd=relocated, env=env)
    restored = json.loads((restarted/'storage-result.json').read_text())
    assert restored['reopened'] and restored['root'] == storage_result['root']
    assert restored['scene'] == storage_result['scene']
    manifest = json.loads((relocated/'forge.standalone.json').read_text())
    actual = {p.relative_to(relocated).as_posix() for p in relocated.rglob('*') if p.is_file()}
    assert actual == set(manifest['files']) | {'forge.standalone.json'}, 'Runtime wrote into its installation'
    for file, info in manifest['files'].items():
        assert hashlib.sha256((relocated/file).read_bytes()).hexdigest() == info['sha256']
    manifest_path = relocated/'forge.standalone.json'
    original_manifest = manifest_path.read_bytes()
    failures = []
    def rejected(label):
        result = subprocess.run([str(relocated/'forge_game.exe'), '--verify-startup'], cwd=relocated,
                                env=env, text=True, capture_output=True, timeout=30)
        assert result.returncode != 0, label
        assert 'Fatal' in result.stderr or result.stderr, (label, result.stdout)
        failures.append({'case': label, 'diagnostic': result.stderr[-4096:]})
    manifest_path.write_text('{}')
    rejected('corrupt-manifest')
    manifest_path.write_bytes(original_manifest)
    invalid = json.loads(original_manifest)
    invalid['target']['backend'] = 'vulkan'
    manifest_path.write_text(json.dumps(invalid))
    rejected('wrong-backend')
    manifest_path.write_bytes(original_manifest)
    user_settings = user_root/'settings.json'
    saved_settings = user_settings.read_bytes()
    try:
        user_settings.write_bytes(b'{"payload":')
        rejected('corrupt-user-settings')
        assert user_settings.read_bytes() == b'{"payload":', 'Failed startup overwrote preferences'
        assert 'Fatal:' in (user_root/'runtime.log').read_text()
    finally:
        user_settings.write_bytes(saved_settings)
    if modules:
        invalid = json.loads(original_manifest)
        invalid['settings']['modules'][0]['fingerprint'] = '0'*64
        manifest_path.write_text(json.dumps(invalid))
        rejected('wrong-sdk-fingerprint')
        manifest_path.write_bytes(original_manifest)
        library_path = relocated/manifest['settings']['modules'][0]['library']
        saved_library = library_path.read_bytes()
        library_path.unlink()
        rejected('missing-module-library')
        library_path.write_bytes(saved_library)
    (evidence/'production-rejections.json').write_text(json.dumps(failures, indent=2))
    if args.retain:
        shutil.copytree(relocated, args.retain.resolve())
    (evidence/'relocation.json').write_text(json.dumps({
        'production_startup': True, 'source_removed': True,
        'kit_removed': True, 'path_system_only': True,
        'profile': manifest['engine']['profile'],
        'engine': manifest['engine'], 'module_count': len(modules),
        'installation_unchanged': True,
        'saves_settings_survive_second_move': True,
        'mixed_runtime_content': bool(args.source), 'remaining': 'Gameplay SDK session/save consumer acceptance tracked separately.'}, indent=2))
print('Relocated production standalone startup and captured host workflow passed')
