"""Run the native callsite check in the headless engine, preserving R10 evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--mod", required=True, type=Path)
parser.add_argument("--game", required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent
exe = root / "out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe"
report = root / "evidence/addon-integration/r11-runtime.json"
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest().upper()
# Known installed package inputs, including both declared database archives.
paths = [args.mod / p for p in ("main.lua", "config.reg", "config/ai.reg",
                                "res/databaseJmv.res", "res/database.res")]
paths += sorted((args.mod / "lua").glob("*.lua"))
before = {str(p): sha(p) for p in paths}
binary_hash = sha(exe)
subprocess.run([str(exe), "--inspect-addon", str(args.mod.resolve()), str(report)],
               cwd=args.game, timeout=40, check=True,
               creationflags=subprocess.CREATE_NO_WINDOW)
data = json.loads(report.read_text(encoding="utf-8"))
assert data["success"] and data["fullMainExecuted"] and not data["gameplayActivated"]
dispatch = data["engineDispatch"]
for field in ("nativeHpMpCallsites", "movementCallsite", "failedMutationRolledBack",
              "invalidNumberRejected", "constStatsPreserved", "callbackReferenceExpired",
              "sessionOwnership", "baseMovementPreserved"):
    assert dispatch[field], field
assert dispatch["realPerkPriceCases"] == 96 and dispatch["skillPriceCases"] == 6
assert data["typedNativeBridge"]["constructorForms"]
assert before == {str(p): sha(p) for p in paths}, "Test inputs changed"
assert binary_hash == sha(exe), "Binary changed during the check"
binary = exe.read_bytes()
pe = struct.unpack_from("<I", binary, 60)[0]
assert binary[pe:pe+4] == b"PE\0\0"
assert struct.unpack_from("<H", binary, pe+4)[0] == 0x14c
assert struct.unpack_from("<H", binary, pe+24)[0] == 0x10b
evidence = {"success": True, "exe": binary_hash, "inputs": before,
            "engineDispatch": dispatch, "artifact": "PE32 x86",
            "limits": "Headless fixture stats and loaded real perk DB; movement shared helper, not full L1ExecMove or a played game"}
(report.parent / "r11-check.json").write_text(
    json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(dispatch, ensure_ascii=False, indent=2))
