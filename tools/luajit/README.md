# LuaJIT x64 GC64 static library

Build with VS 2022 Build Tools, v143 and a Windows SDK installed:

```powershell
msbuild tools/luajit/luajit.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64
```

Alternatively run `powershell -NoProfile -ExecutionPolicy Bypass -File
tools/luajit/build.ps1`. The script locates the existing VS installation with
vswhere; it does not install tools or fetch dependencies. Only Release x64 is
declared. `-Force` refreshes a previously built library.

The recipe copies pinned `src/`, `dynasm/` and `.relver` into
`out/obj/x64/Release/luajit/source/`. It uses the pinned `msvcbuild.bat` steps:
compile minilua, generate `buildvm_arch.h` from `vm_x64.dasc`, generate
`luajit.h` from `.relver`, compile buildvm, generate the PE VM object and all
definition headers, compile C sources and archive the static library.
These build generators run as part of compilation; no interpreter or game is
run for verification. No `luajit.exe` is built.

Only the private batch copy changes: add `/MD /DLUAJIT_ENABLE_GC64`, finish after
the library, and return a nonzero failure code. Upstream x64 enables GC64 through
`lj_arch.h` by default; `LUAJIT_ENABLE_GC64` documents the explicit request,
while `LUAJIT_DISABLE_GC64` is never passed. `vcvars64.bat` selects native x64
tools. No Lua 5.2 compatibility switch is enabled. No vendor C/header is patched.

Outputs: `out/x64/Release/luajit/luajit.lib`, matching public headers in `include/`
(including generated `luajit.h`), `jit/` Lua modules and `COPYRIGHT`. Build objects,
generated headers, VM object and compiler PDB remain in the private intermediate
directory. Existing successful output is reused when its inputs are unchanged.

The solution/project dependency retains its project GUID and library path.
The host includes only the matching public headers from the generated include/
directory and links this library. P14 removes LuaState and the entire old
luajit-x86 tree; no internal lj_* headers are used by the host.
Building this library does not imply that EIRebuild64 links or runs.