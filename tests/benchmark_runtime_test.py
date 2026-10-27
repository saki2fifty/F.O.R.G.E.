"""Independent workload/statistics/admission checks and real runtime report flow."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import benchmark_runtime as bench

RUNTIME = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else None


def report(values=(1, 2, 3), layouts=None):
    work = dict(revision=1, counts=[4], warmup=2, samples=3, repeats=1, simulation_hz=60,
                scopes=list(bench.SCOPES), native_systems=False, chunk_ticks=8,
                discard_observation_tick=True)
    if layouts is not None:
        work.update(revision=2, layouts=layouts, scopes=list(bench.PHYSICS_SCOPES if 'physics' in layouts else bench.SCOPES))
    data = dict(format=bench.FORMAT, workload=work, units='microseconds', cases={},
                provenance=dict(configuration={'type': 'Release'}, compiler_sha256='compiler',
                                environment={'cpu': 'test'}, runtime_sha256='runtime'))
    if layouts is not None and 'physics' in layouts:
        data['provenance']['native_physics'] = dict(source_revision='a' * 40, configuration={})
    for layout in layouts if layouts is not None else ('flat', 'hierarchy'):
        scene, _ = bench.fixture(layout, 4)
        data['cases'][layout + '/4'] = dict(
            scene_sha256=hashlib.sha256(json.dumps(scene, sort_keys=True).encode()).hexdigest(),
            runs=[dict(ticks=[4, 5, 6], verified_entities=4,
                       microseconds={scope: list(values) for scope in work['scopes']})],
            summary_us={scope: bench.summarize(values) for scope in work['scopes']})
        if layout == 'physics':
            data['cases'][layout + '/4']['runs'][0].update(verified_bodies=4, final_tick=6)
    return data


class BenchmarkTests(unittest.TestCase):
    def test_fixtures_and_independent_parent_sums(self):
        scene, expected = bench.fixture('hierarchy', 8)
        rows = scene['entities']
        self.assertEqual(expected[rows[5]['id']], [6, 0, 0])
        self.assertEqual(expected[rows[7]['id']], [8, 0, 0])
        self.assertEqual(rows[7]['parent'], rows[1]['id'])
        self.assertTrue(all(uuid.UUID(row['id']).version == 4 for row in rows))
        self.assertEqual((scene, expected), bench.fixture('hierarchy', 8))
        flat, positions = bench.fixture('flat', 8)
        self.assertEqual(positions[flat['entities'][7]['id']], [7, 0, 0])
        self.assertNotEqual(flat['entities'][7]['id'], rows[7]['id'])
        for count in (0, 4097, True, 4.5):
            with self.assertRaises(ValueError):
                bench.fixture('flat', count)

    def test_percentiles_and_invalid_samples(self):
        self.assertEqual(bench.summarize(list(range(1, 21))),
                         dict(mean=10.5, median=10.5, p95=19, maximum=20))
        for values in ([], [-1], [True], [float('nan')], [float('inf')]):
            with self.assertRaises(ValueError):
                bench.summarize(values)

    def test_world_proof_rejects_changed_source_or_pose(self):
        scene, expected = bench.fixture('flat', 2)
        rows = copy.deepcopy(scene['entities'])
        for row, x in zip(rows, (0, 1)):
            row.update(spatial_resolved=True, world_affine=[1,0,0,x,0,1,0,0,0,0,1,0])
        state = dict(scene=scene, effective_scene=dict(entities=rows))
        bench.verify_world(state, scene, expected)
        rows[1]['world_affine'][3] = 2
        with self.assertRaises(ValueError):
            bench.verify_world(state, scene, expected)
        with self.assertRaises(ValueError):
            bench.verify_world(dict(state, scene={}), scene, expected)

    def test_report_consistency_and_comparison(self):
        old, new = report(), report((2, 4, 6))
        bench.validate_report(old)
        self.assertEqual(bench.compare(new, old)['changes']['flat/4'][bench.SCOPES[0]]['percent_change'], 100)
        self.assertIsNone(bench.compare(new, report((0, 0, 0)))['changes']['flat/4'][bench.SCOPES[0]]['percent_change'])
        for mutate in (
                lambda x: x.update(units='seconds'),
                lambda x: x['provenance']['environment'].update(cpu='other'),
                lambda x: x['cases'].pop('flat/4'),
                lambda x: x['cases']['flat/4']['runs'][0]['ticks'].reverse(),
                lambda x: x['cases']['flat/4']['runs'][0].update(verified_entities=3),
                lambda x: x['cases']['flat/4']['summary_us'][bench.SCOPES[0]].update(median=99)):
            bad = copy.deepcopy(old)
            mutate(bad)
            with self.assertRaises(ValueError):
                bench.compare(new, bad)

    def test_physics_fixture_and_independent_free_fall(self):
        source, expected = bench.fixture('physics', 65)
        rows = source['entities']
        self.assertEqual(expected[rows[64]['id']], [0, 100, 3])
        self.assertEqual(expected[rows[63]['id']], [189, 100, 0])
        self.assertTrue(all(row['spatial']['mode'] == 'world' and 'parent' not in row for row in rows))
        self.assertTrue(all(row['components']['forge.physics_body']['linear_damping'] == 0 and
                            row['components']['forge.physics_body']['allow_sleeping'] is False for row in rows))
        # Four symplectic Euler steps from rest move0.02725m, independently of fixtures.
        simulated = copy.deepcopy(source)
        for row in simulated['entities']:
            row['components']['forge.local_translation']['y'] = 99.97275
        effective = copy.deepcopy(simulated)
        for row in effective['entities']:
            x, _, z = expected[row['id']]
            row.update(spatial_resolved=True, world_affine=[1,0,0,x,0,1,0,99.97275,0,0,1,z])
        state = dict(timing=dict(tick=4), scene=simulated, effective_scene=effective,
                     physics=dict(tick=4, bodies=65, active_dynamic=65, sleeping_dynamic=0,
                                  characters=0, bodies_2d=0, joints=0, joints_2d=0, gravity=[0,-9.81,0]))
        bench.verify_physics(state, source, expected)
        first = lambda value: value['scene']['entities'][0]['components']
        for mutate in (
                lambda v: v['physics'].update(bodies=64),
                lambda v: v['physics'].update(active_dynamic=64, sleeping_dynamic=1),
                lambda v: v['physics'].update(gravity=[0,-5,0]),
                lambda v: v['physics'].update(tick=3),
                lambda v: first(v)['forge.local_translation'].update(y=100),
                lambda v: first(v)['forge.local_translation'].update(x=float('nan')),
                lambda v: first(v)['forge.physics_body'].update(gravity_factor=0),
                lambda v: v['effective_scene']['entities'][0]['world_affine'].__setitem__(7, 100),
                lambda v: v['effective_scene']['entities'][0].update(spatial_resolved=False),
                lambda v: v['scene']['entities'].pop()):
            bad = copy.deepcopy(state)
            mutate(bad)
            with self.assertRaises(ValueError):
                bench.verify_physics(bad, source, expected)
        self.assertEqual(source, bench.fixture('physics', 65)[0])

    def test_selected_workloads_and_physics_proof_admission(self):
        original = report(layouts=['physics'])
        bench.validate_report(original)
        self.assertEqual(set(bench.compare(original, original)['changes']['physics/4']), set(bench.PHYSICS_SCOPES))
        for mutate in (
                lambda v: v['workload'].update(layouts=['physics', 'physics']),
                lambda v: v['workload'].update(layouts=['unknown']),
                lambda v: v['workload'].update(revision=True),
                lambda v: v['workload'].update(scopes=list(bench.SCOPES)),
                lambda v: v['cases']['physics/4']['runs'][0]['microseconds'].pop('PhysicsStep'),
                lambda v: v['cases']['physics/4']['runs'][0].update(verified_bodies=3),
                lambda v: v['cases']['physics/4']['runs'][0].update(final_tick=7),
                lambda v: v['provenance']['native_physics'].update(source_revision='b' * 40)):
            bad = copy.deepcopy(original)
            mutate(bad)
            with self.assertRaises(ValueError):
                bench.compare(original, bad)
        with self.assertRaises(ValueError):
            bench.compare(original, report())
        bench.validate_report(report(layouts=['hierarchy']))
        bench.validate_report(report(layouts=['physics', 'flat', 'hierarchy']))

    @unittest.skipUnless(RUNTIME, 'Pass a built forge_runtime to exercise real protocol reports')
    def test_real_process_reports_and_immutable_output(self):
        with tempfile.TemporaryDirectory(prefix='runtime-workloads-', dir=RUNTIME.parent) as temporary:
            root = Path(temporary)
            first, second = root / 'first.json', root / 'second.json'
            args = [sys.executable, str(ROOT / 'tools/benchmark_runtime.py'), '--runtime', str(RUNTIME),
                    '--counts', '4', '12', '--warmup', '2', '--samples', '3', '--repeats', '1']
            subprocess.run([*args, '--output', str(first)], check=True)
            subprocess.run([*args, '--baseline', str(first), '--output', str(second)], check=True)
            a, z = json.loads(first.read_text()), json.loads(second.read_text())
            self.assertEqual(len(z['cases']), 4)
            self.assertEqual(z['comparison']['baseline_runtime_sha256'], a['provenance']['runtime_sha256'])
            self.assertFalse(z['provenance']['source_hint']['binary_origin_verified'])
            before = first.read_bytes()
            rejected = subprocess.run([*args, '--output', str(first)], capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertEqual(first.read_bytes(), before)
            malformed = root / 'bad.json'
            a['cases']['flat/4']['summary_us'][bench.SCOPES[0]]['median'] = 99
            malformed.write_text(json.dumps(a))
            failed = root / 'failed.json'
            rejected = subprocess.run([*args, '--baseline', str(malformed), '--output', str(failed)], capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertFalse(failed.exists())

    @unittest.skipUnless(RUNTIME, 'Pass a built forge_runtime for dynamic-physics reports')
    def test_real_physics_reports_and_baselines(self):
        with tempfile.TemporaryDirectory(prefix='physics-workloads-', dir=RUNTIME.parent) as temporary:
            root = Path(temporary)
            first, second = root / 'physics.json', root / 'compared.json'
            args = [sys.executable, str(ROOT / 'tools/benchmark_runtime.py'), '--runtime', str(RUNTIME),
                    '--workloads', 'physics', 'flat', '--counts', '4', '--warmup', '2',
                    '--samples', '9', '--repeats', '1']
            subprocess.run([*args, '--output', str(first)], check=True)
            subprocess.run([*args, '--baseline', str(first), '--output', str(second)], check=True)
            a, z = json.loads(first.read_text()), json.loads(second.read_text())
            self.assertEqual(set(z['cases']), {'physics/4', 'flat/4'})
            run = z['cases']['physics/4']['runs'][0]
            self.assertEqual(run['ticks'], [4,5,6,7,8,9,10,11,13])
            self.assertEqual(run['final_tick'], 13)
            self.assertEqual(run['verified_bodies'], 4)
            self.assertEqual(set(run['microseconds']), set(bench.PHYSICS_SCOPES))
            self.assertEqual(z['comparison']['baseline_runtime_sha256'], a['provenance']['runtime_sha256'])
            self.assertEqual(len(z['provenance']['native_physics']['source_revision']), 40)
            self.assertFalse(z['provenance']['native_physics']['binary_origin_verified'])
            before = first.read_bytes()
            rejected = subprocess.run([*args, '--output', str(first)], capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertEqual(first.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
