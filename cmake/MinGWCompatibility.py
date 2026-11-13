"""Apply exact, reviewed compatibility patches to build-directory copies only."""

from pathlib import Path
import re
import sys


def apply_patch(root, patch):
    lines = patch.read_text(encoding="utf-8").splitlines(keepends=True)
    index = 0
    while index < len(lines):
        if not lines[index].startswith("--- "):
            raise ValueError(f"Unexpected patch line: {lines[index]}")
        relative = lines[index + 1][4:].strip()
        target = root / relative
        if not target.resolve().is_relative_to(root.resolve()):
            raise ValueError(f"Patch target outside build directory: {relative}")
        original = target.read_text(encoding="latin1").splitlines(keepends=True)
        output = []
        cursor = 0
        index += 2
        while index < len(lines) and lines[index].startswith("@@ "):
            match = re.match(r"@@ -(\d+)(?:,\d+)? \+\d+(?:,\d+)? @@", lines[index])
            start = int(match[1]) - 1
            output.extend(original[cursor:start])
            cursor = start
            index += 1
            while index < len(lines) and not lines[index].startswith(("@@ ", "--- ")):
                operation, content = lines[index][0], lines[index][1:]
                if operation in " -":
                    if cursor >= len(original) or original[cursor] != content:
                        raise ValueError(f"Patch context mismatch: {relative}:{cursor + 1}")
                    cursor += 1
                if operation in " +":
                    output.append(content)
                index += 1
        output.extend(original[cursor:])
        target.write_text("".join(output), encoding="latin1", newline="\n")


if __name__ == "__main__":
    build_root = Path(sys.argv[1])
    for patch in sorted((Path(__file__).parent / "mingw").glob("*.patch")):
        apply_patch(build_root, patch)
