"""Blender 5.2 headless authoring/export entry point. See docs/character-export.md."""
import argparse
import json
import math
import os
from pathlib import Path
import sys
import tempfile

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate import Diagnostics, close, matrix, read_glb, validate, validate_metadata, write_glb


def custom(owner, name, default=None):
    value = owner.get(name, default)
    if isinstance(value, str):
        return json.loads(value)
    if hasattr(value, 'to_dict'):
        return value.to_dict()
    return value


def source_checks(objects, profile, kind, d):
    p = profile
    scene = bpy.context.scene
    tol = p['skeleton']['bindTolerance']
    d.warning('profile', 'human.v1 has no texture dimension limits; artist budget requires a separate contract')
    if scene.unit_settings.system != 'METRIC' or abs(scene.unit_settings.scale_length-p['coordinates']['sourceSceneUnitScale']) > tol:
        d.error('scene units', 'expected metres', 'set Metric, Unit Scale='+str(p['coordinates']['sourceSceneUnitScale']))
    meshes = [o for o in objects if o.type == 'MESH']
    rigs = [o for o in objects if o.type == 'ARMATURE']
    if not meshes:
        d.error('collection', 'no meshes', 'include export meshes')
    if kind != 'item' and len(rigs) != 1:
        d.error('collection', f'expected one armature, got {len(rigs)}', 'include one profile armature')
    C = Matrix(p['coordinates']['blenderToProfileRows'])
    bones = {b['name']: b for b in p['skeleton']['bones']}
    for rig in rigs:
        # Object placement is deliberately rejected rather than silently changing an asset.
        if not close(rig.matrix_world, Matrix.Identity(4), tol):
            d.error(rig.name, 'scene placement/scale on armature', 'prepare a unit model container with canonical rest coordinates')
        for b in bones.values():
            if b['required'] and b['name'] not in rig.data.bones:
                d.error(rig.name, 'missing '+b['name'], 'include required bone and ancestors')
        root = rig.data.bones.get(p['skeleton']['root'])
        origin = root.matrix_local.translation if root else Vector((0, 0, 0))
        if origin.length > tol:
            d.error(rig.name, 'rest root not at origin', 'subtract root position from rest skeleton and mesh consistently')
        for bone in rig.data.bones:
            b = bones.get(bone.name)
            if b is None:
                d.error(bone.name, 'unknown joint', 'remove unknown bone or select correct profile')
                continue
            if (bone.parent.name if bone.parent else None) != b['parent']:
                d.error(bone.name, 'incorrect hierarchy', 'restore profile parent')
            G = C @ Matrix.Translation(-origin) @ bone.matrix_local @ C.inverted()
            if not close(G, matrix({'matrix': b['inverseBindMatrix']}).inverted(), tol):
                d.error(bone.name, 'rest bind differs', 'restore profile rest matrix; do not apply current pose as rest')
        if rig.animation_data and rig.animation_data.nla_tracks:
            d.error(rig.name, 'NLA tracks present', 'bake clips as named actions and remove NLA tracks in export copy')
    if kind == 'character' and len(rigs) == 1:
        rig = rigs[0]
        for socket in p['sockets']:
            obj = next((o for o in objects if o.name == socket['name']), None)
            if obj is None:
                d.error(socket['name'], 'missing socket', 'create Empty bone-parented to '+socket['parent'])
            elif obj.type != 'EMPTY' or obj.parent != rig or obj.parent_type != 'BONE' or obj.parent_bone != socket['parent']:
                d.error(socket['name'], 'incorrect socket parent', 'use Empty with profile bone parent')
            else:
                # Blender bone parenting includes a translation to the bone tail.
                bone = rig.data.bones[obj.parent_bone]
                local = Matrix.Translation((0, bone.length, 0)) @ obj.matrix_parent_inverse @ obj.matrix_basis
                if not close(C @ local @ C.inverted(), matrix(socket), tol):
                    d.error(socket['name'], 'incorrect socket orientation/offset', 'set bone-relative rest frame from profile, accounting for Blender bone tail')
    used_materials = set()
    for obj in meshes:
        where = obj.name
        if not close(obj.matrix_world, Matrix.Identity(4), tol):
            d.error(where, 'non-unit mesh transform', 'prepare mesh in model coordinates with identity transform')
        modifiers = [m for m in obj.modifiers if m.show_viewport and m.type != 'ARMATURE']
        if modifiers:
            d.error(where, 'unbaked modifiers: '+', '.join(m.type for m in modifiers), 'bake final geometry/morphs in export copy')
        if kind != 'item':
            arm = [m for m in obj.modifiers if m.type == 'ARMATURE' and m.show_viewport]
            if len(arm) != 1 or arm[0].object not in rigs:
                d.error(where, 'missing/ambiguous skin modifier', 'bind to exported profile armature')
            counts = {'unweighted': 0, 'unnormalized': 0, 'too_many': 0, 'unknown': 0}
            groups = {g.index: g.name for g in obj.vertex_groups}
            for vertex in obj.data.vertices:
                weights = [(groups[g.group], g.weight) for g in vertex.groups if g.weight != 0]
                if not weights:
                    counts['unweighted'] += 1
                if abs(sum(w for _, w in weights)-1) > tol:
                    counts['unnormalized'] += 1
                if len(weights) > p['gltf']['maxJointInfluences']:
                    counts['too_many'] += 1
                if any(n not in bones or not math.isfinite(w) or w < 0 for n, w in weights):
                    counts['unknown'] += 1
            for reason, count in counts.items():
                if count:
                    d.error(where, f'{count} vertices: {reason}', 'keep normalized nonnegative profile bone weights, maximum '+str(p['gltf']['maxJointInfluences']))
        for slot in obj.material_slots:
            if slot.material is None:
                d.error(where, 'empty material slot', 'assign metallic-roughness material')
            else:
                used_materials.add(slot.material)
                if kind == 'character' and slot.material.get('ei_zone') not in p['zones']:
                    d.error(where+'/'+slot.material.name, 'missing/unknown body zone', 'set material custom property ei_zone to a profile zone')
        if not obj.material_slots:
            d.error(where, 'no material', 'assign material (and body zone)')
        meta = custom(scene, 'ei', {})
        morphs = meta.get('morphs', [])
        keys = list(obj.data.shape_keys.key_blocks)[1:] if obj.data.shape_keys else []
        if [k.name for k in keys] != [m['name'] for m in morphs]:
            d.error(where, 'shape keys differ from scene.ei.morphs', 'use the same ordered channels on all body/garment meshes')
        for shape, rule in zip(keys, morphs):
            if shape.slider_min != rule['min'] or shape.slider_max != rule['max'] or shape.value != rule['default']:
                d.error(where+'/'+shape.name, 'morph range/default differs', 'match scene.ei.morphs ranges and reset value to default')
    for mat in sorted(used_materials, key=lambda x: x.name):
        if not mat.use_nodes:
            d.error(mat.name, 'material has no shader nodes', 'use Principled BSDF metallic-roughness')
            continue
        outputs = [n for n in mat.node_tree.nodes if n.type == 'OUTPUT_MATERIAL' and n.is_active_output]
        shader_links = list(outputs[0].inputs['Surface'].links) if outputs else []
        shader = shader_links[0].from_node if shader_links else None
        if shader is None or shader.type != 'BSDF_PRINCIPLED':
            d.error(mat.name, 'unsupported surface graph', 'connect Principled BSDF directly to Material Output')
        if shader:
            for field in ('Transmission Weight', 'Coat Weight', 'Sheen Weight', 'Anisotropic IOR Level'):
                socket = shader.inputs.get(field)
                if socket and (socket.is_linked or socket.default_value != 0):
                    d.error(mat.name, 'unsupported '+field, 'use core metallic-roughness inputs')
        for node in mat.node_tree.nodes:
            if node.type != 'TEX_IMAGE' or not node.image:
                continue
            image = node.image
            if min(image.size) <= 0:
                d.error(mat.name+'/'+image.name, 'image unavailable/zero dimensions', 'pack or restore source image')
            if image.file_format not in ('PNG', 'JPEG'):
                d.error(mat.name+'/'+image.name, 'unsupported texture format '+image.file_format, 'convert to PNG/JPEG')
            # Follow graph links to identify semantic usage, including Separate Color/Normal Map.
            pending, seen, usage = [node], set(), set()
            while pending:
                current = pending.pop()
                if current in seen:
                    continue
                seen.add(current)
                for output in current.outputs:
                    for link in output.links:
                        if link.to_node == shader:
                            usage.add(link.to_socket.name)
                        else:
                            pending.append(link.to_node)
            color = bool(usage & {'Base Color', 'Emission Color'})
            linear = bool(usage & {'Normal', 'Roughness', 'Metallic', 'Alpha'})
            space = image.colorspace_settings.name
            # Color texture alpha is linear even when its RGB uses sRGB.
            if color and space != 'sRGB' or linear and not color and space != 'Non-Color':
                d.error(mat.name+'/'+image.name, 'incorrect color space '+space, 'use sRGB for color RGB and Non-Color for normal/ORM')
            if color and usage & {'Normal', 'Roughness', 'Metallic'}:
                d.error(mat.name+'/'+image.name, 'image reused as color and data', 'separate color and linear texture inputs')
    if kind == 'character':
        actions = {a.name: a for a in bpy.data.actions}
        for clip in p['clips']:
            action = actions.get(clip['name'])
            if clip['required'] and action is None:
                d.error('actions', 'missing '+clip['name'], 'author action with exact profile clip name')
            if action:
                names = {m.name.split(':', 1)[0] for m in action.pose_markers}
                names.update(e['name'] for e in custom(action, 'ei', {}).get('events', []))
                for event in clip['requiredEvents']:
                    if event not in names:
                        d.error(action.name, 'missing event '+event, 'add action pose marker or ei.events entry')


