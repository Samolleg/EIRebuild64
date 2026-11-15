"""Blender scene helpers and GLB serialization; runtime acceptance is in ModelCheck."""
import json
import math
import struct
from pathlib import Path
from mathutils import Matrix, Quaternion, Vector


class Diagnostics:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def error(self, where, problem, fix):
        self.errors.append(f'{where}: {problem}. Fix: {fix}')

    def warning(self, where, problem):
        self.warnings.append(f'{where}: {problem}')

    def show(self):
        for title, entries in [('ERRORS', self.errors), ('WARNINGS', self.warnings)]:
            print(f'{title} ({len(entries)})', flush=True)
            for entry in entries:
                print('  ' + entry, flush=True)


def matrix(node):
    if 'matrix' in node:
        return Matrix([node['matrix'][i:i+4] for i in range(0, 16, 4)]).transposed()
    q = node.get('rotation', [0, 0, 0, 1])
    return Matrix.LocRotScale(Vector(node.get('translation', [0, 0, 0])),
                               Quaternion((q[3], *q[:3])),
                               Vector(node.get('scale', [1, 1, 1])))


def close(a, b, tolerance):
    return all(math.isfinite(a[r][c]) and abs(a[r][c]-b[r][c]) <= tolerance
               for r in range(4) for c in range(4))


def read_glb(path):
    raw = Path(path).read_bytes()
    if len(raw) < 20 or struct.unpack_from('<4sII', raw) != (b'glTF', 2, len(raw)):
        raise ValueError('invalid GLB 2 header/length')
    chunks = []
    offset = 12
    while offset < len(raw):
        length, kind = struct.unpack_from('<II', raw, offset)
        offset += 8
        if length % 4 or offset + length > len(raw):
            raise ValueError('invalid GLB chunk length')
        chunks.append((kind, raw[offset:offset+length]))
        offset += length
    if not chunks or chunks[0][0] != 0x4e4f534a or len(chunks) > 2:
        raise ValueError('expected JSON then optional BIN')
    if len(chunks) == 2 and chunks[1][0] != 0x004e4942:
        raise ValueError('expected BIN chunk')
    return json.loads(chunks[0][1]), chunks[1][1] if len(chunks) > 1 else b''


def write_glb(path, doc, binary):
    data = json.dumps(doc, ensure_ascii=True, separators=(',', ':'), allow_nan=False).encode()
    data += b' ' * (-len(data) % 4)
    binary += b'\0' * (-len(binary) % 4)
    chunks = struct.pack('<II', len(data), 0x4e4f534a) + data
    if binary:
        chunks += struct.pack('<II', len(binary), 0x004e4942) + binary
    Path(path).write_bytes(struct.pack('<4sII', b'glTF', 2, 12+len(chunks)) + chunks)


class Accessors:
    def __init__(self, doc, binary):
        self.doc, self.binary = doc, binary

    def view(self, index):
        view = self.doc['bufferViews'][index]
        if view.get('buffer', 0) != 0:
            raise ValueError('external buffer')
        start = view.get('byteOffset', 0)
        end = start + view['byteLength']
        if start < 0 or end > len(self.binary):
            raise ValueError('bufferView outside BIN')
        return self.binary[start:end], view.get('byteStride')

    def get(self, index):
        a = self.doc['accessors'][index]
        formats = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
        counts = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
        code, count = formats[a['componentType']], counts[a['type']]
        fmt = '<' + code * count
        size = struct.calcsize(fmt)
        values = [[0] * count for _ in range(a['count'])]
        if 'bufferView' in a:
            data, stride = self.view(a['bufferView'])
            stride = stride or size
            if stride < size:
                raise ValueError('accessor stride smaller than element')
            values = [list(struct.unpack_from(fmt, data, a.get('byteOffset', 0)+i*stride))
                      for i in range(a['count'])]
        if 'sparse' in a:
            sparse = a['sparse']
            indices, _ = self.view(sparse['indices']['bufferView'])
            data, _ = self.view(sparse['values']['bufferView'])
            ic = formats[sparse['indices']['componentType']]
            previous = -1
            for i in range(sparse['count']):
                ix = struct.unpack_from('<'+ic, indices, sparse['indices'].get('byteOffset', 0)+i*struct.calcsize(ic))[0]
                if ix <= previous or ix >= len(values):
                    raise ValueError('invalid sparse index')
                previous = ix
                values[ix] = list(struct.unpack_from(fmt, data, sparse['values'].get('byteOffset', 0)+i*size))
        if a.get('normalized') and code != 'f':
            maximum = {'b': 127, 'B': 255, 'h': 32767, 'H': 65535, 'I': 4294967295}[code]
            values = [[max(-1, x/maximum) for x in row] for row in values]
        if any(not math.isfinite(x) for row in values for x in row):
            raise ValueError('non-finite accessor value')
        return values


def validate_metadata(meta, p, kind, d):
    expected = p['metadata'][{'character': 'asset', 'clothing': 'garment', 'item': 'item'}[kind]]
    for field in ('profile', 'kind'):
        if meta.get(field) != expected[field]:
            d.error('ei metadata', f'incorrect {field}', f'set {field}={expected[field]}')
    if kind == 'clothing':
        hides = meta.get('hides')
        if not isinstance(hides, list) or len(set(hides)) != len(hides) or any(z not in p['zones'] for z in hides):
            d.error('hides', 'missing/invalid zones', 'set unique profile zone names; [] is allowed')
    if kind == 'item':
        if meta.get('socket') not in {s['name'] for s in p['sockets']}:
            d.error('item.socket', 'unknown socket', 'choose a profile socket')
        grip = meta.get('grip', {})
        if any(not isinstance(grip.get(f), list) or len(grip[f]) != n or not all(math.isfinite(x) for x in grip[f])
               for f, n in [('translation', 3), ('rotation', 4), ('scale', 3)]):
            d.error('item.grip', 'missing/invalid explicit TRS', 'author grip translation/rotation/scale')
        elif any(x <= 0 for x in grip['scale']) or abs(sum(x*x for x in grip['rotation'])**0.5-1) > p['skeleton']['quaternionNormTolerance']:
            d.error('item.grip', 'invalid scale/quaternion', 'use positive scale and unit rotation')
    morphs = meta.get('morphs', [])
    morph_names = [m['name'] for m in morphs]
    if len(set(morph_names)) != len(morph_names):
        d.error('morphs', 'duplicate names', 'use unique morph names')
    for m in morphs:
        if not all(math.isfinite(m[x]) for x in ('min', 'max', 'default')) or not m['min'] <= m['default'] <= m['max']:
            d.error('morph '+m['name'], 'invalid range/default', 'put default inside finite min/max')
    if kind != 'item' and not isinstance(meta.get('morphSet'), str):
        d.error('morphSet', 'missing identifier', 'set the shared body/garment morphSet, including empty sets')
    return morphs, morph_names
