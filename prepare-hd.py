import hashlib, json, pathlib, shutil, struct

root = pathlib.Path(__file__).resolve().parent
mod = pathlib.Path(r'C:\GOG Games\Evil Islands\Starter\MODS\JamaisVu135')
dest = root.parent/'handoffs/eirebuild-x86-hd-20260916-r3'
dest.mkdir(exist_ok=False)
(dest/'HD').mkdir()
(dest/'Config').mkdir()
groups = {'textures.res': [], 'figures.res': [], 'redress.res': []}
section = ''
for line in (mod/'config.ini').read_text(encoding='cp1251').splitlines():
    line = line.strip()
    if line.startswith('['): section = line.lower()
    if section != '[res]' or '=' not in line: continue
    group, name = line.split('=', 1)
    if group in groups: groups[group].append(name)

def digest(path):
    with path.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()

records, winners = [], {}
for group, names in groups.items():
    winners[group] = {}
    for relative in names:
        source = mod/relative
        target = dest/'HD'/source.name
        with source.open('rb') as f:
            sig, count, offset, namesize = struct.unpack('<4I', f.read(16))
            assert sig == 0x19ce23c, (source, hex(sig))
            assert offset + count*22 + namesize <= source.stat().st_size
            f.seek(offset)
            table, pool = f.read(count*22), f.read(namesize)
            entries = []
            for i in range(count):
                nxt, size, data, stamp, length, namepos = struct.unpack_from('<4IHI', table, i*22)
                if not data: continue
                assert data+size <= offset and namepos+length <= namesize
                assert nxt == 0xffffffff or nxt < count
                name = pool[namepos:namepos+length].decode('cp1251').lower()
                entries.append(name)
                winners[group].setdefault(name, source.name)
        shutil.copy2(source, target)
        original_hash = digest(source)
        assert original_hash == digest(target)
        records.append(dict(group=group, source=str(source), target='HD/'+source.name,
                            size=source.stat().st_size, sha256=original_hash, entries=len(entries)))
lines = [f"{r['group']}={r['target'].replace('/', chr(92))}" for r in records]
(dest/'Config/graphics-resources.ini').write_bytes(('\r\n'.join(lines)+'\r\n').encode('ascii'))
out = root/'out/Win32/Release'
shutil.copy2(out/'EI_Rebuild_dx11_Release_Win32.exe', dest/'EIRebuild.exe')
(dest/'shaders').mkdir()
for p in (out/'shaders/Win32_Release').glob('*.cso'): shutil.copy2(p, dest/'shaders'/p.name)
sym = root/'symbols/20260916-r3'; sym.mkdir()
shutil.copy2(out/'EI_Rebuild_dx11_Release_Win32.pdb', sym/'EIRebuild.pdb')
result = dict(source_manifest=str(mod/'config.ini'), source_manifest_sha256=digest(mod/'config.ini'),
              exe_sha256=digest(dest/'EIRebuild.exe'), archives=records,
              selected_entries={g:len(v) for g,v in winners.items()},
              examples={g:dict(list(v.items())[:8]) for g,v in winners.items()},
              runtime_verified=False)
(root/'evidence/hd-resources.json').write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf-8')
print(json.dumps({'archives':len(records),'bytes':sum(r['size'] for r in records),'entries':result['selected_entries']},ensure_ascii=False))
