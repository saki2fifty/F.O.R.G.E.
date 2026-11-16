"""Source-checkout CPU workload reports using FORGE's existing runtime test client.

These measure collected host scopes, not IPC latency, render FPS or GPU work.
"""
import argparse
import copy
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from runtime_client import Runtime

FORMAT = 'forge.runtime_cpu_benchmark.v1'
SCOPES = ('FixedSimulationTick', 'PresentationPoseCapture')
PHYSICS_SCOPES = SCOPES + ('PhysicsSynchronization', 'PhysicsStep', 'PhysicsAdoption')
PLANAR_LAYOUTS = ('physics2d', 'contacts2d')
LAYOUTS = ('flat', 'hierarchy', 'physics', *PLANAR_LAYOUTS)
LAYOUT_LIMITS = {name: 1024 if name in PLANAR_LAYOUTS else 4096 for name in LAYOUTS}
PHYSICS_LAYOUTS = ('physics', *PLANAR_LAYOUTS)
PLANAR_ORACLE = dict(substeps=4, base_tolerance_m=.0002,
                     rounding_unit=2 ** -24, operation_margin=16)
CONTACT_ORACLE = dict(support_tolerance_m=.01, other_channels_tolerance_m=.0002,
                      sleeping=False, rotation_locked=True, bodies_per_pair=2)
NAMESPACE = uuid.UUID('16100000-1610-4161-8161-161000000000')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fixture_id(label):
    value = hashlib.sha256((str(NAMESPACE) + '/' + label).encode()).digest()[:16]
    return str(uuid.UUID(bytes=value, version=4))


