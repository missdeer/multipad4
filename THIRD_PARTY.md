# Third-party components

## Redistribution status

MultiPad4's original code is licensed under GPL-3.0-or-later. Imported
material retains the terms documented below and in its upstream notices.
The main executable compiles Notepad++ search and Plugins Admin code and
resources. GPLv3 is a common version allowed by their GPL-3.0-or-later
notices and the search-result lexer's GPL-2.0-or-later notice.
[MultiPad4 license](LICENSE)

- The bundled themes, embedded plugin catalogue and upstream toolbar assets
  have additional licensing questions documented below. Resolve these for the
  files included in each release.
- GPLv3 binary distribution also requires a compliant way to obtain the
  corresponding source, including build and adaptation scripts, and preservation
  of applicable license and attribution notices. The current build and release
  workflow ships selected notices. Its package paths omit notices such as the
  main executable's Notepad4 and Scintilla texts.
  [GPLv3 sections 1, 5 and 6](https://www.gnu.org/licenses/gpl-3.0.html)
  [Package workflow](.github/workflows/msvc-build.yml)

## notepad4 and its Scintilla fork

Source: https://github.com/zufuliu/notepad4 at commit `3fcfb70fd0d3c8802100c85f7293ccb326aa8b64` (copied from the user-provided `tmp/notepad4` checkout).

- Application code/resources: `3rdparty/notepad4/src` and `3rdparty/notepad4/res`.
- Application license/notices: `3rdparty/notepad4/License.txt` (BSD 3-Clause and component notices).
- Scintilla: `3rdparty/notepad4-scintilla/scintilla`.
- Scintilla/Lexilla license: `3rdparty/notepad4-scintilla/scintilla/License.txt`.
- Additional upstream notices: `3rdparty/notepad4-scintilla/Notepad4-License.txt`.

Preserve these notices in source and binary distributions as required by each license. All eleven `locale/*/Notepad4.rc` files were added from the same pinned GitHub revision. The product loads the complete lexer configuration catalogue from reviewed XML; only the development baseline exporter compiles its original static objects; adapted editor algorithms retain their upstream provenance. Scintilla host services include custom transliteration and the bundled standalone Boost.Regex headers. Migration and behavioral validation status is tracked in `docs/MIGRATION_STATUS.md`.

Scintilla's compiled components also use Chromium-derived IME code
(BSD 3-Clause), Boost.Regex (BSL-1.0), and input data from Julia and emoji-data
(MIT). Their notices appear in the Notepad4 license. Scintilla's original
regular expression engine credits public-domain routines. Scripts also
derive character property tables from the Unicode Character Database,
whose source data uses the Unicode License v3.
[Unicode terms](https://www.unicode.org/license.txt)

Notepad4's notices identify the Microsoft Visual Studio Image Library
2017 and 2022. MultiPad4 compiles the imported toolbar bitmaps into its
Windows resources. Microsoft separately licenses the image libraries and
sets conditions on application redistribution and applying licenses that
require source disclosure or modification rights to its images. Identify
the derived bitmaps and confirm the applicable rights or replace those
assets before a GPL release. [Microsoft Image Library and EULAs](https://www.microsoft.com/en-us/download/details.aspx?id=35825)

The development baselines in `tests/baselines/lexer-catalog.xml` and
`tests/baselines/lexer-behavior.txt` are derived from this same pinned notepad4
revision and its Scintilla fork. The language defaults, styles, keywords and
completion snippets retain the upstream license above. `tools/ExportLexerBaseline.cpp`
exports the compiled objects without modifying upstream files; reproduction and
coverage are documented in `docs/LEXER_BASELINE.md`.

## matepath

Imported without source changes from the user-provided `tmp/notepad4/matepath`
checkout: `3rdparty/matepath/src/{Dlapi,DropSource}.{h,cpp}`. The original
BSD 3-Clause license and source readme are in `3rdparty/matepath/doc`.
Copyright © 2011–2026 Zufu Liu and contributors; metapath copyright
© 1996–2012 Florian Balmer. Preserve the license in binary distributions.

`cmake/Matepath.cmake` generates a host adaptation of the directory API in the
build tree. It provides Windows SDK typed PIDL compatibility, releases filtered
enumeration items, frees deleted list items through notification data, initializes
Shell output data, and resolves icons on the owning STA instead of accessing
Shell interfaces and controls from an un-marshaled worker thread. Host window,
navigation history, settings and MDI dispatch are in `plugins/Matepath/MatepathPane.*`.

## Notepad++ search UI and result lexer

Reference: user-provided `tmp/notepad-plus-plus` checkout at
`61e17dde113e01f097939b111cac38427879864c`. Original search resource,
resource IDs and LexSearchResult are preserved in `3rdparty/notepad-plus-plus`.
`src/SearchDialog.rc` adapts the dialog layout (Copyright 2021 Don HO,
GPL-3.0-or-later); `SearchDialog.cpp` and `SearchResultsPane.cpp` adapt its
Find/Replace/Find in Files/Mark and Finder interaction to explicit MDI documents.
`SearchResultLexer.cpp` adapts LexSearchResult's style/fold model (Copyright
1998-2001 Neil Hodgson, Copyright 2003 Don HO, GPL-2.0-or-later) using container
styling rather than the private Lexilla markings-pointer ABI. The upstream
GPL license is included in `LICENSE`; Scintilla's notice in
`COPYING.search-result` also applies to the original lexer. Distributing these
adaptations requires complying with the applicable GPL terms.


## Notepad++ plugin interface

`3rdparty/notepad-plus-plus-plugin-api` contains unmodified `PluginInterface.h`,
`Notepad_plus_msgs.h`, and `Docking.h` from the user-provided
`tmp/notepad-plus-plus` checkout, together with its GPL-3.0-or-later `LICENSE`.
The headers retain their copyright and license notices. `src/PluginHost.cpp`
implements the host adapter, using the upstream plugin loading sequence and ABI
as references; it does not import Notepad++'s application or docking manager.
Plugin DLLs in the generated build directory are user-provided runtime inputs,
not redistributed source dependencies. Their own licenses continue to apply.

## Notepad++ Plugins Admin

`src/PluginAdmin.*` adapts the Plugins Admin menu/handler, modeless dialog,
five category lists, search and checked batch operations from the same local
Notepad++ reference. Original resources and GPL notices are retained under
`3rdparty/notepad-plus-plus/PluginsAdmin`. Menu command IDs 48014/48015 are
reserved from dynamic plugin commands and routed explicitly before their range.
`src/PluginAdminWorker.ps1` supplies a MultiPad4-specific exit/wait, package
verification, staging, rollback and restart adapter instead of N++'s GUP process.

The executable embeds `pl.x64.json` from the official nppPluginList repository.
`CATALOG_REVISION` pins its source revision and `README.catalog.md` retains its
upstream documentation. The pinned tree and catalogue contain no explicit
license for the data. Confirm reuse and redistribution rights or supply a
separately licensed catalogue.
[Pinned nppPluginList source](https://github.com/notepad-plus-plus/nppPluginList/tree/55901005d3f42ea228ec64b200479d03c5ff0718)

Plugin management compiles the unmodified nlohmann/json 3.12.0 header from the
reference checkout. [MIT license](3rdparty/nlohmann-json/LICENSE.MIT)

## Notepad4 SIMD application routines

`src/SimdText.*` adapts the active application SIMD routines from the local
`tmp/notepad4/src/Edit.cpp` and `EditEncoding.cpp`: EOL counting and mode
selection, indentation inference, UTF-16 Latin-extension detection, the
Zach Wegner UTF-8 validator, and `CheckUTF7`'s ASCII scan. Original Notepad4
licensing is retained under `3rdparty/notepad4/License.txt`; the UTF-8
validator's MIT notice is also retained in `SimdText.cpp`.
The file also credits Bjoern Hoehrmann's MIT-licensed UTF-8 decoder.

`src/SimdGraphics.*` adapts `Helpers.cpp` bitmap alpha merge/blend and contrast
verification, plus `Edit.cpp`'s UTF-16 HTML tag packing. GraphicUtils.h and
VectorISA.h are used directly from the pinned Scintilla fork. Adaptations
bound input reads, pad short tails locally, handle arbitrary bitmap widths,
and preserve MultiPad4's deterministic UTF-16 detection when statistical
Unicode detection is disabled. Vendored sources remain unchanged.

## External language and theme configuration

`config/languages/default.xml` and the native light/dark palettes contain data
exported from the pinned notepad4 revision above; its BSD/component notices apply.
The same XML is embedded without a second handwritten C++ defaults table.
`config/themes` bundles native MultiPad4 conversions of all 22 XML presets from
`PowerEditor/installer/themes` at Notepad++ revision
`61e17dde113e01f097939b111cac38427879864c`. Conversion was performed once; product
code only reads the native schema and includes no Notepad++ XML compatibility
layer or style binding table. Each converted file retains its original author
credits and license comments (including individual MIT notices where present).
The Notepad++ GPL text is shipped as `config/NOTEPADPLUSPLUS-LICENSE.txt`.
The individual notices include CC-BY-3.0 in `Solarized.xml` and related
presets, a `GPL2` label in `DarkModeDefault.xml` and `Zenburn.xml`, and
imprecise GNU license references in some other presets. Verify the exact
GNU terms where the file leaves the version or upgrade permission unclear.
`DansLeRuSH-Dark.xml` contains both MIT-style permission text and a
CC-BY-NC-SA-3.0 label. Confirm which grant applies or omit the preset from
a distribution intended to permit commercial use.
[Theme inventory](config/themes/README.md)
[CC BY 3.0](https://creativecommons.org/licenses/by/3.0/)
[CC BY-NC-SA 3.0](https://creativecommons.org/licenses/by-nc-sa/3.0/)
The native regression theme in `tests/fixtures/theme-native.xml` was written for
this project.
Windows XmlLite is an operating-system component, not a bundled parser library.
`cmake/ScintillaInstrumentation.cmake` generates optional instrumentation copies
under the build directory and preserves the upstream Scintilla source originals.

## miniz

Plugin management uses the ZIP reader from the [miniz 3.1.2 release](https://github.com/richgel999/miniz/releases/tag/3.1.2).
The upstream amalgamated sources and MIT license are preserved in
`3rdparty/miniz/`. The build links the reader statically and ships the
license as `Miniz.License.txt`.

## Build tools and runtime libraries

- MSVC-family builds use the Windows SDK. Redistributable
  Microsoft runtimes are subject to Visual Studio terms and the REDIST list.
  [Visual Studio 2022 redistribution terms](https://learn.microsoft.com/visualstudio/releases/2022/redistribution)
- oneAPI packages explicitly include `libmmd.dll` and `svml_dispmd.dll`.
  Check the installed toolchain's EULA and redistributable list for those DLLs.
  [Intel developer tool license agreements](https://www.intel.com/content/www/us/en/developer/articles/license/end-user-license-agreement.html)
- MinGW packages copy runtime DLLs according to PE imports. GCC runtimes such
  as libgcc and libstdc++ use GPL terms with the GCC Runtime Library Exception.
  LLVM runtimes such as libc++ use Apache-2.0 with LLVM exceptions. Use the
  MSYS2 package metadata and notices to verify the DLLs in each published
  variant and their exact terms. The GPL-licensed mingwdeployqt program runs
  during packaging.
  [GCC exception](https://www.gnu.org/licenses/gcc-exception-3.1.html)
  [LLVM license](https://llvm.org/LICENSE.txt)
  [mingwdeployqt license](https://github.com/missdeer/mingwdeployqt/blob/master/LICENSE)
- The file icon generator uses Pillow (MIT-CMU) and Windows' installed
  Segoe UI font. The repository ships generated icon images.
  [Pillow license](https://pillow.readthedocs.io/en/stable/about.html#license)

## Kate LSP Client reference

`plugins/LSPClient` is a native Win32 adaptation informed by the local Kate
`addons/lspclient` implementation (not a Qt/KTextEditor binary port).
Reference author: Mark Nauwelaerts, copyright 2019, MIT license.
See `plugins/LSPClient/Kate.License.txt` and `docs/LSP_CLIENT.md` for the native
implementation scope and differences. Uses the existing bundled nlohmann JSON;
no additional runtime library is introduced.
