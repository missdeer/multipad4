"""Inventory P0 migration inputs; never interpret C++ initializer data.

Configuration values come only from lexer_baseline_export's compiled objects.
This script checks coverage and records consumers, bindings and provenance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "docs/lexer-baseline-inventory.json"
BASELINE = ROOT / "tests/baselines/lexer-catalog.xml"


def read(path):
    return path.read_text(encoding="utf-8-sig")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def declared_fields(struct):
    header = read(ROOT / "3rdparty/notepad4/src/EditLexer.h")
    body = header.split("struct " + struct + " {", 1)[1].split("};", 1)[0]
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", body, flags=re.S)
    body = body.split("constexpr", 1)[0]
    return [re.findall(r"\b\w+\b", declaration)[-1]
            for declaration in body.split(";") if declaration.strip()]


def inventory():
    root = ET.parse(BASELINE).getroot()
    if root.tag != "lexer-baseline" or root.get("version") != "1":
        raise ValueError("Unknown baseline schema")
    languages = root.findall("language")
    catalog = re.findall(r"^extern EDITLEXER (lex\w+);", read(ROOT / "tools/StaticLexerCatalog.h"), re.M)
    if catalog != [entry.get("symbol") for entry in languages]:
        raise ValueError("Baseline does not cover the ordered application catalogue")
    fields = declared_fields("EDITLEXER")
    style_fields = declared_fields("EDITSTYLE")
    direct = [entry.get("name") for entry in languages[0].findall("field")]
    strings = {"pszName", "szExtensions", "szStyleBuf", "pszDefExt"}
    if set(fields) != set(direct) | strings | {"pszKeyWords", "Styles"}:
        raise ValueError("EDITLEXER fields changed; update the compiled exporter")
    if set(style_fields) != {"iStyle", "rid", "iNameLen", "pszName", "pszDefault"}:
        raise ValueError("EDITSTYLE fields changed; update the compiled exporter")
    objects = {}
    sources = []
    for path in sorted((ROOT / "3rdparty/notepad4/src/EditLexers").glob("stl*.cpp")):
        symbols = re.findall(r"^EDITLEXER (lex\w+)\s*=", read(path), re.M)
        relative = path.relative_to(ROOT).as_posix()
        sources.append({"path": relative, "sha256": digest(path), "objects": symbols})
        for symbol in symbols:
            if symbol in objects:
                raise ValueError("Duplicate source object: " + symbol)
            objects[symbol] = relative
    if set(objects) != set(catalog):
        raise ValueError("Uncatalogued or missing upstream objects")
    style_count = keyword_count = 0
    migration = []
    for index, language in enumerate(languages):
        values = {entry.get("name"): int(entry.get("value")) for entry in language.findall("field")}
        if set(values) != set(direct):
            raise ValueError("Inconsistent exported fields")
        if int(language.get("legacyIndex")) != index or language.get("source") != objects[language.get("symbol")]:
            raise ValueError("Incorrect source or legacy index")
        words = language.findall("keywords/slot")
        styles = language.findall("styles/style")
        if len(words) != values["keywordCount"] or len(styles) != values["iStyleCount"]:
            raise ValueError("Missing keyword or style slots")
        for slot, word in enumerate(words):
            if int(word.get("index")) != slot or int(word.get("attributes")) != (values["keywordAttr"] >> (slot * 4)) & 15:
                raise ValueError("Keyword ordering/attributes mismatch")
        if len(language.findall("completionExtras/slot")) != 4:
            raise ValueError("Missing completion extras")
        migration.append({"id": language.get("id"), "legacyIndex": index, "resourceId": values["rid"],
                          "legacyName": language.findtext("name"), "styleIds": [style.get("id") for style in styles]})
        style_count += len(styles)
        keyword_count += len(words)
    consumers = []
    pattern = re.compile(r"\b(?:lex[A-Z]\w*|lex2ndTextFile|lexerCatalog|LanguageDefinition|LanguageStyle|LanguageCatalog|EDITLEXER|EDITSTYLE|LPCEDITLEXER|PEDITLEXER|NP2LEX_\w+|NP2STYLE_\w+|GetDocumentLexer|GetCompletionCache|GetCompletionExtraKeywords)\b")
    for path in sorted((ROOT / "src").glob("*")):
        if path.suffix not in {".h", ".cpp"}:
            continue
        hits = [{"line": index, "symbols": sorted(set(pattern.findall(line)))}
                for index, line in enumerate(read(path).splitlines(), 1) if pattern.search(line)]
        if hits:
            consumers.append({"path": path.relative_to(ROOT).as_posix(), "references": hits})
    # Source ranges explicitly identify application-side rules that are not
    # members of EDITLEXER; later phases must migrate or retain these bindings.
    rules = {
        "src/StyleManager.cpp": ["SchemeFilename", "ResolveScheme", "SchemeForCommand", "Dialect", "SelectDocumentScheme", "LoadStylePreferences", "SaveStylePreferences"],
        "src/Session.cpp": ["Scheme", "Dialect", "Style"],
        "src/CompletionEngine.cpp": ["kwDoxyDoc", "kwXmlSchema", "kwXmlStylesheet", "kwApache", "kwPList", "Initialize"],
        "src/LexerDetection.cpp": ["Detect", "MatchCPPKeyword"],
    }
    bindings = []
    for filename, tokens in rules.items():
        path = ROOT / filename
        bindings.append({"path": filename, "sha256": digest(path), "rules": [
            {"line": index, "text": line} for index, line in enumerate(read(path).splitlines(), 1)
            if any(token in line for token in tokens)]})
    localization = []
    paths = [ROOT / "3rdparty/notepad4/src/Notepad4.rc", *sorted((ROOT / "3rdparty/notepad4/locale").glob("*/Notepad4.rc"))]
    for path in paths:
        localization.append({"path": path.relative_to(ROOT).as_posix(), "sha256": digest(path),
                             "languageStrings": [line.strip() for line in read(path).splitlines() if re.search(r"\bNP2LEX_", line)]})
    return {"version": 1, "upstream": root.get("upstream"), "license": root.get("license"),
            "languageCount": len(languages), "sourceCount": len(sources), "styleCount": style_count,
            "keywordSlotCount": keyword_count, "lexerFields": fields, "styleFields": style_fields,
            "sources": sources, "migration": migration, "consumers": consumers,
            "applicationBindings": bindings, "localization": localization}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify the reviewed inventory without overwriting it")
    args = parser.parse_args()
    text = json.dumps(inventory(), ensure_ascii=False, indent=2) + "\n"
    if args.check:
        if not OUTPUT.exists() or read(OUTPUT) != text:
            raise SystemExit("Lexer inventory changed; regenerate and review docs/lexer-baseline-inventory.json")
    else:
        OUTPUT.write_text(text, encoding="utf-8", newline="\n")
    print("Lexer inventory coverage verified")


if __name__ == "__main__":
    main()
