# Original UI font fixture

`FORGE-Weight-Test.ttf` and `generate_weight_test.py` are independently authored
FORGE test data and generator, included for this sample and FORGE verification.
No outside font outlines or font-writer implementation are included.

The font contains only an original geometric `H`, a space and an empty missing
glyph. Its `wght` axis spans100–900 with default400. Named font records identify
100/400/900; ordinary CSS weight selection also tests interpolation. It is a
focused test font, not a general game UI typeface.

Run `python3 tests/fixtures/fonts/generate_weight_test.py` to reproduce its exact
bytes. The writer uses only the Python standard library and the primary
[OpenType table contracts](https://learn.microsoft.com/en-us/typography/opentype/spec/).
Its native raster and shaping behavior is checked separately from its binary
checksums. Glyph advances stay600design units while the outline weight changes.
