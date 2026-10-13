"""Prepare a private build-only handoff; never start or install the game."""
import hashlib
import json
import pathlib
import re
import shutil
import struct
import xml.etree.ElementTree as ET

root = pathlib.Path(__file__).resolve().parent
reference = root.parent / 'src_eirebuild' / 'EIRebuild Good dx11'
output = root / 'out' / 'Win32' / 'Release'
handoff = root.parent / 'handoffs' / 'eirebuild-x86-20260916-r1'
if handoff.exists():
    raise SystemExit('Handoff already exists; use a new revision.')
handoff.mkdir(parents=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
inputs = []
for rel in ['EI_Rebuild.vcxproj', 'luajit-x86/luajit.vcxproj']:
    project = root / rel
    for node in ET.parse(project).getroot().iter():
        if node.tag.split('}')[-1] not in ('ClCompile', 'ClInclude', 'ResourceCompile', 'FxCompile', 'Object'):
            continue
        name = node.get('Include')
        if not name:
            continue
        path = project.parent / name
        local_rel = path.relative_to(root)
        old = reference / local_rel
        inputs.append({'path': str(local_rel), 'sha256': sha(path),
                       'reference_sha256': sha(old), 'unchanged': sha(path) == sha(old)})
assert all(x['unchanged'] for x in inputs), 'Unexpected gameplay/source modification'
exe = output / 'EI_Rebuild_dx11_Release_Win32.exe'
data = exe.read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
assert data[pe:pe+4] == b'PE\0\0'
assert struct.unpack_from('<H', data, pe+4)[0] == 0x14c
assert struct.unpack_from('<H', data, pe+24)[0] == 0x10b
imports = re.findall(r'^    ([\w.-]+\.dll)\s*$', (root/'evidence/pe-imports.txt').read_text(encoding='utf-8-sig'), re.M | re.I)
assert imports and 'mss32.dll' in imports
engine = pathlib.Path(r'C:\GOG Games\Evil Islands\Starter\Engine')
dlls = []
for name in imports:
    if name.lower().startswith('api-ms-'):
        dlls.append({'name': name, 'resolution': 'Windows API-set contract; runtime untested'})
        continue
    candidates = [output/name, pathlib.Path(r'C:\Windows\SysWOW64')/name, engine/name]
    found = next((p for p in candidates if p.is_file()), None)
    assert found, f'Missing direct dependency: {name}'
    if found.parent == engine:
        shutil.copy2(found, handoff/name)
    dlls.append({'name': name, 'path': str(found), 'sha256': sha(found)})
shutil.copy2(exe, handoff/exe.name)
# EIRebuild no longer builds or loads OpenEI shader artifacts; the embedded renderer owns its shader package.
# Keep exact matching debug information out of the user handoff.
symbols = root/'symbols'/'20260916-r1'
symbols.mkdir(parents=True, exist_ok=False)
shutil.copy2(output/(exe.stem+'.pdb'), symbols/(exe.stem+'.pdb'))
result = {'status': 'build-and-static-artifact-pass; gameplay-unverified',
          'source_root': str(root), 'reference_root': str(reference),
          'branch_commit': 'N/A: isolated source snapshot, not Git worktree',
          'exe_sha256': sha(exe), 'pdb_sha256': sha(symbols/(exe.stem+'.pdb')),
          'machine': 'x86 PE32 GUI', 'inputs': inputs, 'direct_imports': dlls,
          'files': [{'path': str(p.relative_to(handoff)), 'sha256': sha(p)}
                    for p in handoff.rglob('*') if p.is_file()],
          'not_verified': ['recursive DLL loads', 'initialization', 'map rendering', 'combat', 'save/load', 'addon ABI']}
(root/'evidence/handoff.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
(handoff/'README.txt').write_text(
    'EIRebuild x86, private build-only handoff, 2026-09-16 r1.\n'
    'Build and static artifact checks passed; game startup and gameplay NOT tested.\n'
    'This folder contains no game resources or isolated settings. Do not launch yet.\n'
    'Next: prepare isolated resources/config and registry settings, then test startup/map.\n'
    'Do not install over game.exe or load original-address addon/dinput hooks.\n'
    'Requires installed x86 Microsoft VC++ runtime and MFC. PDB retained separately.\n', encoding='utf-8')
print(json.dumps({'handoff': str(handoff), 'source_inputs_unchanged': len(inputs),
                  'direct_imports': len(imports), 'exe_sha256': sha(exe)}, ensure_ascii=False))
