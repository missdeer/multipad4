# Bundled themes

`light.xml` and `dark.xml` are MultiPad4's native palettes derived from notepad4.
The other 22 XML files were converted once from the Notepad++ presets in
`PowerEditor/installer/themes` at revision
`61e17dde113e01f097939b111cac38427879864c` in the user-provided reference checkout.
Every shipped preset now uses MultiPad4's `<theme version="1">` schema.

The original author credits and license comments are retained before each theme.
The Notepad++ project license is included in `../NOTEPADPLUSPLUS-LICENSE.txt`;
individual theme notices, including MIT notices where present, remain in each XML.
See the repository's `THIRD_PARTY.md` for provenance. Original installation
instructions in preserved upstream comments refer to the source application;
these converted files belong in MultiPad4's `config/themes` directory.

Choose a preset in Language Style Configurator. The application only reads and
writes native MultiPad4 themes; it contains no Notepad++ XML decoder, style-ID
mapping table, or automatic conversion path. Styles are stored with explicit local
semantic IDs, and absent categories use the normal native palette inheritance.
Saving edits creates a user snapshot and leaves these presets unchanged.

Presets:

- `Bespin.xml`
- `Black board.xml`
- `Choco.xml`
- `DansLeRuSH-Dark.xml`
- `DarkModeDefault.xml`
- `Deep Black.xml`
- `Hello Kitty.xml`
- `HotFudgeSundae.xml`
- `khaki.xml`
- `Mono Industrial.xml`
- `Monokai.xml`
- `MossyLawn.xml`
- `Navajo.xml`
- `Obsidian.xml`
- `Plastic Code Wrap.xml`
- `Ruby Blue.xml`
- `Solarized-light.xml`
- `Solarized.xml`
- `Twilight.xml`
- `Vibrant Ink.xml`
- `vim Dark Blue.xml`
- `Zenburn.xml`
