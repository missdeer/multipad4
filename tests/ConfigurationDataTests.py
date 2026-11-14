"""Compare reviewed product XML to every field of the compiled P0 baseline."""
from pathlib import Path
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parent.parent
baseline = ET.parse(root / "tests/baselines/lexer-catalog.xml").getroot()
product = ET.parse(root / "config/languages/default.xml").getroot()


def value(node):
    # Container indentation is editorial; leaf text, null and attributes are data.
    return node.tag, node.attrib, node.text if not len(node) else None, [value(child) for child in node]


languages = product.findall("language")
assert len(languages) == len(baseline.findall("language")) == 90
for source, target in zip(baseline.findall("language"), languages):
    assert source.get("id") == target.get("id")
    assert source.get("legacyIndex") == target.get("legacyIndex")
    for tag in ["field", "name", "extensions", "keywords", "styles", "completionExtras"]:
        assert [value(node) for node in source.findall(tag)] == [value(node) for node in target.findall(tag)], (source.get("id"), tag)
    groups = {node.get("id"): int(node.get("packed")) for node in target.findall("styles/style")}
    ids = set()
    for category in target.findall("categories/category"):
        assert category.get("id") not in ids
        ids.add(category.get("id"))
        packed = groups[category.get("group")]
        styles = [packed & 255]
        packed >>= 8
        while packed:
            styles.append(packed & 255)
            packed >>= 8
        if int(category.get("native")) not in styles:
            assert category.get("group")==target.get("id")+".default"
            assert category.get("id").split(".")[-1] in {"identifier","interface","enumeration"}
commands = set()
for language in languages:
    assert language.find('variants') is not None
    assert language.find('aliases') is not None
    for variant in language.findall('variants/variant'):
        command = int(variant.get('command'))
        assert command not in commands
        commands.add(command)
assert commands == set(range(40700, 40739))
for tag in ["dialectCompletion"]:
    assert [value(node) for node in baseline.findall(tag)] == [value(node) for node in product.findall(tag)]
print("90 languages: all fields, keyword/null slots, grouped styles and completion extras match P0")
