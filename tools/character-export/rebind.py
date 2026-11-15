"""Rebuild a prepared skin from its author scene without changing either input.

Run with Blender --background --python rebind.py -- --source author.blend
--prepared prepared.blend --action "author idle" --out corrected.blend.
Materials/UVs come from the prepared scene; bind geometry and motion come from
the author scene. Mesh names, topology and named weights must match exactly.
"""
import argparse
import os
from pathlib import Path
import sys

import bpy
from mathutils import Matrix


def skin_meshes(rig):
    return {o.name: o for o in bpy.context.scene.objects if o.type == 'MESH'
            and any(m.type == 'ARMATURE' and m.object == rig for m in o.modifiers)}


def weights(obj):
    names = {g.index: g.name for g in obj.vertex_groups}
    return [{names[g.group]: g.weight for g in v.groups if g.weight}
            for v in obj.data.vertices]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'prepared', 'out'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--action', required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if args.out.resolve() in (args.source.resolve(), args.prepared.resolve()):
        raise ValueError('output must differ from both input scenes')

    bpy.ops.wm.open_mainfile(filepath=str(args.source))
    rigs = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
    if len(rigs) != 1:
        raise ValueError('expected one author armature')
    source = rigs[0]
    source.animation_data_create()
    source.animation_data.action = bpy.data.actions[args.action]
    for track in source.animation_data.nla_tracks:
        track.mute = True
    start, end = source.animation_data.action.frame_range
    if start != int(start) or end != int(end):
        raise ValueError('bake requires integral action frame range')
    bpy.context.scene.frame_set(int(start))
    roots = [b for b in source.data.bones if b.parent is None]
    if len(roots) != 1:
        raise ValueError('expected one author rest root')
    # One normalization for BOTH skin geometry and bone deformation. Armature
    # object animation is host placement, not a second mesh-space transform.
    normalize = Matrix.Translation(-roots[0].matrix_local.translation)
    unnormalize = normalize.inverted()
    rest = {b.name: b.matrix_local.copy() for b in source.data.bones}
    meshes = {}
    for name, obj in skin_meshes(source).items():
        transform = normalize @ source.matrix_world.inverted() @ obj.matrix_world
        meshes[name] = {
            'positions': [transform @ v.co for v in obj.data.vertices],
            'polygons': [tuple(p.vertices) for p in obj.data.polygons],
            'weights': weights(obj),
            'transform': transform,
        }
        if obj.data.shape_keys and any(k.value != 0 for k in list(obj.data.shape_keys.key_blocks)[1:]):
            raise ValueError('reset author morphs to Basis before rebinding')
    frames = []
    for frame in range(int(start), int(end)+1):
        bpy.context.scene.frame_set(frame)
        for name, obj in skin_meshes(source).items():
            if obj.data.shape_keys and any(k.value != 0 for k in list(obj.data.shape_keys.key_blocks)[1:]):
                raise ValueError('animated author morph requires an explicit morph rebind: '+name)
            transform = normalize @ source.matrix_world.inverted() @ obj.matrix_world
            if max(abs(transform[r][c]-meshes[name]['transform'][r][c])
                   for r in range(4) for c in range(4)) > 1e-4:
                raise ValueError('animated mesh container: '+name)
        frames.append({b.name: normalize @ source.pose.bones[b.name].matrix
                       @ rest[b.name].inverted() @ unnormalize
                       for b in source.data.bones})

    bpy.ops.wm.open_mainfile(filepath=str(args.prepared))
    rigs = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
    if len(rigs) != 1:
        raise ValueError('expected one prepared armature')
    target = rigs[0]
    if set(rest) != set(target.data.bones.keys()):
        raise ValueError('author/prepared joint names differ')
    if max(abs(target.matrix_world[r][c]-Matrix.Identity(4)[r][c])
           for r in range(4) for c in range(4)) > 1e-5:
        raise ValueError('prepared armature must have identity world transform')
    targets = skin_meshes(target)
    if set(meshes) != set(targets):
        raise ValueError('author/prepared skin mesh names differ')
    for name, obj in targets.items():
        src = meshes[name]
        if ([tuple(p.vertices) for p in obj.data.polygons] != src['polygons']
                or weights(obj) != src['weights']):
            raise ValueError('author/prepared topology or named weights differ: '+name)
        if obj.data.shape_keys:
            raise ValueError('prepared shape keys require an explicit morph rebind')
        if max(abs(obj.matrix_world[r][c]-Matrix.Identity(4)[r][c])
               for r in range(4) for c in range(4)) > 1e-5:
            raise ValueError('prepared mesh must have identity world transform: '+name)
        for vertex, position in zip(obj.data.vertices, src['positions']):
            vertex.co = position
        obj.data.update()

    target.animation_data_clear()
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)
    for bone in target.pose.bones:
        for constraint in list(bone.constraints):
            bone.constraints.remove(constraint)
        bone.rotation_mode = 'QUATERNION'
    for frame, deformation in enumerate(frames, 1):
        goals = {b.name: deformation[b.name] @ b.matrix_local for b in target.data.bones}
        for bone in target.pose.bones:
            parent = bone.parent
            bone.matrix_basis = bone.bone.convert_local_to_pose(
                goals[bone.name], bone.bone.matrix_local,
                parent_matrix=goals[parent.name] if parent else Matrix.Identity(4),
                parent_matrix_local=parent.bone.matrix_local if parent else Matrix.Identity(4),
                invert=True)
            for path in ('location', 'rotation_quaternion', 'scale'):
                bone.keyframe_insert(path, frame=frame, group=bone.name)
    target.animation_data.action.name = 'idle'
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = len(frames)
    bpy.context.scene.frame_set(1)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out))
    print('REBOUND', len(meshes), 'meshes,', len(rest), 'joints,', len(frames), 'idle frames')


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('REBIND FAILED:', type(exc).__name__+':', exc, flush=True)
        sys.stderr.flush()
        os._exit(1)
