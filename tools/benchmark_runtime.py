"""Source-checkout CPU workload reports using FORGE's existing runtime test client.

These measure collected host scopes, not IPC latency, render FPS or GPU work.
"""
import argparse
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
NAMESPACE = uuid.UUID('16100000-1610-4161-8161-161000000000')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fixture_id(label):
    value = hashlib.sha256((str(NAMESPACE) + '/' + label).encode()).digest()[:16]
    return str(uuid.UUID(bytes=value, version=4))


def fixture(layout, count):
    if layout not in ('flat', 'hierarchy') or type(count) is not int or not 1 <= count <= 4096:
        raise ValueError('Expected flat/hierarchy and 1..4096 entities')
    ids = [fixture_id(f'{layout}/{count}/{i}') for i in range(count)]
    rows, expected = [], {}
    for i, identity in enumerate(ids):
        local = [i % 16, (i // 16) % 16, i // 256]
        row = dict(id=identity, name=f'CPU workload {i}', spatial=dict(mode='follow_structure'),
                   components={'forge.local_translation': dict(zip('xyz', local)),
                               'forge.local_rotation': dict(x=0, y=0, z=0, w=1),
                               'forge.local_scale': dict(x=1, y=1, z=1)})
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


def summarize(values):
    if not values or any(isinstance(v, bool) or not isinstance(v, (int, float)) or
                         not math.isfinite(v) or v < 0 for v in values):
        raise ValueError('Expected finite nonnegative samples')
    ordered = sorted(values)
    return dict(mean=statistics.fmean(values), median=statistics.median(values),
                p95=ordered[math.ceil(.95 * len(values)) - 1], maximum=ordered[-1])


def collect(runtime, layout, count, warmup, samples):
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
        measured = {scope: [] for scope in SCOPES}
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
            for scope in SCOPES:
                for tick in chunk:
                    rows = [row for row in profile['samples'] if row['category'] == 'runtime'
                            and row['name'] == scope and row['tick'] == tick]
                    if len(rows) != 1 or rows[0]['count'] != 1 or 'start_seconds' not in rows[0]:
                        raise ValueError('Required measured tick was evicted or is ambiguous')
                    measured[scope].append(rows[0]['seconds'] * 1000000)
            ticks.extend(chunk)
        verify_world(state, source, expected)
        worker.request('quit')
        worker.process.wait(timeout=5)
        if worker.process.returncode:
            raise ValueError('Runtime exited unsuccessfully')
        return dict(ticks=ticks, microseconds=measured, verified_entities=len(expected))
    finally:
        worker.close()


def metadata(runtime, cache):
    configuration = {}
    keys = {'CMAKE_BUILD_TYPE', 'CMAKE_CXX_COMPILER', 'CMAKE_CXX_FLAGS',
            'CMAKE_CXX_FLAGS_RELEASE', 'FORGE_BUILD_EDITOR', 'FORGE_BUILD_GAME',
            'FORGE_BUILD_ASSET_TOOLS', 'FORGE_BUILD_NATIVE_SDK', 'FORGE_BUILD_MCP'}
    for line in cache.read_text(encoding='utf-8').splitlines():
        if '=' in line and ':' in line and not line.startswith(('//', '#')):
            key, value = line.split('=', 1)
            name = key.split(':', 1)[0]
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
    return dict(runtime_sha256=digest(runtime), tool_sha256=digest(Path(__file__)),
                client_sha256=digest(ROOT / 'tests/runtime_client.py'), source_hint=hint,
                version=subprocess.check_output([runtime, '--version'], text=True).strip(),
                sdk=json.loads(subprocess.check_output([runtime, '--sdk-info'], text=True)),
                cache_sha256=digest(cache), configuration=configuration, compiler_sha256=digest(Path(configuration['CMAKE_CXX_COMPILER'])),
                environment=dict(os=platform.platform(), machine=platform.machine(), cpu=cpu,
                                 logical_processors=os.cpu_count(), python=platform.python_version(),
                                 affinity=sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None))


def compatible_headers(current, baseline):
    for key in ('format', 'workload'):
        if current[key] != baseline[key]:
            raise ValueError(f'Incompatible baseline {key}')
    for key in ('configuration', 'compiler_sha256', 'environment'):
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
    if (work['revision'] != 1 or work['simulation_hz'] != 60 or work['scopes'] != list(SCOPES) or
            work['native_systems'] is not False or work['chunk_ticks'] != 8 or
            work['discard_observation_tick'] is not True):
        raise ValueError('Unsupported workload policy')
    cases = report['cases']
    if set(cases) != {f'{layout}/{n}' for layout in ('flat', 'hierarchy') for n in counts}:
        raise ValueError('Incomplete workload cases')
    for layout in ('flat', 'hierarchy'):
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
                if set(run['microseconds']) != set(SCOPES):
                    raise ValueError('Missing measured scopes')
                for values in run['microseconds'].values():
                    if len(values) != work['samples'] or any(v > 3600000000 for v in values):
                        raise ValueError('Invalid measured sample bounds')
                    summarize(values)
            if set(case['summary_us']) != set(SCOPES):
                raise ValueError('Missing scope summaries')
            for scope in SCOPES:
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
        for scope in SCOPES:
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
    parser.add_argument('--counts', type=int, nargs='+', default=[64, 512, 4096])
    parser.add_argument('--warmup', type=int, default=32)
    parser.add_argument('--samples', type=int, default=32)
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    if len(args.counts) > 6 or len(set(args.counts)) != len(args.counts) or any(not 1 <= n <= 4096 for n in args.counts):
        parser.error('Choose1..6 distinct entity counts in1..4096')
    if not 1 <= args.warmup <= 128 or not 1 <= args.samples <= 128 or not 1 <= args.repeats <= 5:
        parser.error('Warmup/samples must be1..128; repeats1..5')
    if args.output.exists():
        parser.error('Output already exists; choose a new report path')
    runtime = args.runtime.resolve(strict=True)
    cache = (args.cache or runtime.parent / 'CMakeCache.txt').resolve(strict=True)
    provenance = metadata(runtime, cache)
    workload = dict(revision=1, counts=args.counts, warmup=args.warmup, samples=args.samples,
                    repeats=args.repeats, simulation_hz=60, scopes=list(SCOPES),
                    native_systems=False, chunk_ticks=8, discard_observation_tick=True)
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
    for layout in ('flat', 'hierarchy'):
        for count in args.counts:
            name = f'{layout}/{count}'
            scene, _ = fixture(layout, count)
            runs = [collect(runtime, layout, count, args.warmup, args.samples) for _ in range(args.repeats)]
            report['cases'][name] = dict(scene_sha256=hashlib.sha256(json.dumps(scene, sort_keys=True).encode()).hexdigest(),
                                        runs=runs, summary_us={scope: summarize([v for run in runs for v in run['microseconds'][scope]]) for scope in SCOPES})
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
