"""Build and run the explicit portable regression subset in an existing CMake profile.

This diagnostic never establishes complete matrix, editor or package acceptance.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


CASES = {
    'application_state_gates': 'forge_application_state_gate_tests',
    'application_state_pipeline': 'forge_application_state_pipeline_tests',
    'tile_set_cooked': 'forge_tile_set_cooked_tests',
    'tile_map_play_receiver': 'forge_tile_map_play_receiver_tests',
    'build_command': 'forge_build_command_tests',
    'texture_image': 'forge_texture_image_tests',
    'ui_widget_keyboard': 'forge_ui_widget_keyboard_tests',
    'ui_range_automation': 'forge_ui_range_automation_tests',
    'ui_button_automation': 'forge_ui_button_automation_tests',
    'ui_markup': 'forge_ui_markup_tests',
    'ui_localization_presenter': 'forge_ui_localization_presenter_tests',
    'ui_localization_pipeline': 'forge_ui_localization_pipeline_tests',
    'ui_localization_runtime': 'forge_ui_localization_runtime_tests',
    'navigation_tile_sources': 'forge_navigation_tile_source_tests',
    'navigation_tile_obstacles': 'forge_navigation_tile_source_tests',
    'navigation_tile_colliders': 'forge_navigation_tile_source_tests',
    'navigation_tile_dynamic': 'forge_navigation_tile_source_tests',
    'navigation': 'forge_navigation_tests',
    'static_mesh_navigation': 'forge_static_mesh_navigation_tests',
    'animation': 'forge_animation_tests',
    'audio_impulse_export': 'forge_audio_impulse_export_tests',
    'audio_stream_source': 'forge_audio_stream_source_tests',
    'mesh_wind_authoring': 'forge_mesh_wind_authoring_tests',
    'asset_publication': 'forge_asset_publication_tests',
    'runtime_content_package': 'forge_runtime_package_tests',
}


def select_tests(document, *, require_commands=False):
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
    for name, test in selected.items():
        command = test.get('command')
        if not command:
            if require_commands:
                raise ValueError('Built regression executable is missing: ' + name)
            continue
        if name == 'asset_publication':
            if len(command) != 4 or Path(command[1]).name != 'asset_publication_test.py':
                raise ValueError('Unexpected asset publication supervisor')
            executable = Path(command[2])
        else:
            executable = Path(command[0])
        target = executable.stem if executable.suffix == '.exe' else executable.name
        if not re.fullmatch(r'forge_[a-z0-9_]+', target) or target != CASES[name]:
            raise ValueError('Regression case has an unexpected native executable: ' + str(executable))
    return selected, sorted(set(CASES.values()) | {'forge_asset_build_worker', 'forge_nav_build', 'gltf2ozz'})


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
    # CTest omits commands for native executables that have not been built yet.
    # Cross-check the explicit selection against its generated owner after building.
    configured = json.loads(subprocess.check_output(
        [args.ctest, '--test-dir', str(build), '--show-only=json-v1'], text=True))
    selected, _ = select_tests(configured, require_commands=True)
    pattern = '^(' + '|'.join(re.escape(name) for name in selected) + ')$'
    subprocess.run([args.ctest, '--test-dir', str(build), '-R', pattern,
                    '--no-tests=error', '--output-on-failure'], check=True)


if __name__ == '__main__':
    main()
