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
        self.lines: queue.Queue[str | None] = queue.Queue(maxsize=2)
        self.session = ""
        self.request_id = 0
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.request("hello")

    def _read(self):
        try:
            while True:
                line = self.process.stdout.readline(16 * 1024 * 1024 + 1)
                if not line:
                    break
                if len(line) > 16 * 1024 * 1024:
                    break
                self.lines.put_nowait(line)
        finally:
            try:
                self.lines.put_nowait(None)
            except queue.Full:
                pass

    def request(self, command: str, **data):
        if self.process.poll() is not None:
            raise RuntimeError('Runtime process has exited')
        self.request_id += 1
        request = dict(protocol=2, id=self.request_id, session=self.session, command=command, **data)
        self.process.stdin.write(json.dumps(request)+'\n')
        self.process.stdin.flush()
        try:
            line = self.lines.get(timeout=5)
        except queue.Empty as error:
            self.close()
            raise RuntimeError('Runtime request timed out') from error
        if line is None:
            raise RuntimeError('Runtime exited during request')
        response = json.loads(line)
        if response.get('protocol') != 2 or response.get('id') != self.request_id:
            raise RuntimeError('Invalid/stale runtime response')
        if command == 'hello':
            self.session = response.get('session', '')
        if not self.session or response.get('session') != self.session:
            raise RuntimeError('Stale runtime session')
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
        self.pending: Path | None = None
        self.paused = True
        self.prior_paused = True
        self.activation_generation = 0
        self.state = 'Idle'
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
        # Use representative current state; probe cannot mutate the live runtime.
        try:
            representative = self.poll()['scene']
        except RuntimeError as error:
            return False, str(error)
        probe = Runtime(self.runtime_path)
        try:
            probe.request('replace', scene=representative)
            probe.request('load_module', path=str(target))
            probe.request('step')
        except Exception as error:
            return False, f'Candidate validation failed; previous module retained: {error}'
        finally:
            probe.close()
        return self.activate(target, output)

    def activate(self, target: Path, output=''):
        """Install an already probed artifact; first LIVE tick commits it."""
        try:
            self.poll()
            if self.pending:
                self._rollback(resume=False)  # Supersede, never stack transactions.
            else:
                self.prior_paused = self.paused
            boundary = self.runtime.request('pause')
            self.paused = True
            self.checkpoint = boundary['scene']
            self.pending = target
            try:
                loaded = self.runtime.request('load_module', path=str(target))
            except RuntimeError as error:
                if 'Play restart required:' not in str(error):
                    raise
                self.runtime.close()
                self.runtime = Runtime(self.runtime_path)
                self.runtime.request('replace', scene=self.checkpoint)
                loaded = self.runtime.request('load_module', path=str(target))
                output += '\nSchema changed; restarted play with host-owned scene values'
            self.activation_generation = loaded['activation']['generation']
            self.state = 'LoadedPendingFirstTick'
            if not self.prior_paused:
                self.runtime.request('resume')
                self.paused = False
            return True, output or 'Reload pending first live fixed tick'
        except (RuntimeError, BrokenPipeError, OSError) as error:
            self._rollback()
            return False, f'Reload failed; recovered previous checkpoint: {error}'

    def _observe(self, result):
        self.paused = result['timing']['paused']
        activation = result['activation']
        if self.pending and activation['generation'] == self.activation_generation and activation['state'] == 'active':
            self.active = self.pending
            self.pending = None
            self.state = 'Active'
        if not self.pending:
            self.checkpoint = result['scene']
        return result

    def _rollback(self, resume=True):
        self.runtime.close()
        self.runtime = Runtime(self.runtime_path)
        self.runtime.request('replace', scene=self.checkpoint)
        if self.active:
            self.runtime.request('load_module', path=str(self.active))
        self.pending = None
        self.paused = True
        self.state = 'Failed/Reverted'
        if resume and not self.prior_paused:
            self.runtime.request('resume')
            self.paused = False

    def recover(self):
        if not self.pending:
            self.prior_paused = self.paused
        self._rollback()

    def _control(self, command):
        try:
            return self._observe(self.runtime.request(command))
        except (RuntimeError, BrokenPipeError, OSError):
            self.recover()
            raise RuntimeError('Play failed; previous module and latest checkpoint restored')

    def poll(self):
        return self._control('snapshot')

    def pause(self):
        return self._control('pause')

    def resume(self):
        return self._control('resume')

    def step(self):
        if not self.paused:
            raise RuntimeError('Single Step requires Pause')
        return self._control('step')

    def digest(self):
        digest = hashlib.sha256()
        for path in sorted(self.project.rglob('*')):
            if path.is_file() and (path.suffix in {'.c','.cpp','.h','.hpp','.cmake'} or path.name=='CMakeLists.txt'):
                digest.update(str(path.relative_to(self.project)).encode())
                digest.update(path.read_bytes())
        return digest.hexdigest()

    def close(self):
        self.pending = None
        self.state = 'Stopped'
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
                if session.paused:
                    session.resume()
                session.poll()
            except RuntimeError as error:
                print(error,flush=True)
            time.sleep(1/60)
    except KeyboardInterrupt:
        pass
    finally:
        session.close()

if __name__=='__main__':
    main()
