"""Launch the editor SDK fixture against the final Windows package.

The shipped FORGE-Windows-x64 ZIP contains the static editor and
the matching NativeSdk; the shipped ZIP must NOT contain the editor
fixture executable. The fixture executable is shipped separately via
the FORGE-Editor-SDK-Fixture artifact (the exe only — DLLs and
resources are reused from the shipped package so a missing packaged
dependency fails verification rather than being masked by a parallel
DLL bundle).

The acceptance helper extracts the final ZIP into a private scratch,
copies only the fixture EXE beside the extracted forge_editor.exe
(so it picks up the matching shipped DLLs/resources), copies the
ordinary editable SDK reference project (with hidden files like
.forge/) to a separate scratch, and launches the fixture with an
isolated userdata base. The fixture writes an evidence directory
whose workflow.json and stage captures are asserted against the
schema produced by tests/editor_sdk_workflow.hpp and the final
workflow.json written by main.cpp.

Failure gates the FORGE-Windows-x64 upload. Extracted package,
copied project, and private userdata live outside the evidence
directory and are cleaned up on success/failure via
tempfile.TemporaryDirectory.

Python stdlib only.
"""

import argparse
import json
import os
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zip', type=Path, required=True,
                    help='Path to the final FORGE-Windows-x64 ZIP package.')
parser.add_argument('--fixture', type=Path, required=True,
                    help='Path to the editor fixture executable '
                         '(forge_editor_fixture.exe).')
parser.add_argument('--project', type=Path, required=True,
                    help='Path to the ordinary editable SDK reference '
                         'project root (includes hidden files).')
parser.add_argument('--evidence', type=Path, required=True,
                    help='Directory that retains the fixture stdout/'
                         'stderr logs, captures, and final workflow '
                         'JSON. Extracted package, project copy, and '
                         'private userdata live outside this directory.')
args = parser.parse_args()
zip_path = args.zip.resolve()
fixture_path = args.fixture.resolve()
project_path = args.project.resolve()
evidence = args.evidence.resolve()
evidence.mkdir(parents=True, exist_ok=True)


def log_path(name):
    return evidence / name


def terminate_tree(process):
    if process.poll() is not None:
        return
    if os.name == 'nt':
        subprocess.run(['taskkill', '/F', '/T', '/PID', str(process.pid)],
                       capture_output=True, text=True)
    else:
        process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


