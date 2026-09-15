#!/usr/bin/env python3
"""Restart-bound plugin package staging. This tool never loads a native library."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import uuid


def atomic_json(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    pending=path.with_name(path.name+'.'+uuid.uuid4().hex+'.pending')
    try:
        pending.write_text(json.dumps(value,indent=2)+'\n')
        os.replace(pending,path)
    finally:
        pending.unlink(missing_ok=True)


def read_json(path: Path, default):
    return json.loads(path.read_text()) if path.exists() else default


class PluginStore:
    def __init__(self, root: Path, sdk: str, toolchain: str, capabilities: set[str]):
        self.root=root.resolve()
        self.sdk=sdk
        self.toolchain=toolchain
        self.capabilities=capabilities
        self.root.mkdir(parents=True,exist_ok=True)

    def validate(self, package: Path):
        manifest=json.loads((package/'plugin.json').read_text())
        for key in ('id','version'):
            if not isinstance(manifest.get(key),str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*',manifest[key]):
                raise ValueError('Invalid plugin '+key)
        if manifest.get('api_version')!=1:
            raise ValueError('Unsupported plugin API version')
        if manifest.get('kind') not in ('c-abi','native-cpp'):
            raise ValueError('Unsupported plugin kind')
        if manifest['kind']=='native-cpp' and (manifest.get('sdk')!=self.sdk or manifest.get('toolchain')!=self.toolchain):
            raise ValueError('Native C++ SDK/toolchain mismatch')
        required=manifest.get('capabilities',[])
        if not isinstance(required,list) or not all(isinstance(x,str) for x in required) or not set(required)<=self.capabilities:
            raise ValueError('Unavailable plugin capability')
        dependencies=manifest.get('dependencies',{})
        if not isinstance(dependencies,dict) or not all(isinstance(k,str) and isinstance(v,str) for k,v in dependencies.items()):
            raise ValueError('Dependencies must map plugin IDs to exact versions')
        library=manifest.get('library','')
        if not isinstance(library,str) or not library or Path(library).is_absolute():
            raise ValueError('Invalid plugin library path')
        binary=(package/library).resolve()
        if not binary.is_relative_to(package.resolve()) or not binary.is_file():
            raise ValueError('Plugin library missing or outside package')
        if hashlib.sha256(binary.read_bytes()).hexdigest()!=manifest.get('sha256'):
            raise ValueError('Plugin library checksum mismatch')
        return manifest

    def stage(self, package: Path):
        package=package.resolve()
        # Symlinks would make package contents mutable outside the staged directory.
        if any(p.is_symlink() for p in package.rglob('*')):
            raise ValueError('Plugin packages may not contain symlinks')
        manifest=self.validate(package)
        destination=self.root/'packages'/(manifest['id']+'-'+manifest['version']+'-'+uuid.uuid4().hex)
        destination.parent.mkdir(exist_ok=True)
        shutil.copytree(package,destination)
        try:
            self.validate(destination)
            pending=read_json(self.root/'pending.json',read_json(self.root/'active.json',{}))
            pending[manifest['id']]=str(destination.relative_to(self.root))
            atomic_json(self.root/'pending.json',pending)
        except Exception:
            shutil.rmtree(destination)
            raise
        return manifest

    def disable(self, plugin_id: str):
        pending=read_json(self.root/'pending.json',read_json(self.root/'active.json',{}))
        pending.pop(plugin_id,None)
        atomic_json(self.root/'pending.json',pending)

    def startup(self, safe_mode=False):
        if safe_mode:
            return []
        active=read_json(self.root/'active.json',{})
        selection=read_json(self.root/'pending.json',active)
        manifests={}
        for plugin_id, relative in selection.items():
            package=(self.root/relative).resolve()
            if not package.is_relative_to(self.root/'packages'):
                raise ValueError('Plugin selection escapes package store')
            manifest=self.validate(package)
            if manifest['id']!=plugin_id:
                raise ValueError('Plugin selection identity mismatch')
            manifests[plugin_id]=manifest
        ordered=[]
        visiting=set()
        done=set()
        def visit(plugin_id):
            if plugin_id in done:
                return
            if plugin_id in visiting:
                raise ValueError('Cyclic plugin dependency')
            visiting.add(plugin_id)
            for dependency,version in manifests[plugin_id].get('dependencies',{}).items():
                if dependency not in manifests or manifests[dependency]['version']!=version:
                    raise ValueError('Missing or incompatible dependency: '+dependency)
                visit(dependency)
            visiting.remove(plugin_id)
            done.add(plugin_id)
            ordered.append(manifests[plugin_id])
        for plugin_id in sorted(manifests):
            visit(plugin_id)
        # Only package validation has succeeded; callers must initialize code and then commit.
        return ordered

    def commit_startup(self):
        self.startup()
        pending=self.root/'pending.json'
        if pending.exists():
            atomic_json(self.root/'previous.json',read_json(self.root/'active.json',{}))
            atomic_json(self.root/'active.json',json.loads(pending.read_text()))
            pending.unlink()
