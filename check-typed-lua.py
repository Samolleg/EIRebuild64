"""Headless native-object/Lua integration test; never opens the game window."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--mod", type=Path, required=True)
parser.add_argument("--game", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
exe = root / "out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe"
report = root / "evidence/addon-integration/typed-runtime.json"
inputs = [args.mod / "main.lua", args.mod / "config.reg", args.mod / "config/ai.reg"]
inputs += sorted((args.mod / "lua").glob("*.lua"))
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest().upper()
before = {str(p): sha(p) for p in inputs}
subprocess.run([str(exe), "--inspect-addon", str(args.mod.resolve()), str(report)],
               cwd=args.game, timeout=40, check=True,
               creationflags=subprocess.CREATE_NO_WINDOW)
data = json.loads(report.read_text(encoding="utf-8"))
assert data["success"] and data["fullMainExecuted"]
assert not data["gameplayActivated"]
checks = data["typedNativeBridge"]
for key in ("arrayBoundsRejected", "expiredObjectRejected", "manaAndMoveWriteback",
            "rawAddressRejected", "typedItemWriteback", "wrongKindRejected"):
    assert checks[key], key
assert 67.18 < checks["nativeStatsHealth"] < 67.19
assert 43.03 < checks["nativeStatsMana"] < 43.04
assert before == {str(p): sha(p) for p in inputs}, "Inputs changed during test"
evidence = {"exe": sha(exe), "inputs": before, "nativeChecks": checks,
            "test": "full installed main.lua and scoped native fixture objects",
            "gameplayActivated": False, "success": True}
(report.parent / "typed-check.json").write_text(
    json.dumps(evidence, indent=2, ensure_ascii=False), encoding="utf-8")
print(json.dumps(checks, indent=2))
