import argparse
from collections import defaultdict
from functools import lru_cache
from itertools import combinations
from pathlib import Path
import os
import re
from xml.etree import ElementTree

from PIL import Image, ImageColor, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "res/icons"
SIZES = (16, 24, 32, 48, 64)
FRAMES = {(size, size) for size in SIZES}

STYLES = {
    "code": ("#36569D", (((12, 9), (8, 13), (12, 17)), ((20, 9), (24, 13), (20, 17)), ((18, 8), (14, 18)))),
    "markup": ("#9E452A", (((13, 9), (9, 13), (13, 17)), ((19, 9), (23, 13), (19, 17)))),
    "style": ("#A13069", (((9, 10), (17, 10)), ((9, 14), (22, 14)), ((9, 18), (14, 18)))),
    "data": ("#78600D", (((13, 8), (11, 8), (11, 11), (9, 13), (11, 15), (11, 18), (13, 18)), ((19, 8), (21, 8), (21, 11), (23, 13), (21, 15), (21, 18), (19, 18)))),
    "table": ("#18714F", (((9, 9), (23, 9), (23, 18), (9, 18), (9, 9)), ((9, 13), (23, 13)), ((15, 10), (15, 18)))),
    "database": ("#31619B", (((9, 10), (16, 8), (23, 10), (16, 12), (9, 10), (9, 16), (16, 18), (23, 16), (23, 10)), ((9, 13), (16, 15), (23, 13)))),
    "terminal": ("#22685A", (((9, 9), (14, 13), (9, 17)), ((16, 17), (23, 17)))),
    "settings": ("#4F6476", (((9, 10), (23, 10)), ((9, 16), (23, 16)), ((13, 8), (13, 12)), ((19, 14), (19, 18)))),
    "build": ("#77549B", (((10, 10), (16, 7), (22, 10), (16, 13), (10, 10)), ((10, 14), (16, 17), (22, 14)))),
    "hardware": ("#1C6D81", (((8, 16), (11, 16), (11, 10), (16, 10), (16, 16), (21, 16), (21, 10), (24, 10)),)),
    "math": ("#92532C", (((8, 17), (12, 14), (16, 16), (23, 8)), ((19, 8), (23, 8), (23, 12)))),
    "document": ("#57667E", (((9, 9), (18, 9)), ((9, 13), (23, 13)), ((9, 17), (20, 17)))),
    "diagram": ("#186D83", (((10, 17), (16, 9), (22, 17), (10, 17)), ((16, 9), (16, 17)))),
    "media": ("#8B387D", (((12, 8), (21, 13), (12, 18), (12, 8)),)),
    "diff": ("#346D34", (((9, 12), (15, 12)), ((12, 9), (12, 15)), ((17, 17), (23, 17)))),
}
GROUPS = {
    "markup": "html xml php",
    "style": "css",
    "data": "json toml yaml",
    "table": "csv",
    "database": "sql",
    "terminal": "autohotkey autoit3 awk bash batch powershell vim winhex",
    "settings": "config ini",
    "build": "cmake gn gradle innosetup jamfile makefile nsis resourcescript",
    "hardware": "asm cil llvm smali verilog vhdl wasm",
    "math": "abaqus apdl fortran julia mathematica matlab r sas",
    "document": "ansi latex markdown texinfo text text-secondary typst",
    "diagram": "asymptote blockdiag graphviz",
    "media": "avisynth",
    "diff": "diff",
}
KINDS = {language: kind for kind, languages in GROUPS.items() for language in languages.split()}
ACCENTS = {
    "cpp": "#3B529C", "csharp": "#6946A1", "go": "#086D83", "html": "#A84324",
    "java": "#9F353C", "javascript": "#806000", "json": "#735818", "kotlin": "#7541AB",
    "lua": "#41469E", "markdown": "#33687E", "perl": "#185C86", "php": "#624B9A",
    "powershell": "#235F9A", "python": "#1C6396", "ruby": "#9F324B", "rust": "#9A462F",
    "swift": "#A74629", "typescript": "#254FA1", "xml": "#7250A2", "yaml": "#925430",
    "zig": "#855819",
}
PIXELS = dict(zip("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", (
    "25755 65656 34443 65556 74647 74644 34553 55755 72227 11152 55655 44447 57755 "
    "57775 25552 65644 25531 65655 34616 72222 55557 55552 55775 55255 55222 71247 "
    "75557 26227 61247 61216 55711 74616 34653 71122 35356 35616"
).split(), strict=True))


