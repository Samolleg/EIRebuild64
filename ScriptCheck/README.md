# ScriptCheck

Standalone EIRebuild64 MOB script compiler, Release/x64. Input files are
opened read-only; scripts are compiled without execution, world startup,
Lua, renderer, audio or mod loading.

```text
ScriptCheck.exe "path\map.mob"
ScriptCheck.exe --text "path\unsaved-buffer.txt"
ScriptCheck.exe --stdin < unsaved-buffer.txt
```

Text and stdin are raw Windows-1251. Filenames use Unicode Windows APIs.
Input is limited to 256 MiB. Embedded NUL is rejected; MOB Script chunks
may have a single trailing terminator. MOB extraction uses the existing
Game/SaveLoader.cpp and Game/SimpleCryptographer.cpp.

One UTF-8 JSON line without BOM is written to stdout, as in MAL18:

```json
{"ok":true,"line":0,"code":0,"error":"No errors found","compiler":"EIRebuild <commit>"}
```

Exit codes: 0 = compilation succeeded, 1 = script error, 2 = input/utility
failure. The first compiler line/code/error is preserved, with messages
transcoded from CP1251. Utility failures have line 0 and code -1. Compiler
identity is the source commit recorded at build time, with `-dirty` for
tracked uncommitted edits. It is not replaced with MAL18's commit.

Uses ScriptLanguage::Supported() and the current x64 command registry.
Inputs compile independently: secondary AddMob files do not inherit
their parent's globals/groups and can report unresolved names. Syntax
acceptance does not validate map references or gameplay. Callers must
treat a crash or missing/malformed JSON as utility failure.

Requires Git, VS 2022 C++/MFC v143 and Windows SDK:

```text
MSBuild EI_Rebuild.sln /t:ScriptCheck /p:Configuration=Release /p:Platform=x64
```

Output: `out/x64/Release/ScriptCheck.exe`. ScriptCheckCore imports
ScriptCompilerCore with `/MT` and static MFC. No x86, app-local VC++ or
MFC runtime DLLs are required. The game uses the same compiler sources
through its dynamic-runtime ScriptCompilerCore library.

A delivery's `tools/ScriptCheck` directory also includes this README and
`manifest.json` with the full source `commit`, executable `sha256`,
`runtime` (`static CRT/MFC, x64`) and `path` (`ScriptCheck.exe`). Malachite
packages this directory after checking the manifest hash and PE machine;
it does not require a compiler checkout or Git at packaging time.
