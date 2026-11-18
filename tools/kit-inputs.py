"""Content identity for the Release x64 kit built by build-task.ps1."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def git(repo, *args):
    return subprocess.check_output(['git', '--no-optional-locks', '-C', repo, *args])


def fingerprint(args):
    repo, project = args.repo, args.project
    if git(repo, 'status', '--porcelain', '--untracked-files=all'):
        raise ValueError('working tree contains local changes')
    names = git(repo, '-c', 'core.quotepath=false', 'ls-files', '-z').decode().split('\0')
    tree = git(repo, 'rev-parse', 'HEAD^{tree}').decode().strip()
    digest = hashlib.sha256()

    def field(*values):
        digest.update(json.dumps(values, ensure_ascii=True).encode() + b'\n')

    def record(label, path):
        if path.is_symlink() or path.is_junction():
            raise ValueError(f'linked build input: {path}')
        if path.is_dir():
            for child in sorted(path.iterdir()):
                record(label + '/' + child.name, child)
        else:
            field(label, sha(path))

    field('schema', 1, tree, 'Release', 'x64', str(args.vc), str(args.sdk), args.sdk_version)
    for name in names:
        if name:
            record('source/' + name, repo / name)
    # Include the actual materialized bytes (including LFS), not just Git IDs.
    deps = project / 'deps'
    roots = {
        'assets': args.assets / 'eid3d_hosted', 'materials': args.materials,
        'physx': deps / 'physx/5.13.0/sdk',
        'dlss': Path(os.environ.get('EIREBUILD64_DLSS_SDK') or deps / 'dlss-sdk/v310.9.16'),
        'ffmpeg': deps / 'ffmpeg/9.0.2/x64',
        'ffmpeg-src': deps / 'ffmpeg/9.0.2/downloads/ffmpeg-9.0.2.tar.xz',
        'fidelityfx': deps / 'fidelityfx-sdk/2.4.0', 'agility': deps / 'agility-sdk/1.620.2',
        'rtxdi': deps / 'rtxdi/3.2.0', 'nvapi': deps / 'nvapi/70d337db',
        'dxc': deps / 'dxc/1.9.2609', 'cgltf': deps / 'cgltf/v1.15',
        'zstd': deps / 'zstd/v1.5.7/lib', 'zstd-license': deps / 'zstd/v1.5.7/LICENSE',
        'zstd-copying': deps / 'zstd/v1.5.7/COPYING',
        'msbuild': args.vs / 'MSBuild', 'vc-support': args.vs / 'VC/Auxiliary/Build',
        'compiler': args.vc / 'bin/Hostx64/x64', 'vc-lib': args.vc / 'lib/x64',
        'vc-include': args.vc / 'include', 'sdk-include': args.sdk / 'Include' / args.sdk_version,
        'sdk-lib': args.sdk / 'Lib' / args.sdk_version, 'sdk-bin': args.sdk / 'bin' / args.sdk_version / 'x64',
        'd3dcompiler': Path(os.environ['SystemRoot']) / 'System32/d3dcompiler_47.dll',
        'powershell': args.pwsh, 'python': Path(sys.executable), 'git': Path(shutil.which('git')),
        'launch': project / 'tools/project/invoke-build.ps1', 'supervisor': project / 'tools/project/build_process.py',
    }
    for pkg in ET.parse(repo / 'packages.config').getroot():
        key = pkg.attrib['id'].lower() + '/' + pkg.attrib['version'].lower()
        roots['nuget/' + key] = deps / 'nuget' / key
    for label, path in sorted(roots.items()):
        record(label, path)
    # MSBuild imports environment properties; store only the resulting digest.
    overwritten = {'TMP', 'TEMP', 'CL', '_CL_', 'LINK', '_LINK_', '_MSPDBSRV_ENDPOINT_',
                   'MSBUILDDISABLENODEREUSE', 'EID3D11_SHADER_STORE', 'EIREBUILD64_SHADER_RESOURCE_MANIFEST'}
    for key, value in sorted(os.environ.items()):
        if key not in overwritten and not key.startswith(('CODEX_', 'OTEL_')):
            field('env', key, value)
    if args.manifest:
        manifest = json.loads(args.manifest.read_text('utf-8-sig'))
        field('shader-manifest', manifest)
        donor = Path(manifest['accepted_exe'])
        if sha(donor) != manifest['accepted_exe_sha256']:
            raise ValueError('accepted shader donor changed')
        for row in manifest['files']:
            if sha(args.manifest.parent / row['name']) != row['sha256']:
                raise ValueError('shader payload changed')
        for row in manifest['libraries']:
            name = row['name']
            folder = args.cache / 'work/out/x64/Release' / name
            for ext, key in (('.lib', 'sha256'), ('.pdb', 'pdb_sha256')):
                if sha(folder / (name + ext)) != row[key]:
                    raise ValueError('shader reuse library changed')
    return digest.hexdigest()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for arg in ('repo', 'project', 'vs', 'vc', 'sdk', 'assets', 'materials', 'pwsh', 'cache'):
        parser.add_argument('--' + arg, type=Path, required=True)
    parser.add_argument('--sdk-version', required=True)
    parser.add_argument('--manifest', type=Path)
    try:
        print(fingerprint(parser.parse_args()))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'Kit reuse unavailable: {error}', file=sys.stderr)
        sys.exit(3)
