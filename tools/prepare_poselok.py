#!/usr/bin/env python3
"""Prepare an explicit, reviewable Poselok migration; never installs game files.

Proposal mode reads current inputs and writes exact edits. Assembly accepts that
same proposal after owner review, with source hashes and byte guards intact.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import difflib
from pathlib import Path

ROOT, SECTION, UNIT = 0xA000, 0xB000, 0xBBBB0000
SCRIPT, OLD_SCRIPT = 0xACCEECCB, 0xACCEECCA
ID, MODEL, NAME, POSITION, PLAYER = 0xB002, 0xB006, 0xB004, 0xB009, 0xB011
CONTAINERS = {UNIT, 0xBBBC0000, 0xBBBD0000, 0xBBBE0000}
POINTS = {POSITION, 0xBBBC0005, 0xBBBD0001, 0xBBBE0001}
SPATIAL = {"movetopoint", "rotateto", "umsentry", "distanceunitpoint", "setcp", "#dontbeidiot"}


def digest(data):
    return hashlib.sha256(data).hexdigest().upper()


def chunks(data):
    result, at = [], 0
    while at < len(data):
        if len(data)-at < 8:
            raise ValueError("Truncated MOB chunk header")
        kind, size = struct.unpack_from("<II", data, at)
        if size < 8 or at+size > len(data):
            raise ValueError("Invalid MOB chunk length")
        result.append((kind, data[at+8:at+size]))
        at += size
    return result


def pack(records):
    return b"".join(struct.pack("<II", kind, len(body)+8)+body for kind, body in records)


def one(records, kind):
    found = [body for key, body in records if key == kind]
    if len(found) != 1:
        raise ValueError(f"Expected one chunk {kind:08X}, found {len(found)}")
    return found[0]


def read_mob(path):
    raw = path.read_bytes()
    top = chunks(raw)
    if pack(top) != raw:
        raise ValueError("MOB no-op round trip differs")
    root = chunks(one(top, ROOT))
    if any(k == OLD_SCRIPT for k, _ in root):
        raise ValueError("The Poselok migration requires the existing coded source")
    coded = one(root, SCRIPT)
    seed, = struct.unpack_from("<I", coded)
    state, plain = seed, bytearray()
    for value in coded[4:]:
        state = (state*214013+2531011) & 0xFFFFFFFF
        plain.append(value ^ ((state >> 16) & 255))
    objects = chunks(one(root, SECTION))
    return raw, root, objects, seed, bytes(plain)


def encode_script(plain, seed):
    result, state = bytearray(struct.pack("<I", seed)), seed
    for value in plain:
        state = (state*214013+2531011) & 0xFFFFFFFF
        result.append(value ^ ((state >> 16) & 255))
    return bytes(result)


def fields(record):
    return dict(chunks(record[1]))


def object_id(record):
    return struct.unpack("<I", one(chunks(record[1]), ID))[0]


def text(data):
    return data.rstrip(b"\0").decode("cp1251")


def points(body, path=()):
    result = []
    counts = {}
    for kind, payload in chunks(body):
        index = counts.get(kind, 0)
        counts[kind] = index+1
        current = path+((kind, index),)
        if kind in POINTS:
            if len(payload) != 12:
                raise ValueError("Invalid NPC point")
            result.append((current, list(struct.unpack("<3f", payload))))
        elif kind in CONTAINERS:
            result.extend(points(payload, current))
    return result


def translated(point, offset):
    return [struct.unpack("<f", struct.pack("<f", value+shift))[0] for value, shift in zip(point, offset)]


def masked(code):
    """Hide comments and quoted text, retaining byte offsets and line numbers."""
    pattern = re.compile(r'//[^\r\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.S)
    return pattern.sub(lambda m: "".join("\n" if c == "\n" else "\r" if c == "\r" else " " for c in m[0]), code)


def calls(code):
    clean = masked(code)
    for match in re.finditer(r"([A-Za-z_#][A-Za-z0-9_#-]*)\s*\(", clean):
        prefix = clean[clean.rfind("\n", 0, match.start())+1:match.start()]
        if re.search(r"\b(?:Script|DeclareScript)\s*$", prefix):
            continue
        start, cursor, depth = match.end(), match.end(), 1
        spans, last = [], start
        while cursor < len(clean) and depth:
            char = clean[cursor]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if not depth:
                    spans.append((last, cursor))
                    break
            elif char == "," and depth == 1:
                spans.append((last, cursor))
                last = cursor+1
            cursor += 1
        if depth:
            raise ValueError("Unterminated script call")
        # Quoted literals in arguments are taken from the original bytes.
        yield match[1], match.start(), cursor+1, spans


def owner_at(code, offset):
    names = list(re.finditer(r"(?m)^\s*(?:Script\s+([^\s(]+)|(?P<world>WorldScript))\s*$", code[:offset]))
    return (names[-1][1] or "WorldScript") if names else "declaration"


def script_plan(plain, offset, local_keys, main=False):
    code = plain.decode("latin1")
    edits = []
    for name, begin, end, spans in calls(code):
        args = [code[a:b].strip() for a, b in spans]
        replacements = []
        if not main and name.lower() in SPATIAL:
            indices = [1, 2, 3, 4] if name.lower() == "#dontbeidiot" else [1, 2]
            if max(indices) >= len(args):
                raise ValueError(f"Unexpected spatial arguments: {name}")
            if not all(re.fullmatch(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)", args[i]) for i in indices):
                continue  # #DontBeIdiot's parameters are already world coordinates.
            for i in indices:
                a, b = spans[i]
                raw = code[a:b]
                value = float(args[i])+offset[(i-1) % 2]
                number = f"{value:.6f}".rstrip("0").rstrip(".")
                replacements.append((a-begin, b-begin, raw.replace(args[i], number, 1)))
        elif name.lower() == "isequalstring" and not main and len(args) == 2 and args[0] == "szComplete":
            key = args[1][1:-1] if args[1].startswith('"') and args[1].endswith('"') else ""
            if key in local_keys:
                a, b = spans[1]
                replacements.append((a-begin, b-begin, code[a:b].replace(key, local_keys[key], 1)))
        elif main and name.lower() in {"gssetvar", "gssetvarmax", "gsgetvar"} and len(args) >= 2:
            key = args[1][1:-1] if args[1].startswith('"') and args[1].endswith('"') else ""
            replacement = local_keys.get(key)
            if key == "b.bz1g.s1":
                replacement = "b.1m_elder.1m_s1"
            if replacement:
                a, b = spans[1]
                replacements.append((a-begin, b-begin, code[a:b].replace(key, replacement, 1)))
        if replacements:
            before = code[begin:end]
            after = before
            for a, b, value in sorted(replacements, reverse=True):
                after = after[:a]+value+after[b:]
            edits.append(dict(offset=begin, line=code.count("\n", 0, begin)+1, block=owner_at(code, begin), before=before, after=after))
    return edits


def apply_script(plain, edits):
    for row in sorted(edits, key=lambda e: e["offset"], reverse=True):
        start = row["offset"]
        before, after = row["before"].encode("latin1"), row["after"].encode("latin1")
        if plain[start:start+len(before)] != before:
            raise ValueError(f"Script changed at line {row['line']}")
        plain = plain[:start]+after+plain[start+len(before):]
    return plain


def replace_points(body, planned, path=()):
    result, counts = [], {}
    for kind, payload in chunks(body):
        index = counts.get(kind, 0)
        counts[kind] = index+1
        current = path+((kind, index),)
        if kind in POINTS:
            row = planned.pop(current)
            if payload != struct.pack("<3f", *row["before"]):
                raise ValueError("NPC coordinates differ from the approved list")
            # Preserve Z's original bytes, including its zero sign.
            payload = struct.pack("<2f", *row["after"][:2])+payload[8:]
        elif kind in CONTAINERS:
            payload = replace_points(payload, planned, current)
        result.append((kind, payload))
    return pack(result)


def replace_mob_script(raw, seed, script):
    records = []
    for kind, payload in chunks(raw):
        if kind == ROOT:
            payload = pack([(key, encode_script(script, seed) if key == SCRIPT else value)
                            for key, value in chunks(payload)])
        records.append((kind, payload))
    return pack(records)


def registration(analysis, source_script, peace):
    row = json.loads(json.dumps(peace["settlements"][0]))
    row["revision"] = 3
    row["mob"] = "settlements/1m_poselok.mob"
    row["aliases"] = {}
    for unit in analysis["basecam_units"]:
        names = unit["bindings"]+[entry["alias"] for entry in unit["display_aliases"]]+[unit["name"]]
        for name in names:
            if name and re.fullmatch(r"[A-Za-z0-9_#-]+", name) and not re.fullmatch(r"id\d+", name, re.I):
                old = row["aliases"].setdefault(name.lower(), unit["id"])
                if old != unit["id"]:
                    raise ValueError("Conflicting NPC alias")
    known = {name.lower() for name in row["aliases"]}
    imports = {key.lower(): value for key, value in row.get("object_imports", {}).items()}
    row["object_imports"] = imports
    # Only the shared hero and actors absent from this MOB are external names.
    for name, begin, end, spans in calls(source_script.decode("latin1")):
        if name.lower() != "getobjectbyname" or len(spans) != 1:
            continue
        a, b = spans[0]
        value = source_script[a:b].strip()
        if value.startswith(b'"') and value.endswith(b'"'):
            actor = value[1:-1].decode("cp1251")
            if actor.lower() not in known:
                imports.setdefault(actor.lower(), actor)
    for actor in ["Hero"]+[f"Merc{i}" for i in range(3, 11)]:
        imports.setdefault(actor.lower(), actor)
    state = row.setdefault("state_imports", {})
    state.update({"q.": "q.", "z.": "z.", "jmv.": "jmv.", "FirstComing": "FirstComing", "techTime": "techTime"})
    for i in range(1, 11):
        state[f"merc{i}"] = f"merc{i}"
    bindings = row["briefings"] = {}
    for briefing in analysis["briefings"]:
        if briefing["scope"] != "local":
            state[briefing["key"]] = briefing["key"]
            continue
        topic = briefing["topic"]
        if topic == "constr_1":
            binding = {"shop": 1}
        elif briefing["text_present"]:
            binding = {"text": topic}
            if briefing["db_record"]:
                binding["reward"] = briefing["db_record"]["name"]
        else:
            continue  # A script marker is retained without inventing a dialogue.
        previous = bindings.setdefault(topic, binding)
        if previous != binding:
            raise ValueError("Ambiguous briefing resource binding")
    return {"version": 1, "settlements": [row]}


def assemble(source, inventory, approved, peace, output, evidence):
    """Apply only the owner's exact list, after verifying both current sources."""
    if output.exists():
        raise ValueError("Candidate destination already exists; preserve it")
    source, output = source.resolve(), output.resolve()
    if output.is_relative_to(source.parent) or source.is_relative_to(output):
        raise ValueError("Candidate must be outside the installed mod")
    plan = json.loads(approved.read_text(encoding="utf-8"))
    if plan["version"] != 1:
        raise ValueError("Unknown migration plan")
    raw_base, base_root, objects, base_seed, base_script = read_mob(source/"basecam.mob")
    raw_zone, zone_root, zone_objects, zone_seed, zone_script = read_mob(source/"zone1.mob")
    if plan["source_sha256"] != dict(basecam=digest(raw_base), zone1=digest(raw_zone)):
        raise ValueError("A source MOB changed after owner approval; do not apply")
    planned_units = {u["id"]: u for u in plan["units"]}
    if len(planned_units) != 51:
        raise ValueError("Expected the approved 51 NPC records")
    units = []
    for record in objects:
        if record[0] != UNIT:
            continue
        unit = planned_units.pop(object_id(record))
        if digest(pack([record])) != unit["record_sha256"]:
            raise ValueError("NPC record changed after approval")
        planned = {tuple(tuple(step) for step in p["path"]): p for p in unit["points"]}
        body = replace_points(record[1], planned)
        if planned:
            raise ValueError("An approved NPC point was not applied")
        fields_out = []
        for kind, payload in chunks(body):
            if kind == PLAYER:
                if payload != bytes([unit["player_before"]]):
                    raise ValueError("NPC ownership changed after approval")
                payload = bytes([unit["player_after"]])
            fields_out.append((kind, payload))
        units.append((UNIT, pack(fields_out)))
    if planned_units:
        raise ValueError("An approved NPC is missing")
    new_base = apply_script(base_script, plan["scripts"]["basecam"])
    new_zone = apply_script(zone_script, plan["scripts"]["zone1"])
    module_root = [(0xD000, b""), (SCRIPT, encode_script(new_base, base_seed)),
                   (0xFF01, one(base_root, 0xFF01)), (SECTION, pack(units))]
    module = pack([(ROOT, pack(module_root))])
    main = replace_mob_script(raw_zone, zone_seed, new_zone)
    analysis = json.loads(inventory.read_text(encoding="utf-8-sig"))
    catalog = registration(analysis, base_script, json.loads(peace.read_text(encoding="utf-8")))
    # Parse prepared bytes back through the same container boundaries before
    # publication. Ordinary source checks do not replace engine/game acceptance.
    module_check = chunks(one(chunks(module), ROOT))
    if [object_id(o) for o in chunks(one(module_check, SECTION))] != [object_id(o) for o in units]:
        raise ValueError("Prepared module identities differ")
    if [(k, v) for k, v in chunks(one(chunks(main), ROOT)) if k != SCRIPT] != [(k, v) for k, v in zone_root if k != SCRIPT]:
        raise ValueError("An unrelated Zone1 chunk changed")
    if [(k, v) for k, v in chunks(main) if k != ROOT] != [(k, v) for k, v in chunks(raw_zone) if k != ROOT]:
        raise ValueError("Zone1 AI graph or another outer chunk changed")
    temp = output.with_name(output.name+".preparing")
    if temp.exists() or temp.parent != output.parent:
        raise ValueError("Preparation directory must be new and beside the candidate")
    temp.mkdir(parents=True)
    maps = temp/"Maps64"
    (maps/"settlements").mkdir(parents=True)
    (maps/"settlements/1m_poselok.mob").write_bytes(module)
    (maps/"zone1.mob").write_bytes(main)
    dependencies = {"AutoRunSpellAddon123.mob"}
    for name, begin, end, spans in calls(zone_script.decode("latin1")):
        if name.lower() == "addmob" and len(spans) == 1:
            a, b = spans[0]
            value = zone_script[a:b].strip()
            if not (value.startswith(b'"') and value.endswith(b'"')):
                raise ValueError("Zone1 has a dynamic MOB dependency")
            dependencies.add(value[1:-1].decode("cp1251"))
    copies = ["zone1.mpr"]+sorted(dependencies)
    copied_inputs = {}
    for name in copies:
        if Path(name).name != name:
            raise ValueError("Unexpected non-flat Zone1 dependency")
        input_path = source/name
        before_stat = input_path.stat()
        data = input_path.read_bytes()
        after_stat = input_path.stat()
        if (before_stat.st_size, before_stat.st_mtime_ns) != (after_stat.st_size, after_stat.st_mtime_ns):
            raise ValueError("A map input changed during preparation")
        (maps/name).write_bytes(data)
        copied_inputs[name] = dict(bytes=len(data), sha256=digest(data))
    (temp/"settlements.json").write_text(json.dumps(catalog, ensure_ascii=False, indent=2), encoding="utf-8")
    for name, expected_seed, expected_script in [("settlements/1m_poselok.mob", base_seed, new_base),
                                                  ("zone1.mob", zone_seed, new_zone)]:
        _, _, _, actual_seed, actual_script = read_mob(maps/name)
        if actual_seed != expected_seed or actual_script != expected_script:
            raise ValueError("Written MOB script differs from the approved result")
    source_info = dict(plan["source_sha256"])
    outputs = {}
    for file in sorted(temp.rglob("*")):
        if file.is_file():
            data = file.read_bytes()
            outputs[file.relative_to(temp).as_posix()] = dict(bytes=len(data), sha256=digest(data))
    manifest = dict(version=1, status="PREPARED - NOT INSTALLED - ENGINE/GAME ACCEPTANCE OPEN",
                    approved_edits_sha256=digest(approved.read_bytes()), source_mob_sha256=source_info,
                    copied_inputs=copied_inputs,
                    npcs=len(units), points=sum(len(u["points"]) for u in plan["units"]),
                    script_edits={k: len(v) for k, v in plan["scripts"].items()},
                    map_directory="Maps64", files=outputs)
    (temp/"manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    readme = """# Кандидат Посёлка для Zone1, Rebuild64

Подготовлен по подтверждённому EDITS этапа 6. В игру не установлен.

- `Maps64/settlements/1m_poselok.mob`: 51 NPC, их точки и все 243 блока исходного сценария Посёлка; без повторного окружения, графа и настроек мира.
- `Maps64/zone1.mob`: рабочая копия нынешней Zone1 с 10 согласованными ссылками на темы. Остальные чанки и AI-граф сохранены.
- `Maps64/zone1.mpr` и три служебных MOB скопированы без изменения. `settlements.json` регистрирует `1m_`, область, импорты, темы и магазин.
- `manifest.json` фиксирует входы, подтверждённый список и подготовленные файлы.

Это фрагмент данных Zone1, а не самостоятельный мод и не полный набор карт кампании. Движку x64 теперь нужен `Maps64`; старый `Maps` не используется как запасной источник. Остальные карты/фон/переходы потребуют отдельно подготовленного набора Maps64 перед полной игровой приёмкой.

Сборка контейнера и повторный разбор не подтверждают компиляцию MOB движком, проходимость NPC, диалоги/награды/найм и бой, save/load или качество постановки. Эти сценарии проверяет владелец на отдельно разрешённом игровом комплекте. Установка и запуск данным кандидатом не разрешаются.
"""
    (temp/"README_RU.md").write_text(readme, encoding="utf-8")
    # The resolved source and destination were checked above. This moves only
    # our newly created directory to its explicit sibling target.
    temp.rename(output)
    evidence.mkdir(parents=True, exist_ok=True)
    diffs = []
    for name, before, after in [("basecam", base_script, new_base), ("zone1", zone_script, new_zone)]:
        diffs.extend(difflib.unified_diff(before.decode("cp1251").splitlines(True), after.decode("cp1251").splitlines(True),
                                        fromfile=name+".mob/source", tofile=name+".mob/candidate"))
    (evidence/"scripts.patch").write_text("".join(diffs), encoding="utf-8", newline="")
    (evidence/"assembly.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


def proposal(source, inventory, output):
    if (output/"edits.json").exists() or (output/"EDITS.md").exists():
        raise ValueError("Preserve the existing proposal; use a new review directory")
    output.mkdir(parents=True, exist_ok=True)
    base, root, objects, seed, script = read_mob(source/"basecam.mob")
    zone, _, zone_objects, _, zone_script = read_mob(source/"zone1.mob")
    by_base = {object_id(o): o for o in objects if o[0] in {0xB001, UNIT, 0xBBAC0000, 0xBBBF0000}}
    by_zone = {object_id(o): o for o in zone_objects if o[0] in {0xB001, UNIT, 0xBBAC0000, 0xBBBF0000}}
    # The unchanged blacksmith-side house anchors the inherited village layout.
    a, b = fields(by_base[929]), fields(by_zone[155])
    if text(a[MODEL]) != "stbuho5" or text(b[MODEL]) != "stbuho5":
        raise ValueError("Village anchor changed; review placement before preparing")
    offset = [y-x for x, y in zip(struct.unpack("<3f", a[POSITION]), struct.unpack("<3f", b[POSITION]))]
    offset[2] = 0.0
    analysis = json.loads(inventory.read_text(encoding="utf-8-sig"))
    local = {row["key"]: row["qualified_key"] for row in analysis["briefings"] if row["scope"] == "local"}
    units = []
    for record in objects:
        if record[0] != UNIT:
            continue
        f = fields(record)
        geometry = [dict(path=list(path), before=point, after=translated(point, offset)) for path, point in points(record[1])]
        player = f[PLAYER][0]
        units.append(dict(id=object_id(record), name=text(f[NAME]), prototype=text(f[0xBBBB0002]),
                          player_before=player, player_after=2 if player == 0 else player, points=geometry,
                          record_sha256=digest(pack([record]))))
    if len(units) != 51:
        raise ValueError("Basecam population changed; refresh inventory")
    plan = dict(version=1, status="PROPOSED - OWNER APPROVAL REQUIRED", translation=offset,
                source_sha256=dict(basecam=digest(base), zone1=digest(zone)), units=units,
                scripts=dict(basecam=script_plan(script, offset, local), zone1=script_plan(zone_script, offset, local, True)))
    (output/"edits.json").write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding="utf-8")
    lines = ["# X64PEACE01, этап 6 — точные изменения кандидатов MOB", "",
             "Статус: **предложено; сценарные изменения и сборка MOB не применены**.", "",
             "Исходники — текущие установленные `basecam.mob` и `zone1.mob`; запись будет только в рабочий `Maps64`.",
             "Полный машинный список с байтовыми смещениями: [edits.json](edits.json). Ни один другой фрагмент скрипта не меняется.", "",
             f"- basecam SHA-256: `{digest(base)}`", f"- zone1 SHA-256: `{digest(zone)}`",
             f"- Перенос плоскости: X +{offset[0]:.9f}, Y +{offset[1]:.9f}, Z без смещения; опора — basecam 929 → Zone1 155 (`stbuho5`).",
             "- В отдельный MOB входят 51 Unit и полный исходный скрипт; здания, растения, свет, звук, AI-граф и настройки мира повторно не переносятся.",
             "- ID/имена изолирует загрузчик `1m_`; сырые числовые ID MOB сохраняются.", "",
             "## NPC: положение и принадлежность", "", "| ID | Имя | Исходное XYZ → новое XYZ | Игрок |", "|---|---|---|---|"]
    for unit in units:
        point = next(p for p in unit["points"] if p["path"][0][0] == POSITION)
        fmt = lambda p: ", ".join(f"{x:.4f}" for x in p)
        lines.append(f"| {unit['id']} | {unit['name']} | {fmt(point['before'])} → {fmt(point['after'])} | {unit['player_before']} → {unit['player_after']} |")
    lines += ["", "Сторожевые и маршрутные точки каждого NPC переносятся тем же смещением; точные значения находятся в `edits.json`. Повороты, характеристики, инвентарь и настройки AI сохраняются.",
              "Три исходных NPC игрока 0 предлагается отнести к жителям (игрок 2), чтобы несостоящие в отряде NPC не попадали под управление игрока на обычной карте.", "",
              "## Скрипты: было → стало", "",
              "Координаты и параметры `#DontBeIdiot` переносятся в Zone1. Сравнения результата разговора получают полный ключ `1m_`; остальные GS-вызовы самого модуля уже разрешаются загрузчиком в его контексте.",
              "В основном MOB меняются только ссылки на местные темы Посёлка. Прежние `LeaveToZone` остаются: код этапа 3 уже преобразует этот маршрут в локальное действие без смены карты.", ""]
    for name, rows in plan["scripts"].items():
        lines += [f"### {name}.mob — {len(rows)} замен", ""]
        for number, row in enumerate(rows, 1):
            display = lambda s: s.encode("latin1").decode("cp1251")
            lines += [f"{number}. `{row['block']}`, строка {row['line']}; одно совпадение по указанному байтовому месту.",
                      "", "```text", "Было: "+display(row["before"]), "Стало: "+display(row["after"]), "```", ""]
    (output/"EDITS.md").write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps(dict(units=len(units), point_fields=sum(len(u["points"]) for u in units),
                          script_edits={k: len(v) for k, v in plan["scripts"].items()}, translation=offset), indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--proposal", type=Path)
    parser.add_argument("--approved-edits", type=Path)
    parser.add_argument("--peace", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    if args.approved_edits:
        if not (args.peace and args.output and args.evidence) or args.proposal:
            parser.error("Assembly requires --peace, --output and --evidence; no --proposal")
        assemble(args.source, args.inventory, args.approved_edits, args.peace, args.output, args.evidence)
    elif args.proposal:
        proposal(args.source, args.inventory, args.proposal)
    else:
        parser.error("Choose proposal preparation or assembly of the approved list")


if __name__ == "__main__":
    main()
