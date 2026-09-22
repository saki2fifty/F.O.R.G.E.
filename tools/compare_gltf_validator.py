"""Compare pinned official fixtures with an explicitly supplied Khronos validator.

Test utility only; it neither downloads dependencies nor changes source fixtures.
All JSON reports, stderr and binary provenance go to the caller's output directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def compare(executable, corpus, output):
    executable, corpus, output = map(lambda p: Path(p).resolve(), (executable, corpus, output))
    output.mkdir(parents=True, exist_ok=True)
    help_result = subprocess.run([str(executable), '--help'], capture_output=True, text=True,
                                 timeout=15)
    version = help_result.stdout + help_result.stderr
    if 'version 2.0.0-dev.3.10' not in version:
        raise ValueError('Comparison requires the recorded official 2.0.0-dev.3.10 release')
    provenance = json.loads((corpus / 'provenance.json').read_text(encoding='utf-8'))
    if provenance['revision'] != 'c6a6bd13ab2b3c685c7903d03561b8a9392f38b8':
        raise ValueError('Fixture revision changed; review comparison expectations')
    for name, row in provenance['files'].items():
        path = (corpus / name).resolve()
        path.relative_to(corpus)
        data = path.read_bytes()
        if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Fixture resource differs from pinned source: ' + name)
    results = []
    for fixture in provenance['fixtures']:
        source = (corpus / fixture['source']).resolve()
        source.relative_to(corpus)
        row = provenance['files'][fixture['source']]
        data = source.read_bytes()
        if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Fixture differs from pinned source: ' + fixture['name'])
        result = subprocess.run([str(executable), '--stdout', '--no-write-timestamp', str(source)],
                                capture_output=True, text=True, timeout=30)
        name = fixture['name'].replace('/', '-')
        (output / (name + '.json')).write_text(result.stdout, encoding='utf-8')
        (output / (name + '.stderr')).write_text(result.stderr, encoding='utf-8')
        report = json.loads(result.stdout)
        results.append(dict(fixture=fixture['name'], exit=result.returncode,
                            issues=report['issues']))
    summary = dict(validator_release='2.0.0-dev.3.10',
                   validator_source='bcd52cc4ba5f333b2999a58f67cc05ddf28b4fb1',
                   validator_binary_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                   fixture_revision=provenance['revision'], results=results)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    errors = sum(row['issues']['numErrors'] for row in results)
    if errors or any(row['exit'] != 0 for row in results):
        raise RuntimeError('Official validator rejected fixtures; inspect retained reports')
    print(f'{len(results)} official fixtures: zero validator errors; warnings and infos retained')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('validator')
    parser.add_argument('corpus')
    parser.add_argument('output')
    args = parser.parse_args()
    compare(args.validator, args.corpus, args.output)