def enrich(doc, binary, objects, profile, kind, meta):
    key = profile['animation']['extrasKey']
    doc['asset'].setdefault('extras', {})[key] = meta
    materials = doc.get('materials', [])
    for mesh in doc.get('meshes', []):
        for primitive in mesh['primitives']:
            if kind == 'character' and 'material' in primitive:
                material = materials[primitive['material']]
                zone = material.get('extras', {}).get('ei_zone')
                primitive.setdefault('extras', {})[key] = {'zone': zone}
    fps = bpy.context.scene.render.fps / bpy.context.scene.render.fps_base
    for anim in doc.get('animations', []):
        action = bpy.data.actions.get(anim.get('name', ''))
        if action:
            ei = custom(action, 'ei', {})
            events = list(ei.get('events', []))
            for i, marker in enumerate(action.pose_markers):
                name, _, payload = marker.name.partition(':')
                events.append({'id': f'marker-{i}', 'name': name,
                               'time': (marker.frame-action.frame_range[0])/fps,
                               'payload': json.loads(payload) if payload else {}})
            ei['events'] = sorted(events, key=lambda e: (e['time'], e['id']))
            anim.setdefault('extras', {})[key] = ei
    return doc, binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', required=True, choices=['character', 'clothing', 'item'])
    parser.add_argument('--out', type=Path)
    parser.add_argument('--collection', help='Export collection; default: all objects in active scene')
    parser.add_argument('--profile', type=Path, default=Path(__file__).resolve().parents[2]/'Data/profiles/human.v1.json')
    parser.add_argument('--check-glb', type=Path, help='Validate existing GLB without exporting')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    d = Diagnostics()
    try:
        profile = json.loads(args.profile.read_text(encoding='utf-8-sig'))
        if profile.get('schema') != 'ei.character-profile' or profile.get('version') != 1:
            raise ValueError('unsupported character profile schema/version')
        if args.check_glb:
            validate(*read_glb(args.check_glb), profile, args.kind, d)
        else:
            if args.out is None or args.out.suffix.lower() != '.glb':
                parser.error('--out must name a .glb file')
            if args.collection:
                collection = bpy.data.collections.get(args.collection)
                if collection is None:
                    raise ValueError('collection not found: '+args.collection)
                objects = list(collection.all_objects)
            else:
                objects = list(bpy.context.scene.objects)
            source_checks(objects, profile, args.kind, d)
            meta = custom(bpy.context.scene, 'ei', {})
            validate_metadata(meta, profile, args.kind, d)
            if not d.errors:
                bpy.ops.object.select_all(action='DESELECT')
                for obj in objects:
                    if obj.type in ('ARMATURE', 'MESH', 'EMPTY'):
                        obj.hide_set(False)
                        obj.select_set(True)
                args.out.parent.mkdir(parents=True, exist_ok=True)
                fd, candidate = tempfile.mkstemp(suffix='.glb', prefix='.ei-export-', dir=args.out.parent)
                os.close(fd)
                try:
                    result = bpy.ops.export_scene.gltf(
                        filepath=candidate, export_format='GLB', use_selection=True,
                        export_yup=True, export_extras=True, export_cameras=False,
                        export_lights=False, export_normals=True, export_tangents=True,
                        export_animations=args.kind == 'character', export_animation_mode='ACTIONS',
                        export_anim_single_armature=True, export_rest_position_armature=True,
                        export_force_sampling=True, export_frame_range=False,
                        export_anim_slide_to_zero=True, export_skins=True,
                        export_def_bones=False, export_all_influences=False,
                        export_influence_nb=profile['gltf']['maxJointInfluences'],
                        export_morph=True, export_morph_normal=True,
                        export_morph_tangent=True, export_materials='EXPORT',
                        export_image_format='AUTO', export_apply=False)
                    if result != {'FINISHED'}:
                        raise ValueError('Blender glTF exporter did not finish')
                    doc, binary = enrich(*read_glb(candidate), objects, profile, args.kind, meta)
                    write_glb(candidate, doc, binary)
                    validate(*read_glb(candidate), profile, args.kind, d)
                    if not d.errors:
                        # Re-read serialized result before atomic publication; existing output is preserved on failure.
                        os.replace(candidate, args.out)
                        print('EXPORTED '+str(args.out), flush=True)
                finally:
                    if os.path.exists(candidate):
                        os.unlink(candidate)
    except Exception as exc:
        d.error('export', f'{type(exc).__name__}: {exc}', 'correct input or profile; no output was published')
    d.show()
    return 1 if d.errors else 0


if __name__ == '__main__':
    # Blender otherwise exits zero after a script failure. Flush diagnostics before explicit status.
    status = main()
    sys.stdout.flush()
    sys.stderr.flush()
    if status:
        os._exit(status)
