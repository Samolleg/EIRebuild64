# EI PAK version 1.1 — integration contract

One legacy archive maps to one PAK with the same basename, for example
`figures.res` becomes `figures.pak`. Nested MOD/ANM containers remain unchanged
inside their outer member. All integers are unsigned little-endian. Structures
are serialized field by field without compiler padding. There are no timestamps,
absolute input paths, random identifiers or platform-dependent fields.

## Header

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | Magic `45 49 50 41 4B 0D 0A 00` |
| 8 | 4 | Version, `0x00010001` (1.1); old version 1 is also readable |
| 12 | 4 | Header size, 72 for 1.1; 40 for version 1 |
| 16 | 8 | Entry count |
| 24 | 8 | Table offset, aligned to 8 bytes |
| 32 | 8 | Table byte size, extending exactly to EOF |
| 40 | 32 | SHA-256 of the exact table bytes, version 1.1 only |

Payloads follow the header in entry order. Each starts at the next multiple of
8, with zero padding. The table follows the last payload at the next multiple
of 8. An empty 1.1 archive has a table offset of 72 and a table size of zero.

## Table

Each record contains the following fixed fields, followed immediately by its name.

| Relative offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | Name byte count |
| 4 | 4 | Method, 0 for stored, 1 for zstd, 2 for deleted (1.1 only) |
| 8 | 8 | Absolute payload offset |
| 16 | 8 | Stored byte count |
| 24 | 8 | Original byte count |
| 32 | 8 | XXH64 of original bytes with seed 0 |
| 40 | 8 | Reserved, zero |
| 48 | variable | Name bytes without terminator |

Records retain the original name bytes and are strictly sorted by lookup key.
Method 2 is a tombstone: original size, stored size and XXH64 are all zero;
there is no payload, and the offset is the next aligned payload position.
A tombstone stops lookup in every lower mount, including original base RES.
An empty method-0 file is a real resource, distinct from a tombstone.
Duplicate lookup keys, overlapping
payloads, unknown methods, nonzero reserved fields and invalid ranges are rejected.
Original sizes are limited to UINT32_MAX by existing game resource APIs. Empty
input files are valid: method 0, both sizes zero, and XXH64 of empty bytes
(seed 0, 0xef46db3751d8e999). The pack command lists them as warnings and
returns success. Zero-length payloads may share the next aligned offset.

XXH64 provides a fast corruption check consistent with the measured decompression
throughput. It is bundled with zstd, so the format needs one local dependency.
The archive is intended for trusted local game content. Integrity checks are
performed by `verify` and `unpack`, and once per entry per open archive during
diagnostic runtime reads. Ordinary runtime reads validate ranges, method and
decompressed size. Each method-1 member contains exactly one independent frame
produced by zstd 1.5.7 at level 3, with no dictionary or worker threads.

MP3 frame/ID3, 7z, ZIP, gzip, PNG, JPEG, zstd and Ogg signatures select method 0.
All other members use level 3, including members whose compressed size increases.
For these pinned inputs and codec version, the same input produces identical PAK
bytes with an empty or populated compression cache.

## Names and filesystem round-trip

Names are original RES byte strings, not UTF-8 and not pre-folded. Names contain
1 through 1024 non-NUL bytes. Lookup uses the fixed CRT CP1251 lowercase
table (`_tolower_l(byte, _create_locale(LC_CTYPE, ".1251"))`), independent of
Font CharSet and process locale, with separators unchanged. Duplicate lookup
keys are rejected before publication. No Unicode folding or normalization is
applied. The byte mapping is 41-5A -> 61-7A, C0-DF -> E0-FF,
and 80:90, 81:83, 8A:9A, 8C:9C, 8D:9D, 8E:9E, 8F:9F,
A1:A2, A3:BC, A5:B4, A8:B8, AA:BA, AF:BF, B2:B3, BD:BE
(all hexadecimal); all other bytes remain unchanged.
Legacy RES lookup remains unchanged: `CResourceBase::GetIndexOfName`
uses `sum(tolower(unsigned-byte)) % count` and `_strnicmp` with equal lengths;
`CTextManager::Create` selects LC_CTYPE from Font CharSet.

Directory components with equal lookup keys use the spelling of their first
occurrence in archive record order; file components retain their own spelling.
Thus a folder round-trip preserves lookup keys and payload bytes, rather than
the original directory case. No raw-name metadata is introduced.

For disk paths, decode legacy bytes through CP1251 to Unicode. A backslash is
a directory separator. Percent-encode `%`, `<>:"|?*`, `/`, bytes below 0x20,
trailing dots/spaces of a component, and the first byte of a Windows device name
(CON, PRN, AUX, NUL, COM1-9, LPT1-9, including extensions). Escapes are `%XX`, with
uppercase hex for the original byte. `pack` decodes escapes before lookup-key
validation. For example, `item%3F` is `item?`, and `item%253F` is literal
`item%3F`. The raw resource `_deleted.txt` is written as `%5Fdeleted.txt`, to
avoid collision with the deletion list. Empty path components are rejected and
extraction refuses symlink output entries. Use a fresh output folder for unpack.

