"""Regression for the production kit input identity, using small local SDK fixtures."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scratch', type=Path, required=True)
    scratch = parser.parse_args().scratch
    spec = importlib.util.spec_from_file_location('kit_inputs', Path(__file__).with_name('kit-inputs.py'))
    implementation = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(implementation)
    with tempfile.TemporaryDirectory(dir=scratch) as tmp:
        root = Path(tmp)
        repo = root/'repo'
        repo.mkdir()
        def git(*args):
            subprocess.run(['git','-C',repo,*args], check=True, capture_output=True)
        git('init','-b','main')
        git('config','user.name','Test')
        git('config','user.email','test@localhost')
        git('config','commit.gpgsign','false')
        git('config','core.hooksPath',str(root/'disabled-hooks'))
        (repo/'packages.config').write_text('<packages/>')
        git('add','.')
        git('commit','-m','base')
        dirs = ['assets/eid3d_hosted','materials','deps/physx/5.13.0/sdk','deps/dlss-sdk/v310.9.16',
                'deps/ffmpeg/9.0.2/x64','deps/fidelityfx-sdk/2.4.0','deps/agility-sdk/1.620.2',
                'deps/rtxdi/3.2.0','deps/nvapi/70d337db','deps/dxc/1.9.2609','deps/cgltf/v1.15',
                'deps/zstd/v1.5.7/lib','vs/MSBuild','vs/VC/Auxiliary/Build','vc/bin/Hostx64/x64',
                'vc/lib/x64','vc/include','sdk/Include/1','sdk/Lib/1','sdk/bin/1/x64','cache/work/out/obj']
        for name in dirs:
            (root/name).mkdir(parents=True)
        for name in ('deps/ffmpeg/9.0.2/downloads/ffmpeg-9.0.2.tar.xz','deps/zstd/v1.5.7/LICENSE',
                     'deps/zstd/v1.5.7/COPYING','tools/project/invoke-build.ps1','tools/project/build_process.py',
                     'materials/material.json','vc/bin/Hostx64/x64/cl.exe','external.h'):
            file=root/name
            file.parent.mkdir(parents=True,exist_ok=True)
            file.write_bytes(b'abc')
        project = next(p for p in Path(__file__).resolve().parents if (p/'RULES.md').is_file())
        shutil.copy2(project/'tools/pz_deploy_preview.py', root/'tools/pz_deploy_preview.py')
        (root/'cache/work/out/obj/CL.read.1.tlog').write_text(str(root/'external.h'),encoding='utf-16')
        args = SimpleNamespace(repo=repo, project=root, vc=root/'vc', vs=root/'vs', sdk=root/'sdk',
                               sdk_version='1', pwsh=Path(sys.executable), cache=root/'cache', manifest=None,
                               assets=root/'assets', materials=root/'materials', save_external=root/'reads.json',
                               check_external=None)
        (repo/'author.cpp').write_text('source')
        clean_input = implementation.fingerprint(args)
        warm = implementation.InputHashes(root, args.cache)
        probe = repo/'author.cpp'
        warm.sha(probe)
        with warm.io.open_snapshot(probe) as handle:
            reliable = warm.io.file_token(handle) is not None
        assert warm.reused == int(reliable)
        with patch.object(warm.io, 'file_token', return_value=None):
            assert warm.sha(probe) == hashlib.sha256(probe.read_bytes()).hexdigest()
            assert warm.hashed == 2 - int(reliable)
        git('add','author.cpp')
        git('commit','-m','commit the checked candidate')
        args.check_external=args.save_external
        assert implementation.fingerprint(args)==clean_input
        with patch.dict(os.environ,{'BROWSER_USE_ACCESS_PIPE_PATH':'different-session'}):
            assert implementation.fingerprint(args)==clean_input
        for name in ('materials/material.json','vc/bin/Hostx64/x64/cl.exe','external.h',
                     'deps/ffmpeg/9.0.2/downloads/ffmpeg-9.0.2.tar.xz'):
            file=root/name
            stamp=file.stat()
            file.write_bytes(b'xyz')
            os.utime(file,ns=(stamp.st_atime_ns,stamp.st_mtime_ns))
            assert implementation.fingerprint(args)!=clean_input,name
            file.write_bytes(b'abc')
        # Replacement at the same path, timestamp and size must miss the cache.
        file = root/'external.h'
        old = file.stat()
        replacement = root/'replacement'
        replacement.write_bytes(b'xyz')
        os.utime(replacement, ns=(old.st_atime_ns, old.st_mtime_ns))
        replacement.replace(file)
        assert implementation.fingerprint(args) != clean_input
        file.write_bytes(b'abc')
        (root/'cache/kit-input-sha-v1.cache').write_bytes(b'partial write')
        assert implementation.fingerprint(args) == clean_input
        (repo/'untracked.h').write_bytes(b'new')
        assert implementation.fingerprint(args) != clean_input
        (repo/'untracked.h').unlink()
        assert implementation.fingerprint(args) == clean_input
        (repo/'author.cpp').write_text('other')
        assert implementation.fingerprint(args)!=clean_input
        donor = root/'accepted.exe'
        donor.write_bytes(b'donor')
        res = root/'test.res'
        res.write_bytes(b'resource')
        manifest = {'accepted_exe': str(donor), 'accepted_exe_sha256': hashlib.sha256(b'donor').hexdigest(),
                    'files': [{'name': res.name, 'sha256': hashlib.sha256(b'resource').hexdigest()}],
                    'libraries': []}
        args.manifest=root/'manifest.json'
        args.manifest.write_text(json.dumps(manifest))
        first=implementation.fingerprint(args)
        args.manifest.write_text(json.dumps(manifest, sort_keys=True, indent=2))
        assert implementation.fingerprint(args)==first
        res.write_bytes(b'changed')
        try:
            implementation.fingerprint(args)
        except ValueError as error:
            assert 'shader payload changed' in str(error)
        else:
            raise AssertionError('changed shader payload was accepted')
        print('PASS staged/committed source, launcher noise, changed source, material, compiler, SDK and external tlog dependency')


if __name__=='__main__':
    main()
