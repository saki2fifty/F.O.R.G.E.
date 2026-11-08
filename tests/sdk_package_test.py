"""Build from installed SDK, inspect binary dependencies, run from a relocated clean package."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,tempfile
build=Path(sys.argv[1]).resolve();cmake=sys.argv[2];ninja=sys.argv[3]
cache=dict(line.split('=',1) for line in (build/'CMakeCache.txt').read_text().splitlines() if '=' in line and not line.startswith(('//','#')))
compiler=next(value for key,value in cache.items() if key.startswith('CMAKE_CXX_COMPILER:'))
compiler_arg='-DCMAKE_CXX_COMPILER='+compiler
build_jobs=str(max(1,int(os.environ.get('FORGE_SDK_TEST_JOBS', '2'))))
def validate_guides(stage):
    for document in (stage/'sdk/docs').rglob('*.md'):
        for target in re.findall(r'\[[^\]]+\]\(([^)]+)\)', document.read_text(encoding='utf-8')):
            path=target.split('#',1)[0]
            if path and '://' not in path:
                resolved=(document.parent/path).resolve()
                assert resolved.is_relative_to(stage/'sdk') and resolved.is_file(), f'Broken installed guide link: {target}'

focused_targets = {
    '--sound-cues-only': 'audio_gameplay',
    '--audio-routing-only': 'audio_gameplay',
    '--animation-graphs-only': 'animation_gameplay',
    '--ecs-workers-only': 'parallel_motion_gameplay',
    '--sequence-only': 'sequence_gameplay',
    '--prefab-only': 'prefab_gameplay',
    '--ui-localization-only': 'ui_localization_gameplay',
    '--ui-values-only': 'ui_values_gameplay',
    '--camera-output-only': 'camera_output_gameplay',
    '--navigation-behavior-only': 'navigation_behavior_gameplay',
    '--decals-only': 'decal_gameplay',
    '--soft-shadows-only': 'soft_shadow_gameplay',
    '--reflection-probes-only': 'reflection_probe_gameplay',
    '--component-animation-only': 'component_animation_gameplay',
    '--motion-only': 'gamepad_motion_gameplay',
    '--navigation-only': 'planar_navigation_gameplay', '--time-only': 'time_scale_gameplay',
    '--gamepads-only': 'multiple_gamepads_gameplay', '--feedback-only': 'gamepad_feedback_gameplay', '--input-only': 'gameplay',
    '--audio-pause-only': 'unpaused_audio_gameplay', '--audio-filters-only': 'audio_gameplay',
    '--tile-one-way-only': 'tile_physics_gameplay', '--gestures-only': 'touch_gestures_gameplay', '--touch-only': 'touch_input_gameplay', '--sprite-frames-only': 'sprite_image_gameplay',
}
if len(sys.argv) == 5 and sys.argv[4] in focused_targets:
    gestures_only = sys.argv[4] == '--gestures-only'
    touch_only = sys.argv[4] == '--touch-only'
    sprite_frames_only = sys.argv[4] == '--sprite-frames-only'
    tile_one_way_only = sys.argv[4] == '--tile-one-way-only'
    audio_filters_only = sys.argv[4] == '--audio-filters-only'
    audio_pause_only = sys.argv[4] == '--audio-pause-only'
    time_only = sys.argv[4] == '--time-only'
    feedback_only = sys.argv[4] == '--feedback-only'
    gamepads_only = sys.argv[4] == '--gamepads-only'
    input_only = sys.argv[4] == '--input-only'
    target = focused_targets[sys.argv[4]]
    # Reuse the install/sample owner for a focused public-boundary check.
    with tempfile.TemporaryDirectory(prefix='public-client-sdk-', dir=build) as temporary:
        base=Path(temporary);stage=base/'sdk';client=base/'client';moved=base/'relocated SDK'
        subprocess.run([cmake,'--install',str(build),'--prefix',str(stage),'--component','NativeSdk'],check=True)
        validate_guides(stage)
        subprocess.run([cmake,'-S',str(stage/'sdk/sample'),'-B',str(client),'-G','Ninja',
                        compiler_arg,'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_MAKE_PROGRAM='+ninja,
                        '-DFORGE_NATIVE_SDK='+str(stage)],check=True)
        subprocess.run([cmake,'--build',str(client),'--target',target,
                        '--parallel',build_jobs],check=True)
        windows=os.name=='nt';exe='.exe' if windows else '';ext='.dll' if windows else '.so'
        module=client/(target+ext)
        if not windows:
            imports=subprocess.check_output(['readelf','-d',str(module)],text=True)
            exports=subprocess.check_output(['nm','-D','--defined-only',str(module)],text=True)
            assert 'libflecs.so.4' in imports and ' ecs_init' not in exports
        assert not (stage/'sdk/include/forge/assets.hpp').exists()
        stage.rename(moved)
        env=os.environ.copy()
        if not windows:env['LD_LIBRARY_PATH']=str(moved/'bin')+':'+env.get('LD_LIBRARY_PATH','')
        if sys.argv[4] == '--animation-graphs-only':
            source = Path(__file__).resolve().parents[1]
            assert (moved/'sdk/include/forge/animation_components.hpp').read_bytes() == (source/'include/forge/animation_components.hpp').read_bytes()
            subprocess.run([sys.executable, str(Path(__file__).with_name('mcp_workflow_test.py')),
                            str(build/('forge_tools'+exe)), str(build/('forge_mcp_workflow_host'+exe)),
                            str(moved/'bin'/('forge_runtime'+exe)), '-', str(build/('forge_nav_build'+exe)),
                            str(base/'graph-mcp-capture.json.gz'), '--animation-graph-module-only', str(module)],
                            env=env, cwd=moved, check=True, timeout=180)
            print('Installed-only Animator graph input layout, original C++ system and relocated SDK runtime through both MCP envelopes passed')
            sys.exit(0)
        if sys.argv[4] == '--ecs-workers-only':
            source = Path(__file__).resolve().parents[1]
            for name in ('runtime_execution.hpp',):
                assert (moved/'sdk/include/forge'/name).read_bytes() == (source/'include/forge'/name).read_bytes()
            for name in ('ParallelMotion.cpp', 'ParallelMotion.hpp'):
                assert (moved/'sdk/sample'/name).read_bytes() == (source/'samples/projects/FeatureGallery/CodeExamples'/name).read_bytes()
            subprocess.run([sys.executable, str(Path(__file__).with_name('runtime_workers_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module),
                            str(source/'samples/projects/FeatureGallery')],
                            env=env, cwd=moved, check=True, timeout=120)
            print('Installed-only original parallel system and relocated runtime worker policy passed')
            sys.exit(0)
        if sys.argv[4] == '--sequence-only':
            source = Path(__file__).resolve().parents[1]
            assert (moved/'sdk/include/forge/animation_components.hpp').read_bytes() == (source/'include/forge/animation_components.hpp').read_bytes()
            subprocess.run([sys.executable, str(Path(__file__).with_name('animation_sequence_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)),
                            str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'),
                            str(module)], env=env, cwd=moved, check=True, timeout=150)
            print('Installed-only paired sequence values, exact recovery and relocated SDK/runtime hosts passed')
            sys.exit(0)
        if sys.argv[4] == '--prefab-only':
            assert not (moved/'sdk/include/forge/prefab.hpp').exists()
            for profile in ('--variant', '--suppression', '--structure'):
                subprocess.run([sys.executable, str(Path(__file__).with_name('prefab_spawn_process_test.py')),
                                str(moved/'bin'/('forge_runtime'+exe)),
                                str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'),
                                str(module), profile], env=env, cwd=moved, check=True, timeout=120)
            print('Installed-only Prefab values/suppression/structure, relocated SDK and both runtime hosts passed')
            sys.exit(0)
        if sys.argv[4] == '--camera-output-only':
            source = Path(__file__).resolve().parents[1]
            for name in ('render_components.hpp', 'ui_components.hpp', 'asset_ref.hpp'):
                assert (moved/'sdk/include/forge'/name).read_bytes() == (source/'include/forge'/name).read_bytes()
            assert not (moved/'sdk/include/forge/render_texture.hpp').exists()
            subprocess.run([sys.executable, str(source/'tests/camera_output_process_test.py'),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module),
                            str(source/'samples/projects/FeatureGallery/forge.project.json'), str(base/'camera-process')],
                           env=env, cwd=moved, check=True, timeout=120)
            print('Installed-only typed Camera/UI output, relocated SDK and both runtime hosts passed')
            sys.exit(0)
        if sys.argv[4] == '--ui-values-only':
            assert (moved/'sdk/include/forge/ui_values.hpp').read_bytes() == (Path(__file__).resolve().parents[1]/'include/forge/ui_values.hpp').read_bytes()
            subprocess.run([sys.executable, str(Path(__file__).with_name('ui_values_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module),
                            str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'), str(base/'values-process')],
                           env=env, cwd=moved, check=True, timeout=120)
            print('Installed-only UI Values vector/string lifecycle, relocated SDK and both runtime hosts passed')
            sys.exit(0)
        if sys.argv[4] == '--ui-localization-only':
            assert (moved/'sdk/include/forge/ui_components.hpp').is_file()
            assert not (moved/'sdk/include/forge/ui_localization.hpp').exists()
            subprocess.run([sys.executable, str(Path(__file__).with_name('ui_localization_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module),
                            str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'), str(base/'localization-process')],
                           env=env, cwd=moved, check=True, timeout=120)
            print('Installed-only localization value/string lifetime, relocated SDK and both runtime hosts passed')
            sys.exit(0)
        if sys.argv[4] == '--navigation-behavior-only':
            assert (moved/'sdk/include/forge/navigation_behavior_components.hpp').is_file()
            assert not (moved/'sdk/include/forge/navigation_behavior.hpp').exists()
            subprocess.run([sys.executable, str(Path(__file__).with_name('navigation_behavior_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module),
                            str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'), str(base/'behavior-process')],
                           env=env, cwd=moved, check=True, timeout=120)
            print('Installed-only behavior POD/cursor/resource-kind8, relocated SDK and both runtime hosts passed')
            sys.exit(0)
        if sys.argv[4] == '--motion-only':
            subprocess.run([sys.executable, str(Path(__file__).with_name('gamepad_motion_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module),
                            str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery')],
                           env=env, cwd=moved, check=True, timeout=90)
            print('Installed public controller-motion example and relocated runtime passed')
            sys.exit(0)
        if sys.argv[4] == '--decals-only':
            assert (moved/'sdk/include/forge/decal.hpp').is_file()
            subprocess.run([sys.executable, str(Path(__file__).with_name('decal_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module), str(base/'decal-runtime')],
                           env=env, cwd=moved, check=True, timeout=90)
            print('Installed public decal header, relocated SDK, both hosts and recovery passed')
            sys.exit(0)
        if sys.argv[4] == '--soft-shadows-only':
            subprocess.run([sys.executable, str(Path(__file__).with_name('soft_shadow_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module), str(base/'shadow-runtime')],
                           env=env, cwd=moved, check=True, timeout=90)
            print('Installed public Light source fields, relocated SDK, both hosts and recovery passed')
            sys.exit(0)
        if sys.argv[4] == '--reflection-probes-only':
            assert (moved/'sdk/include/forge/reflection_probe.hpp').is_file()
            subprocess.run([sys.executable, str(Path(__file__).with_name('reflection_probe_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module), str(base/'probe-runtime')],
                           env=env, cwd=moved, check=True, timeout=90)
            print('Installed public reflection-probe header, relocated SDK, both hosts and recovery passed')
            sys.exit(0)
        if gamepads_only:
            subprocess.run([sys.executable, str(Path(__file__).with_name('multiple_gamepads_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module),
                            str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery')],
                           env=env, cwd=moved, check=True, timeout=90)
            print('Installed public multiple-gamepad client and relocated runtime passed')
            sys.exit(0)
        if touch_only or gestures_only:
            subprocess.run([sys.executable, str(Path(__file__).with_name('touch_gesture_process_test.py' if gestures_only else 'touch_input_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module),
                            str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery')],
                           env=env, cwd=moved, check=True, timeout=90)
            print('Installed public touch client and relocated runtime passed')
            sys.exit(0)
        if sys.argv[4] == '--component-animation-only':
            subprocess.run([sys.executable, str(Path(__file__).with_name('component_animation_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module),
                            str(Path(__file__).resolve().parents[1]/'samples/projects/ComponentAnimation'),
                            str(base/'run')], env=env, cwd=moved, check=True, timeout=120)
            print('Installed canonical component animation example, both hosts and recovery passed')
            sys.exit(0)
        if sprite_frames_only:
            subprocess.run([sys.executable, str(Path(__file__).with_name('sprite_image_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)), str(module), str(base/'run')],
                           env=env, cwd=moved, check=True, timeout=90)
            print('Installed public Sprite frame-bank/Texture-key values, both hosts, live tuning and recovery passed')
            sys.exit(0)
        if tile_one_way_only:
            subprocess.run([sys.executable,str(Path(__file__).with_name('sdk_tile_physics_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)),str(module),
                            str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'), '--one-way'],
                           env=env,cwd=moved,check=True,timeout=90)
            print('Installed public one-way Map edits/queries, both hosts and recovery passed')
            sys.exit(0)
        if audio_filters_only:
            subprocess.run([sys.executable,str(Path(__file__).with_name('audio_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)),str(module)],
                           env=env,cwd=moved,check=True,timeout=90)
            print('Installed public AudioSource filter values, commands and recovery passed')
            sys.exit(0)
        if sys.argv[4] == '--sound-cues-only':
            source = Path(__file__).resolve().parents[1]
            assert (moved/'sdk/include/forge/audio_components.hpp').read_bytes() == (source/'include/forge/audio_components.hpp').read_bytes()
            subprocess.run([sys.executable,str(Path(__file__).with_name('sound_cue_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)),str(source/'samples/projects/FeatureGallery'),str(module)],
                           env=env,cwd=moved,check=True,timeout=90)
            print('Installed-only Sound Cue nested values, original C++ writes/triggers and both relocated hosts passed')
            sys.exit(0)
        if sys.argv[4] == '--audio-routing-only':
            source = Path(__file__).resolve().parents[1]
            assert (moved/'sdk/include/forge/audio_components.hpp').read_bytes() == (source/'include/forge/audio_components.hpp').read_bytes()
            subprocess.run([sys.executable, str(Path(__file__).with_name('audio_routing_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)),
                            str(source/'samples/projects/FeatureGallery'), str(module)],
                           env=env, cwd=moved, check=True, timeout=90)
            print('Installed-only AudioSource custom route layout, original C++ writes, both relocated runtime hosts and private recovery passed')
            sys.exit(0)
        if audio_pause_only:
            subprocess.run([sys.executable,str(Path(__file__).with_name('unpaused_audio_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)),str(module)],
                           env=env,cwd=moved,check=True,timeout=90)
            print('Installed public audio controls, shared Flecs and relocated SDK Play passed')
            sys.exit(0)
        if input_only:
            subprocess.run([str(build/('forge_analog_input_tests'+exe)), str(module)],
                           env=env,cwd=moved,check=True,timeout=90)
            print('Installed public analog client and SDK relocation passed')
            sys.exit(0)
        if feedback_only:
            subprocess.run([sys.executable,str(Path(__file__).with_name('gamepad_feedback_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)),str(module),
                            str(build/('forge_gamepad_feedback_tests'+exe))],
                           env=env,cwd=moved,check=True,timeout=90)
            print('Installed public gamepad feedback client and relocated runtime passed')
            sys.exit(0)
        if time_only:
            subprocess.run([sys.executable,str(Path(__file__).with_name('simulation_speed_process_test.py')),
                            str(moved/'bin'/('forge_runtime'+exe)),str(module)],
                           env=env,cwd=moved,check=True,timeout=90)
            print('Installed public time client and relocated SDK Play passed')
            sys.exit(0)
        subprocess.run([sys.executable,str(Path(__file__).with_name('navigation_process_test.py')),
                        str(moved/'bin'/('forge_runtime'+exe)),str(build/('forge_navigation_tests'+exe)),
                        str(build/('forge_nav_build'+exe)),str(module),'--planar'],
                       env=env,cwd=moved,check=True,timeout=90)
        subprocess.run([sys.executable,str(Path(__file__).with_name('navigation_sdk_play_test.py')),
                        str(moved/'bin'/('forge_runtime'+exe)),str(build/('forge_navigation_tests'+exe)),
                        str(build/('forge_nav_build'+exe)),str(module)],
                       env=env,cwd=moved,check=True,timeout=90)
    print('Installed-only XY navigation client, shared Flecs and relocated runtime/recovery passed')
    sys.exit(0)
stage=build/'sdk-package-test'
if stage.exists():shutil.rmtree(stage)
subprocess.run([cmake,'--install',str(build),'--prefix',str(stage),'--component','NativeSdk'],check=True)
client=build/'sdk-client-test'
# Reject a mismatched client configuration before its code can be loaded.
bad=build/'sdk-incompatible-client-test'
for directory in (bad,client):
 if directory.exists():shutil.rmtree(directory)
result=subprocess.run([cmake,'-S',str(stage/'sdk/sample'),'-B',str(bad),'-G','Ninja',compiler_arg,'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_MAKE_PROGRAM='+ninja,'-DFORGE_NATIVE_SDK='+str(stage),'-DCMAKE_CXX_FLAGS_RELEASE=-DNDEBUG -DFORGE_SDK_TEST_MISMATCH=1'],capture_output=True,text=True)
assert result.returncode!=0 and 'compiler flags mismatch' in result.stdout+result.stderr,result.stdout+result.stderr
assert not (stage/'sdk/include/forge/assets.hpp').exists(), 'Private AssetCatalog leaked into installed SDK'
assert (stage/'sdk/include/forge/application_state_components.hpp').is_file()
assert not (stage/'sdk/include/forge/application_state_activation.hpp').exists(), 'Host-only state activation leaked into installed SDK'
assert (stage/'sdk/docs/extension-guide.md').exists()
# All installed guides must remain usable without the source checkout.
for relative in ('docs/physics-2d.md', 'docs/decisions/024-2d-physics.md'):
    document=stage/'sdk'/relative
    assert document.is_file(), f'Missing installed 2D guide: {relative}'
validate_guides(stage)
assert (stage/'sdk/include/forge/engine_assets.hpp').exists()
assert (stage/'sdk/sample/PaintedCollision.hpp').is_file()
assert (stage/'sdk/sample/tile_physics_probe.cpp').is_file()
assert (stage/'sdk/include/forge/engine_texture_assets.hpp').exists()
assert (stage/'sdk/include/forge/texture_dimension.hpp').exists()
assert not (stage/'sdk/include/forge/texture_asset.hpp').exists(), 'Cooked texture API leaked into value-only SDK'
assert (stage/'sdk/include/forge/primitive_catalog.hpp').exists()
assert (stage/'sdk/include/forge/transform_components.hpp').exists()
assert (stage/'sdk/sample/Physics2DJoints.hpp').is_file()
assert (stage/'sdk/sample/character_2d_probe.cpp').is_file()
subprocess.run([cmake,'-S',str(stage/'sdk/sample'),'-B',str(client),'-G','Ninja',compiler_arg,'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_MAKE_PROGRAM='+ninja,'-DFORGE_NATIVE_SDK='+str(stage)],check=True)
subprocess.run([cmake,'--build',str(client),'--parallel',build_jobs],check=True)
windows=os.name=='nt';exe='.exe' if windows else '';ext='.dll' if windows else '.so'
runtime=stage/'bin'/('forge_runtime'+exe)
module=client/('gameplay'+ext)
# Imports must refer to the shared Flecs library; no private exported Flecs implementation.
if windows:
    imports=subprocess.check_output(['dumpbin','/imports',str(module)],text=True)
    exports=subprocess.check_output(['dumpbin','/exports',str(module)],text=True)
    assert 'flecs.dll' in imports.lower() and 'ecs_init' in imports
    assert ' ecs_init' not in exports
else:
    imports=subprocess.check_output(['readelf','-d',str(module)],text=True)
    exports=subprocess.check_output(['nm','-D','--defined-only',str(module)],text=True)
    assert 'libflecs.so.4' in imports and ' ecs_init' not in exports
    hostdeps=subprocess.check_output(['readelf','-d',str(runtime)],text=True)
    assert 'libflecs.so.4' in hostdeps and '$ORIGIN' in hostdeps
project=stage/'project';project.mkdir();shutil.copy2(module,project/module.name)
# Relocate after building: no build-tree or old install-path dependence for execution.
relocated=build/'sdk-relocated-test'
if relocated.exists():shutil.rmtree(relocated)
stage.rename(relocated);stage=relocated;project=stage/'project';runtime=stage/'bin'/('forge_runtime'+exe)
env=os.environ.copy();env.pop('LD_LIBRARY_PATH',None);env.pop('LD_PRELOAD',None)
env['PATH']=str(Path(env.get('SystemRoot','C:/Windows'))/'System32') if windows else '/usr/bin:/bin'
env['FORGE_SDK_TRACE']=str(stage/'trace.txt')
env['FORGE_SDK_SPAWN_TEST']='1'
info=json.loads(subprocess.check_output([str(runtime),'--sdk-info'],env=env,cwd=stage,text=True))
assert info['profile']=='shared-native-sdk'
action='12345678-1234-4234-8234-123456789abc'
manifest=dict(version=2,name='SDK test',startup_scene=None,simulation_hz=120,input=dict(version=1,actions=[dict(id=action,name='Probe',kind='digital',bindings=[dict(control='key.space')])]),modules=[dict(id='project.sdk_probe',sdk='experimental-1',implementation='1',fingerprint=info['fingerprint'],library=module.name,dependencies=['forge.input','forge.transforms'])])
(project/'forge.project.json').write_text(json.dumps(manifest))
inspected=json.loads(subprocess.check_output([str(runtime),'--inspect-sdk',str(project)],env=env,cwd=stage,text=True))
assert inspected['fingerprint']==info['fingerprint'] and inspected['components'][0]['id']=='project.health',inspected
# Inspection closes its own module/world; keep the runtime lifetime trace separate.
(stage/'trace.txt').unlink()
p=subprocess.Popen([str(runtime),'--sdk-project',str(project)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=env,cwd=stage)
session='';seq=0
def request(command,**extra):
 global session,seq
 seq+=1;p.stdin.write(json.dumps(dict(protocol=2,id=seq,session=session,command=command,**extra))+'\n');p.stdin.flush()
 line=p.stdout.readline();assert line,p.stderr.read()
 r=json.loads(line);assert r['ok'],r
 if command=='hello':session=r['session']
 return r
try:
 r=request('hello');assert r['timing']['simulation_hz']==120
 request('step',input_events=[dict(control='key.space',value=1)])
 r=request('step')
 # The installed module creates registered runtime content and assigns native
 # MeshRenderer/TRS without private engine headers or Diligent ownership.
 r=request('snapshot')
 meshes=[e for e in r['scene']['entities'] if e['name']=='SDK runtime mesh']
 assert len(meshes)==1 and 'forge.mesh_renderer' in meshes[0]['components'],r
 assert meshes[0]['components']['forge.local_translation']==dict(x=4,y=5,z=6),r
 request('quit');assert p.wait(timeout=10)==0
finally:
 if p.poll() is None:p.kill();p.wait()
trace=(stage/'trace.txt').read_text();assert trace.count('tick\n')==2 and trace.endswith('unload\n'),trace
print('Installed SDK client, PE/ELF shared dependency, relocated runtime, fixed ticks and unload verified')
subprocess.run([sys.executable,str(Path(__file__).with_name('sdk_character_2d_test.py')),
                str(runtime),str(client/('character_2d_gameplay'+ext)),
                str(client/('character_2d_denied_gameplay'+ext))],
               env=env,cwd=stage,check=True,timeout=120)
# Prove the installed audio headers and callback boundary from the relocated host too.
subprocess.run([sys.executable,str(Path(__file__).with_name('sdk_physics_2d_joint_test.py')),
                str(runtime),str(client/('physics_2d_joint_gameplay'+ext))],
               env=env,cwd=stage,check=True,timeout=120)
subprocess.run([sys.executable,str(Path(__file__).with_name('audio_process_test.py')),str(runtime),str(client/('audio_gameplay'+ext))],env=env,cwd=stage,check=True,timeout=40)
# Compile and load the installed Animator component across the same shared Flecs boundary.
subprocess.run([sys.executable,str(Path(__file__).with_name('animation_process_test.py')),str(runtime),str(build/('forge_animation_tests'+exe)),str(build/'tools'/('gltf2ozz'+exe)),str(Path(__file__).resolve().parents[1]/'samples/animation/two-joints.gltf'),str(client/('animation_gameplay'+ext))],env=env,cwd=stage,check=True,timeout=60)
subprocess.run([sys.executable,str(Path(__file__).with_name('material_animation_process_test.py')),
                str(runtime),str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'),
                str(client/('material_animation_gameplay'+ext)),str(build/'installed-material-animation')],
               env=env,cwd=stage,check=True,timeout=120)
subprocess.run([sys.executable,str(Path(__file__).with_name('sprite_image_process_test.py')),
                str(runtime),str(client/('sprite_image_gameplay'+ext)),str(build/'installed-sprite-images')],
               env=env,cwd=stage,check=True,timeout=60)
subprocess.run([sys.executable,str(Path(__file__).with_name('animation_sequence_process_test.py')),
                str(runtime),str(Path(__file__).resolve().parents[1]/'samples/projects/FeatureGallery'),
                str(client/('sequence_gameplay'+ext))],env=env,cwd=stage,check=True,timeout=120)
# Ordinary gameplay starter: installed-only headers, admitted data and fixed system.
# No internal probe behavior, private headers, graphics device or second Flecs.
starter = build / 'sdk-starter-client-test'
subprocess.run([cmake,'-S',str(stage/'sdk/template'),'-B',str(starter),'-G','Ninja',compiler_arg,
                '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_MAKE_PROGRAM='+ninja,
                '-DForgeNativeSdk_DIR='+str(stage/'sdk')],check=True)
subprocess.run([cmake,'--build',str(starter),'--parallel',build_jobs],check=True)
starter_project = stage / 'starter-project'; starter_project.mkdir()
shutil.copy2(starter/('gameplay'+ext),starter_project/('gameplay'+ext))
starter_manifest = dict(manifest)
starter_manifest['modules'] = [dict(id='project.gameplay',sdk='experimental-1',implementation='1',
    fingerprint=info['fingerprint'],library='gameplay'+ext,dependencies=[])]
(starter_project/'forge.project.json').write_text(json.dumps(starter_manifest))
starter_schema=json.loads(subprocess.check_output([str(runtime),'--inspect-sdk',str(starter_project)],
                                                 env=env,cwd=stage,text=True))
assert [c['id'] for c in starter_schema['components']] == ['project.counter'],starter_schema
if windows:
    starter_kits=build/'sdk-starter-kits-test'
    if starter_kits.exists(): shutil.rmtree(starter_kits)
    subprocess.run([cmake,'--install',str(starter),'--component','GameplayRuntime','--prefix',str(starter_kits)],check=True)
    metadata=json.loads((starter_kits/'project.gameplay/forge.module-kit.json').read_text())
    assert metadata['fingerprint']==info['fingerprint'] and metadata['library']=='gameplay.dll'
shutil.rmtree(starter_project)
print('Installed gameplay starter, isolated schema admission and Windows deployment kit verified')
# Retain a clean install for CI artifact, without the test project and trace.
(stage/'trace.txt').unlink();shutil.rmtree(project)

# Preserve executable modes and Linux SONAME symlinks through artifact transport.
subprocess.run([sys.executable,str(Path(__file__).with_name('ui_process_test.py')),str(runtime),str(client/('ui_gameplay'+ext))],env=env,cwd=stage,check=True,timeout=40)
subprocess.run([sys.executable,str(Path(__file__).with_name('phase6_integration_test.py')),str(runtime),str(build/('forge_navigation_tests'+exe)),str(build/('forge_nav_build'+exe)),str(build/('forge_animation_tests'+exe)),str(build/'tools'/('gltf2ozz'+exe)),str(Path(__file__).resolve().parents[1]/'samples/animation/two-joints.gltf'),str(client/('combined_gameplay'+ext))],env=env,cwd=stage,check=True,timeout=90)
subprocess.run([sys.executable,str(Path(__file__).with_name('navigation_process_test.py')),
                str(runtime),str(build/('forge_navigation_tests'+exe)),str(build/('forge_nav_build'+exe)),
                str(client/('planar_navigation_gameplay'+ext)),'--planar'],env=env,cwd=stage,check=True,timeout=90)
# Every regular delivered SDK file is hashed; retain symlink identities separately.
files={p.relative_to(stage).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in stage.rglob('*') if p.is_file() and not p.is_symlink()}
links={p.relative_to(stage).as_posix():os.readlink(p) for p in stage.rglob('*') if p.is_symlink()}
for path in links:
 assert (stage/path).resolve().is_relative_to(stage.resolve()), 'SDK symlink escapes package'
(stage/'sdk-manifest.json').write_text(json.dumps(dict(version=1,build=json.loads((stage/'build.json').read_text()),files=files,symlinks=links),indent=2)+'\n')
shutil.make_archive(str(build/'experimental-native-sdk'), 'gztar', root_dir=stage)
