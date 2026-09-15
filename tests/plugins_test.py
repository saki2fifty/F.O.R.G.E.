import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import sys
sys.dont_write_bytecode = True
spec=importlib.util.spec_from_file_location('plugins',Path(sys.argv[1])/'tools/forge_plugins.py')
plugins=importlib.util.module_from_spec(spec)
spec.loader.exec_module(plugins)
with tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
    root=Path(temp)
    package=root/'input'
    package.mkdir()
    binary=b'package validation fixture, never executed'
    (package/'plugin.dll').write_bytes(binary)
    manifest=dict(id='example',version='1',api_version=1,kind='native-cpp',sdk='test',toolchain='test',capabilities=['panels'],library='plugin.dll',sha256=hashlib.sha256(binary).hexdigest())
    (package/'plugin.json').write_text(json.dumps(manifest))
    store=plugins.PluginStore(root/'store','test','test',{'panels'})
    store.stage(package)
    assert not (root/'store/active.json').exists()
    assert store.startup()[0]['id']=='example'
    store.commit_startup()
    active=(root/'store/active.json').read_text()
    (package/'plugin.dll').write_bytes(b'tampered')
    try:
        store.stage(package)
        raise AssertionError('Checksum accepted')
    except ValueError:
        pass
    assert (root/'store/active.json').read_text()==active
    assert store.startup(safe_mode=True)==[]
    store.disable('example')
    assert (root/'store/active.json').read_text()==active
    assert store.startup()==[]
    store.commit_startup()
    assert json.loads((root/'store/active.json').read_text())=={}
print('Plugin staging, checksum rejection and restart-bound activation passed')
