"""Profile-driven GLB inspection. Run with Blender's Python (mathutils required)."""
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


def image_size(data, mime):
    if mime == 'image/png' and data.startswith(b'\x89PNG\r\n\x1a\n'):
        return struct.unpack_from('>II', data, 16)
    if mime == 'image/jpeg' and data.startswith(b'\xff\xd8'):
        offset = 2
        while offset < len(data):
            if data[offset] != 255:
                break
            while data[offset] == 255:
                offset += 1
            marker = data[offset]
            offset += 1
            if marker in (0xd9, 0xda):
                break
            if marker == 1 or 0xd0 <= marker <= 0xd7:
                continue
            length = struct.unpack_from('>H', data, offset)[0]
            if length < 2 or offset+length > len(data):
                break
            if marker in (0xc0, 0xc1, 0xc2):
                height, width = struct.unpack_from('>HH', data, offset+3)
                return width, height
            offset += length
    raise ValueError('invalid or unsupported PNG/JPEG image')


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


def validate(doc, binary, profile, kind, d):
    """One publication rule set; checks actual serialized values, never source claims."""
    p = profile
    tolerance = p['skeleton']['bindTolerance']
    key = p['animation']['extrasKey']
    meta = doc.get('asset', {}).get('extras', {}).get(key, {})
    if doc.get('asset', {}).get('version') != p['gltf']['version']:
        d.error('asset.version', 'unsupported glTF', 'export glTF '+p['gltf']['version'])
    for ext in doc.get('extensionsRequired', []):
        if ext not in p['gltf']['supportedExtensions']:
            d.error('extensionsRequired', ext, 'remove unsupported extension')
    for collection in ('buffers', 'images'):
        for i, resource in enumerate(doc.get(collection, [])):
            if 'uri' in resource:
                d.error(f'{collection}[{i}]', 'URI resource', 'embed resource in GLB')
    buffers = doc.get('buffers', [])
    if len(buffers) != 1 or not 0 <= len(binary)-buffers[0]['byteLength'] <= 3:
        raise ValueError('expected one embedded buffer with correct byteLength')
    a = Accessors(doc, binary)
    # Decode every accessor, including otherwise unused data, before inspecting semantics.
    for i in range(len(doc.get('accessors', []))):
        a.get(i)
    nodes = doc.get('nodes', [])
    names = {}
    parents = {}
    for i, node in enumerate(nodes):
        name = node.get('name', '')
        if name in names:
            d.error(name, 'duplicate node name', 'use unique object and bone names')
        names[name] = i
        for child in node.get('children', []):
            if child in parents or child < 0 or child >= len(nodes):
                raise ValueError('invalid child or multiple parents')
            parents[child] = i
        for field, length in [('translation', 3), ('rotation', 4), ('scale', 3), ('matrix', 16)]:
            if field in node and (len(node[field]) != length or not all(math.isfinite(x) for x in node[field])):
                raise ValueError('invalid node '+field)
        if 'matrix' in node and any(x in node for x in ('translation', 'rotation', 'scale')):
            raise ValueError('node has matrix and TRS')
        if any(x <= 0 for x in node.get('scale', [1, 1, 1])) or abs(matrix(node).determinant()) < 1e-12:
            d.error(name, 'singular/reflected transform', 'use positive scale')
        q = node.get('rotation', [0, 0, 0, 1])
        if abs(sum(x*x for x in q)**0.5-1) > p['skeleton']['quaternionNormTolerance']:
            d.error(name, 'non-unit quaternion', 'normalize rotation')
    globals_ = {}

    def global_matrix(i, trail=()):
        if i in trail:
            raise ValueError('node hierarchy cycle')
        if i not in globals_:
            globals_[i] = (global_matrix(parents[i], trail+(i,)) if i in parents else Matrix.Identity(4)) @ matrix(nodes[i])
        return globals_[i]

    for i in range(len(nodes)):
        global_matrix(i)
    scenes = doc.get('scenes', [])
    if len(scenes) != 1 or doc.get('scene', 0) != 0:
        d.error('scenes', 'ambiguous scene', 'export one scene')
    if doc.get('cameras') or any('KHR_lights_punctual' in n.get('extensions', {}) for n in nodes):
        d.error('scene', 'camera/light exported', 'export model objects only')
    bones = {b['name']: b for b in p['skeleton']['bones']}
    skins = doc.get('skins', [])
    if kind == 'character' and len(skins) != 1:
        d.error('skins', f'expected one skin, got {len(skins)}', 'use one armature')
    if kind == 'clothing' and not skins:
        d.error('skins', 'garment has no skin', 'bind garment to profile skeleton')
    for si, skin in enumerate(skins):
        joints = skin['joints']
        joint_names = [nodes[j].get('name') for j in joints]
        if len(set(joints)) != len(joints):
            raise ValueError('duplicate skin joints')
        for b in bones.values():
            if b['required'] and b['name'] not in joint_names:
                d.error(f'skin[{si}]', 'missing '+b['name'], 'include required bone and ancestors')
        ibms = a.get(skin['inverseBindMatrices']) if 'inverseBindMatrices' in skin else []
        if len(ibms) != len(joints):
            d.error(f'skin[{si}]', 'missing inverse bind matrices', 'export skin bind matrices')
        for ix, j in enumerate(joints):
            name = nodes[j].get('name')
            if name not in bones:
                d.error(str(name), 'unknown joint', 'remove joint or use the correct profile')
                continue
            b = bones[name]
            parent_name = nodes[parents[j]].get('name') if j in parents else None
            # A unit armature container is allowed above the root.
            if b['parent'] is not None and (parent_name != b['parent'] or parents[j] not in joints):
                d.error(name, 'incorrect parent/ancestor closure', 'restore parent '+b['parent'])
            if b['parent'] is None and j in parents and not close(global_matrix(parents[j]), Matrix.Identity(4), tolerance):
                d.error(name, 'non-unit root container', 'remove scene placement from armature container')
            expected_global = matrix({'matrix': b['inverseBindMatrix']}).inverted()
            if not close(global_matrix(j), expected_global, tolerance):
                d.error(name, 'bind pose/scale differs', 'restore profile rest pose and unit container')
            if ix < len(ibms) and not close(matrix({'matrix': ibms[ix]}), matrix({'matrix': b['inverseBindMatrix']}), tolerance):
                d.error(name, 'inverse bind differs', 'export canonical bind without scene placement')
    if kind == 'character':
        for socket in p['sockets']:
            name = socket['name']
            j = names.get(name)
            if j is None:
                d.error(name, 'missing socket', 'add Empty parented to '+socket['parent'])
            elif (nodes[parents[j]].get('name') if j in parents else None) != socket['parent'] or not close(matrix(nodes[j]), matrix(socket), tolerance):
                d.error(name, 'incorrect socket frame', 'restore profile parent and local TRS (grip axes)')
    morphs, morph_names = validate_metadata(meta, p, kind, d)
    meshes = doc.get('meshes', [])
    if not meshes:
        d.error('meshes', 'no geometry', 'include at least one mesh')
    for mi, mesh in enumerate(meshes):
        where = mesh.get('name', str(mi))
        targets = mesh.get('extras', {}).get('targetNames', [])
        if targets != morph_names or mesh.get('weights', [0]*len(targets)) != [m['default'] for m in morphs]:
            d.error(where, 'morph names/defaults differ from asset metadata', 'use matching ordered morph channels')
        for primitive in mesh['primitives']:
            attrs = primitive['attributes']
            count = doc['accessors'][attrs['POSITION']]['count']
            if any(doc['accessors'][ix]['count'] != count for ix in attrs.values()):
                raise ValueError(where+': mismatched vertex attribute counts')
            indices = [row[0] for row in a.get(primitive['indices'])] if 'indices' in primitive else list(range(count))
            if len(indices) % 3 or any(x != int(x) or x < 0 or x >= count for x in indices):
                d.error(where, 'invalid triangle indices', 'triangulate and repair mesh indices')
            if primitive.get('mode', 4) != p['gltf']['primitiveMode']:
                d.error(where, 'non-triangle primitive', 'triangulate geometry')
            for attr in p['materials']['requiredAttributes']:
                if attr not in attrs:
                    d.error(where, 'missing '+attr, 'export mesh normals and positions')
            if kind == 'character' and primitive.get('extras', {}).get(key, {}).get('zone') not in p['zones']:
                d.error(where, 'missing body zone', 'assign a profile zone per material primitive')
            if len(primitive.get('targets', [])) != len(targets) or any(set(t)-set(p['gltf']['morphTargets']) for t in primitive.get('targets', [])):
                d.error(where, 'invalid morph targets', 'export allowed channels with matching names')
            for attr in attrs:
                if attr.startswith(('JOINTS_', 'WEIGHTS_')) and attr not in ('JOINTS_0', 'WEIGHTS_0'):
                    d.error(where, 'extra influence set '+attr, 'reduce weights to profile maximum')
            weights = a.get(attrs['WEIGHTS_0']) if 'WEIGHTS_0' in attrs else []
            if kind != 'item' and not weights:
                d.error(where, 'unskinned geometry', 'bind all mesh vertices')
            if weights and 'JOINTS_0' not in attrs:
                d.error(where, 'weights without joints', 'export joint indices')
            bad = sum(any(x < 0 for x in row) or abs(sum(row)-1) > tolerance or
                      sum(x > 0 for x in row) > p['gltf']['maxJointInfluences'] for row in weights)
            if bad:
                d.error(where, f'{bad} invalid vertex weights', 'normalize nonnegative weights and limit influences')
            if 'NORMAL' in attrs and any(abs(sum(x*x for x in row)**0.5-1) > tolerance for row in a.get(attrs['NORMAL'])):
                d.error(where, 'non-unit normals', 'recalculate normals')
            if 'material' in primitive:
                material = doc['materials'][primitive['material']]
                slots = dict(material.get('pbrMetallicRoughness', {}))
                slots.update(material)
                for field, value in slots.items():
                    if field.endswith('Texture') and isinstance(value, dict):
                        transform = value.get('extensions', {}).get('KHR_texture_transform')
                        uv = value.get('texCoord', 0)
                        if transform is not None or uv != p['materials']['textureTexCoord']:
                            d.error(where+'/'+field, 'per-map UV/transform unsupported in human.v1',
                                    'use TEXCOORD_0 without KHR_texture_transform on every texture')
                        if uv not in p['gltf']['uvSets'] or 'TEXCOORD_'+str(uv) not in attrs:
                            d.error(where, 'missing/unsupported texture UV', 'supply TEXCOORD_0 used by material')
                if 'normalTexture' in material and p['materials']['normalMapRequiresTangent']:
                    if 'TANGENT' not in attrs:
                        d.error(where, 'normal map requires TANGENT', 'export authored glTF tangents')
                    else:
                        tangent = doc['accessors'][attrs['TANGENT']]
                        if tangent['type'] != 'VEC4' or tangent['componentType'] != 5126:
                            d.error(where, 'invalid TANGENT format', 'export float VEC4 tangents')
                        elif any(abs(sum(x*x for x in row[:3])**0.5-1) > tolerance or
                                 abs(abs(row[3])-1) > tolerance for row in a.get(attrs['TANGENT'])):
                            d.error(where, 'invalid tangent direction/handedness', 'export unit tangents with w=+1 or -1')
    for node in nodes:
        if 'mesh' in node and kind != 'item' and 'skin' not in node:
            d.error(node.get('name', 'mesh'), 'mesh node has no skin', 'export an armature modifier')
        if 'mesh' in node and 'skin' in node:
            skin = skins[node['skin']]
            for primitive in meshes[node['mesh']]['primitives']:
                attrs = primitive['attributes']
                if 'JOINTS_0' in attrs and any(x < 0 or x >= len(skin['joints']) for row in a.get(attrs['JOINTS_0']) for x in row):
                    d.error(node.get('name', 'mesh'), 'joint index outside skin', 'rebind mesh')
    for i, mat in enumerate(doc.get('materials', [])):
        if mat.get('alphaMode', 'OPAQUE') not in p['materials']['alphaModes']:
            d.error(f'material[{i}]', 'unknown alpha mode', 'choose profile alpha mode')
        unsupported = set(mat.get('extensions', {}))-set(p['gltf']['supportedExtensions'])
        if unsupported:
            d.error(f'material[{i}]', 'unsupported workflow '+str(sorted(unsupported)), 'use core metallic-roughness')
        pbr = mat.get('pbrMetallicRoughness', {})
        factors = pbr.get('baseColorFactor', [1, 1, 1, 1]) + [pbr.get('metallicFactor', 1), pbr.get('roughnessFactor', 1)]
        if len(pbr.get('baseColorFactor', [1, 1, 1, 1])) != 4 or any(not math.isfinite(x) or not 0 <= x <= 1 for x in factors):
            d.error(f'material[{i}]', 'invalid PBR factors', 'use finite core glTF factors in [0,1]')
    for i, image in enumerate(doc.get('images', [])):
        data, _ = a.view(image['bufferView'])
        mime = image.get('mimeType')
        try:
            width, height = image_size(data, mime)
            if min(width, height) == 0:
                raise ValueError('zero-size image')
        except (ValueError, IndexError, struct.error) as exc:
            d.error(f'image[{i}]', str(exc), 'convert image to a valid nonzero PNG/JPEG')
    d.warning('profile', 'human.v1 specifies no texture dimension limit; image dimensions have no profile budget')
    animations = doc.get('animations', [])
    anim_names = [x.get('name') for x in animations]
    if len(set(anim_names)) != len(anim_names):
        d.error('animations', 'duplicate names', 'use unique clip names')
    if kind == 'character':
        for clip in p['clips']:
            if clip['name'] in p['animation']['exportRequiredClips'] and clip['name'] not in anim_names:
                d.error('animations', 'missing '+clip['name'], 'author named action and required events')
    for anim in animations:
        name = anim.get('name', '?')
        duration = 0
        channels = set()
        for ch in anim['channels']:
            target = ch['target']
            pair = (target['node'], target['path'])
            sampler = anim['samplers'][ch['sampler']]
            times = [row[0] for row in a.get(sampler['input'])]
            output = a.get(sampler['output'])
            interpolation = sampler.get('interpolation', 'LINEAR')
            multiplier = 3 if interpolation == 'CUBICSPLINE' else 1
            value_count = len(output)
            if target['path'] == 'weights':
                mesh = meshes[nodes[target['node']]['mesh']]
                multiplier *= len(mesh.get('extras', {}).get('targetNames', []))
            if value_count != len(times)*multiplier:
                d.error(name, 'animation output count differs from input', 're-export animation samplers')
            if not times or times[0] < 0 or any(x >= y for x, y in zip(times, times[1:])):
                d.error(name, 'invalid key times', 'use increasing nonnegative times')
            duration = max(duration, max(times, default=0))
            if pair in channels or target['path'] not in p['gltf']['animationPaths'] or interpolation not in p['gltf']['interpolation']:
                d.error(name, 'duplicate/unsupported animation channel', 'export one allowed channel per node/path')
            channels.add(pair)
            if 'matrix' in nodes[target['node']]:
                d.error(name, 'animated matrix node', 'export animated TRS')
            if nodes[target['node']].get('name') == p['skeleton']['root'] and target['path'] == 'translation':
                values = output[1::3] if interpolation == 'CUBICSPLINE' else output
                rest = bones[p['skeleton']['root']]['translation']
                if any(any(abs(x-y) > tolerance for x, y in zip(value, rest)) for value in values):
                    d.error(name, 'root motion in in-place clip', 'remove world movement from root animation')
                if interpolation == 'CUBICSPLINE' and any(abs(x) > tolerance for row in output[0::3]+output[2::3] for x in row):
                    d.error(name, 'root motion tangents', 'zero root translation tangents')
            if target['path'] == 'rotation':
                values = output[1::3] if interpolation == 'CUBICSPLINE' else output
                if any(abs(sum(x*x for x in q)**0.5-1) > p['skeleton']['quaternionNormTolerance'] for q in values):
                    d.error(name, 'non-unit animated quaternion', 'normalize rotation keys')
        events = anim.get('extras', {}).get(key, {}).get('events', [])
        ids, order = set(), []
        event_names = set()
        for event in events:
            if not all(f in event for f in p['events']['requiredFields']):
                d.error(name, 'incomplete event', 'supply '+str(p['events']['requiredFields']))
                continue
            en = event['name']
            rule = p['events']['types'].get(en)
            payload = event['payload']
            if event['id'] in ids or not isinstance(event['id'], str) or not math.isfinite(event['time']) or not 0 <= event['time'] <= duration:
                d.error(name, 'invalid event id/time', 'use unique id and time within clip')
            ids.add(event['id'])
            order.append((event['time'], event['id']))
            event_names.add(en)
            if rule is None or not isinstance(payload, dict) or any(f not in payload for f in rule['payload']):
                d.error(name, 'unknown event/incomplete payload', 'follow profile event schema')
            elif any(payload.get(f[:-6]) not in v for f, v in rule.items() if f.endswith('Values')):
                d.error(name, 'invalid event payload value', 'choose profile payload value')
            elif en == 'attachment_visibility' and not isinstance(payload.get('visible'), bool):
                d.error(name, 'visibility must be boolean', 'set visible true/false')
            elif en == 'sound' and (not isinstance(payload.get('indices'), list) or any(type(x) is not int or x < 0 for x in payload['indices'])):
                d.error(name, 'invalid sound indices', 'use a list of nonnegative integer sound codes')
        if order != sorted(order):
            d.error(name, 'unsorted events', 'sort by time then id')
        for clip in p['clips']:
            if clip['name'] == name:
                for en in clip['requiredEvents']:
                    if en not in event_names:
                        d.error(name, 'missing event '+en, 'add action marker / authored event')
    seen_bindings = set()
    for binding in meta.get('bindings', []):
        fields = p['animation']['bindingRequiredFields']
        if any(f not in binding for f in fields) or binding.get('clip') not in anim_names:
            d.error('bindings', 'missing field/unknown clip', 'follow profile bindingRequiredFields')
            continue
        request = tuple(binding[f] for f in p['animation']['requestFields'])
        if request in seen_bindings:
            d.error('bindings', 'ambiguous request tuple', 'keep one binding per request')
        seen_bindings.add(request)
