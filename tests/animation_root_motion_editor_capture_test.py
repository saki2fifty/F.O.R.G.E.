"""Native root mode and durable-joint Inspector controls at actual UI scales."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fixture, gallery, evidence = (Path(p).resolve() for p in sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='root-motion-capture-') as scratch:
    project = Path(scratch)/'Gallery'
    shutil.copytree(gallery, project)
    result = subprocess.run([fixture, evidence, '--root-motion', project],
                            capture_output=True, text=True, encoding='utf-8', timeout=150)
    (evidence/'capture.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout+result.stderr
    for label in ('animation-root-motion', 'animation-root-motion-150', 'animation-root-motion-200',
                  'live-animation', 'live-animation-150', 'live-animation-200',
                  'animation-subtree', 'animation-subtree-150', 'animation-subtree-200',
                  'animation-overlay', 'animation-overlay-150', 'animation-overlay-200',
                  'two-bone-ik', 'two-bone-ik-150', 'two-bone-ik-200',
                  'joint-aim', 'joint-aim-150', 'joint-aim-200',
                  'live-two-bone-ik', 'live-two-bone-ik-150', 'live-two-bone-ik-200',
                  'live-joint-aim', 'live-joint-aim-150', 'live-joint-aim-200',
                  'reach-target', 'reach-target-150', 'reach-target-200',
                  'aim-target', 'aim-target-150', 'aim-target-200',
                  'live-reach-target', 'live-aim-target',
                  'animation-bone-mask', 'animation-bone-mask-150', 'animation-bone-mask-200',
                  'weighted-mask-game', 'animation-blend-space', 'animation-blend-space-150',
                  'animation-blend-space-200', 'blend-space-game', 'animation-state-graph',
                  'animation-state-graph-150', 'animation-state-graph-200'):
        image = evidence/f'editor-{label}.ppm'
        assert image.is_file() and image.stat().st_size > 1920*1080, image
print('Native animation controls, target guides, bone-mask, blend-space and state-graph documents captured at100/150/200%; Game markers, weighted masks and numeric blending at100%')
