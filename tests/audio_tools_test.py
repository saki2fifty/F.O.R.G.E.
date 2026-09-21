"""The public shared import command publishes and reimports an AudioClip safely."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import wave

tool, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
if root.exists():
    shutil.rmtree(root)
(root / 'Assets').mkdir(parents=True)
source = root / 'Assets/tone.wav'

def wav(channels, rate):
    with wave.open(str(source), 'wb') as output:
        output.setnchannels(channels)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(b'\x00\x10' * channels * 480)

def run(operation, *arguments, ok=True):
    result = subprocess.run([str(tool), '--assets', operation, str(root), *arguments],
                            text=True, capture_output=True, timeout=45)
    value = json.loads(result.stdout)
    assert (result.returncode == 0) == ok and value['ok'] == ok, (result, value)
    return value

wav(1, 48000)
first = run('import', 'Assets/tone.wav')
record = run('query')['assets'][0]
assert record['type'] == 'audio_clip' and record['id'] == first['asset'], record
assert record['metadata']['forge.audio']['duration'] == .01, record
again = run('import', 'Assets/tone.wav')
assert again['asset'] == first['asset'] and again['cache_hit'], again
baseline = (root / 'forge.assets.json').read_bytes()
sidecar = source.with_name(source.name + '.forge-import.json')
assert sidecar.is_file()
settings = sidecar.read_bytes()
source.write_bytes(b'corrupt but bounded WAV input')
run('import', 'Assets/tone.wav', ok=False)
assert (root / 'forge.assets.json').read_bytes() == baseline
assert sidecar.read_bytes() == settings
wav(2, 24000)
newer = run('import', 'Assets/tone.wav')
assert newer['asset'] == first['asset'] and newer['build_key'] != first['build_key']
record = run('query')['assets'][0]
assert record['metadata']['forge.audio']['sample_rate'] == 24000
assert record['metadata']['forge.audio']['channels'] == 2
print('Audio import CLI: identity, metadata, cache hit, failure retention and reimport passed')
