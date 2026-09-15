#!/usr/bin/env python3
"""Native authoring CLI. Build artifacts belong in the caller-selected work directory."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import time
import uuid


class Runtime:
    """Bounded requests to a disposable runtime; never load native code here."""
    def __init__(self, executable: Path):
        self.process = subprocess.Popen([str(executable)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, text=True, bufsize=1)
        self.lines: queue.Queue[str | None] = queue.Queue()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        try:
            for line in self.process.stdout:
                self.lines.put(line)
        finally:
            self.lines.put(None)

    def request(self, command: str, **data):
        if self.process.poll() is not None:
            raise RuntimeError('Runtime process has exited')
        self.process.stdin.write(json.dumps(dict(protocol=1, command=command, **data))+'\n')
        self.process.stdin.flush()
        try:
            line = self.lines.get(timeout=5)
        except queue.Empty as error:
            self.close()
            raise RuntimeError('Runtime request timed out') from error
        if line is None:
            raise RuntimeError('Runtime exited during request')
        response = json.loads(line)
        if not response.get('ok'):
            raise RuntimeError(response.get('error', 'Runtime rejected request'))
        return response

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=5)
        if self.process.stdin:
            self.process.stdin.close()
        self.reader.join(timeout=5)
        if self.process.stdout:
            self.process.stdout.close()


def create_project(project: Path, sdk: Path, language: str):
    if project.exists() and any(project.iterdir()):
        raise ValueError('Project directory must be empty')
    project.mkdir(parents=True, exist_ok=True)
    suffix = 'c' if language == 'c' else 'cpp'
    template = (sdk/'samples/native/movement.c').read_text()
    (project/f'gameplay.{suffix}').write_text(template)
    # SDK location is passed at configure time; generated project is relocatable.
    (project/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.24)
project(ForgeGameplay LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
if(NOT FORGE_SDK)
  message(FATAL_ERROR "Set FORGE_SDK to the FORGE source checkout")
endif()
add_library(gameplay MODULE gameplay.'''+suffix+''')
target_include_directories(gameplay PRIVATE "${FORGE_SDK}/include")
set_target_properties(gameplay PROPERTIES PREFIX "")
''')
    (project/'main.scene.json').write_text(json.dumps({'version':1,'entities':[
        {'id':'player','name':'Player','components':{'forge.position':{'x':0,'y':1,'z':0}}}
    ]}, indent=2)+'\n')


class Session:
    def __init__(self, project: Path, work: Path, sdk: Path, runtime: Path,
                 cmake: str = 'cmake', ninja: str = 'ninja'):
        self.project, self.work, self.sdk = project.resolve(), work.resolve(), sdk.resolve()
        self.runtime_path, self.cmake, self.ninja = runtime.resolve(), cmake, ninja
        self.build_dir = self.work/'build'
        self.artifacts = self.work/'modules'
        self.artifacts.mkdir(parents=True, exist_ok=True)
        self.checkpoint = json.loads((self.project/'main.scene.json').read_text())
        self.active: Path | None = None
        self.runtime = Runtime(self.runtime_path)
        self.runtime.request('replace', scene=self.checkpoint)

    def configure(self):
        subprocess.run([self.cmake, '-S', str(self.project), '-B', str(self.build_dir),
                        '-G', 'Ninja', '-DFORGE_SDK='+str(self.sdk),
                        '-DCMAKE_MAKE_PROGRAM='+self.ninja, '-DCMAKE_BUILD_TYPE=Debug'], check=True)

    def build(self):
        result = subprocess.run([self.cmake, '--build', str(self.build_dir), '--target', 'gameplay'],
                                capture_output=True, text=True)
        output = result.stdout+result.stderr
        (self.work/'build.log').write_text(output)
        if result.returncode:
            return False, output
        extension = '.dll' if os.name == 'nt' else '.so'
        source = self.build_dir/('gameplay'+extension)
        if not source.is_file():
            return False, f'Compiler produced no module at {source}'
        target = self.artifacts/('gameplay-'+uuid.uuid4().hex+extension)
        shutil.copy2(source, target)
        # Candidate runs in an isolated probe before touching the active runtime.
        probe = Runtime(self.runtime_path)
        try:
            probe.request('replace', scene=self.checkpoint)
            probe.request('load_module', path=str(target))
            probe.request('step', seconds=0)
        except Exception as error:
            return False, f'Candidate validation failed; previous module retained: {error}'
        finally:
            probe.close()
        try:
            self.checkpoint = self.runtime.request('snapshot')['scene']
            self.runtime.request('load_module', path=str(target))
        except (RuntimeError, BrokenPipeError, OSError) as error:
            # Incompatible schema is an explicit restart, not an in-place migration.
            if 'Play restart required:' in str(error):
                self.runtime.close()
                self.runtime = Runtime(self.runtime_path)
                self.runtime.request('replace', scene=self.checkpoint)
                self.runtime.request('load_module', path=str(target))
                self.active = target
                return True, f'{error}; restarted play with host-owned scene values'
            self.recover()
            return False, f'Reload failed; recovered previous checkpoint: {error}'
        self.active = target
        return True, output or 'Native module loaded'

    def recover(self):
        self.runtime.close()
        self.runtime = Runtime(self.runtime_path)
        self.runtime.request('replace', scene=self.checkpoint)
        if self.active:
            self.runtime.request('load_module', path=str(self.active))

    def step(self, seconds=1/60):
        try:
            result = self.runtime.request('step', seconds=seconds)
            self.checkpoint = result['scene']
            return result
        except (RuntimeError, BrokenPipeError, OSError):
            self.recover()
            raise RuntimeError('Play failed; previous module and latest checkpoint restored')

    def digest(self):
        digest = hashlib.sha256()
        for path in sorted(self.project.rglob('*')):
            if path.is_file() and (path.suffix in {'.c','.cpp','.h','.hpp','.cmake'} or path.name=='CMakeLists.txt'):
                digest.update(str(path.relative_to(self.project)).encode())
                digest.update(path.read_bytes())
        return digest.hexdigest()

    def close(self):
        self.runtime.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['create','watch'])
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--language', choices=['c','cpp'], default='cpp')
    parser.add_argument('--work', type=Path)
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    args = parser.parse_args()
    if args.action=='create':
        create_project(args.project,args.sdk,args.language)
        return
    if not args.work or not args.runtime:
        parser.error('watch requires --work and --runtime')
    session = Session(args.project,args.work,args.sdk,args.runtime,args.cmake,args.ninja)
    try:
        session.configure()
        digest = None
        while True:
            current = session.digest()
            if current != digest:
                ok, log = session.build()
                print(('BUILD OK: ' if ok else 'BUILD FAILED: ')+log, flush=True)
                digest = current
            try:
                session.step()
            except RuntimeError as error:
                print(error,flush=True)
            time.sleep(1/60)
    except KeyboardInterrupt:
        pass
    finally:
        session.close()

if __name__=='__main__':
    main()
