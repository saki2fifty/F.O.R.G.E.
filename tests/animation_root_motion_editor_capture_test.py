"""Native root mode and durable-joint Inspector controls at actual UI scales."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile

fixture, gallery, evidence = (Path(p).resolve() for p in sys.argv[1:4])
evidence.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=evidence.parent, prefix='root-motion-capture-') as scratch:
    project = Path(scratch)/'Gallery'
    shutil.copytree(gallery, project)
    try:
        result = subprocess.run([fixture, evidence, '--root-motion', project],
                                capture_output=True, text=True, encoding='utf-8', timeout=150)
    except subprocess.TimeoutExpired as error:
        partial = ''.join(value.decode('utf-8', errors='replace') if isinstance(value, bytes)
                          else value or '' for value in (error.stdout, error.stderr))
        (evidence/'capture.log').write_text(partial, encoding='utf-8')
        raise
    (evidence/'capture.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert 'Diligent Engine: ERROR:' not in result.stdout+result.stderr
    for stage, label in enumerate(('animation-root-motion', 'animation-root-motion-150', 'animation-root-motion-200',
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
                  'animation-state-graph-150', 'animation-state-graph-200'), 250):
        image = evidence/f'editor-{label}.ppm'
        assert image.is_file() and image.stat().st_size > 1920*1080, image
        metrics = json.loads((evidence/f'chrome-{stage}.json').read_text())
        assert metrics['controls_ready'] and metrics['document_in_front'], (label, metrics)
        if stage <= 273 or (stage >= 282 and stage not in (285, 289)):
            assert metrics['capture_settling'] == 'stable_panel' and metrics['stable_control_frames'] >= 3, (label, metrics)
        elif 274 <= stage <= 281:
            assert metrics['capture_settling'] == 'stable_marker' and metrics['stable_control_frames'] >= 3 and metrics['marker_visible'], (label, metrics)
        else:
            assert metrics['capture_settling'] == 'scene_game' and metrics['fixture_frames'] > 12, (label, metrics)
print('Native animation controls, target guides, bone-mask, blend-space and state-graph documents captured at100/150/200%; Game markers, weighted masks and numeric blending at100%')
