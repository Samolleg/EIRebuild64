# EI PAK version 1

One legacy archive maps to one PAK with the same basename, for example
`figures.res` becomes `figures.pak`. Nested MOD/ANM containers remain unchanged
inside their outer member. All integers are unsigned little-endian. Structures
are serialized field by field without compiler padding. There are no timestamps,
absolute input paths, random identifiers or platform-dependent fields.

## Header

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | Magic `45 49 50 41 4B 0D 0A 00` |
| 8 | 4 | Version, 1 |
| 12 | 4 | Header size, 40 |
| 16 | 8 | Entry count |
| 24 | 8 | Table offset, aligned to 8 bytes |
| 32 | 8 | Table byte size, extending exactly to EOF |

Payloads follow the header in entry order. Each starts at the next multiple of
8, with zero padding. The table follows the last payload at the next multiple
of 8. An empty archive has a table offset of 40 and a table size of zero.

## Table

Each record contains the following fixed fields, followed immediately by its name.

| Relative offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | Name byte count |
| 4 | 4 | Method, 0 for stored or 1 for zstd |
| 8 | 8 | Absolute payload offset |
| 16 | 8 | Stored byte count |
| 24 | 8 | Original byte count |
| 32 | 8 | XXH64 of original bytes with seed 0 |
| 40 | 8 | Reserved, zero |
| 48 | variable | Name bytes without terminator |

Records are strictly sorted by normalized byte name. Duplicate names, overlapping
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

Names use legacy resource byte strings. ASCII A-Z fold to a-z, and `/` becomes
`\`. The supported high bytes are E0-FF, which stay lowercase in the game's
1251/1252 lookup. Names contain 1 through 1024 bytes. Absolute paths, colons,
control bytes, empty/dot/parent segments, trailing dots/spaces and Windows device
names are rejected. Case-insensitive duplicates are collected before any output
archive is written. This follows the existing renderer's legacy name contract.

RES names can contain literal Windows-forbidden characters. The installed mod's
`texts.res` contains literal `?` characters. `unpack` percent-encodes `%`, `*`,
`?`, `"`, `<`, `>` and `|` in disk filenames using uppercase hexadecimal bytes.
`pack` decodes those escapes before validating resource names. For example,
`item%3F` on disk represents `item?` in the archive, and `item%253F` represents
the literal resource name `item%3f`. Slashes retain their directory meaning.
Lookup uses the decoded archive name. Extraction refuses symlink output entries.

## Commands

```text
EIRebuild64Pack pack <folder> <out.pak>
EIRebuild64Pack pack --cache <folder>
EIRebuild64Pack pack --cache <cache-folder> <folder> <out.pak>
EIRebuild64Pack unpack <in.res|in.pak> <folder>
EIRebuild64Pack verify <in.pak>
```

The short cached command writes `<folder>.pak` and uses the sibling
`.eipak-cache` directory. Outputs and caches must be outside the input directory.
The explicit cached form lets the migration share a cache across archives.
Cache keys include original XXH64, original size, codec version, level and recipe
version. Cached frames are decompressed and compared to source bytes before
reuse. A damaged entry is recompressed. Only changed or missing entries require
compression. Concurrent writers use separate staging files and atomically
replace completed PAK/cache files. An interrupted write preserves the earlier
published file. Error exit codes are 1 for operation failure and 2 for usage.

`unpack` accepts the game's original 0x019CE23C RES layout. Unused table slots
are skipped. Duplicate records referring to the same payload collapse to one
file, while conflicting duplicates fail extraction. All names are validated
before extracting a RES. No game installation files are written.

## Runtime integration

`CModPackage::ResourcePlan` retains the existing configured archive order within
each tier and resolves same-basename PAK siblings. Diagnostic loose files in the
mod directory precede mod PAK, then existing mod RES, then base RES according to
the existing ignore-base configuration. Existing config entries can continue to
name RES after conversion. An explicit PAK config path is also accepted.

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
