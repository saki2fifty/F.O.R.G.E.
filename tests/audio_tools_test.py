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

fixtures = Path(__file__).resolve().parents[1] / 'samples/projects/FeatureGallery/Assets/AudioCodecs'
for extension in ('flac', 'mp3'):
    path = root / 'Assets' / ('Stereo.' + extension)
    original = (fixtures / path.name).read_bytes()
    path.write_bytes(original)
    first = run('import', 'Assets/' + path.name)
    again = run('import', 'Assets/' + path.name)
    assert first['asset'] == again['asset'] and again['cache_hit']
    record = next(item for item in run('query')['assets'] if item['id'] == first['asset'])
    assert record['metadata']['forge.import']['importer'] == 'forge.audio.' + extension
    assert record['metadata']['forge.audio']['source_format'] == extension
    assert record['metadata']['forge.audio']['frames'] == 12000
    assert record['metadata']['forge.audio']['channels'] == 2
    baseline = (root / 'forge.assets.json').read_bytes()
    sidecar = path.with_name(path.name + '.forge-import.json')
    settings = sidecar.read_bytes()
    path.write_bytes(b'corrupt compressed audio source')
    run('import', 'Assets/' + path.name, ok=False)
    assert (root / 'forge.assets.json').read_bytes() == baseline and sidecar.read_bytes() == settings
    path.write_bytes(original)
    assert run('import', 'Assets/' + path.name)['asset'] == first['asset']
print('MP3/FLAC public import/cache/identity and failed-reimport retention passed')
