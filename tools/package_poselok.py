#!/usr/bin/env python3
"""Assemble a private, immutable playtest handoff; never installs or runs it."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
from datetime import datetime, timezone

import prepare_poselok as mob


def sha(data):
    return hashlib.sha256(data).hexdigest().upper()


def read(path):
    before = path.stat()
    data = path.read_bytes()
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError(f"Input changed while reading: {path}")
    return data


def info(data):
    return {"bytes": len(data), "sha256": sha(data)}


def resource(path, name):
    """Read the native RES/MPR table without loading the engine."""
    with path.open("rb") as f:
        header = f.read(16)
        magic, count, table, names_size = struct.unpack("<4I", header)
        if magic != 0x019CE23C or count > 65536:
            raise ValueError(f"Invalid RES: {path}")
        f.seek(table)
        index = f.read(count * 22 + names_size)
        found = None
        for n in range(count):
            at = n * 22
            size, offset = struct.unpack_from("<II", index, at + 4)
            length, text_at = struct.unpack_from("<HI", index, at + 16)
            text = index[count * 22 + text_at:count * 22 + text_at + length].decode("cp1251")
            if text.lower() != name.lower():
                continue
            if offset < 16 or offset + size > table or found is not None:
                raise ValueError(f"Invalid/duplicate RES entry {name}: {path}")
            f.seek(offset)
            found = f.read(size)
            if len(found) != size:
                raise ValueError(f"Truncated RES entry {name}: {path}")
        return found


def short_chunks(data):
    result, at = [], 0
    while at < len(data):
        if len(data) - at < 2:
            raise ValueError("Truncated resource chunk header")
        kind = data[at]
        width = 4 if data[at + 1] & 1 else 1
        if len(data) - at < width + 1:
            raise ValueError("Truncated resource chunk length")
        size = int.from_bytes(data[at + 1:at + width + 1], "little") >> 1
        at += 1 + width
        if size > len(data) - at:
            raise ValueError("Invalid resource chunk length")
        result.append((kind, data[at:at + size]))
        at += size
    return result


def single(records, kind):
    values = [value for key, value in records if key == kind]
    if len(values) != 1:
        raise ValueError(f"Expected one resource field {kind}, got {len(values)}")
    return values[0]


def briefing_names(data):
    body = single(short_chunks(data), 1)
    table = single(short_chunks(body), 2)
    names = set()
    for kind, record in short_chunks(table):
        if kind != 1:
            raise ValueError("Unexpected briefing record")
        name = single(short_chunks(record), 0).rstrip(b"\0").decode("cp1251")
        # CDatabase builds its name map by assignment; repeated placeholder
        # names are legal in the installed resource and do not remove rewards.
        names.add(name)
    return names


def map_key(value):
    value = value.replace("\\", "/")
    lower = value.lower()
    if lower.startswith("maps64/"):
        value = value[7:]
    elif lower.startswith("maps/"):
        value = value[5:]
    parts = value.split("/")
    if any(p in ("", ".", "..") for p in parts) or ":" in value:
        raise ValueError(f"Unsafe map path: {value}")
    return value.lower()


def manifest_files(root):
    return {p.relative_to(root).as_posix(): info(read(p))
            for p in sorted(root.rglob("*")) if p.is_file() and p.name != "SHA256SUMS"}


def sums(root):
    records = manifest_files(root)
    (root / "SHA256SUMS").write_text("".join(
        f"{value['sha256'].lower()}  {name}\n" for name, value in records.items()), encoding="utf-8", newline="\n")
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--symbols", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--build-log", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    workspace, game = args.workspace.resolve(), args.game.resolve()
    output, symbols = args.output.resolve(), args.symbols.resolve()
    for target in (output, symbols):
        if not target.is_relative_to(workspace / "work/handoffs") or target.is_relative_to(game):
            raise ValueError("Handoff must be inside the workspace handoff directory, outside the game")
        if target.exists() or target.with_name(target.name + ".assembling").exists():
            raise ValueError(f"Refusing to replace a handoff: {target}")
    subprocess.run(["git", "-C", str(repo), "diff", "--exit-code", args.source_commit,
                    "--", "Game", "EI_Rebuild.vcxproj", "external/owned-renderer"], check=True)
    mod = game / "MODS/Dejavu"
    candidate = repo / "data/settlements/zone1-poselok"
    prepared = json.loads(read(candidate / "manifest.json"))
    edits_path = workspace / "work/supervision/X64PEACE01/stage6/edits.json"
    edits_raw = read(edits_path)
    if sha(edits_raw) != prepared["approved_edits_sha256"]:
        raise ValueError("Approved edit list changed")
    edits = json.loads(edits_raw)["scripts"]["zone1"]
    if sha(read(mod / "Maps/basecam.mob")) != prepared["source_mob_sha256"]["basecam"]:
        raise ValueError("Source settlement changed since its approved migration")
    for name, expected in prepared["files"].items():
        if info(read(candidate / name)) != expected:
            raise ValueError(f"Prepared settlement changed: {name}")
    raw, records, _, seed, code = mob.read_mob(mod / "Maps/zone1.mob")
    updated = code
    for edit in sorted(edits, key=lambda e: e["offset"], reverse=True):
        at = edit["offset"]
        before, after = edit["before"].encode("cp1251"), edit["after"].encode("cp1251")
        if updated[at:at + len(before)] != before:
            raise ValueError("Zone1 script changed; requires a new reviewed edit list")
        updated = updated[:at] + after + updated[at + len(before):]
    if updated != mob.read_mob(candidate / "Maps64/zone1.mob")[4]:
        raise ValueError("Current Zone1 script differs from the approved source")
    root_payload = mob.pack([(kind, mob.encode_script(updated, seed)
                             if kind == mob.SCRIPT else body) for kind, body in records])
    source_top = mob.chunks(raw)
    zone = mob.pack([(kind, root_payload if kind == mob.ROOT else body) for kind, body in source_top])
    if len(zone) != len(raw) + len(updated) - len(code):
        raise ValueError("Zone1 container size differs from the approved script delta")
    # Registration and the NPC MOB remain exactly the accepted stage-six candidate.
    registry_bytes = read(candidate / "settlements.json")
    definition = json.loads(registry_bytes)["settlements"][0]
    rewards = {v["reward"] for v in definition["briefings"].values() if v.get("reward")}
    # Same explicit override order as the current mod config. Record the owner.
    qdb, qdb_owner = None, None
    for relative in ("res/databaseJmv.res", "res/database.res"):
        qdb = resource(mod / relative, "quests.qdb")
        if qdb is not None:
            qdb_owner = relative
            break
    if qdb is None or not rewards <= briefing_names(qdb):
        raise ValueError("Current quest database is missing settlement rewards")
    layout_bytes = read(mod / "world-map-layouts.json")
    layout = json.loads(layout_bytes)
    files = sorted(p for p in (mod / "Maps").iterdir() if p.is_file()
                   and p.suffix.lower() in (".mpr", ".mob", ".mq", ".grp"))
    keys = {map_key(p.name) for p in files}
    if len(keys) != len(files):
        raise ValueError("Duplicate map resource key")
    layout_refs = set()
    layout_reference_count = 0
    for profile in layout["profiles"]:
        for allod in profile["allods"]:
            for entry in allod["zones"]:
                for key in ("mprPath", "mobPath"):
                    if entry.get(key):
                        layout_reference_count += 1
                        layout_refs.add(map_key(entry[key]))
    if not layout_refs <= keys:
        raise ValueError(f"Incomplete background maps: {sorted(layout_refs - keys)}")
    config_ini = read(mod / "config.ini")
    config_reg = read(mod / "config.reg")
    for path in (b"res\\databaseJmv.res", b"res\\database.res"):
        if path.lower() not in config_ini.lower() or path.lower() not in config_reg.lower():
            raise ValueError("Database providers no longer match the selected mod")
    dll_root = repo / "dependencies/ffmpeg/8.1.3/x64/bin"
    support = workspace / "work/handoffs/eirebuild64-render01-r2"
    stage = output.with_name(output.name + ".assembling")
    symbol_stage = symbols.with_name(symbols.name + ".assembling")
    stage.mkdir(parents=True)
    symbol_stage.mkdir(parents=True)
    written, inputs = {}, {}

    def put(name, data):
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        written[name] = info(data)

    def copy(source, name):
        data = read(source)
        put(name, data)
        inputs[str(source)] = info(data)

    for source in files:
        copy(source, "MODS/Dejavu/Maps64/" + source.name)
    put("MODS/Dejavu/Maps64/zone1.mob", zone)
    copy(candidate / "Maps64/settlements/1m_poselok.mob", "MODS/Dejavu/Maps64/settlements/1m_poselok.mob")
    put("MODS/Dejavu/settlements.json", registry_bytes)
    put("MODS/Dejavu/world-map-layouts.json", layout_bytes)
    exe_root = repo / "out/x64/Release/EI_Rebuild"
    copy(exe_root / "EIRebuild64.exe", "Engine64/EIRebuild64.exe")
    copy(exe_root / "nvngx_dlss.dll", "Engine64/nvngx_dlss.dll")
    for name in ("avcodec-62.dll", "avformat-62.dll", "avutil-60.dll", "swresample-6.dll", "swscale-9.dll"):
        copy(dll_root / name, "Engine64/" + name)
    # Reuse notices and matching LGPL source, never a historical EXE or settings.
    for directory in ("licenses", "sources"):
        for path in sorted((support / directory).rglob("*")):
            if path.is_file():
                copy(path, path.relative_to(support).as_posix())
    for name in ("THIRD_PARTY.md", "THIRD_PARTY_SOURCE.md"):
        copy(support / name, name)
    copy(repo / "docs/settlement-persistence.md", "docs/SETTLEMENT_CHECK_RU.md")
    readme = read(repo / "data/settlements/poselok-playtest/README_RU.md").decode("utf-8")
    readme = readme.replace("@SOURCE_COMMIT@", args.source_commit)
    put("README_RU.md", readme.encode("utf-8"))
    critical = {str(mod / name): info(read(mod / name)) for name in (
        "config.ini", "config.reg", "SPELLADDON.INI", "main.lua", "world-map-layouts.json",
        "res/database.res", "res/databaseJmv.res", "res/texts.res")}
    check_raw, check_records, _, check_seed, check_code = mob.read_mob(stage / "MODS/Dejavu/Maps64/zone1.mob")
    if check_raw != zone or check_seed != seed or check_code != updated or \
            [(k, v) for k, v in check_records if k != mob.SCRIPT] != [(k, v) for k, v in records if k != mob.SCRIPT]:
        raise ValueError("Zone1 readback changed non-script chunks")
    if [(k, v) for k, v in mob.chunks(check_raw) if k != mob.ROOT] != \
            [(k, v) for k, v in source_top if k != mob.ROOT]:
        raise ValueError("Zone1 readback changed outer chunks, including its AI graph")
    for name, expected in written.items():
        if info(read(stage / name)) != expected:
            raise ValueError(f"Handoff readback differs: {name}")
    pdb = read(exe_root / "EIRebuild64.pdb")
    (symbol_stage / "EIRebuild64.pdb").write_bytes(pdb)
    report = {"version": 1, "status": "PREPARED, NOT INSTALLED, GAME ACCEPTANCE OPEN",
              "utc": datetime.now(timezone.utc).isoformat(), "branch": "task/X64PEACE01",
              "native_source_commit": args.source_commit, "save_schema": 6,
              "map_files": len(files) + 1, "mpr": sum(p.suffix.lower() == ".mpr" for p in files),
              "mob": sum(p.suffix.lower() == ".mob" for p in files) + 1,
              "background_references": len(layout_refs), "background_reference_count": layout_reference_count,
              "zone1_approved_script_edits": len(edits),
              "zone1_input": info(raw), "zone1_output": info(zone),
              "settlement_mob": written["MODS/Dejavu/Maps64/settlements/1m_poselok.mob"],
              "quest_owner": qdb_owner, "quest_payload": info(qdb), "bound_rewards_present": len(rewards),
              "engine": written["Engine64/EIRebuild64.exe"], "symbols": info(pdb),
              "build_log": str(args.build_log.resolve()), "build_log_sha256": sha(read(args.build_log)),
              "critical_shared_inputs_not_distributed": critical, "copied_inputs": inputs,
              "files": written, "zone1_non_script_chunks_preserved": True,
              "engine_execution": "not run", "tests": "not run", "installation": "not performed"}
    put("manifest.json", (json.dumps(report, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    sums(stage)
    (symbol_stage / "PAIR.json").write_text(json.dumps({"source": args.source_commit,
        "exe": report["engine"], "pdb": info(pdb)}, indent=2) + "\n", encoding="utf-8", newline="\n")
    sums(symbol_stage)
    # Publish once. Existing handoffs are immutable, even after a failed assembly.
    os.rename(symbol_stage, symbols)
    os.rename(stage, output)
    print(json.dumps({"handoff": str(output), "symbols": str(symbols), "maps": report["map_files"],
                      "mpr": report["mpr"], "mob": report["mob"], "engine": report["engine"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
