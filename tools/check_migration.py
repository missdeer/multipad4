"""Compare the pinned resource inventory with actual runtime command routing.

This is a wiring check. Behavioral regression and visual acceptance are separate.
Run multipad4 --migration-report tmp/migration-runtime.json before this script.
"""
from pathlib import Path
import json
import sys

root = Path(__file__).resolve().parents[1]
runtime_path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "tmp/migration-runtime.json"
runtime = json.loads(runtime_path.read_text(encoding="utf-8-sig"))
inventory = json.loads((root / "docs/notepad4-ui-inventory.json").read_text(encoding="utf-8"))
commands = {item["command"]: item["value"] for item in inventory["menu_entries"]}
missing = {name: value for name, value in commands.items()
           if value is None or not runtime.get(str(value), {}).get("supported", False)}
print(f"Menu commands routed: {len(commands) - len(missing)}/{len(commands)}")
for name, value in sorted(missing.items(), key=lambda item: (item[1] or 0, item[0])):
    print(f"  {value}: {name}")
sys.exit(bool(missing))
