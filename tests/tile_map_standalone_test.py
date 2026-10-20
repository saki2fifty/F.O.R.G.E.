"""Export Gallery74 through the real shared kit and launch its relocated game."""
import argparse
import json
from pathlib import Path
from gallery_standalone_export import verify_scene


def verify(tools, gallery, kit, evidence, retain=None):
    scene_source = 'Scenes/74-tile-maps.scene.json'
    scene = json.loads((Path(gallery) / scene_source).read_text(encoding='utf-8'))
    maps = [row['components']['forge.tile_map_renderer'] for row in scene['entities']
            if 'forge.tile_map_renderer' in row['components']]
    assert len(maps) == 2 and maps[0]['map'] == maps[1]['map']
    receipt = verify_scene(tools, gallery, kit, evidence, scene_source, retain)
    (Path(evidence) / 'tile-map-export.json').write_text(
        json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('tools', 'gallery', 'kit', 'evidence'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--retain', type=Path)
    args = parser.parse_args()
    verify(args.tools, args.gallery, args.kit, args.evidence, args.retain)
    print('Shared Tile Map Gallery export and two source-free production startups passed')
