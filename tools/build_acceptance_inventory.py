"""Index every pinned UI item and its evidence without claiming behavioral parity.

Usage: python tools/build_acceptance_inventory.py tmp/migration-status-check.json
The runtime JSON is produced by multipad4 --migration-report. Test references
are navigation aids only; neither a reference nor a supported flag is a pass.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--output", type=Path, default=root / "docs/MIGRATION_ACCEPTANCE_INVENTORY.json")
    args = parser.parse_args()
    runtime_bytes = args.runtime.read_bytes()
    runtime = json.loads(runtime_bytes.decode("utf-8-sig"))
    inventory = json.loads((root / "docs/notepad4-ui-inventory.json").read_text(encoding="utf-8"))
    upstream = root / "3rdparty/notepad4/src"
    resource = (upstream / "resource.h").read_text(encoding="utf-8-sig")
    ids = {name: int(value, 0) for name, value in re.findall(
        r"^#define\s+(\w+)\s+(0x[\da-fA-F]+|\d+)\s*(?://[^\n]*)?$", resource, re.M)}
    # MigrationSmokeTest's dialog-resource loop only verifies resource presence.
    test_files = sorted((root / "tests").glob("*.h")) + [root / "src/SmokeTest.h", root / "src/FileCommandSmokeTest.h"]
    references = {}
    for path in test_files:
        for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
            for name in set(re.findall(r"\b(?:IDM_|CMD_|BME_|IDT_|IDD_|IDACC_)\w+", line)):
                references.setdefault(name, []).append(f"{path.relative_to(root).as_posix()}:{number}")

    def row(name, value=None):
        value = ids.get(name) if value is None else value
        state = runtime.get(str(value))
        return {"id": name, "value": value,
                "runtime_supported": state.get("supported") if state else None,
                "test_references": references.get(name, []),
                "behavioral_acceptance": "pending"}

    commands = {}
    for entry in inventory["menu_entries"]:
        item = commands.setdefault(entry["command"], row(entry["command"], entry["value"]))
        item.setdefault("upstream_menu_entries", []).append({key: entry[key] for key in ("menu", "label", "line")})
    dialogs = [{**row(item["id"]), "upstream_line": item["line"], "caption": item["caption"]} for item in inventory["dialogs"]]
    toolbar = [row(item["id"], item["value"]) for item in inventory["toolbar_commands"]]
    status = [{"index": i, "id": name, "behavioral_acceptance": "pending"} for i, name in enumerate(inventory["status_items"])]
    accelerators = []
    rc = (upstream / "Notepad4.rc").read_text(encoding="utf-8-sig")
    for table, body in re.findall(r"^(\w+)\s+ACCELERATORS\s*\nBEGIN\n(.*?)^END", rc, re.M | re.S):
        for key, name, flags in re.findall(r'^\s*("[^"]+"|VK_\w+),\s*(\w+),([^\n]+)', body, re.M):
            item = {**row(name), "table": table, "key": key, "flags": flags.strip()}
            if name == "CMD_CTRLTAB":
                item["mdi_override"] = "ID_NEXTTAB (Ctrl+Tab); original insert-tab command is superseded"
            if name == "IDM_FILE_EXIT" and key == '"W"' and "CONTROL" in flags:
                item["mdi_override"] = "ID_CLOSE (Ctrl+W); close the active document"
            if table == "IDR_ACCFINDREPLACE":
                item["route_owner"] = "SearchDialog::Procedure; not a MainFrame command"
            accelerators.append(item)
    rows = list(commands.values())
    result = {
        "upstream_revision": inventory["upstream_revision"],
        "note": "All conditional resource variants included. Test references and runtime support are not behavioral acceptance. See MIGRATION_ACCEPTANCE.md for scenario results.",
        "runtime_report": args.runtime.as_posix(),
        "runtime_report_sha256": hashlib.sha256(runtime_bytes).hexdigest(),
        "counts": {"menu_commands": len(rows), "menu_commands_routed": sum(item["runtime_supported"] is True for item in rows),
                   "menu_commands_with_test_references": sum(bool(item["test_references"]) for item in rows),
                   "dialogs": len(dialogs), "toolbar_buttons": len(toolbar), "status_fields": len(status),
                   "upstream_accelerator_entries": len(accelerators)},
        "menu_commands": rows, "dialogs": dialogs, "toolbar_buttons": toolbar,
        "status_fields": status, "accelerators": accelerators,
        "environment_checks": ["real mouse and keyboard input", "IME composition", "drag and drop",
                               "multi-monitor DPI", "physical/virtual printer output", "real system integration"],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result["counts"], ensure_ascii=False))


if __name__ == "__main__":
    main()
