"""Read current manifest and cook available RGB atlases; never materialize LFS.

Usage: python bake_bahonec.py --clone PATH --exe PATH --out PATH
Only --out is written. Metrics and unavailable source paths go to results.json.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess


def pointer(path):
    with path.open('rb') as stream:
        return stream.read(42).startswith(b'version https://git-lfs.github.com/spec/v1')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clone', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = args.clone.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    results = {'atlases': [], 'orm': [], 'lfs_pointers': [], 'missing': []}

    def run(options, output):
        proc = subprocess.run([str(args.exe), *options, '--output', str(output)],
                              capture_output=True, text=True, check=True)
        print(output.name, proc.stdout.strip(), flush=True)
        return {'dds_bytes': output.stat().st_size, 'metrics': proc.stdout.strip()}

    manifest = json.loads((root / 'config/natural-nails-runtime.json').read_text('utf-8'))
    for resource, entry in manifest['resources'].items():
        source = root / entry['file']
        if source.suffix != '.rgb':
            continue
        if pointer(source):
            results['lfs_pointers'].append(entry['file'])
            continue
        raw = source.read_bytes()
        if len(raw) != entry['bytes'] or hashlib.sha256(raw).hexdigest() != entry['sha256']:
            raise ValueError(f'manifest hash/size mismatch: {source}')
        size = math.isqrt(len(raw) // 3)
        if size * size * 3 != len(raw):
            raise ValueError('expected square RGB8 atlas')
        result = run(['--role', 'color', '--space', 'srgb', '--input', str(source),
                      '--width', str(size), '--height', str(size)], args.out / f'{resource}_color.dds')
        results['atlases'].append(dict(resource=resource, source=entry['file'],
                                       source_bytes=len(raw), **result))

    # Each pair follows the author's naming convention in authoring/equipment.
    authoring = root / 'assets/authoring/equipment'
    rough_maps = sorted(authoring.rglob('*roughness.png'))
    for rough in rough_maps:
        metal = rough.with_name(rough.name.replace('roughness', 'metallic'))
        usable = True
        for source in (rough, metal):
            rel = source.relative_to(root).as_posix()
            if not source.exists():
                results['missing'].append(rel)
                usable = False
            elif pointer(source):
                results['lfs_pointers'].append(rel)
                usable = False
        if usable:
            name = rough.parent.name + '_' + rough.stem.replace('_roughness', '') + '_orm.dds'
            result = run(['--role', 'orm', '--space', 'linear', '--ao-value', '1',
                          '--roughness', str(rough), '--metallic', str(metal)], args.out / name)
            results['orm'].append(dict(source=str(rough.relative_to(root)), **result))
    (args.out / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