def catalog():
    extensions = defaultdict(set)
    for language in ElementTree.parse(ROOT / "config/languages/default.xml").findall("language"):
        fields = [language.findtext("extensions", "")]
        fields.extend(variant.attrib["extensions"] for variant in language.findall("variants/variant"))
        for field in fields:
            for item in filter(None, (entry.strip() for entry in field.split(";"))):
                extension = item.removeprefix(".").lower()
                if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", extension):
                    raise ValueError(f"Invalid extension for an icon filename: {item!r}")
                extensions[extension].add(language.attrib["id"])
    return extensions


def small_labels(extensions):
    labels = {ext: ext.upper() for ext in extensions if len(ext) <= 3}
    taken = set(labels.values())
    preferred = {"json": "JSN", "cppm": "CPM", "yaml": "YAM", "html": "HTL", "markdown": "MDN"}
    for ext in sorted(extensions - labels.keys()):
        letters = re.sub(r"[^A-Z0-9]", "", ext.upper())
        initials = "".join(part[0] for part in re.split(r"[-_]", ext.upper()) if part)
        candidates = [preferred.get(ext, ""), initials if len(initials) == 3 else "", letters[:3]]
        candidates.extend("".join(chars) for chars in combinations(letters, 3))
        candidates.extend(f"{letters[:1]}{number:02}" for number in range(100))
        label = next(candidate for candidate in candidates if candidate and candidate not in taken)
        labels[ext] = label
        taken.add(label)
    return labels


@lru_cache(maxsize=128)
def font(size, bold=True):
    name = "segoeuib.ttf" if bold else "segoeui.ttf"
    return ImageFont.truetype(str(Path(os.environ["WINDIR"]) / "Fonts" / name), size)


def render(size, extension, label, owners):
    language = next(iter(owners)) if len(owners) == 1 else ""
    kind = KINDS.get(language, "code" if language else "document")
    color, symbol = STYLES[kind]
    color = ACCENTS.get(language, color)
    tint = tuple(round(channel * 0.21 + 255 * 0.79) for channel in ImageColor.getrgb(color))
    scale = size / 8
    image = Image.new("RGBA", (size * 4, size * 4))
    draw = ImageDraw.Draw(image)

    def points(coords):
        return [(round(x_coord * scale), round(y_coord * scale)) for x_coord, y_coord in coords]

    sheet = [(6, 1.6), (20, 1.6), (27, 8.6), (27, 27.5), (5, 27.5), (5, 2.6)]
    draw.polygon(points([(x_coord + .7, y_coord + .9) for x_coord, y_coord in sheet]), fill="#11243B36")
    draw.polygon(points(sheet), fill="#FAFCFF")
    draw.polygon(points([(20, 2), (20, 8.6), (26.7, 8.6)]), fill=tint)
    draw.line(points(sheet + sheet[:1]), fill="#58677C", width=round(scale * 1.1), joint="curve")
    draw.line(points([(20, 2), (20, 8.6), (26.7, 8.6)]), fill="#58677C", width=round(scale * .7))
    for stroke in symbol:
        draw.line(points(stroke), fill=color, width=round(scale * 1.65), joint="curve")

    draw.rounded_rectangle(points([(1.8, 20.1), (30.2, 30.3)]), radius=round(scale * 2), fill=color)
    if size == 16:
        image = image.resize((size, size), Image.Resampling.LANCZOS)
        draw = ImageDraw.Draw(image)
        start = (17 - 4 * len(label)) // 2
        for index, char in enumerate(label):
            for row, bits in enumerate(PIXELS.get(char, "00070")):
                for column, bit in enumerate((4, 2, 1)):
                    if int(bits) & bit:
                        draw.point((start + 4 * index + column, row + 10), fill="white")
        return image

    limit = 3 if size < 32 else 5 if size < 48 else 8
    text = extension.upper() if len(extension) <= limit or size >= 64 else label
    typeface = font(round(scale * 8.8))
    left, top, right, bottom = typeface.getbbox(text)
    word = Image.new("L", (right - left, bottom - top))
    ImageDraw.Draw(word).text((-left, -top), text, font=typeface, fill=255)
    if word.width > round(25.5 * scale):
        word.thumbnail((round(25.5 * scale), word.height), Image.Resampling.LANCZOS)
    image.paste("white", (round(16 * scale - word.width / 2), round(25.05 * scale - word.height / 2)), word)
    return image.resize((size, size), Image.Resampling.LANCZOS)


