"""Build and run the explicit portable regression subset in an existing CMake profile.

This diagnostic never establishes complete matrix, editor or package acceptance.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


CASES = (
    'application_state_gates', 'application_state_pipeline', 'tile_set_cooked',
    'tile_map_play_receiver', 'build_command', 'texture_image', 'ui_widget_keyboard',
    'ui_range_automation', 'ui_button_automation', 'ui_markup',
    'ui_localization_presenter', 'ui_localization_pipeline', 'ui_localization_runtime',
    'navigation_tile_sources', 'navigation', 'static_mesh_navigation', 'animation',
    'audio_impulse_export', 'audio_stream_source', 'mesh_wind_authoring',
    'asset_publication', 'runtime_content_package',
)


def select_tests(document):
    selected = {}
    for test in document['tests']:
        name = test['name']
        if name in CASES:
            if name in selected:
                raise ValueError('Duplicate regression case: ' + name)
            selected[name] = test
    missing = set(CASES) - selected.keys()
    if missing:
        raise ValueError('Configured profile is missing regression cases: ' + ', '.join(sorted(missing)))
    targets = set()
    for name, test in selected.items():
        command = test['command']
        if name == 'asset_publication':
            if len(command) != 4 or Path(command[1]).name != 'asset_publication_test.py':
                raise ValueError('Unexpected asset publication supervisor')
            executable = Path(command[2])
        else:
            executable = Path(command[0])
        target = executable.stem if executable.suffix == '.exe' else executable.name
        if not re.fullmatch(r'forge_[a-z0-9_]+', target):
            raise ValueError('Regression case has an unexpected native executable: ' + str(executable))
        targets.add(target)
    return selected, sorted(targets | {'forge_asset_build_worker', 'forge_nav_build', 'gltf2ozz'})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ctest', default='ctest')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--list-only', action='store_true')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    build = args.build.resolve()
    configured = json.loads(subprocess.check_output(
        [args.ctest, '--test-dir', str(build), '--show-only=json-v1'], text=True))
    selected, targets = select_tests(configured)
    evidence = dict(cases=list(CASES), targets=targets,
                    scope='Explicit core regression diagnostic; not a complete platform or delivery gate')
    print(json.dumps(evidence, indent=2), flush=True)
    if args.list_only:
        return
    output = build / 'grid-test-images/core-regressions'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'selection.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    subprocess.run([args.cmake, '--build', str(build), '--target', *targets,
                    '--parallel', str(args.jobs)], check=True)
    pattern = '^(' + '|'.join(re.escape(name) for name in selected) + ')$'
    subprocess.run([args.ctest, '--test-dir', str(build), '-R', pattern,
                    '--no-tests=error', '--output-on-failure'], check=True)


if __name__ == '__main__':
    main()
