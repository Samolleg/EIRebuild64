"""Content identity for the Release x64 kit built by build-task.ps1."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
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
    names = git(repo, '-c', 'core.quotepath=false', 'ls-files', '--cached', '--others',
                '--exclude-standard', '-z').decode().split('\0')
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

    # The producer builds these exact files even if the author commits them
    # afterwards. Accept independently requires clean HEAD and identical trees.
    field('schema', 2, 'Release', 'x64', str(args.vc), str(args.sdk), args.sdk_version)
    properties = set()
    for name in sorted(set(names)):
        if name:
            record('source/' + name, repo / name)
            if name.endswith(('.vcxproj', '.props', '.targets', '.ps1', '.mak', '.cmd', '.bat')):
                text = (repo / name).read_bytes()
                for prop in re.findall(rb'\$\(([A-Za-z_][A-Za-z0-9_]*)\)|\$env:([A-Za-z_][A-Za-z0-9_]*)|%([A-Za-z_][A-Za-z0-9_]*)%', text, re.I):
                    properties.add(next(p for p in prop if p).decode().upper())
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
    for variable in ('INCLUDE', 'LIB', 'LIBPATH', 'VCTargetsPath', 'DirectoryBuildPropsPath',
                     'DirectoryBuildTargetsPath', 'ForceImportBeforeCppTargets', 'ForceImportAfterCppTargets'):
        for i, value in enumerate(filter(None, os.environ.get(variable, '').split(';'))):
            path = Path(value)
            if not path.is_absolute():
                raise ValueError(f'cannot certify relative {variable}')
            if path.exists():
                roots[f'override/{variable}/{i}'] = path
    # MSBuild also discovers directory-wide imports up the persistent build tree.
    for directory in (args.cache / 'work', *(args.cache / 'work').parents):
        for name in ('Directory.Build.props', 'Directory.Build.targets', 'Directory.Build.rsp', 'MSBuild.rsp'):
            path = directory / name
            if path.is_file() and not (directory == args.cache / 'work' and name in names):
                roots['ancestor/' + str(path)] = path
    for label, path in sorted(roots.items()):
        record(label, path)
    # Track reads outside the recipe (e.g. an additional imported SDK or a
    # system resource used by the linker). Keep the successful build's list:
    # a later task is allowed to replace the shared work tree and its tlogs.
    if args.check_external:
        external = json.loads(args.check_external.read_text('utf8'))
    else:
        work = args.cache / 'work'
        known = {os.path.normcase(str(path)) for path in [work, *roots.values()]}
        prefixes = tuple(path.rstrip('\\') + '\\' for path in known)
        seen = set()
        for log in (work / 'out').rglob('*.read.*.tlog'):
            lines = log.read_text('utf-16').splitlines()
            for line in set(lines):
                if line.startswith('^'):
                    continue
                name = os.path.normcase(line.removeprefix('\\\\?\\'))
                if os.path.isabs(name) and name not in known and not name.startswith(prefixes):
                    seen.add(name)
        external = sorted(seen)
        args.save_external.write_text(json.dumps(external), encoding='utf8')
    for path in external:
        record('tracked/' + path, Path(path))
    # MSBuild imports environment properties; store only the resulting digest.
    overwritten = {'TMP', 'TEMP', 'CL', '_CL_', 'LINK', '_LINK_', '_MSPDBSRV_ENDPOINT_',
                   'MSBUILDDISABLENODEREUSE', 'EID3D11_SHADER_STORE', 'EIREBUILD64_SHADER_RESOURCE_MANIFEST'}
    properties.update(('PATH','PATHEXT','INCLUDE','LIB','LIBPATH','COMSPEC','SYSTEMROOT',
                       'PLATFORM','CONFIGURATION','PROCESSOR_ARCHITECTURE','EI_PYTHON'))
    for key, value in sorted(os.environ.items()):
        if key not in overwritten and (key in properties or key.startswith(
                ('MSBUILD','VCTOOLS','WINDOWSSDK','EIREBUILD64_', 'EICACHE', 'EID3D', 'VSCMD_', 'UCRT'))):
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
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--save-external', type=Path)
    mode.add_argument('--check-external', type=Path)
    try:
        print(fingerprint(parser.parse_args()))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'Kit reuse unavailable: {error}', file=sys.stderr)
        sys.exit(3)
