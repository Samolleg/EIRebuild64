# ScriptCompilerCore

The MOB compiler shared by EIRebuild64 and ScriptCheck. Release/x64,
MSVC v143, C++17 and multibyte MFC. The game uses dynamic CRT/MFC; the
ScriptCheckCore project imports this project with static CRT/MFC. Both
compile the same Game/ScriptCompiler.cpp, ScriptParser.cpp and
ScriptLanguage.cpp. There is one parser implementation and one command
registry, including the commands already present in EIRebuild64.

The MAL17 separation is ported without its x86 serialization. Shared
types retain the current x64 initializers, SaveSchema declarations and
friends. SaveSchema still writes explicit fields, never object layouts.
Runtime SetObject binds the real retain/release operations; the standalone
compiler never owns a world object. Current x64 pointer-difference handling
and the zero-argument command registry fix are retained.

CScriptCompiler takes an explicit ScriptLanguage and an invariant reporter.
CompileText returns a compiled world thread without executing it. Game entry
points select the actual session language; ScriptCheck selects Supported().
ScriptText.cpp keeps file loading and text export in the game. WorldServer,
Lua, renderer, audio and mod startup are not linked into the compiler.

This separation changes in-memory class layouts and requires rebuilding
the game before installation. The save schema and language rules are
unchanged. Game build, execution and save/load acceptance are separate
from building the standalone tool.
