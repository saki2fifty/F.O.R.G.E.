"""Draw the actual authored Inspector before any new Gallery import work."""
from pathlib import Path
import subprocess
import sys
import tempfile

from gallery_project_fixture import copy_capture_project

with tempfile.TemporaryDirectory(prefix='forge-root-inspector-') as temporary:
    project = copy_capture_project(sys.argv[2], Path(temporary) / 'Gallery', 'animation_root_motion')
    subprocess.run([sys.argv[1], project], check=True, timeout=30)
