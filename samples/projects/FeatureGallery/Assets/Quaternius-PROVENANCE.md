# Quaternius locomotion source

Creator: [Quaternius](https://quaternius.com/packs/universalanimationlibrary.html).
Official free [Universal Animation Library Standard](https://quaternius.itch.io/universal-animation-library),
itch upload 17958403, obtained 2026-10-11. Bundled license is CC0 1.0 Universal;
see `Quaternius-LICENSE.txt` and https://creativecommons.org/publicdomain/zero/1.0/.
The bundled notice preserves the original text with normalized line endings and
trailing whitespace. This asset may be used commercially. No Blender add-on or paid pack is distributed.

Original Standard archive SHA256:
`cc73fc4e495b82958207316596317a3f40b9fa38065bde1027937452da537724`.
Source member: `Unreal-Godot/UAL1_Standard.glb`, with movement kept in place.
The `_RM` root-motion variant is not used.

`QuaterniusLocomotion.glb` retains the original mesh, skin, rest hierarchy and
Idle_Loop, Sprint_Loop and Walk_Loop animation channels. Unused animation accessors,
buffer views and bytes were removed; surviving offsets were repacked. It is not
retargeted or altered by a Blender add-on. Derived source SHA256:
`4d18f077d349e8939dc5526f75301800f4b6c2e147cd90f3978a9341398918d4`.

The ordinary FORGE model importer published the Windows/D3D12/desktop bundle.
`Locomotion.states.json` maps state 1 to Idle_Loop, 2 to Walk_Loop and 3 to
Sprint_Loop (displayed as Run). Scene 10 instantiates three ordinary model roots
with distinct stable EntityIds and these state selections. Runtime export follows
all three cooked clip dependencies; it does not require the source GLB.
