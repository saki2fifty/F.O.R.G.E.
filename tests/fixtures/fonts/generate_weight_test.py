"""Original geometric test font; no external font outlines or writer code.

Writes only space and a geometric H, with one weight axis. Binary layouts follow
the primary OpenType sfnt/glyf/fvar/gvar/STAT specifications. No font-tool package
or product dependency is required. This is a bounded test fixture writer.
"""
from pathlib import Path
import hashlib
import struct


def pack(fmt, *values):
    return struct.pack('>' + fmt, *values)


def checksum(data):
    data += bytes((-len(data)) % 4)
    return sum(struct.unpack('>' + 'I' * (len(data) // 4), data)) & 0xffffffff


def build():
    tables = {}
    # A single original twelve-point H contour in a 1000-unit em square.
    points = [(80, 0), (180, 0), (180, 260), (420, 260), (420, 0), (520, 0),
              (520, 700), (420, 700), (420, 360), (180, 360), (180, 700), (80, 700)]
    glyph = pack('5h2H', 1, 80, 0, 520, 700, 11, 0) + bytes([1] * len(points))
    for axis in (0, 1):
        previous = 0
        for point in points:
            glyph += pack('h', point[axis] - previous)
            previous = point[axis]
    glyph += bytes(len(glyph) % 2)
    tables[b'glyf'] = glyph
    # Glyph0 empty .notdef, glyph1 H, glyph2 empty space.
    tables[b'loca'] = pack('4H', 0, 0, len(glyph) // 2, len(glyph) // 2)
    tables[b'hmtx'] = pack('HhHhHh', 600, 0, 600, 80, 300, 0)
    tables[b'maxp'] = pack('I15H', 0x00010000, 3, 12, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    tables[b'head'] = pack('4I2H2Q4h3H2h', 0x00010000, 0x00010000, 0, 0x5f0f3cf5,
                            3, 1000, 0, 0, 80, 0, 520, 700, 0, 8, 2, 0, 0)
    tables[b'hhea'] = pack('I3hH3h3h4h2H', 0x00010000, 800, -200, 0, 600, 0, 80,
                            520, 1, 0, 0, 0, 0, 0, 0, 0, 3)
    tables[b'post'] = pack('Iihh5I', 0x00030000, 0, -75, 50, 0, 0, 0, 0, 0)
    os2 = bytearray(78)
    struct.pack_into('>HhHHH', os2, 0, 0, 600, 400, 5, 0)
    struct.pack_into('>I', os2, 42, 1)  # Basic Latin.
    os2[58:62] = b'FRGE'
    struct.pack_into('>HHHhhhHH', os2, 62, 0x40, 32, 72, 800, -200, 0, 800, 200)
    tables[b'OS/2'] = bytes(os2)
    # Format4 Unicode map: space ->2, H ->1, sentinel ->0.
    cmap = pack('7H', 4, 40, 0, 6, 4, 1, 2)
    cmap += pack('3HH3H3h3H', 32, 72, 65535, 0, 32, 72, 65535, -30, -71, 1, 0, 0, 0)
    assert len(cmap) == 40
    tables[b'cmap'] = pack('HHHHI', 0, 1, 3, 1, 12) + cmap
    names = {1: 'FORGE Weight Test', 2: 'Regular', 4: 'FORGE Weight Test Regular',
             6: 'FORGEWeightTest-Regular', 16: 'FORGE Weight Test', 17: 'Regular',
             256: 'Weight', 257: 'Light', 258: 'Regular', 259: 'Bold'}
    records = storage = b''
    for key, value in sorted(names.items()):
        encoded = value.encode('utf-16-be')
        records += pack('6H', 3, 1, 0x409, key, len(encoded), len(storage))
        storage += encoded
    tables[b'name'] = pack('3H', 0, len(names), 6 + len(records)) + records + storage
    tables[b'fvar'] = pack('8H', 1, 0, 16, 2, 1, 20, 3, 8)
    tables[b'fvar'] += b'wght' + pack('3i2H', 100 << 16, 400 << 16, 900 << 16, 0, 256)
    for label, weight in ((257, 100), (258, 400), (259, 900)):
        tables[b'fvar'] += pack('HHi', label, 0, weight << 16)
    # Embedded peaks with private all-point selection; no intermediate regions.
    # Four phantom-point deltas are zero, retaining the advance width.
    x_signs = [-1, 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1]
    y_signs = [0, 0, -1, -1, 0, 0, 0, 0, 1, 1, 0, 0]
    headers = payload = b''
    for peak, scale in ((16384, 50), (-16384, -25)):
        data = b'\0'  # Private packed point count0 means every point.
        for signs in (x_signs, y_signs):
            deltas = [scale * value for value in signs] + [0] * 4
            data += bytes([len(deltas) - 1]) + pack('16b', *deltas)
        headers += pack('HHh', len(data), 0xa000, peak)
        payload += data
    variation = pack('2H', 2, 4 + len(headers)) + headers + payload
    tables[b'gvar'] = pack('4HI2HI', 1, 0, 1, 0, 36, 3, 1, 36)
    tables[b'gvar'] += pack('4I', 0, 0, len(variation), len(variation)) + variation
    # One weight axis and three named STAT values; Regular is elidable.
    stat_header = pack('4HIHIH', 1, 1, 8, 1, 20, 3, 28, 258)
    assert len(stat_header) == 20
    stat_values = b''.join(pack('4Hi', 1, 0, 2 if weight == 400 else 0, label, weight << 16)
                           for label, weight in ((257, 100), (258, 400), (259, 900)))
    tables[b'STAT'] = stat_header + b'wght' + pack('2H', 256, 0) + pack('3H', 6, 18, 30) + stat_values
    count = len(tables)
    power = 1 << (count.bit_length() - 1)
    result = bytearray(pack('I4H', 0x00010000, count, power * 16, count.bit_length() - 1,
                            count * 16 - power * 16) + bytes(count * 16))
    positions = {}
    for i, (tag, data) in enumerate(sorted(tables.items())):
        positions[tag] = len(result)
        struct.pack_into('>4sIII', result, 12 + 16 * i, tag, checksum(data), len(result), len(data))
        result += data + bytes((-len(data)) % 4)
    struct.pack_into('>I', result, positions[b'head'] + 8, (0xb1b0afba - checksum(result)) & 0xffffffff)
    assert checksum(result) == 0xb1b0afba
    return bytes(result)


if __name__ == '__main__':
    result = build()
    target = Path(__file__).with_name('FORGE-Weight-Test.ttf')
    target.write_bytes(result)
    print(target.name, len(result), hashlib.sha256(result).hexdigest())
