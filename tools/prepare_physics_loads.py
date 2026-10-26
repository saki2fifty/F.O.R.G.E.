"""Prepare the editable C++ example using the existing exact SDK and source identity.

Compiled module kits come from forge_install_gameplay_runtime on Windows. Linux
validation can supply the compiled module directly; this does not create a Linux
graphical runtime kit. No C++ compiler or third-party source is redistributed.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def run_json(*arguments):
    result = subprocess.run(list(map(str, arguments)), text=True, capture_output=True, timeout=180)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return json.loads(result.stdout)


def prepare(source, output, runtime, tools, module=None, module_kit=None,
            component='project.body_loads', dependencies=None, import_sources=()):
    source, output, runtime, tools = (Path(p).resolve() for p in (source, output, runtime, tools))
    if output.exists():
        raise ValueError('Example destination must be new')
    shutil.copytree(source, output)
    deployment = output / 'Native/Builds/example/project.gameplay'
    deployment.mkdir(parents=True)
    info = run_json(runtime, '--sdk-info')
    if module_kit:
        module_kit = Path(module_kit).resolve()
        kit = json.loads((module_kit / 'forge.module-kit.json').read_text())
        if (kit['format'] != 'forge.module-kit' or kit['version'] != 1 or
                kit['fingerprint'] != info['fingerprint']):
            raise ValueError('Example module kit does not match the runtime')
        for file in list(kit['files']) + ['forge.module-kit.json']:
            target = deployment / file
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(module_kit / file, target)
        library = kit['library']
    else:
        module = Path(module).resolve()
        library = module.name
        shutil.copyfile(module, deployment / library)
    settings_path = output / 'forge.project.json'
    settings = json.loads(settings_path.read_text())
    identity = run_json(tools, '--assets', 'code-identity', output, info['fingerprint'])
    settings['modules'] = [dict(id='project.gameplay', sdk='experimental-1', implementation='1',
        fingerprint=info['fingerprint'], source_identity=identity['source_identity'],
        library=(deployment / library).relative_to(output).as_posix(),
        dependencies=dependencies if dependencies is not None else ['forge.physics'])]
    settings_path.write_text(json.dumps(settings, indent=2) + '\n')
    inspected = run_json(runtime, '--inspect-sdk', output)
    declarations = {entry['id']: entry for entry in inspected['components']}
    if component not in declarations:
        raise ValueError('Unexpected example schema')
    registry = json.loads((output / 'forge.assets.json').read_text())
    scene_sources = {asset['source'] for asset in registry['assets'] if asset['type'] == 'scene'}
    # Editable templates may not have registered their scenes yet. Use the source
    # owner's bounded discovery, including secondary scenes, rather than relying
    # on an already populated catalog or manufacturing catalog records here.
    scan = run_json(tools, '--assets', 'scan', output, 'Scenes')
    if not scan['complete']:
        raise ValueError('Example scene discovery did not complete')
    scene_sources.update(file['source'] for file in scan['files']
                         if file['source_kind'] == 'scene' and not file['alias_of'])
    for source_path in sorted(scene_sources):
        scene_path = (output / source_path).resolve()
        if output.resolve() not in scene_path.parents:
            raise ValueError('Example scene source escapes its project')
        scene = json.loads(scene_path.read_text())
        for entity in scene['entities']:
            for name, data in entity['components'].items():
                if name not in declarations:
                    continue
                declaration = declarations[name]
                stamp = dict(format='forge.authored-component', version=1, module=declaration['module'],
                             schema_version=declaration['schema_version'], digest=declaration['digest'])
                if '$forge' in data and data['$forge'] != stamp:
                    raise ValueError('Example source schema needs explicit migration')
                data['$forge'] = stamp
        scene_path.write_text(json.dumps(scene, indent=2) + '\n')
    # Explicit dependency order supplied by the example's packaging recipe. The
    # ordinary asset owner validates, cooks and publishes each source; this script
    # does not manufacture cache records or rewrite dependency identities.
    for source in import_sources:
        run_json(tools, '--assets', 'import', output, source)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('source', 'output', 'runtime', 'tools'):
        parser.add_argument('--' + name, required=True, type=Path)
    modules = parser.add_mutually_exclusive_group(required=True)
    modules.add_argument('--module', type=Path)
    modules.add_argument('--module-kit', type=Path)
    parser.add_argument('--component', default='project.body_loads')
    parser.add_argument('--dependency', dest='dependencies', action='append')
    parser.add_argument('--import-source', dest='import_sources', action='append', default=[])
    arguments = parser.parse_args()
    print(prepare(**vars(arguments)))
