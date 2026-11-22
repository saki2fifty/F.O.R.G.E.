"""Publish the editable alpha example and draw its relocated source-free Game."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('fixture', 'gallery', 'renderer', 'scratch'):
    parser.add_argument(name, type=Path)
parser.add_argument('--vulkan', action='store_true')
parser.add_argument('--opacity', action='store_true')
parser.add_argument('--trails', action='store_true')
parser.add_argument('--collision', action='store_true')
parser.add_argument('--scene-collision', action='store_true')
parser.add_argument('--xy-scene-collision', action='store_true')
parser.add_argument('--world-trails', action='store_true')
parser.add_argument('--xy-world-trails', action='store_true')
parser.add_argument('--gpu-collision', action='store_true')
parser.add_argument('--gpu-trails', action='store_true')
parser.add_argument('--gpu-sorted', action='store_true')
parser.add_argument('--countercases', action='store_true')
parser.add_argument('--gpu-opacity', action='store_true')
parser.add_argument('--gpu-graph-opacity', action='store_true')
parser.add_argument('--weighted', action='store_true')
parser.add_argument('--weighted-graph', action='store_true')
parser.add_argument('--billboards', action='store_true')
parser.add_argument('--gpu-billboards', action='store_true')
parser.add_argument('--gpu-graph-billboards', action='store_true')
parser.add_argument('--worker', type=Path)
parser.add_argument('--graph-fixture', type=Path)
arguments = parser.parse_args()
assert not arguments.countercases or arguments.gpu_sorted, 'Countercases require GPU sorted mode'
fixture, gallery, renderer, scratch = (getattr(arguments, name).resolve()
    for name in ('fixture', 'gallery', 'renderer', 'scratch'))
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='particle-alpha-', dir=scratch) as temporary:
    billboards = (arguments.billboards or arguments.gpu_billboards or arguments.gpu_graph_billboards
                  or arguments.weighted or arguments.weighted_graph
                  or arguments.gpu_opacity or arguments.gpu_graph_opacity or arguments.gpu_sorted
                  or arguments.scene_collision or arguments.xy_scene_collision)
    action = ('--verify-export-xy-world-trails' if arguments.xy_world_trails else
              '--verify-export-world-trails' if arguments.world_trails else
              '--verify-export-xy-scene-collision' if arguments.xy_scene_collision else
              '--verify-export-scene-collision' if arguments.scene_collision else
              '--verify-export-gpu-sorted-countercases' if arguments.countercases else
              '--verify-export-gpu-sorted' if arguments.gpu_sorted else
              '--verify-export-gpu-trails' if arguments.gpu_trails else
              '--verify-export-gpu-collision' if arguments.gpu_collision else
              '--verify-export-gpu-graph-opacity' if arguments.gpu_graph_opacity else
              '--verify-export-gpu-opacity' if arguments.gpu_opacity else
              '--verify-export-weighted-graph' if arguments.weighted_graph else
              '--verify-export-weighted' if arguments.weighted else
              '--verify-export-collision' if arguments.collision else
              '--verify-export-trails' if arguments.trails else
              '--verify-export-gpu-graph-billboards' if arguments.gpu_graph_billboards else
              '--verify-export-gpu-billboards' if arguments.gpu_billboards else
              '--verify-export-billboards' if arguments.billboards else
              '--verify-export-opacity' if arguments.opacity else '--verify-export')
    worker = [str(arguments.worker.resolve())] if billboards and arguments.worker else []
    assert not billboards or worker, 'Camera-plane texture requires its asset worker'
    if (arguments.gpu_graph_billboards or arguments.weighted_graph or arguments.gpu_graph_opacity
            or (arguments.countercases and not arguments.vulkan)):
        assert not arguments.vulkan and arguments.graph_fixture, 'Program cook needs native Windows'
        source = Path(temporary) / 'graph-source'
        shutil.copytree(gallery, source)
        subprocess.run([str(arguments.graph_fixture.resolve()), '--prepare', str(source), *worker],
                       check=True, timeout=900)
        gallery = source
    subprocess.run([str(fixture), action, str(gallery), temporary, *worker],
                   check=True, timeout=120)
    content = list(Path(temporary).glob('*/relocated-content'))
    assert len(content) == 1, content
    mode = (('--opacity-gallery' if arguments.opacity else '--alpha-gallery') if arguments.vulkan
            else ('particle-opacity-gallery' if arguments.opacity else 'particle-alpha-gallery'))
    if arguments.xy_world_trails:
        mode = '--xy-world-trail-gallery' if arguments.vulkan else 'particle-xy-world-trail-gallery'
    elif arguments.world_trails:
        mode = '--world-trail-gallery' if arguments.vulkan else 'particle-world-trail-gallery'
    elif arguments.xy_scene_collision:
        mode = '--xy-scene-collision-gallery' if arguments.vulkan else 'particle-xy-scene-collision-gallery'
    elif arguments.scene_collision:
        mode = '--scene-collision-gallery' if arguments.vulkan else 'particle-scene-collision-gallery'
    elif arguments.countercases:
        mode = '--gpu-sorted-countercases' if arguments.vulkan else 'particle-gpu-sorted-countercases'
    elif arguments.gpu_sorted:
        mode = '--gpu-sorted-gallery' if arguments.vulkan else 'particle-gpu-sorted-gallery'
    elif arguments.gpu_trails:
        mode = '--gpu-trail-gallery' if arguments.vulkan else 'particle-gpu-trail-gallery'
    elif arguments.gpu_collision:
        mode = '--gpu-collision-gallery' if arguments.vulkan else 'particle-gpu-collision-gallery'
    elif arguments.gpu_graph_opacity:
        mode = 'particle-gpu-graph-opacity-gallery'
    elif arguments.gpu_opacity:
        mode = '--gpu-opacity-gallery' if arguments.vulkan else 'particle-gpu-opacity-gallery'
    elif arguments.weighted_graph:
        mode = 'particle-weighted-gpu-graph-gallery'
    elif arguments.weighted:
        mode = '--weighted-gallery' if arguments.vulkan else 'particle-weighted-gpu-gallery'
    elif arguments.collision:
        mode = '--collision-gallery' if arguments.vulkan else 'particle-collision-gallery'
    elif arguments.trails:
        mode = '--trail-gallery' if arguments.vulkan else 'particle-trail-gallery'
    elif arguments.gpu_graph_billboards:
        mode = 'particle-gpu-graph-billboard-gallery'
    elif arguments.gpu_billboards:
        mode = '--gpu-billboard-gallery' if arguments.vulkan else 'particle-gpu-billboard-gallery'
    elif arguments.billboards:
        mode = '--billboard-gallery' if arguments.vulkan else 'particle-billboard-gallery'
    subprocess.run([str(renderer), str(scratch / 'native'), mode, str(content[0])],
                   check=True, timeout=150)