The root `_deleted.txt` is CP1251/legacy bytes, without BOM, one raw resource name
per line; writers emit CRLF, and readers also accept LF. Empty lines are ignored.
It is metadata, never packed as an ordinary resource. Example:

```text
old_item.adb
Effects\obsolete.wav
```

`pack` converts these names to method-2 records, rejecting duplicate deleted
names and any key also represented by a live file. `unpack` recreates the list.
A physical deletion from a source folder alone does not suppress base fallback;
add the raw name to this list. RES timestamps, slot order and alignment are not
preserved: RES is an input only, not a byte-for-byte re-export format.
## Commands

```text
EIRebuild64Pack pack <folder> <out.pak>
EIRebuild64Pack pack --cache <folder>
EIRebuild64Pack pack --cache <cache-folder> <folder> <out.pak>
EIRebuild64Pack unpack <in.res|in.pak> <folder>
EIRebuild64Pack verify <in.pak>
EIRebuild64Pack list <in.pak> --json
EIRebuild64Pack extract <in.pak> <legacy-name> <file>
EIRebuild64Pack apply <in.pak> <changes.json>
EIRebuild64Pack txn <changes-group.json>
EIRebuild64Pack recover <mod-folder>
```

The short cached command writes `<folder>.pak` and uses the sibling
`.eipak-cache` directory. Outputs and caches must be outside the input directory.
The explicit cached form lets the migration share a cache across archives.
Cache keys include original XXH64, original size, codec version, level and recipe
version. Cached frames are decompressed and compared to source bytes before
reuse. A damaged entry is recompressed. Only changed or missing entries require
compression. PAK writers acquire an exclusive lock and atomically
replace completed PAK files; cache files use independent staging files. An interrupted write preserves the earlier
published file. Error exit codes are 1 for operation failure and 2 for usage.

`unpack` accepts the game's original 0x019CE23C RES layout. Unused table slots
are skipped. Duplicate records referring to the same payload collapse to one
file, while conflicting duplicates fail extraction. All names are validated
before extracting a RES. No game installation files are written.

## Writer API and command schemas

`Pak/` is the reference C++ implementation. Native mapper integrations must pin
the rebuild64 commit containing it (including codec and JSON dependencies), as
with navigation. `Apply`, `Transaction`, and `Recover` in `Pak.h` expose the same
implementation as the CLI. .NET and Python writers must invoke the utility,
never implement another PAK serializer. Independent readers may follow this
document, checking their results against `list --json` and `extract`.

CLI JSON is UTF-8, without BOM. Resource identity is `name_hex`, an even-length
hexadecimal string of original name bytes; this avoids JSON encoding ambiguity.
`list --json` emits an array of objects containing `name_hex`, `path` (escaped
Unicode path, UTF-8 in JSON, forward-slash directory separators), `method`,
`size`, `stored` (byte counts), and `hash` (16 hex digits representing the eight
little-endian XXH64 bytes). Tombstones have method 2 and zero sizes/hash.
`extract` accepts a Unicode command-line name converted losslessly to CP1251;
it errors for an absent or deleted name and verifies the extracted payload.

`apply archive.pak changes.json` creates or edits one archive. Example:

```json
{
  "expected_sha256": "<lowercase SHA-256 of opened archive; null if new>",
  "changes": [
    {"op": "put", "name_hex": "6974656D2E616462", "file": "edited/item.adb"},
    {"op": "delete", "name_hex": "6F6C642E616462"}
  ]
}
```

`put` inserts or replaces a resource, including replacing a tombstone. `delete`
adds/replaces a tombstone even when the name was not in the top archive. There
is no operation that silently removes a tombstone and restores lower fallback.
Repeated lookup keys in one change list are errors. Relative `file` paths are
relative to the JSON file, never the process working directory. Unchanged
members are read and verified; directory spelling follows the canonicalization rule above.
A failed
single-archive operation leaves the previously published PAK in place.

`txn changes-group.json` applies a group on one filesystem volume:

```json
{
  "archives": [
    {"pak": "res/database.pak", "expected_sha256": "<opened SHA-256>", "changes": [
      {"op": "put", "name_hex": "6974656D2E616462", "file": "edited/item.adb"}
    ]},
    {"pak": "res/texts.pak", "expected_sha256": "<opened SHA-256>", "changes": [
      {"op": "put", "name_hex": "6974656D2E747874", "file": "edited/item.txt"}
    ]}
  ]
}
```

Relative archive paths are relative to the group JSON. Empty groups, duplicate
target paths and targets on different volumes are errors. The caller selects
every archive affected by a semantic edit; the writer cannot infer cross-file
references. These groups must be saved and initially migrated together:

| Edit | Required group |
| --- | --- |
| Database records and referenced text/audio | database plus affected texts, speech and/or sfx archives |
| Animation and animation database | archive containing ANM plus archive containing its matching ADB |
| Terrain/map and atlas | archive containing MPR plus archive(s) containing the matching atlas/profile/pages |

If both members belong to one PAK, use one `apply`. Several resources in the
same archive must be combined into one change list, not repeated targets.