def preview():
    samples = "cpp hpp cs java py js ts jsx json yaml toml ini html xml svg css sh ps1 sql csv md rs go zig patch vhd cmake txt".split()
    image = Image.new("RGB", (1112, 468), "#F1F4F8")
    draw = ImageDraw.Draw(image)
    draw.text((24, 14), "MultiPad4  /  File type icons", font=font(29), fill="#192838")
    draw.text((25, 55), "48 px  /  Light background", font=font(15, False), fill="#44576C")
    draw.rectangle((0, 293, 1111, 467), fill="#1D2633")
    draw.text((25, 307), "16 px  /  Dark background", font=font(15, False), fill="#CBD6E3")
    for index, ext in enumerate(samples):
        center = 49 + 78 * (index % 14)
        row = index // 14
        with Image.open(OUTPUT / f"file-{ext}.ico") as icon:
            for size, top, step, text_color in ((48, 85, 96, "#293B51"), (16, 338, 55, "#DEE7F0")):
                frame = icon.ico.getimage((size, size))
                image.paste(frame, (center - size // 2, top + step * row), frame)
                draw.text((center, top + step * row + size + 5), f".{ext}", font=font(13, False), anchor="mt", fill=text_color)
    image.save(OUTPUT / "preview.png", optimize=True)


def main():
    parser = argparse.ArgumentParser(description="Generate MultiPad4's Windows file type icons.")
    parser.add_argument("--check", action="store_true", help="Only validate the existing icon set.")
    check = parser.parse_args().check
    extensions = catalog()
    labels = small_labels(extensions.keys())
    assert len(labels) == len(set(labels.values())) == len(extensions)
    if not check:
        OUTPUT.mkdir(parents=True, exist_ok=True)
    small_frames = set()
    for ext, owners in sorted(extensions.items()):
        path = OUTPUT / f"file-{ext}.ico"
        if not check:
            images = [render(size, ext, labels[ext], owners) for size in SIZES]
            images[-1].save(path, sizes=sorted(FRAMES), append_images=images[:-1])
        with Image.open(path) as icon:
            assert icon.format == "ICO" and icon.ico.sizes() == FRAMES, path
            for size in SIZES:
                frame = icon.ico.getimage((size, size))
                assert frame.size == (size, size) and frame.mode == "RGBA", path
                assert frame.getchannel("A").getextrema() == (0, 255), path
                if size == 16:
                    small_frames.add(frame.tobytes())
    assert len(small_frames) == len(extensions), "Duplicate 16 px icons"
    assert {path.stem for path in OUTPUT.glob("*.ico")} == {f"file-{ext}" for ext in extensions}
    if not check:
        preview()
    print(f"Verified {len(extensions)} icons, {len(SIZES)} sizes per icon.")


if __name__ == "__main__":
    main()