# All extracted package, copied project, and private userdata live in
# a single private scratch that is removed on exit. Only logs,
# captures, and JSON evidence are retained under --evidence.
with tempfile.TemporaryDirectory(prefix='FORGE editor-sdk ') as temporary:
    scratch = Path(temporary)

    # ---- extract final ZIP into private scratch -------------------------
    extracted = scratch / 'package'
    extracted.mkdir(parents=True)
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(extracted)
    manifest_path = extracted / 'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    for required in ('build_id', 'source_commit', 'files', 'native_sdk'):
        if required not in manifest:
            raise AssertionError('Final ZIP manifest missing required field: '
                                 + required)
    native_meta = manifest.get('native_sdk') or {}
    sdk_relpath = native_meta.get('path')
    if not sdk_relpath:
        raise AssertionError('Final ZIP manifest has no native_sdk.path entry')
    sdk_root = (extracted / sdk_relpath).resolve()
    if not (sdk_root / 'bin' / 'forge_runtime.exe').is_file():
        raise AssertionError('Extracted SDK does not contain '
                             'bin/forge_runtime.exe: ' + str(sdk_root))

    # ---- place fixture EXE beside the shipped editor -------------------
    # The shipped editor DLLs/resources in the extracted package are the
    # matching build; copying the fixture next to forge_editor.exe makes
    # the fixture pick them up via the standard Windows DLL search
    # order. The fixture's own stage in the FORGE-Editor-SDK-Fixture
    # artifact is intentionally not duplicated here.
    fixture_exe_name = fixture_path.name
    editor_dir = extracted
    fixture_dst = editor_dir / fixture_exe_name
    if fixture_dst.exists():
        fixture_dst.unlink()
    shutil.copy2(fixture_path, fixture_dst)

    # ---- copy project (hidden files preserved) to private scratch ------
    project_dir = scratch / 'project'
    project_dir.mkdir(parents=True)
    for entry in project_path.iterdir():
        target = project_dir / entry.name
        if entry.is_dir():
            shutil.copytree(entry, target)
        else:
            shutil.copy2(entry, target)

    # ---- isolated userdata base + fixture output -----------------------
    # The fixture output directory contains all native screenshots
    # (editor-<label>.ppm), per-stage trace JSONs, and the final
    # workflow.json. It must live outside the temp scratch so the
    # screenshots and failure traces survive TemporaryDirectory
    # cleanup on every outcome. Package/project/userdata stay in
    # scratch and are removed on exit.
    userdata = scratch / 'userdata'
    userdata.mkdir(parents=True)
    output_dir = evidence / 'fixture-output'
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)

    # ---- launch fixture with bounded watchdog --------------------------
    env = os.environ.copy()
    env['PATH'] = str(Path(os.environ.get('SystemRoot', 'C:/Windows'))
                      / 'System32')
    command = [str(fixture_dst),
               str(output_dir),
               '--sdk-play',
               str(project_dir),
               str(sdk_root),
               str(userdata)]
    fixture_log = log_path('fixture.log')
    with fixture_log.open('w', encoding='utf-8', newline='') as stream:
        process = subprocess.Popen(command,
                                   cwd=str(extracted),
                                   env=env,
                                   stdout=stream,
                                   stderr=subprocess.STDOUT)
        # Fixture has its own 240s bound for a clean run; give the
        # watchdog margin beyond that so a final screenshot/trace
        # flush can complete before we tear the process tree down.
        try:
            returncode = process.wait(timeout=300)
        except subprocess.TimeoutExpired:
            terminate_tree(process)
            raise AssertionError('Fixture executable exceeded 300s watchdog')

    if returncode != 0:
        raise AssertionError('Fixture executable returned non-zero exit code: '
                             + str(returncode))

    # ---- assert evidence schema, identity, captures --------------------
    workflow_path = output_dir / 'workflow.json'
    if not workflow_path.is_file():
        raise AssertionError('Final workflow.json missing under: '
                             + str(output_dir))
    trace = json.loads(workflow_path.read_text(encoding='utf-8'))

    def assert_field(trace, key, expected, summary):
        actual = trace.get(key)
        if actual != expected:
            summary[key] = {'actual': actual, 'expected': expected}
            raise AssertionError('Trace field mismatch for ' + key + ': ' +
                                 json.dumps(summary, default=str))

    summary = {}
    assert_field(trace, 'complete', True, summary)
    assert_field(trace, 'source_commit', manifest.get('source_commit'), summary)
    assert_field(trace, 'build_id', manifest.get('build_id'), summary)
    assert_field(trace, 'scene_round_trip', True, summary)
    assert_field(trace, 'save_load', True, summary)
    assert_field(trace, 'binding_persisted_same_process', True, summary)
    assert_field(trace, 'binding_persisted_restart', True, summary)
    assert_field(trace, 'sdk_play', True, summary)
    # Focus-gated menu routing regression: the helper refused to
    # set complete=true unless the surrender → click Capture
    # gameplay input → restore sequence reached the gameplay
    # substate. Asserted as REQUIRED because the adapter's
    # acquire_routing predicate now depends on the Game panel
    # holding ImGui focus; without this gate the SDK runtime can
    # silently reclaim logical routing after the user surrenders
    # it by clicking outside the Game panel. Acceptance must not
    # pass on shipped editor builds that bypass this coverage.
    assert_field(trace, 'focus_gated_routing', True, summary)

    # The fixture writes complete=true captures as PPM images named
    # editor-<label>.ppm and sidecar JSONs named sdk-<label>.json.
    expected_captures = [
        'sdk-play-active',
        'sdk-main-menu',
        'sdk-gameplay',
        'sdk-outside-surrender',
        'sdk-outside-regain',
        'sdk-interaction',
        'sdk-moved',
        'sdk-pause',
        'sdk-candidate',
        'sdk-applied',
        'sdk-rebound-jump',
        'sdk-saved',
        'sdk-main-returned',
        'sdk-restored',
        'sdk-persisted-binding',
        'sdk-editor-recovered',
        'sdk-restart-loaded',
        'sdk-restarted-persisted-binding',
        'sdk-editor-usable',
    ]
    missing_ppm = [name for name in expected_captures
                   if not (output_dir / ('editor-' + name + '.ppm')).is_file()]
    if missing_ppm:
        summary['missing_ppm'] = missing_ppm
        raise AssertionError('Required stage PPM captures missing: '
                             + ', '.join(missing_ppm))
    empty_ppm = [name for name in expected_captures
                 if (output_dir / ('editor-' + name + '.ppm')).stat().st_size == 0]
    if empty_ppm:
        summary['empty_ppm'] = empty_ppm
        raise AssertionError('Required stage PPM captures empty: '
                             + ', '.join(empty_ppm))
    missing_sidecar = [name for name in expected_captures
                       if not (output_dir / ('sdk-' + name + '.json')).is_file()]
    if missing_sidecar:
        summary['missing_sidecar'] = missing_sidecar
        raise AssertionError('Required stage sidecar JSONs missing: '
                             + ', '.join(missing_sidecar))

    # Persist the acceptance summary into the evidence directory so
    # the upload step can publish it. output_dir is already under
    # evidence and contains the workflow.json, sidecars, and PPMs.
    captures = sorted(p.name for p in output_dir.iterdir()
                      if p.is_file()
                      and (p.suffix in ('.ppm', '.json')
                           or p.name == 'sdk-workflow-failure.json'))
    (evidence / 'editor-sdk-acceptance.json').write_text(json.dumps({
        'fixture_exit_code': returncode,
        'fixture_log': str(fixture_log),
        'fixture_output': str(output_dir),
        'workflow': str(workflow_path),
        'complete': trace.get('complete'),
        'source_commit': trace.get('source_commit'),
        'build_id': trace.get('build_id'),
        'package_source_commit': manifest.get('source_commit'),
        'package_build_id': manifest.get('build_id'),
        'scene_round_trip': trace.get('scene_round_trip'),
        'save_load': trace.get('save_load'),
        'binding_persisted_same_process': trace.get('binding_persisted_same_process'),
        'binding_persisted_restart': trace.get('binding_persisted_restart'),
        'sdk_play': trace.get('sdk_play'),
        'focus_gated_routing': trace.get('focus_gated_routing'),
        'captures': captures,
        'hardware': 'WARP rasterizer on Windows CI; physical GPU acceptance '
                    'requires manual local execution and is not asserted here.',
    }, indent=2), encoding='utf-8')

print('Editor SDK acceptance against final package passed: '
      'complete=true, source_commit/build_id match package, '
      'all required stage PPMs and sidecars present.')