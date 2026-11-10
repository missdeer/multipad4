"""Rebuild the upstream UI inventory; this is a migration ledger, not a parity claim."""
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[1]
source = root / '3rdparty/notepad4/src'
rc = (source / 'Notepad4.rc').read_text(encoding='utf-8-sig')
cpp = (source / 'Notepad4.cpp').read_text(encoding='utf-8-sig')
header = (source / 'Notepad4.h').read_text(encoding='utf-8-sig')
resources = (source / 'resource.h').read_text(encoding='utf-8-sig')
ids = {name: int(value, 0) for name, value in re.findall(r'^#define\s+(\w+)\s+(0x[\da-fA-F]+|\d+)\s*(?://[^\n]*)?$', resources, re.M)}
menus = []
menu = None
depth = 0
for line_number, line in enumerate(rc.splitlines(), 1):
    declaration = re.match(r'^(\w+)\s+MENU\s*$', line)
    if declaration:
        menu = declaration[1]
        depth = 0
    if menu:
        if line.strip() == 'BEGIN':
            depth += 1
        item = re.match(r'\s*MENUITEM\s+"((?:[^"\\]|\\.)*)",\s*(\w+)', line)
        if item:
            menus.append({'menu': menu, 'label': item[1], 'command': item[2], 'value': ids.get(item[2]), 'line': line_number})
        if line.strip() == 'END':
            depth -= 1
            if depth == 0:
                menu = None

dialogs = []
for match in re.finditer(r'^(IDD_\w+)\s+DIALOG(?:EX)?\b.*?^END\s*$', rc, re.M | re.S):
    caption = re.search(r'^CAPTION\s+"([^"]*)"', match[0], re.M)
    dialogs.append({'id': match[1], 'caption': caption[1] if caption else None, 'line': rc.count('\n', 0, match.start()) + 1})

toolbar_source = cpp[cpp.index('tbbMainWnd[]'):cpp.index('// NOLINTEND', cpp.index('tbbMainWnd[]'))]
toolbar = list(dict.fromkeys(re.findall(r'\bIDT_\w+', toolbar_source)))
status = list(dict.fromkeys(item for item in re.findall(r'\bStatusItem_\w+', header) if item != 'StatusItem_ItemCount'))
handlers = sorted(set(re.findall(r'^\s*case\s+(IDM_\w+)\s*:', cpp, re.M)))
commands = sorted(set(item['command'] for item in menus))
result = {
    'upstream_revision': '3fcfb70fd0d3c8802100c85f7293ccb326aa8b64',
    'note': 'All conditional resource variants are included. Inventory does not imply implementation or behavioral parity.',
    'counts': {'menu_entries': len(menus), 'unique_menu_commands': len(commands), 'dialogs': len(dialogs), 'toolbar_commands': len(toolbar), 'status_items': len(status), 'legacy_command_cases': len(handlers)},
    'menu_entries': menus,
    'dialogs': dialogs,
    'toolbar_commands': [{'id': command, 'value': ids.get(command)} for command in toolbar],
    'status_items': status,
    'legacy_command_cases': handlers,
    'accelerator_resources': re.findall(r'^(\w+)\s+ACCELERATORS\s*$', rc, re.M),
}
output = root / 'docs/notepad4-ui-inventory.json'
output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result['counts'], ensure_ascii=False))

