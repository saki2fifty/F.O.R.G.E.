"""Launch the editor SDK fixture against the final Windows package.

The editor ZIP and matching optional Developer Kit contain the static editor and
NativeSdk respectively; neither ZIP may contain the editor
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
import re
import shutil
import subprocess
import tempfile
import time
import zipfile
from pathlib import Path


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--developer-zip', type=Path, required=True,
                    help='Matching optional FORGE Developer Kit ZIP.')
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
developer_zip_path = args.developer_zip.resolve()
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
    for required in ('build_id', 'source_commit', 'files'):
        if required not in manifest:
            raise AssertionError('Final ZIP manifest missing required field: '
                                 + required)
    with zipfile.ZipFile(developer_zip_path) as archive:
        archive.extractall(extracted)
    developer = json.loads((extracted/'developer-manifest.json').read_text())
    import hashlib
    if (developer['build_id'] != manifest['build_id'] or
        developer['source_commit'] != manifest['source_commit'] or
        developer['editor_manifest_sha256'] != hashlib.sha256(manifest_path.read_bytes()).hexdigest()):
        raise AssertionError('Developer Kit does not match editor ZIP')
    for name, digest in developer['files'].items():
        if hashlib.sha256((extracted/name).read_bytes()).hexdigest() != digest:
            raise AssertionError('Developer Kit file mismatch: '+name)
    sdk_relpath = 'NativeSdk'
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
    editor_dir = extracted/'bin'
    fixture_dst = editor_dir / fixture_exe_name
    if fixture_dst.exists():
        fixture_dst.unlink()
    shutil.copy2(fixture_path, fixture_dst)

    # ---- resolve the actual project root ------------------------------
    # tests/physics_acceptance_fixture.py::make_project writes the
    # authored project under <output>/PhysicsAcceptance-<uuid>/ so
    # parallel test runs cannot collide on a fixed path. The
    # standalone-audit job therefore uploads the OUTER
    # <output>/editor-sdk-project/ tree (an envelope containing one
    # PhysicsAcceptance-<uuid>/ directory) rather than the inner
    # project root. The editor fixture's open_project expects a
    # direct project root with forge.project.json at its top level;
    # pointing it at the outer envelope forces the empty_scene()
    # fallback (Build80 acceptance observed: Hierarchy = 0 entities,
    # Game = No active Camera, Content = Not imported, page == "").
    # Resolve the actual root here so the fixture gets the same path
    # the standalone-audit authored.
    def resolve_project_root(envelope):
        if (envelope / 'forge.project.json').is_file():
            return envelope
        nested = [child for child in envelope.iterdir()
                  if child.is_dir() and (child / 'forge.project.json').is_file()]
        if len(nested) == 1:
            return nested[0]
        if not nested:
            raise AssertionError(
                'Editor SDK project envelope has no forge.project.json at '
                + str(envelope) + ' or in any immediate subdirectory. The '
                'standalone-audit job is expected to upload a single '
                'PhysicsAcceptance-<uuid>/ envelope, but the artifact at '
                + str(envelope) + ' contains no forge.project.json — refuse '
                'to launch the fixture so the next build can diagnose the '
                'generator / upload step instead of silently regressing to '
                'empty_scene().')
        raise AssertionError(
            'Editor SDK project envelope has multiple immediate subdirectories '
            'with forge.project.json: ' + ', '.join(str(p) for p in nested)
            + '. The acceptance gate expects exactly one PhysicsAcceptance-* '
            'envelope per artifact; ambiguous inputs must fail loud, not be '
            'picked arbitrarily.')

    actual_project_root = resolve_project_root(project_path)

    # ---- copy project (hidden files preserved) to private scratch ------
    project_dir = scratch / 'project'
    project_dir.mkdir(parents=True)
    for entry in actual_project_root.iterdir():
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

    reference_failure = None
    try:
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
            # Surface test-only crash capture if the fixture installed one.
            # The fixture registers a SetUnhandledExceptionFilter that writes
            # a best-effort exception record + module + offset + last
            # breadcrumb to <output>/fixture-crash.txt via plain Win32 file
            # I/O before the CRT terminates the process. Reading it back
            # here turns an opaque 0xC0000005 into a useful crash record
            # without changing the underlying acceptance gate. Breadcrumbs
            # are interleaved with normal stdout/stderr in the existing
            # fixture_log.
            crash_path = output_dir / 'fixture-crash.txt'
            extras = []
            if crash_path.is_file():
                extras.append('Fixture crash record at ' + str(crash_path))
            extras.append('Fixture stdout/stderr log at ' + str(fixture_log))
            message = 'Fixture executable returned non-zero exit code: ' + str(returncode)
            if extras:
                message += '\n' + '\n'.join(extras)
            raise AssertionError(message)

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

    except Exception as failure:
        reference_failure = failure
        (evidence/'reference-failure.txt').write_text(str(failure), encoding='utf-8')

    # Ordinary project authoring through shipped Content, Build and export controls.
    # Keep the compiler environment for this separate source-building scenario;
    # reference/relocated checks above removed toolchain PATH entries. Run compiler
    # workloads after timed gameplay acceptance, keeping scenarios independent.
    onboarding = evidence / 'onboarding with spaces'
    onboarding.mkdir(parents=True, exist_ok=True)
    onboarding_env = os.environ.copy()
    # Prove discovery without requiring the special developer launcher. Keep
    # installed CMake/Ninja available; compiler INCLUDE/LIB are initialized by FORGE.
    for key in list(onboarding_env):
        name = key.upper()
        if name in ('VCTOOLSINSTALLDIR', 'INCLUDE', 'LIB', 'LIBPATH') or name.startswith(('VSCMD_', '__VSCMD_')):
            onboarding_env.pop(key, None)

    with (onboarding/'fixture.log').open('w', encoding='utf-8') as stream:
        starter = subprocess.Popen([str(fixture_dst), str(onboarding), '--sdk-onboarding', str(sdk_root)],
                                   cwd=extracted, env=onboarding_env, stdout=stream, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 600
            while starter.poll() is None:
                # EditorFixture removes its private project during teardown.
                # Retain the flushed compiler log while that project still exists.
                for compiler_log in onboarding.glob('project-*/.forge/sdk-build/build.log'):
                    try:
                        shutil.copy2(compiler_log, onboarding/'compiler-build.log')
                    except FileNotFoundError:
                        pass  # Teardown can remove the file between glob and copy.
                if time.monotonic() >= deadline:
                    raise subprocess.TimeoutExpired(starter.args, 600)
                time.sleep(.25)
            starter_code = starter.returncode
        except subprocess.TimeoutExpired:
            terminate_tree(starter)
            raise AssertionError('SDK onboarding exceeded 600s watchdog')
    starter_trace_path=onboarding/'workflow.json'
    if starter_trace_path.is_file():
        observed = json.loads(starter_trace_path.read_text())
        project = observed.get('state', {}).get('project')
        if project:
            compiler_log = Path(project)/'.forge/sdk-build/build.log'
            if compiler_log.is_file():
                shutil.copy2(compiler_log, onboarding/'compiler-build.log')
    if starter_code or not starter_trace_path.is_file():
        raise AssertionError('SDK onboarding failed; see '+str(onboarding/'fixture.log'))
    starter_trace=json.loads(starter_trace_path.read_text())
    if not starter_trace.get('ok'):
        raise AssertionError('SDK onboarding workflow failed: '+str(starter_trace.get('error',starter_trace)))
    if not starter_trace['state'].get('gameplay_current'):
        raise AssertionError('Export completed with stale C++ gameplay source')
    required_onboarding_captures = (
        'cpp-component-create-dialog', 'cpp-system-create-dialog',
        'cpp-source-create-dialog', 'cpp-code-browser', 'cpp-code-selected',
        'cpp-component-created', 'cpp-system-created', 'cpp-component-dirty',
        'gameplay-build-required', 'gameplay-built', 'gameplay-build-rejected',
        'cpp-compiler-diagnostic', 'cpp-rotator-playing', 'cpp-rotator-live-tuned',
        'cpp-system-dirty', 'cpp-system-current', 'cpp-system-reversed',
        'cpp-rotator-inspector', 'gameplay-stale-play-offer',
        'gameplay-save-build-play-offer',
        'starter-export-complete')
    for name in required_onboarding_captures:
        matching = [entry['step'] for entry in starter_trace['trace']
                    if entry.get('operation') == 'capture' and entry.get('value') == name]
        if not matching or any(not (onboarding / f'editor-{step}-{name}.ppm').is_file()
                               for step in matching):
            raise AssertionError('Missing C++ onboarding UI capture: ' + name)
    starter_export=Path(starter_trace['state']['export_output'])
    # The fixture has already removed its source project. PATH now exposes only
    # system DLLs, not the compiler or Developer Kit. Execute after two moves.
    runtime_env=os.environ.copy()
    runtime_env['PATH']=str(Path(os.environ.get('SystemRoot','C:/Windows'))/'System32')
    for number in (1, 2):
        relocated_starter=scratch/('starter exported game relocated '+str(number))
        shutil.move(str(starter_export), relocated_starter)
        result=subprocess.run([str(relocated_starter/'forge_game.exe'),'--verify-startup'],
                              cwd=relocated_starter,env=runtime_env,capture_output=True,
                              text=True,timeout=60)
        output=result.stdout+'\n'+result.stderr
        (onboarding/('relocated-startup-'+str(number)+'.log')).write_text(output)
        if result.returncode:
            raise AssertionError('Relocated starter startup failed: '+result.stderr)
        rotations=[float(value) for value in
                   re.findall(r'FORGE_TEST_ROTATOR_HALF=([-+]?\d+(?:\.\d+)?)', output)]
        if not any(value < -0.0001 for value in rotations):
            raise AssertionError('Relocated game did not execute the edited RotationSystem')
        starter_export=relocated_starter

    if reference_failure is not None:
        raise reference_failure


print('Editor SDK acceptance against final package passed: '
      'complete=true, source_commit/build_id match package, '
      'all required stage PPMs and sidecars present.')