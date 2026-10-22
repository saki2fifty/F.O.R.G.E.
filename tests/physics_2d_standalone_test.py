"""Export Gallery75/76/77 through the production kit and launch relocated games."""
import argparse
import json
from pathlib import Path
from gallery_standalone_export import verify_scene


def verify(tools, gallery, kit, evidence, retain=None, connected=False, painted=False):
    source = ('Scenes/77-painted-2d-collision.scene.json' if painted else
              'Scenes/76-connected-2d-bodies.scene.json' if connected else
              'Scenes/75-2d-physics.scene.json')
    receipt = verify_scene(tools, gallery, kit, evidence, source, retain)
    (Path(evidence) / 'physics-2d-export.json').write_text(
        json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('tools', 'gallery', 'kit', 'evidence'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--retain', type=Path)
    examples = parser.add_mutually_exclusive_group()
    examples.add_argument('--connected', action='store_true', help='Export connected-body Gallery76')
    examples.add_argument('--painted', action='store_true', help='Export painted-collision Gallery77')
    args = parser.parse_args()
    verify(args.tools, args.gallery, args.kit, args.evidence, args.retain, args.connected, args.painted)
    print('Shared 2D physics Gallery export and two source-free production startups passed')