def fixture(layout, count):
    if layout not in LAYOUTS or type(count) is not int or not 1 <= count <= LAYOUT_LIMITS[layout]:
        raise ValueError('Expected a supported workload and entity count within its profile limit')
    if layout == 'contacts2d' and count % 2:
        raise ValueError('Contact workloads require even total body counts in2..1024')
    ids = [fixture_id(f'{layout}/{count}/{i}') for i in range(count)]
    rows, expected = [], {}
    for i, identity in enumerate(ids):
        local = [i % 16, (i // 16) % 16, i // 256]
        row = dict(id=identity, name=f'CPU workload {i}', spatial=dict(mode='follow_structure'),
                   components={'forge.local_translation': dict(zip('xyz', local)),
                               'forge.local_rotation': dict(x=0, y=0, z=0, w=1),
                               'forge.local_scale': dict(x=1, y=1, z=1)})
        if layout in PHYSICS_LAYOUTS:
            # Equal1m boxes at3m centers: identical free fall preserves separation.
            planar = layout in PLANAR_LAYOUTS
            local = ([3 * (i % 64), 100 + 3 * (i // 64), 7] if planar else
                     [3 * (i % 64), 100, 3 * (i // 64)])
            row['spatial']['mode'] = 'world'
            row['components']['forge.local_translation'] = dict(zip('xyz', local))
            row['components']['forge.physics_body_2d' if planar else 'forge.physics_body'] = dict(
                motion=2, density=1000, mass=0, friction=.5, restitution=0,
                gravity_factor=1, linear_damping=0, angular_damping=0, allow_sleeping=False)
            row['components']['forge.box_collider_2d' if planar else 'forge.box_collider'] = (
                dict(x=1, y=1) if planar else dict(x=1, y=1, z=1))
            if layout == 'contacts2d':
                # Each unit Dynamic Box touches its own2m-wide Static support.
                # Separate pairs and lock rotation to bound the measured profile.
                pair, dynamic = divmod(i, 2)
                local = [4 * (pair % 32), 4 * (pair // 32) + (.5 if dynamic else -.5), 7]
                row['components']['forge.local_translation'] = dict(zip('xyz', local))
                row['components']['forge.physics_body_2d'].update(
                    motion=2 if dynamic else 0, lock_rotation=True)
                row['components']['forge.box_collider_2d']['x'] = 1 if dynamic else 2
        position = local
        if layout == 'hierarchy' and i:
            row['parent'] = ids[(i - 1) // 4]
            position = [a + b for a, b in zip(local, expected[row['parent']])]
        expected[identity] = position
        rows.append(row)
    scene = dict(version=5, asset_id=fixture_id(f'{layout}/{count}'), entities=rows)
    return scene, expected


def verify_world(state, source, expected):
    if state['scene'] != source:
        raise ValueError('Workload changed its authored Scene')
    rows = state['effective_scene']['entities']
    if len(rows) != len(expected) or {row['id'] for row in rows} != set(expected):
        raise ValueError('Workload lost or duplicated entities')
    for row in rows:
        x, y, z = expected[row['id']]
        affine = [1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z]
        if not row.get('spatial_resolved') or row.get('world_affine') != affine:
            raise ValueError(f'Incorrect derived transform: {row["id"]}')


def physics_reference(tick, planar):
    """Analytic free fall plus a conservative single-precision forward-error bound."""
    substeps = PLANAR_ORACLE['substeps'] if planar else 1
    n = tick * substeps
    drop = -9.81 * (1 / (60 * substeps)) ** 2 * n * (n + 1) / 2
    tolerance = .0002
    if planar:
        # Positive velocity increments and position sums have no cancellation.
        # Gamma(2n+16) bounds both accumulated roundoff and constant/delta rounding;
        # double world-position adoption contributes less than this float bound.
        relative = (2 * n + PLANAR_ORACLE['operation_margin']) * PLANAR_ORACLE['rounding_unit']
        tolerance = max(PLANAR_ORACLE['base_tolerance_m'], abs(drop) * relative / (1 - relative))
    return drop, tolerance


def verify_physics(state, source, expected, *, planar=False, contacts=False):
    """Check native integration independently, allowing only simulated translation."""
    tick = state['timing']['tick']
    physics = state['physics']
    if (type(tick) is not int or not 1 <= tick <= 272 or physics['tick'] != tick or
            physics['bodies'] != (0 if planar else len(expected)) or
            physics['active_dynamic'] != (0 if planar else len(expected)) or
            physics['sleeping_dynamic'] != 0 or physics['characters'] != 0 or
            physics['bodies_2d'] != (len(expected) if planar else 0) or
            physics['joints'] != 0 or physics['joints_2d'] != 0 or
            physics['gravity'] != [0, -9.81, 0]):
        raise ValueError('Physics workload ownership, gravity or active body count changed')
    if planar and (physics['native_2d_realized'] is not True or physics['shapes_2d'] != len(expected)):
        raise ValueError('Planar workload native body/shape realization changed')
    # Independent symplectic sum, with four native substeps per2D tick and one
    # per3D tick. Zero initial speed/drag and separated bodies exclude responses.
    if contacts and not planar:
        raise ValueError('Contact profile requires planar physics')
    drop, vertical_tolerance = ((0, CONTACT_ORACLE['support_tolerance_m']) if contacts else
                               physics_reference(tick, planar))
    observed = copy.deepcopy(state['scene'])
    original = {row['id']: row for row in source['entities']}
    def vertical_limit(identity):
        if contacts and original[identity]['components']['forge.physics_body_2d']['motion'] == 0:
            return CONTACT_ORACLE['other_channels_tolerance_m']
        return vertical_tolerance
    rows = observed['entities']
    effective = state['effective_scene']['entities']
    for group in (rows, effective):
        if len(group) != len(expected) or {row['id'] for row in group} != set(expected):
            raise ValueError('Physics workload lost or duplicated entities')
        for row in group:
            x, y, z = expected[row['id']]
            position = [x, y + drop, z]
            local = row['components']['forge.local_translation']
            for key, value in zip('xyz', position):
                tolerance = vertical_limit(row['id']) if key == 'y' else .0002
                if (type(local[key]) not in (int, float) or not math.isfinite(local[key]) or
                        abs(local[key] - value) > tolerance):
                    raise ValueError(f'Physics local position differs from independent reference: '
                                     f'{row["id"]} {key}={local[key]!r}, expected={value}, tolerance={tolerance}')
    for row in effective:
        x, y, z = expected[row['id']]
        affine = [1, 0, 0, x, 0, 1, 0, y + drop, 0, 0, 1, z]
        actual = row.get('world_affine', [])
        if (not row.get('spatial_resolved') or len(actual) != 12 or
                any(type(a) not in (int, float) or not math.isfinite(a) or
                    abs(a - z) > (vertical_limit(row['id']) if i == 7 else .0002)
                    for i, (a, z) in enumerate(zip(actual, affine)))):
            raise ValueError('Physics world matrix differs from independent reference')
    for row in rows:
        row['components']['forge.local_translation'] = original[row['id']]['components']['forge.local_translation']
    if observed != source:
        raise ValueError('Physics changed controls, identity or channels outside simulated translation')
    return vertical_tolerance


def workload_policy(work):
    if type(work['revision']) is not int:
        raise ValueError('Invalid workload revision')
    if work['revision'] == 1:
        layouts, scopes = ('flat', 'hierarchy'), SCOPES
        if 'layouts' in work:
            raise ValueError('Legacy workload must not override layouts')
    elif work['revision'] in (2, 3, 4):
        layouts = work['layouts']
        allowed = LAYOUTS[:3] if work['revision'] == 2 else LAYOUTS[:4] if work['revision'] == 3 else LAYOUTS
        if (not isinstance(layouts, list) or not 1 <= len(layouts) <= len(LAYOUTS) or
                any(not isinstance(name, str) or name not in allowed for name in layouts) or
                len(set(layouts)) != len(layouts)):
            raise ValueError('Invalid selected workloads')
        if work['revision'] == 3 and 'physics2d' not in layouts:
            raise ValueError('Planar workload revision requires physics2d')
        if 'physics2d' in layouts and work.get('physics2d_oracle') != PLANAR_ORACLE:
            raise ValueError('Unsupported planar numeric policy')
        if work['revision'] == 4:
            policy = work.get('contacts2d_oracle')
            if ('contacts2d' not in layouts or policy != CONTACT_ORACLE or
                    any(type(policy[k]) is not type(v) for k, v in CONTACT_ORACLE.items())):
                raise ValueError('Contact workload revision requires its supported numeric policy')
        scopes = PHYSICS_SCOPES if any(name in PHYSICS_LAYOUTS for name in layouts) else SCOPES
    else:
        raise ValueError('Unsupported workload revision')
    if work['scopes'] != list(scopes):
        raise ValueError('Unsupported workload scopes')
    if any(type(n) is not int or not 1 <= n <= min(LAYOUT_LIMITS[name] for name in layouts)
           or ('contacts2d' in layouts and n % 2) for n in work['counts']):
        raise ValueError('Invalid entity count for the selected workload profile')
    return layouts, scopes


def summarize(values):
    if not values or any(isinstance(v, bool) or not isinstance(v, (int, float)) or
                         not math.isfinite(v) or v < 0 for v in values):
        raise ValueError('Expected finite nonnegative samples')
    ordered = sorted(values)
    return dict(mean=statistics.fmean(values), median=statistics.median(values),
                p95=ordered[math.ceil(.95 * len(values)) - 1], maximum=ordered[-1])


def collect(runtime, layout, count, warmup, samples, scopes=SCOPES):
    scene, expected = fixture(layout, count)
    worker = Runtime(runtime)
    try:
        initial = worker.request('replace', scene=scene)
        if initial['timing']['simulation_hz'] != 60:
            raise ValueError('Expected the60Hz workload clock')
        generation = initial['world_generation']
        source = initial['scene']
        worker.request('pause')
        status = worker.request('profiling', generation=generation, enabled=True,
                                clear=True, native_systems=False)['cpu_profile']
        if not status['compiled'] or not status['enabled']:
            raise ValueError('This runtime has no enabled CPU collector')
        epoch = status['epoch']
        for _ in range(warmup):
            worker.request('step')
        worker.request('snapshot', profile_watch=True)
        measured = {scope: [] for scope in scopes}
        ticks = []
        while len(ticks) < samples:
            # Reading cpu_profile requests native lifetime-plan observation on
            # the next tick. Consume it outside the measured chunk.
            discarded = worker.request('step')['timing']['tick']
            chunk = list(range(discarded + 1, discarded + 1 + min(8, samples - len(ticks))))
            for tick in chunk:
                state = worker.request('step')
                if not state['timing']['paused'] or state['timing']['tick'] != tick:
                    raise ValueError('Workload did not advance exactly one paused fixed tick')
            state = worker.request('snapshot', profile_watch=True)
            profile = state['cpu_profile']
            if profile['world_generation'] != generation or profile['epoch'] != epoch or profile.get('native_systems'):
                raise ValueError('Workload collector ownership/policy changed')
            for scope in scopes:
                for tick in chunk:
                    rows = [row for row in profile['samples'] if row['category'] == 'runtime'
                            and row['name'] == scope and row['tick'] == tick]
                    if len(rows) != 1 or rows[0]['count'] != 1 or 'start_seconds' not in rows[0]:
                        raise ValueError('Required measured tick was evicted or is ambiguous')
                    measured[scope].append(rows[0]['seconds'] * 1000000)
            ticks.extend(chunk)
        if layout in PHYSICS_LAYOUTS:
            tolerance = verify_physics(state, source, expected, planar=layout in PLANAR_LAYOUTS,
                                       contacts=layout == 'contacts2d')
        else:
            verify_world(state, source, expected)
        # Final readback is complete. Unload through the existing replacement owner
        # before Quit: the bounded shutdown reply need not repeat a huge world.
        worker.request('replace', scene=dict(version=5, asset_id=fixture_id('cleanup'), entities=[]))
        worker.request('quit')
        worker.process.wait(timeout=5)
        if worker.process.returncode:
            raise ValueError('Runtime exited unsuccessfully')
        result = dict(ticks=ticks, microseconds=measured, verified_entities=len(expected))
        if layout in PHYSICS_LAYOUTS:
            result.update(verified_bodies=state['physics']['bodies_2d' if layout in PLANAR_LAYOUTS else 'bodies'],
                          final_tick=state['timing']['tick'])
        if layout == 'physics2d':
            result['vertical_tolerance_m'] = tolerance
        if layout == 'contacts2d':
            result.update(verified_support_pairs=count // 2, support_tolerance_m=tolerance)
        return result
    finally:
        worker.close()


def metadata(runtime, cache, physics=False, physics2d=False):
    configuration = {}
    native_configuration = {}
    native_source = None
    planar_source = None
    planar_configuration = {}
    c_compiler = None
    dependency_base = cache.parent / '_deps'
    native_options = {'DOUBLE_PRECISION', 'CROSS_PLATFORM_DETERMINISTIC',
                      'INTERPROCEDURAL_OPTIMIZATION', 'CPP_RTTI_ENABLED',
                      'FLOATING_POINT_EXCEPTIONS_ENABLED', 'USE_STATIC_MSVC_RUNTIME_LIBRARY'}
    keys = {'CMAKE_BUILD_TYPE', 'CMAKE_CXX_COMPILER', 'CMAKE_CXX_FLAGS',
            'CMAKE_CXX_FLAGS_RELEASE', 'FORGE_BUILD_EDITOR', 'FORGE_BUILD_GAME',
            'FORGE_BUILD_ASSET_TOOLS', 'FORGE_BUILD_NATIVE_SDK', 'FORGE_BUILD_MCP'}
    for line in cache.read_text(encoding='utf-8').splitlines():
        if '=' in line and ':' in line and not line.startswith(('//', '#')):
            key, value = line.split('=', 1)
            name = key.split(':', 1)[0]
            if name in native_options or name.startswith('USE_'):
                native_configuration[name] = value
            if name == 'FETCHCONTENT_SOURCE_DIR_JOLT' and value:
                native_source = Path(value)
            if name == 'FETCHCONTENT_BASE_DIR' and value:
                dependency_base = Path(value)
            if name == 'FETCHCONTENT_SOURCE_DIR_BOX2D' and value:
                planar_source = Path(value)
            if name == 'CMAKE_C_COMPILER' and value:
                c_compiler = Path(value)
            if (name.startswith('BOX2D_') or
                    (name.startswith('CMAKE_') and ('_FLAGS' in name or
                     name.startswith('CMAKE_INTERPROCEDURAL_OPTIMIZATION') or
                     name in ('CMAKE_BUILD_TYPE', 'CMAKE_MSVC_RUNTIME_LIBRARY')))):
                planar_configuration[name] = value
            if name in keys or (name.startswith('FORGE_') and key.endswith(':BOOL')):
                configuration[name] = value
    if not configuration.get('CMAKE_BUILD_TYPE') or not configuration.get('CMAKE_CXX_COMPILER'):
        raise ValueError('Expected a configured single-configuration build cache')
    cpu = platform.processor()
    if sys.platform.startswith('linux'):
        for line in Path('/proc/cpuinfo').read_text().splitlines():
            if line.startswith('model name'):
                cpu = line.split(':', 1)[1].strip()
                break
    hint = {}
    try:
        hint = dict(checkout_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    checkout_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)),
                    binary_origin_verified=False)
    except (OSError, subprocess.CalledProcessError):
        hint = dict(binary_origin_verified=False)
    result = dict(runtime_sha256=digest(runtime), tool_sha256=digest(Path(__file__)),
                client_sha256=digest(ROOT / 'tests/runtime_client.py'), source_hint=hint,
                version=subprocess.check_output([runtime, '--version'], text=True).strip(),
                sdk=json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True)),
                cache_sha256=digest(cache), configuration=configuration, compiler_sha256=digest(Path(configuration['CMAKE_CXX_COMPILER'])),
                environment=dict(os=platform.platform(), machine=platform.machine(), cpu=cpu,
                                 logical_processors=os.cpu_count(), python=platform.python_version(),
                                 affinity=sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None))
    if physics:
        # Reuse CMake's configured shared dependency cache unless Jolt is overridden.
        native_source = native_source or dependency_base / 'jolt-src'
        revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=native_source, text=True).strip()
        dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=native_source, text=True))
        if dirty or len(revision) != 40:
            raise ValueError('Physics reports require the unmodified configured Jolt checkout')
        result['native_physics'] = dict(source_revision=revision, configuration=native_configuration,
            binary_origin_verified=False,
            worker_observation='Joined PhysicsStep wall time; native pool count/utilization not instrumented')
        hint['physics_source_sha256'] = {str(name): digest(ROOT / name)
                                         for name in ('src/physics.cpp', 'cmake/dependencies.cmake')}
    if physics2d:
        planar_source = planar_source or dependency_base / 'box2d-src'
        revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=planar_source, text=True).strip()
        dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=planar_source, text=True))
        repair = json.loads((cache.parent / 'forge-box2d/source.json').read_text(encoding='utf-8'))
        upstream = (planar_source / 'src/snapshot.c').read_bytes().replace(b'\r\n', b'\n')
        if (dirty or revision != repair['upstream_commit'] or
                hashlib.sha256(upstream).hexdigest() != repair['upstream_lf_sha256'] or
                digest(ROOT / 'cmake/box2d_snapshot_guards.cmake') != repair['guard_sha256'] or
                digest(cache.parent / 'forge-box2d/snapshot.c') != repair['compiled_sha256']):
            raise ValueError('Planar reports require the configured clean source and exact staged snapshot repair')
        if c_compiler is None:
            raise ValueError('Planar reports require the configured C compiler')
        result['native_physics_2d'] = dict(source_revision=revision,
            configuration=planar_configuration, c_compiler_sha256=digest(c_compiler),
            snapshot=repair, substeps=4, binary_origin_verified=False,
            worker_observation='Shared PhysicsStep wall time; no per-worker utilization measurement')
        hint['physics_2d_source_sha256'] = {str(name): digest(ROOT / name) for name in
            ('src/physics.cpp', 'src/physics_2d.cpp', 'cmake/dependencies.cmake', 'cmake/box2d_snapshot_guards.cmake')}
    return result


def compatible_headers(current, baseline):
    for key in ('format', 'workload'):
        if current[key] != baseline[key]:
            raise ValueError(f'Incompatible baseline {key}')
    for key in ('configuration', 'compiler_sha256', 'environment', 'native_physics', 'native_physics_2d'):
        layout = {'native_physics': 'physics', 'native_physics_2d': 'physics2d'}.get(key)
        layouts = current['workload'].get('layouts', [])
        if layout and not (set(PLANAR_LAYOUTS).intersection(layouts) if layout == 'physics2d' else layout in layouts):
            continue
        if current['provenance'][key] != baseline['provenance'][key]:
            raise ValueError(f'Incompatible baseline {key}')


def validate_report(report):
    if report['format'] != FORMAT or report['units'] != 'microseconds':
        raise ValueError('Unsupported benchmark format/units')
    work = report['workload']
    counts = work['counts']
    if not isinstance(counts, list) or not 1 <= len(counts) <= 6 or len(set(counts)) != len(counts):
        raise ValueError('Invalid workload counts')
    for name, limit in [('warmup', 128), ('samples', 128), ('repeats', 5)]:
        if type(work[name]) is not int or not 1 <= work[name] <= limit:
            raise ValueError('Invalid workload sampling bounds')
    layouts, scopes = workload_policy(work)
    if (work['simulation_hz'] != 60 or
            work['native_systems'] is not False or work['chunk_ticks'] != 8 or
            work['discard_observation_tick'] is not True):
        raise ValueError('Unsupported workload policy')
    for layout, key in (('physics', 'native_physics'), ('physics2d', 'native_physics_2d')):
        if not (set(PLANAR_LAYOUTS).intersection(layouts) if layout == 'physics2d' else layout in layouts):
            continue
        native = report['provenance'].get(key)
        if not isinstance(native, dict):
            raise ValueError('Missing configured native physics provenance')
        revision = native.get('source_revision')
        if (not isinstance(revision, str) or len(revision) != 40 or
                any(c not in '0123456789abcdef' for c in revision) or
                not isinstance(native.get('configuration'), dict)):
            raise ValueError('Missing configured native physics provenance')
        if layout == 'physics2d':
            snapshot = native.get('snapshot')
            if (not isinstance(snapshot, dict) or snapshot.get('upstream_commit') != revision or
                    native.get('substeps') != 4 or type(native['substeps']) is not int or
                    native.get('binary_origin_verified') is not False):
                raise ValueError('Invalid planar stepping/source attribution')
            for value in [native.get('c_compiler_sha256'), *(snapshot.get(k) for k in
                          ('upstream_lf_sha256', 'guard_sha256', 'compiled_sha256'))]:
                if not isinstance(value, str) or len(value) != 64 or any(c not in '0123456789abcdef' for c in value):
                    raise ValueError('Missing planar compiler/snapshot digest')
    cases = report['cases']
    if set(cases) != {f'{layout}/{n}' for layout in layouts for n in counts}:
        raise ValueError('Incomplete workload cases')
    for layout in layouts:
        for count in counts:
            scene, _ = fixture(layout, count)
            case = cases[f'{layout}/{count}']
            expected_hash = hashlib.sha256(json.dumps(scene, sort_keys=True).encode()).hexdigest()
            if case['scene_sha256'] != expected_hash or len(case['runs']) != work['repeats']:
                raise ValueError('Workload fixture/repeat mismatch')
            for run in case['runs']:
                ticks = run['ticks']
                if (run['verified_entities'] != count or len(ticks) != work['samples'] or
                        any(type(t) is not int or t < 1 for t in ticks) or
                        any(b <= a for a, b in zip(ticks, ticks[1:]))):
                    raise ValueError('Invalid measured tick/correctness coverage')
                if layout in PHYSICS_LAYOUTS and (type(run['verified_bodies']) is not int or
                                            run['verified_bodies'] != count or
                                            type(run['final_tick']) is not int or
                                            not 1 <= run['final_tick'] <= 272 or run['final_tick'] != ticks[-1]):
                    raise ValueError('Missing physics body/tick correctness coverage')
                if layout == 'physics2d' and run.get('vertical_tolerance_m') != physics_reference(run['final_tick'], True)[1]:
                    raise ValueError('Planar numeric tolerance differs from its declared policy')
                if layout == 'contacts2d' and (type(run.get('verified_support_pairs')) is not int or
                        run['verified_support_pairs'] != count // 2 or
                        run.get('support_tolerance_m') != CONTACT_ORACLE['support_tolerance_m']):
                    raise ValueError('Missing independently verified supported pairs or numeric policy')
                if set(run['microseconds']) != set(scopes):
                    raise ValueError('Missing measured scopes')
                for values in run['microseconds'].values():
                    if len(values) != work['samples'] or any(v > 3600000000 for v in values):
                        raise ValueError('Invalid measured sample bounds')
                    summarize(values)
            if set(case['summary_us']) != set(scopes):
                raise ValueError('Missing scope summaries')
            for scope in scopes:
                expected = summarize([v for run in case['runs'] for v in run['microseconds'][scope]])
                if case['summary_us'][scope] != expected:
                    raise ValueError('Summary differs from raw samples')


def compare(current, baseline):
    compatible_headers(current, baseline)
    validate_report(current)
    validate_report(baseline)
    if current['cases'].keys() != baseline['cases'].keys():
        raise ValueError('Incompatible baseline cases')
    result = {}
    for name, case in current['cases'].items():
        before = baseline['cases'][name]
        if case['scene_sha256'] != before['scene_sha256']:
            raise ValueError('Incompatible baseline scene')
        changes = {}
        for scope in current['workload']['scopes']:
            a, z = before['summary_us'][scope]['median'], case['summary_us'][scope]['median']
            summarize([a, z])
            percentage = (z / a - 1) * 100 if a else None
            if percentage is not None and not math.isfinite(percentage):
                raise ValueError('Baseline percentage cannot be represented')
            changes[scope] = dict(baseline_median_us=a, current_median_us=z,
                                  percent_change=percentage)
        result[name] = changes
    return dict(baseline_runtime_sha256=baseline['provenance']['runtime_sha256'], changes=result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--cache', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--workloads', nargs='+', choices=LAYOUTS, default=['flat', 'hierarchy'])
    parser.add_argument('--counts', type=int, nargs='+',
                        help='Default:64,512 and the selected profile maximum (1024 for2D,4096 otherwise)')
    parser.add_argument('--warmup', type=int, default=32)
    parser.add_argument('--samples', type=int, default=32)
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    limit = min(LAYOUT_LIMITS[name] for name in args.workloads)
    if args.counts is None:
        args.counts = [64, 512, limit]
    if len(args.counts) > 6 or len(set(args.counts)) != len(args.counts) or any(not 1 <= n <= limit for n in args.counts):
        parser.error(f'Choose1..6 distinct entity counts in1..{limit} for these workloads')
    if 'contacts2d' in args.workloads and any(n % 2 for n in args.counts):
        parser.error('Contact workloads require even total body counts in2..1024')
    if not 1 <= args.warmup <= 128 or not 1 <= args.samples <= 128 or not 1 <= args.repeats <= 5:
        parser.error('Warmup/samples must be1..128; repeats1..5')
    if len(set(args.workloads)) != len(args.workloads):
        parser.error('Choose distinct workloads')
    if args.output.exists():
        parser.error('Output already exists; choose a new report path')
    runtime = args.runtime.resolve(strict=True)
    cache = (args.cache or runtime.parent / 'CMakeCache.txt').resolve(strict=True)
    provenance = metadata(runtime, cache, physics='physics' in args.workloads,
                          physics2d=bool(set(PLANAR_LAYOUTS).intersection(args.workloads)))
    workload = dict(revision=1, counts=args.counts, warmup=args.warmup, samples=args.samples,
                    repeats=args.repeats, simulation_hz=60, scopes=list(SCOPES),
                    native_systems=False, chunk_ticks=8, discard_observation_tick=True)
    if args.workloads != ['flat', 'hierarchy']:
        workload.update(revision=4 if 'contacts2d' in args.workloads else 3 if 'physics2d' in args.workloads else 2, layouts=args.workloads,
                        scopes=list(PHYSICS_SCOPES if any(name in PHYSICS_LAYOUTS for name in args.workloads) else SCOPES))
    if 'physics2d' in args.workloads:
        workload['physics2d_oracle'] = dict(PLANAR_ORACLE)
    if 'contacts2d' in args.workloads:
        workload['contacts2d_oracle'] = dict(CONTACT_ORACLE)
    layouts, scopes = workload_policy(workload)
    report = dict(format=FORMAT, recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  workload=workload, provenance=provenance, units='microseconds', cases={},
                  scope='Collected fixed-pipeline CPU wall time, profiling overhead included. '
                        'Setup/IPC/serialization/readback correctness outside measured scopes. '
                        'No gameFPS, GPU, physical-hardware or full-engine throughput claim.')
    if args.baseline and args.baseline.stat().st_size > 4 * 1024 * 1024:
        raise ValueError('Baseline exceeds4MiB')
    baseline = json.loads(args.baseline.read_text(encoding='utf-8')) if args.baseline else None
    if baseline is not None:
        validate_report(baseline)
        compatible_headers(report, baseline)
    for layout in layouts:
        for count in args.counts:
            name = f'{layout}/{count}'
            scene, _ = fixture(layout, count)
            runs = [collect(runtime, layout, count, args.warmup, args.samples, scopes) for _ in range(args.repeats)]
            report['cases'][name] = dict(scene_sha256=hashlib.sha256(json.dumps(scene, sort_keys=True).encode()).hexdigest(),
                                        runs=runs, summary_us={scope: summarize([v for run in runs for v in run['microseconds'][scope]]) for scope in scopes})
    validate_report(report)
    if baseline is not None:
        report['comparison'] = compare(report, baseline)
    if (digest(runtime) != provenance['runtime_sha256'] or digest(cache) != provenance['cache_sha256'] or
            digest(Path(__file__)) != provenance['tool_sha256'] or
            digest(ROOT / 'tests/runtime_client.py') != provenance['client_sha256']):
        raise ValueError('Runtime/configuration/consumer changed during measurement')
    encoded = json.dumps(report, indent=2, allow_nan=False) + '\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as stream:
        stream.write(encoded)
    print(f'Wrote {len(report["cases"])} verified CPU workload cases to {args.output}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
