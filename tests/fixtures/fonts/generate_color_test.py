"""Original geometric COLRv0 fixture; reuses FORGE's own sfnt fixture writer."""
from pathlib import Path
import hashlib
import importlib.util
import struct
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('forge_weight_fixture', Path(__file__).with_name('generate_weight_test.py'))
writer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(writer)
pack = writer.pack


def contour(points):
    result = pack('5h2H', 1, min(p[0] for p in points), min(p[1] for p in points),
                  max(p[0] for p in points), max(p[1] for p in points), len(points)-1, 0)
    result += bytes([1] * len(points))
    for axis in (0, 1):
        previous = 0
        for p in points:
            result += pack('h', p[axis] - previous)
            previous = p[axis]
    return result + bytes(len(result) % 2)


def build(foreground=False, wide=False):
    original = writer.build()
    tables = {}
    for i in range(struct.unpack_from('>H', original, 4)[0]):
        tag, _, offset, size = struct.unpack_from('>4sIII', original, 12+16*i)
        if tag not in (b'fvar', b'gvar', b'STAT'):
            tables[tag] = original[offset:offset+size]
    # Reset the previous whole-font checksum adjustment before reassembly.
    head = bytearray(tables[b'head'])
    head[8:12] = bytes(4)
    tables[b'head'] = bytes(head)
    glyphs = [b'', tables[b'glyf'], b'',
              contour([(80,0),(180,0),(180,700),(80,700)]),
              contour([(420,0),(520,0),(520,700),(420,700)]),
              contour([(160,260),(440,260),(440,360),(160,360)])]
    if wide:
        # Empty base outline; three overlapping wide layers span atlas pages at512px.
        rectangle = contour([(80,0),(520,0),(520,700),(80,700)])
        glyphs = [b'', b'', b'', rectangle, rectangle, rectangle]
    offsets = [0]
    for glyph in glyphs:
        offsets.append(offsets[-1] + len(glyph))
    tables[b'glyf'] = b''.join(glyphs)
    tables[b'loca'] = pack('7H', *(n//2 for n in offsets))
    tables[b'hmtx'] = pack('HhHhHhHhHhHh',600,0,600,80,300,0,600,80,600,420,600,160)
    if wide:
        tables[b'hmtx'] = pack('HhHhHhHhHhHh',600,0,600,0,300,0,600,80,600,80,600,80)
    for tag, offset in [(b'maxp',4),(b'hhea',34)]:
        data = bytearray(tables[tag])
        struct.pack_into('>H',data,offset,6)
        tables[tag] = bytes(data)
    tables[b'COLR'] = pack('HHIIH',0,1,14,20,3) + pack('3H',1,0,3)
    tables[b'COLR'] += pack('6H',3,0,4,1,5,65535 if foreground else 2)
    if wide:
        tables[b'COLR'] = pack('HHIIH',0,1,14,20,3) + pack('3H',1,0,3) + pack('6H',3,0,5,2,4,1)
    tables[b'CPAL'] = pack('4HIH',0,3,1,3,14,0)
    tables[b'CPAL'] += bytes([40,60,240,255,230,150,40,180,100,190,30,255])
    names = {1:'FORGE Color Test',2:'Regular',4:'FORGE Color Test Regular',
             6:'FORGEColorTest-Regular',16:'FORGE Color Test',17:'Regular'}
    records = storage = b''
    for key, name in sorted(names.items()):
        data = name.encode('utf-16-be')
        records += pack('6H',3,1,0x409,key,len(data),len(storage))
        storage += data
    tables[b'name'] = pack('3H',0,len(names),6+len(records))+records+storage
    return writer.assemble(tables)


if __name__ == '__main__':
    assert hashlib.sha256(writer.build()).hexdigest() == 'e51dc0fb7ed741a0840f1d187c1ce4a5db6fe338f4764fcfdd3221a6b0f0d6cd'
    for suffix, foreground, wide in (('',False,False),('-Foreground',True,False),('-Wide',False,True)):
        output = Path(__file__).with_name('FORGE-Color'+suffix+'-Test.ttf')
        data = build(foreground, wide)
        output.write_bytes(data)
        print(output.name,len(data),hashlib.sha256(data).hexdigest())