## Publication, locking and recovery

An exclusive Windows file handle to `<target>.pak.lock` serializes writers;
its JSON contains `pid`, `application` and UTC Unix seconds (`utc`). File presence
alone is not a lock. The handle denies sharing and deletes the lock on close,
including process termination. Each writer must acquire the lock **before**
reading the old archive. Stale editor snapshots must be refreshed before Save.

Single archive publication writes `<target>.pak.tmp`, flushes the file with
`FlushFileBuffers`, fully verifies the table and every live payload, then uses
`MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`. The old file is never edited in
place. `apply` holds the target lock throughout read/build/verify/rename.

A group acquires every target lock in ascending case-insensitive absolute path
order, writes and verifies every `.tmp`, then durably publishes
`pak-txn-<id>.json` in the common ancestor directory. Its publication is the
commit point: cancellation after that point requires recovery, not rollback.
Example manifest (paths are relative to the manifest directory):

```json
{
  "version": 1,
  "targets": [
    {"target": "res/database.pak", "tmp": "res/database.pak.tmp", "sha256": "<64 hex digits>"},
    {"target": "res/texts.pak", "tmp": "res/texts.pak.tmp", "sha256": "<64 hex digits>"}
  ]
}
```

The hashes cover entire new PAK files, not only tables. After publishing the
manifest, replace all targets and remove the manifest last. Replacement of a
group is recoverable, not an instantaneous multi-file operation: readers must
not open a group for editing while its manifest exists.

`recover <mod-folder>` scans for manifests, acquires their target locks in the
same order, checks **all** hashes and full PAK verification before any further
rename, then completes the group. If a `.tmp` has already been renamed, the
target must match its expected SHA-256; this permits recovery after any subset
of replacements. Missing/mismatching members fail recovery and retain evidence.
No manifest means uncommitted `.pak.tmp` and `.pak.tmp.work` files may be removed
under their respective target locks. A new writer refuses pending manifests
and stale staging files. Deployment and editors must recover before opening a
group; the game only warns and never performs recovery or writes PAK files.

## Reader lifetime and cache identity

PAK handles use `FILE_SHARE_READ | FILE_SHARE_DELETE`, including the renderer's
container handle. A writer can replace the pathname while an old reader is
open; that reader retains its old immutable snapshot. The header/table/payload
must all be read through that same handle, including its size.

The version-1.1 table SHA-256 at offset 40 is the cache identity. It covers raw
names, methods, sizes, offsets and per-payload hashes. It changes on a rebuild
with changed resource data/metadata, except cryptographic/hash collisions; an
identical deterministic rebuild intentionally retains identity. Editors compare
it using a newly opened handle, then invalidate indexes and decoded resources
if changed. Do not read the new header while retaining old offsets or payload
caches. Version 1 has no header identity; hash its table when importing it.

The game does not hot-reload open archive objects. Reopening resources (for
example where a zone load actually closes/reopens them) can see replacements;
restart is the reliable boundary for every reader/cache. No live reload of an
already open zone is promised. Full `verify` is required before deployment and
publication; runtime payload XXH64 checking remains diagnostic-only.

## Runtime integration

`CModPackage::ResourcePlan` retains the existing configured archive order within
each tier and resolves same-basename PAK siblings. Diagnostic loose files in the
mod directory precede mod PAK, then unshadowed mod RES, then base RES according to
the existing ignore-base configuration. Existing config entries can continue to
name RES after conversion. An explicit PAK config path is also accepted.
When `X.pak` exists, its sibling mod `X.res` is omitted entirely, with a
`res_shadowed` journal warning. Missing PAK entries may still fall back to
other configured unshadowed archives or base RES; tombstones stop that search.

`CMappedResource` and `CWin32MappedResource` serve PAK entries through the shared
`Pak/Pak.cpp` implementation. They retain only requested, reference-counted
payloads until CloseFile/CloseAllFiles. The embedded resource store mounts the
same plans and reads PAK members through that implementation, with weak payload
references. Graphics mount collection applies the PAK tier across categories.
Loose mod mounts are enabled only for `--diagnostics` or `--background`.
PAK open/read errors emit journal/probe events containing the archive/member path
and return failure to the resource caller.

The eimcp implementations of `res_list`, `res_entry_info` and `model_views` live
in the separate tools repository, `tools/eimcp/assets.py` and `server.py`.

## Local build

`tools/prepare-worktree.ps1` copies zstd 1.5.7 from local project deps into ignored
`dependencies/zstd/v1.5.7`. `Pak/Zstd.props` compiles its common, compression and
decompression C sources with the static CRT for the engine and pack utility.
`EIRebuild64Pack.vcxproj` is part of `EI_Rebuild.sln`. No dependency download or
restore is performed. The dependency license and bundled xxHash notice are
recorded in `THIRD_PARTY.md`.

Every apply request and txn member requires expected_sha256: the lowercase SHA-256
of the editor snapshot, or JSON null for a new archive. All preconditions are
checked under all writer locks before staging any archive. A mismatch rejects
the entire edit; callers must reload and resolve conflicts, never retry with a
new hash attached to stale payloads.
