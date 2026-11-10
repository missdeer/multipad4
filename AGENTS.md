# Repository Guidelines

## Project Structure & Module Organization

MultiPad4 is a Windows-only, tabbed MDI editor built with C++20, WTL, and Scintilla. `src/` contains the application, document model, file I/O, Win32 resources, and manifest. `tests/FileIOTests.cpp` covers file handling; `src/SmokeTest.h` provides editor integration checks. `3rdparty/` holds pinned upstream frameworks, notepad4 sources/assets, and Scintilla. `cmake/` contains build integration and compatibility transforms. `docs/PROJECT_SCOPE.md` defines migration requirements; `tools/inventory_notepad4.py` regenerates the UI inventory. Treat `cmake-msvc-build/` and `tmp/` as local generated directories.

## Build, Test, and Development Commands

Install Windows 10/11, VS 2022 C++ desktop tools with ATL and Windows SDK, CMake 3.25+, and Ninja. Qt and MFC are unnecessary. From the repository root, create `cmake-msvc-build/` if absent, then use the existing local MSVC wrappers:

```powershell
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build --output-on-failure
.\cmake-msvc-build\multipad4.exe .\src\MainFrame.cpp
python tools/inventory_notepad4.py
```

These configure, compile, run tests, launch the editor, and refresh the inventory, respectively. `CMakePresets.json` defines `msvc-x64` presets for other machines using a VS Developer Command Prompt.

## Coding Style & Naming Conventions

Match nearby C++ code: four-space indentation, same-line opening braces, PascalCase types/functions, and lowerCamelCase variables. Match class filenames such as `EditorDocument.h`/`.cpp`. Use Unicode Win32 APIs and explicit UTF-8/UTF-16 conversion at boundaries. CMake enables MSVC `/W4`; no repository-wide formatter or linter configuration is present.

## Testing Guidelines

CTest runs custom C++ checks: `fileio_roundtrip` and `editor_integration` (the application’s `--smoke-test` mode). Extend file tests for encoding, newline preservation, invalid input, and failed-save protection; extend smoke checks for document isolation and UI behavior. No numerical coverage target is configured. Manually verify affected drag, input-method, and multi-monitor DPI interactions; include screenshots for visual changes.

## Commit & Pull Request Guidelines

Git history is unavailable in this checkout, so no existing message convention can be verified. Use concise, imperative subjects describing the change. PRs should explain behavior, link relevant issues, report build/test results, and identify remaining manual checks.

## Architecture & Upstream Boundaries

Follow `docs/PROJECT_SCOPE.md`: keep document state isolated and use the established MDI/tabbing/docking frameworks. Preserve vendored framework sources; implement MSVC compatibility changes in `cmake/FrameworkCompatibility.cmake`. Consult `THIRD_PARTY.md` for provenance/licenses. Resource inventory entries alone do not demonstrate implemented notepad4 functionality.
